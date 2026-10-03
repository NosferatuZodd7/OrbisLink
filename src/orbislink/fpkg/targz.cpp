// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/fpkg/targz.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace orbislink::fpkg {

namespace {

namespace fs = std::filesystem;

struct Failure : std::runtime_error
{
	using std::runtime_error::runtime_error;
};
struct Cancelled
{
};

// ───────────────────────────────── input

// Bytes from memory, or from a file read a megabyte at a time.
class Input
{
public:
	Input(const uint8_t *data, size_t size) : data_(data), size_(size) {}
	explicit Input(std::istream *in) : in_(in), buffer_(1 << 20) {}

	bool byte(uint8_t *out)
	{
		if(pos_ == size_ && !refill())
			return false;
		*out = data_[pos_++];
		return true;
	}
	uint64_t consumed() const { return base_ + pos_; }

private:
	bool refill()
	{
		if(!in_)
			return false;
		in_->read(reinterpret_cast<char *>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()));
		const size_t n = static_cast<size_t>(in_->gcount());
		if(n == 0)
			return false;
		base_ += size_;
		data_ = buffer_.data();
		size_ = n;
		pos_ = 0;
		return true;
	}

	const uint8_t *data_ = nullptr;
	size_t size_ = 0;
	size_t pos_ = 0;
	uint64_t base_ = 0;
	std::istream *in_ = nullptr;
	std::vector<uint8_t> buffer_;
};

// Deflate's bits come least significant first.
class Bits
{
public:
	explicit Bits(Input &in) : in_(in) {}

	// As many bits as there are, up to n (at the very end there may be fewer).
	void fill(int n)
	{
		while(count_ < n)
		{
			uint8_t b;
			if(!in_.byte(&b))
				return;
			buffer_ |= uint32_t(b) << count_;
			count_ += 8;
		}
	}
	uint32_t take(int n)
	{
		if(n == 0)
			return 0;
		fill(n);
		if(count_ < n)
			throw Failure("the archive ends too soon");
		const uint32_t v = buffer_ & ((1u << n) - 1);
		buffer_ >>= n;
		count_ -= n;
		return v;
	}
	void dropToByte()
	{
		buffer_ >>= count_ & 7;
		count_ -= count_ & 7;
	}
	// Nothing left at all (checked between gzip members).
	bool atEnd()
	{
		if(count_ > 0)
			return false;
		fill(8);
		return count_ == 0;
	}
	uint32_t peek() const { return buffer_; }
	int available() const { return count_; }
	void drop(int n)
	{
		buffer_ >>= n;
		count_ -= n;
	}

private:
	Input &in_;
	uint32_t buffer_ = 0;
	int count_ = 0;
};

// ───────────────────────────────── Huffman codes

constexpr int kMaxBits = 15;
constexpr int kFastBits = 9;

struct Huffman
{
	uint16_t count[kMaxBits + 1] = {};
	uint16_t symbol[320] = {};
	// Codes of up to 9 bits in one look: symbol | length << 12, 0 = longer.
	uint16_t fast[1 << kFastBits] = {};

	void build(const uint8_t *lengths, int n)
	{
		std::memset(count, 0, sizeof count);
		std::memset(fast, 0, sizeof fast);
		for(int s = 0; s < n; ++s)
			++count[lengths[s]];
		count[0] = 0;
		int left = 1;
		for(int len = 1; len <= kMaxBits; ++len)
		{
			left <<= 1;
			left -= count[len];
			if(left < 0)
				throw Failure("the archive is damaged (bad Huffman code)");
		}
		uint16_t offs[kMaxBits + 2] = {};
		for(int len = 1; len <= kMaxBits; ++len)
			offs[len + 1] = static_cast<uint16_t>(offs[len] + count[len]);
		for(int s = 0; s < n; ++s)
			if(lengths[s])
				symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);

		// The canonical codes, reversed into the stream's bit order.
		int code = 0, index = 0;
		for(int len = 1; len <= kFastBits; ++len)
		{
			for(int i = 0; i < count[len]; ++i, ++index, ++code)
			{
				int reversed = 0;
				for(int b = 0; b < len; ++b)
					reversed |= ((code >> b) & 1) << (len - 1 - b);
				for(int fillBits = reversed; fillBits < (1 << kFastBits); fillBits += 1 << len)
					fast[fillBits] = static_cast<uint16_t>(symbol[index] | (len << 12));
			}
			code <<= 1;
		}
	}

	int decode(Bits &bits) const
	{
		bits.fill(kFastBits);
		if(bits.available() >= kFastBits)
		{
			const uint16_t e = fast[bits.peek() & ((1 << kFastBits) - 1)];
			if(e)
			{
				bits.drop(e >> 12);
				return e & 0x0FFF;
			}
		}
		// Longer codes (or the last bits of the stream): one bit at a time.
		int code = 0, first = 0, index = 0;
		for(int len = 1; len <= kMaxBits; ++len)
		{
			code |= static_cast<int>(bits.take(1));
			const int n = count[len];
			if(code - n < first)
				return symbol[index + (code - first)];
			index += n;
			first += n;
			first <<= 1;
			code <<= 1;
		}
		throw Failure("the archive is damaged (unknown code)");
	}
};

const uint16_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5,
	5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
	769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11,
	11, 12, 12, 13, 13};

uint32_t crc32Update(uint32_t crc, const uint8_t *p, size_t n)
{
	static const std::array<uint32_t, 256> table = [] {
		std::array<uint32_t, 256> t {};
		for(uint32_t i = 0; i < 256; ++i)
		{
			uint32_t c = i;
			for(int k = 0; k < 8; ++k)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[i] = c;
		}
		return t;
	}();
	crc = ~crc;
	for(size_t i = 0; i < n; ++i)
		crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

// ───────────────────────────────── output

// The last 32 KiB stay for back-references; the rest goes to the sink in
// large pieces.
class Output
{
public:
	explicit Output(const std::function<bool(const uint8_t *, size_t)> &sink)
		: sink_(sink), buffer_(kCapacity)
	{
	}

	void put(uint8_t b)
	{
		if(pos_ == kCapacity)
			slide();
		buffer_[pos_++] = b;
		++total_;
	}
	void copy(uint32_t distance, uint32_t length)
	{
		if(distance > total_ || distance > kWindow)
			throw Failure("the archive is damaged (distance too far back)");
		while(length--)
		{
			if(pos_ == kCapacity)
				slide();
			buffer_[pos_] = buffer_[pos_ - distance];
			++pos_;
			++total_;
		}
	}
	void flush()
	{
		if(pos_ > flushed_)
		{
			crc_ = crc32Update(crc_, buffer_.data() + flushed_, pos_ - flushed_);
			if(!sink_(buffer_.data() + flushed_, pos_ - flushed_))
				throw Cancelled();
			flushed_ = pos_;
		}
	}
	// A new gzip member checks its own CRC and size.
	void startMember()
	{
		flush();
		crc_ = 0;
		memberSize_ = total_;
	}
	uint32_t crc() const { return crc_; }
	uint32_t memberSize() const { return static_cast<uint32_t>(total_ - memberSize_); }

private:
	static constexpr size_t kWindow = 32768;
	static constexpr size_t kCapacity = 1 << 20;

	void slide()
	{
		flush();
		std::memmove(buffer_.data(), buffer_.data() + kCapacity - kWindow, kWindow);
		pos_ = flushed_ = kWindow;
	}

	const std::function<bool(const uint8_t *, size_t)> &sink_;
	std::vector<uint8_t> buffer_;
	size_t pos_ = 0;
	size_t flushed_ = 0;
	uint64_t total_ = 0;
	uint64_t memberSize_ = 0;
	uint32_t crc_ = 0;
};

// ───────────────────────────────── inflate

void inflateCodes(Bits &bits, Output &out, const Huffman &lengths, const Huffman &distances)
{
	for(;;)
	{
		int sym = lengths.decode(bits);
		if(sym < 256)
		{
			out.put(static_cast<uint8_t>(sym));
			continue;
		}
		if(sym == 256)
			return;
		sym -= 257;
		if(sym >= 29)
			throw Failure("the archive is damaged (bad length)");
		const uint32_t length = kLengthBase[sym] + bits.take(kLengthExtra[sym]);
		const int dsym = distances.decode(bits);
		if(dsym >= 30)
			throw Failure("the archive is damaged (bad distance)");
		const uint32_t distance = kDistBase[dsym] + bits.take(kDistExtra[dsym]);
		out.copy(distance, length);
	}
}

void inflate(Bits &bits, Output &out)
{
	static const std::pair<Huffman, Huffman> fixed = [] {
		uint8_t l[288];
		for(int i = 0; i < 144; ++i)
			l[i] = 8;
		for(int i = 144; i < 256; ++i)
			l[i] = 9;
		for(int i = 256; i < 280; ++i)
			l[i] = 7;
		for(int i = 280; i < 288; ++i)
			l[i] = 8;
		std::pair<Huffman, Huffman> t;
		t.first.build(l, 288);
		uint8_t d[30];
		std::memset(d, 5, sizeof d);
		t.second.build(d, 30);
		return t;
	}();
	auto lengths = std::make_unique<Huffman>();
	auto distances = std::make_unique<Huffman>();

	bool last = false;
	while(!last)
	{
		last = bits.take(1) != 0;
		const uint32_t type = bits.take(2);
		if(type == 0)
		{
			bits.dropToByte();
			const uint32_t len = bits.take(16);
			const uint32_t nlen = bits.take(16);
			if(len != (~nlen & 0xFFFF))
				throw Failure("the archive is damaged (stored block)");
			for(uint32_t i = 0; i < len; ++i)
				out.put(static_cast<uint8_t>(bits.take(8)));
		}
		else if(type == 1)
		{
			inflateCodes(bits, out, fixed.first, fixed.second);
		}
		else if(type == 2)
		{
			const int nlen = static_cast<int>(bits.take(5)) + 257;
			const int ndist = static_cast<int>(bits.take(5)) + 1;
			const int ncode = static_cast<int>(bits.take(4)) + 4;
			if(nlen > 286 || ndist > 30)
				throw Failure("the archive is damaged (block header)");
			static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
			uint8_t l[320] = {};
			for(int i = 0; i < ncode; ++i)
				l[order[i]] = static_cast<uint8_t>(bits.take(3));
			Huffman codeLengths;
			codeLengths.build(l, 19);
			std::memset(l, 0, sizeof l);
			for(int i = 0; i < nlen + ndist;)
			{
				const int sym = codeLengths.decode(bits);
				if(sym < 16)
				{
					l[i++] = static_cast<uint8_t>(sym);
					continue;
				}
				uint8_t value = 0;
				int repeat;
				if(sym == 16)
				{
					if(i == 0)
						throw Failure("the archive is damaged (repeat with nothing before)");
					value = l[i - 1];
					repeat = 3 + static_cast<int>(bits.take(2));
				}
				else if(sym == 17)
					repeat = 3 + static_cast<int>(bits.take(3));
				else
					repeat = 11 + static_cast<int>(bits.take(7));
				if(i + repeat > nlen + ndist)
					throw Failure("the archive is damaged (too many lengths)");
				while(repeat--)
					l[i++] = value;
			}
			if(l[256] == 0)
				throw Failure("the archive is damaged (no end code)");
			lengths->build(l, nlen);
			distances->build(l + nlen, ndist);
			inflateCodes(bits, out, *lengths, *distances);
		}
		else
			throw Failure("the archive is damaged (block type)");
	}
}

void gunzipStream(Input &input, Output &out)
{
	Bits bits(input);
	bool first = true;
	while(first || !bits.atEnd())
	{
		if(bits.take(8) != 0x1F || bits.take(8) != 0x8B)
		{
			if(first)
				throw Failure("not a gzip file");
			break; // trailing bytes after the last member
		}
		first = false;
		if(bits.take(8) != 8)
			throw Failure("unknown gzip compression");
		const uint32_t flags = bits.take(8);
		bits.take(16);
		bits.take(16); // time
		bits.take(16); // extra flags, system
		if(flags & 4)
		{
			uint32_t extra = bits.take(16);
			while(extra--)
				bits.take(8);
		}
		if(flags & 8)
			while(bits.take(8) != 0)
				;
		if(flags & 16)
			while(bits.take(8) != 0)
				;
		if(flags & 2)
			bits.take(16);

		out.startMember();
		inflate(bits, out);
		out.flush();
		bits.dropToByte();
		const uint32_t crc = bits.take(16) | (bits.take(16) << 16);
		const uint32_t size = bits.take(16) | (bits.take(16) << 16);
		if(crc != out.crc() || size != out.memberSize())
			throw Failure("the archive is damaged (checksum)");
	}
}

// ───────────────────────────────── tar

class TarReader
{
public:
	explicit TarReader(const TarDestination &destination) : destination_(destination) {}

	bool feed(const uint8_t *p, size_t n)
	{
		while(n > 0)
		{
			if(ended_)
				return true;
			if(state_ == State::Header)
			{
				const size_t take = std::min(n, size_t(512) - headerFill_);
				std::memcpy(header_ + headerFill_, p, take);
				headerFill_ += take;
				p += take;
				n -= take;
				if(headerFill_ == 512)
				{
					headerFill_ = 0;
					startEntry();
				}
				continue;
			}
			const size_t take = static_cast<size_t>(std::min<uint64_t>(n, remaining_));
			if(state_ == State::Data)
			{
				if(file_)
				{
					file_->write(reinterpret_cast<const char *>(p), static_cast<std::streamsize>(take));
					if(!*file_)
						throw Failure("could not write " + filePath_);
				}
				else if(collect_)
					collected_.append(reinterpret_cast<const char *>(p), take);
			}
			p += take;
			n -= take;
			remaining_ -= take;
			if(remaining_ == 0)
				endPart();
		}
		return true;
	}

	void finish() const
	{
		if(state_ != State::Header || headerFill_ != 0)
			throw Failure("the archive ends in the middle of a file");
	}

private:
	enum class State { Header, Data, Padding };

	static std::string text(const uint8_t *p, size_t max)
	{
		size_t n = 0;
		while(n < max && p[n])
			++n;
		return std::string(reinterpret_cast<const char *>(p), n);
	}
	static uint64_t number(const uint8_t *p, size_t len)
	{
		if(p[0] & 0x80)
		{
			// Base 256, for sizes past 8 GiB.
			uint64_t v = p[0] & 0x7F;
			for(size_t i = 1; i < len; ++i)
				v = (v << 8) | p[i];
			return v;
		}
		uint64_t v = 0;
		for(size_t i = 0; i < len && p[i]; ++i)
			if(p[i] >= '0' && p[i] <= '7')
				v = v * 8 + (p[i] - '0');
		return v;
	}

	void startEntry()
	{
		bool zero = true;
		for(uint8_t b : header_)
			zero = zero && b == 0;
		if(zero)
		{
			ended_ = ++zeroBlocks_ >= 2;
			return;
		}
		zeroBlocks_ = 0;

		std::string name = text(header_, 100);
		if(std::memcmp(header_ + 257, "ustar", 5) == 0)
		{
			const std::string prefix = text(header_ + 345, 155);
			if(!prefix.empty())
				name = prefix + "/" + name;
		}
		if(!longName_.empty())
		{
			name = longName_;
			longName_.clear();
		}
		const uint64_t size = number(header_ + 124, 12);
		const char type = static_cast<char>(header_[156]);

		kind_ = type;
		collect_ = type == 'L' || type == 'x';
		collected_.clear();
		file_.reset();
		if(type == '0' || type == '\0' || type == '7')
		{
			const std::string target = destination_ ? destination_(name, size) : std::string();
			if(!target.empty())
			{
				filePath_ = target;
				const fs::path path = fs::u8path(target);
				std::error_code ec;
				fs::create_directories(path.parent_path(), ec);
				file_ = std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::trunc);
				if(!*file_)
					throw Failure("could not create " + target);
			}
		}
		padding_ = (512 - size % 512) % 512;
		remaining_ = size;
		state_ = State::Data;
		if(remaining_ == 0)
			endPart();
	}

	void endPart()
	{
		if(state_ == State::Data)
		{
			if(file_)
			{
				file_->close();
				if(!*file_)
					throw Failure("could not write " + filePath_);
				file_.reset();
			}
			if(kind_ == 'L')
				longName_ = text(reinterpret_cast<const uint8_t *>(collected_.data()), collected_.size());
			else if(kind_ == 'x')
				longName_ = paxPath(collected_);
			collect_ = false;
			if(padding_ > 0)
			{
				state_ = State::Padding;
				remaining_ = padding_;
				return;
			}
		}
		state_ = State::Header;
	}

	// "<length> path=<value>\n" among the pax records.
	static std::string paxPath(const std::string &records)
	{
		size_t pos = 0;
		while(pos < records.size())
		{
			const size_t space = records.find(' ', pos);
			if(space == std::string::npos)
				break;
			const size_t length = static_cast<size_t>(std::strtoull(records.c_str() + pos, nullptr, 10));
			if(length == 0 || pos + length > records.size())
				break;
			const std::string record = records.substr(space + 1, pos + length - space - 2);
			if(record.rfind("path=", 0) == 0)
				return record.substr(5);
			pos += length;
		}
		return {};
	}

	const TarDestination &destination_;
	uint8_t header_[512] = {};
	size_t headerFill_ = 0;
	State state_ = State::Header;
	uint64_t remaining_ = 0;
	uint64_t padding_ = 0;
	char kind_ = 0;
	bool collect_ = false;
	std::string collected_;
	std::string longName_;
	std::unique_ptr<std::ofstream> file_;
	std::string filePath_;
	int zeroBlocks_ = 0;
	bool ended_ = false;
};

} // namespace

bool gunzip(const uint8_t *data, size_t size, const std::function<bool(const uint8_t *, size_t)> &sink,
	std::string *error)
{
	try
	{
		Input input(data, size);
		Output out(sink);
		gunzipStream(input, out);
		return true;
	}
	catch(const Failure &f)
	{
		if(error)
			*error = f.what();
	}
	catch(const Cancelled &)
	{
		if(error)
			*error = "cancelled";
	}
	return false;
}

bool extractTarGz(const std::string &archivePath, const TarDestination &destination,
	const TarProgress &progress, std::string *error)
{
	std::ifstream in(fs::u8path(archivePath), std::ios::binary);
	if(!in)
	{
		if(error)
			*error = "could not open " + archivePath;
		return false;
	}
	std::error_code ec;
	const uint64_t total = fs::file_size(fs::u8path(archivePath), ec);
	try
	{
		Input input(&in);
		TarReader tar(destination);
		const std::function<bool(const uint8_t *, size_t)> sink = [&](const uint8_t *p, size_t n) {
			tar.feed(p, n);
			return !progress || progress(input.consumed(), total);
		};
		Output out(sink);
		gunzipStream(input, out);
		tar.finish();
		return true;
	}
	catch(const Failure &f)
	{
		if(error)
			*error = f.what();
	}
	catch(const Cancelled &)
	{
		if(error)
			*error = "cancelled";
	}
	return false;
}

} // namespace orbislink::fpkg
