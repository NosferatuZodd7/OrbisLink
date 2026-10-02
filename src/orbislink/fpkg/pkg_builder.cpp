// SPDX-License-Identifier: LGPL-3.0-only
#include "orbislink/fpkg/pkg_builder.h"

#include "orbislink/fpkg/fpkg_keys.h"
#include "orbislink/fpkg/param_sfo.h"
#include "orbislink/fpkg/pfs_builder.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace orbislink::fpkg {

namespace {

namespace fs = std::filesystem;

enum : uint32_t {
	kDigests = 0x1,
	kEntryKeys = 0x10,
	kImageKey = 0x20,
	kGeneralDigests = 0x80,
	kMetas = 0x100,
	kEntryNames = 0x200,
	kLicenseDat = 0x400,
	kLicenseInfo = 0x401,
	kPsReservedDat = 0x409,
	kParamSfo = 0x1000,
	kPlaygoChunkDat = 0x1001,
	kPlaygoChunkSha = 0x1002,
	kPlaygoManifest = 0x1003,
};

constexpr uint32_t kDrmPs4 = 0xF;
constexpr uint32_t kContentGd = 0x1A;
constexpr uint32_t kContentFlags = 0x08000000 | 0x02000000;
constexpr uint64_t kBodyOffset = 0x2000;
constexpr uint64_t kChunk = 0x10000;

template<typename T> void putBe(uint8_t *p, T v)
{
	for(size_t i = 0; i < sizeof(T); ++i)
		p[i] = static_cast<uint8_t>(static_cast<uint64_t>(v) >> (8 * (sizeof(T) - 1 - i)));
}
template<typename T> void putLe(uint8_t *p, T v)
{
	for(size_t i = 0; i < sizeof(T); ++i)
		p[i] = static_cast<uint8_t>(static_cast<uint64_t>(v) >> (8 * i));
}
uint64_t alignUp(uint64_t v, uint64_t a) { return v % a ? v + (a - v % a) : v; }

struct Meta
{
	uint32_t id = 0;
	uint32_t nameOffset = 0;
	uint32_t flags1 = 0;
	uint32_t flags2 = 0;
	uint32_t dataOffset = 0;
	uint32_t dataSize = 0;

	Bytes bytes() const
	{
		Bytes b(32, 0);
		putBe(b.data(), id);
		putBe(b.data() + 4, nameOffset);
		putBe(b.data() + 8, flags1);
		putBe(b.data() + 12, flags2);
		putBe(b.data() + 16, dataOffset);
		putBe(b.data() + 20, dataSize);
		return b;
	}
	uint32_t keyIndex() const { return (flags2 & 0xF000) >> 12; }
	bool encrypted() const { return (flags1 & 0x80000000u) != 0; }
};

struct Entry
{
	uint32_t id = 0;
	std::string name; // empty: no name in the table
	Bytes data;
	Meta meta;
};

struct Header
{
	uint32_t entryCount = 6;
	uint32_t mainEntDataSize = 0xD00;
	uint64_t bodySize = 0x7E000;
	std::string contentId;
	uint32_t promoteSize = 0;
	Digest scEntries1 {}, scEntries2 {}, digestTable {}, bodyDigest {};
	uint64_t pfsImageOffset = 0x80000;
	uint64_t pfsImageSize = 0;
	uint64_t mountImageSize = 0;
	uint64_t packageSize = 0;
	Digest pfsImageDigest {}, pfsSignedDigest {};

	Bytes serialize() const
	{
		Bytes h(0x490, 0);
		uint8_t *p = h.data();
		p[0] = 0x7F;
		p[1] = 'C';
		p[2] = 'N';
		p[3] = 'T';
		putBe<uint32_t>(p + 0x04, 1);
		putBe<uint32_t>(p + 0x08, 0);
		putBe<uint32_t>(p + 0x0C, 0xF);
		putBe<uint32_t>(p + 0x10, entryCount);
		putBe<uint16_t>(p + 0x14, 6);
		putBe<uint16_t>(p + 0x16, static_cast<uint16_t>(entryCount));
		putBe<uint32_t>(p + 0x18, 0x2A80);
		putBe<uint32_t>(p + 0x1C, mainEntDataSize);
		putBe<uint64_t>(p + 0x20, kBodyOffset);
		putBe<uint64_t>(p + 0x28, bodySize);
		std::memcpy(p + 0x40, contentId.data(), std::min<size_t>(contentId.size(), 0x30));
		putBe<uint32_t>(p + 0x70, kDrmPs4);
		putBe<uint32_t>(p + 0x74, kContentGd);
		putBe<uint32_t>(p + 0x78, kContentFlags);
		putBe<uint32_t>(p + 0x7C, promoteSize);
		putBe<uint32_t>(p + 0x80, 0x20161020);
		putBe<uint32_t>(p + 0x84, 0x1738551);
		putBe<uint32_t>(p + 0x9C, 1); // ekc version
		std::memcpy(p + 0x100, scEntries1.data(), 32);
		std::memcpy(p + 0x120, scEntries2.data(), 32);
		std::memcpy(p + 0x140, digestTable.data(), 32);
		std::memcpy(p + 0x160, bodyDigest.data(), 32);
		putBe<uint32_t>(p + 0x400, 1);
		putBe<uint32_t>(p + 0x404, 1); // one PFS image
		putBe<uint64_t>(p + 0x408, 0x80000000000003CCull);
		putBe<uint64_t>(p + 0x410, pfsImageOffset);
		putBe<uint64_t>(p + 0x418, pfsImageSize);
		putBe<uint64_t>(p + 0x420, 0);
		putBe<uint64_t>(p + 0x428, mountImageSize);
		putBe<uint64_t>(p + 0x430, packageSize);
		putBe<uint32_t>(p + 0x438, 0x10000);
		putBe<uint32_t>(p + 0x43C, 0xD0000);
		std::memcpy(p + 0x440, pfsImageDigest.data(), 32);
		std::memcpy(p + 0x460, pfsSignedDigest.data(), 32);
		return h;
	}
};

// The output file: written at absolute offsets, read back for the digests.
class OutFile
{
public:
	OutFile(const std::string &path, uint64_t size) : path_(fs::u8path(path))
	{
		{
			std::ofstream create(path_, std::ios::binary | std::ios::trunc);
			if(!create)
				throw std::runtime_error("cannot create " + path);
		}
		std::error_code ec;
		fs::resize_file(path_, size, ec);
		if(ec)
			throw std::runtime_error("cannot make room for the package: " + ec.message());
		file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
		if(!file_)
			throw std::runtime_error("cannot open " + path);
	}
	void write(uint64_t offset, const void *data, size_t size)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		file_.seekp(static_cast<std::streamoff>(offset));
		file_.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
		if(!file_)
			throw std::runtime_error("writing the package failed (is the disk full?)");
	}
	void read(uint64_t offset, void *data, size_t size)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		file_.seekg(static_cast<std::streamoff>(offset));
		file_.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
		if(!file_)
			throw std::runtime_error("reading the package back failed");
	}
	Digest sha256(uint64_t offset, uint64_t size, const std::function<void(uint64_t)> &step = {})
	{
		Sha256 hash;
		Bytes buffer(1 << 20);
		uint64_t done = 0;
		while(done < size)
		{
			const size_t n = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - done));
			read(offset + done, buffer.data(), n);
			hash.update(buffer.data(), n);
			done += n;
			if(step)
				step(n);
		}
		return hash.finish();
	}
	void close() { file_.close(); }

private:
	fs::path path_;
	std::fstream file_;
	std::mutex mutex_;
};

Bytes makeEntryKeys(const std::string &contentId, const std::string &passcode)
{
	Bytes out(2048, 0);
	uint8_t id[48] = {};
	std::memcpy(id, contentId.data(), 36);
	const Digest seed = sha256(id, 48);
	std::memcpy(out.data(), seed.data(), 32);
	for(uint32_t i = 0; i < 7; ++i)
	{
		const Digest key = computeKeys(contentId, passcode, i);
		Digest digest = sha256(key.data(), key.size());
		for(int k = 0; k < 32; ++k)
			digest[k] ^= key[k];
		std::memcpy(out.data() + 32 + 32 * i, digest.data(), 32);
		const Bytes encrypted = i == 0
			? rsa2048EncryptKey(kPkgPublicKeys[0], reinterpret_cast<const uint8_t *>(passcode.data()))
			: rsa2048EncryptKey(kPkgPublicKeys[i], key.data());
		std::memcpy(out.data() + 256 + 256 * i, encrypted.data(), 256);
	}
	return out;
}

Bytes makeLicenseDat(const std::string &contentId)
{
	Bytes d(0x400, 0);
	uint8_t *p = d.data();
	putBe<uint32_t>(p, 0x52494600); // "RIF\0"
	putBe<int16_t>(p + 4, 1);
	putBe<int16_t>(p + 6, -1);
	putBe<uint64_t>(p + 8, 0);
	putBe<int64_t>(p + 0x10, 1364222275LL);
	putBe<int64_t>(p + 0x18, INT64_MAX);
	std::memcpy(p + 0x20, contentId.data(), 36);
	putBe<int16_t>(p + 0x50, 0x200); // debug licence
	putBe<int16_t>(p + 0x52, static_cast<int16_t>(kDrmPs4));
	putBe<int16_t>(p + 0x54, static_cast<int16_t>(kContentGd));
	putBe<int16_t>(p + 0x56, 3); // SKU flag, needed for GD
	putBe<int32_t>(p + 0x64, 1);
	uint8_t id[48] = {};
	std::memcpy(id, contentId.data(), 36);
	const Digest h = sha256(id, 48);
	uint8_t *iv = p + 0x260;
	uint8_t *secret = p + 0x270;
	std::memcpy(iv, h.data(), 16);
	std::memcpy(secret, h.data() + 16, 16);
	aes128CbcEncrypt(secret, 144, kRifDebugKey, iv);
	const Digest toSign = sha256(p, 0x300);
	const Bytes signature = rsa2048SignSha256(toSign, kDebugRifModulus, kDebugRifPrivateExponent);
	std::memcpy(p + 0x300, signature.data(), 256);
	return d;
}

Bytes makeLicenseInfo(const std::string &contentId)
{
	Bytes d(0x200, 0);
	std::memcpy(d.data(), contentId.data(), 36);
	putBe<int32_t>(d.data() + 0x40, 0);
	putBe<int32_t>(d.data() + 0x44, static_cast<int32_t>(kContentGd));
	putBe<int32_t>(d.data() + 0x48, 0);
	putBe<int32_t>(d.data() + 0x4C, 1);
	return d;
}

Bytes makeChunkDat(const std::string &contentId, uint64_t packageSize, uint64_t innerSize)
{
	Bytes d(416, 0);
	uint8_t *p = d.data();
	putLe<uint32_t>(p, 0x6f676c70); // "plgo"
	putLe<uint16_t>(p + 8, 1);      // images
	putLe<uint16_t>(p + 10, 1);     // chunks
	putLe<uint16_t>(p + 12, 1);     // mchunks
	putLe<uint16_t>(p + 14, 1);     // scenarios
	putLe<uint32_t>(p + 16, 416);
	putLe<uint16_t>(p + 20, 0);
	putLe<uint16_t>(p + 22, 1);
	std::memset(p + 32, 0xFF, 32);
	std::memcpy(p + 64, contentId.data(), 36);
	static const uint32_t table[16] = {256, 32, 288, 2, 304, 9, 320, 16, 352, 32, 384, 2, 400, 12, 336, 16};
	for(int i = 0; i < 16; ++i)
		putLe<uint32_t>(p + 0xC0 + 4 * i, table[i]);
	// Chunk #0
	p[0x100] = 0x80;
	p[0x102] = 3;
	putLe<uint16_t>(p + 0x10E, 1);
	putLe<uint64_t>(p + 0x110, ~0ull);
	std::memcpy(p + 0x130, "Chunk #0", 8);
	putLe<uint64_t>(p + 0x148, packageSize);
	putLe<uint64_t>(p + 0x158, innerSize);
	// Scenario #0
	p[0x160] = 1;
	putLe<uint16_t>(p + 0x174, 1);
	putLe<uint16_t>(p + 0x176, 1);
	std::memcpy(p + 0x190, "Scenario #0", 11);
	return d;
}

Bytes playgoManifest()
{
	static const char text[] =
		"\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n"
		"<psproject fmt=\"playgo-manifest\" version=\"0990\">\r\n"
		"  <volume>\r\n"
		"    <chunk_info chunk_count=\"1\" scenario_count=\"1\">\r\n"
		"      <scenarios default_id=\"0\">\r\n"
		"        <scenario id=\"0\" type=\"sp\" initial_chunk_count=\"1\" label=\"Scenario #0\">0</scenario>\r\n"
		"      </scenarios>\r\n"
		"    </chunk_info>\r\n"
		"  </volume>\r\n"
		"</psproject>\r\n";
	return Bytes(text, text + sizeof text - 1);
}

std::string majorParamString(const ParamSfo &sfo)
{
	auto value = [&](const char *name) {
		const ParamSfo::Value *v = sfo.find(name);
		return v ? v->toString() : std::string();
	};
	std::string s = "ATTRIBUTE" + value("ATTRIBUTE");
	if(sfo.find("ATTRIBUTE2"))
		s += "ATTRIBUTE2" + value("ATTRIBUTE2");
	s += "CATEGORY" + value("CATEGORY");
	s += "FORMAT" + value("FORMAT");
	s += "PUBTOOLVER" + value("PUBTOOLVER");
	return s;
}

class Builder
{
public:
	Builder(const PkgRequest &request, const PkgProgress &progress)
		: req_(request), progress_(progress)
	{
	}

	void run(const std::string &outputPath);

private:
	void step(const std::string &stage, uint64_t done, uint64_t total)
	{
		if(progress_ && !progress_(stage, done, total))
			throw std::runtime_error("cancelled");
	}
	void buildTree();
	void layout(uint64_t pfsSize, uint64_t innerSize, uint64_t chunkShaReserve);

	const PkgRequest &req_;
	PkgProgress progress_;
	FsDir root_;
	std::vector<std::shared_ptr<std::ifstream>> handles_;
	ParamSfo sfo_;
	std::vector<std::pair<uint32_t, Bytes>> sceSysEntries_;

	Header header_;
	std::vector<Entry> entries_; // in body order
	std::vector<size_t> metaOrder_; // entries sorted by id
	size_t chunkSha_ = 0;
	uint64_t chunkShaNeeded_ = 0;
};

void Builder::buildTree()
{
	for(const PkgSource &src : req_.files)
	{
		const size_t slash = src.targetPath.rfind('/');
		FsDir *dir = slash == std::string::npos ? &root_ : root_.makeDirs(src.targetPath.substr(0, slash));
		auto file = std::make_unique<FsFile>();
		file->name = slash == std::string::npos ? src.targetPath : src.targetPath.substr(slash + 1);
		file->parent = dir;
		if(src.sourcePath.empty())
		{
			auto data = std::make_shared<Bytes>(src.data);
			file->size = data->size();
			file->read = [data](uint64_t offset, uint8_t *out, size_t length) {
				std::memcpy(out, data->data() + offset, length);
			};
		}
		else
		{
			std::error_code ec;
			const auto size = fs::file_size(fs::u8path(src.sourcePath), ec);
			if(ec)
				throw std::runtime_error("cannot read " + src.sourcePath);
			auto in = std::make_shared<std::ifstream>(fs::u8path(src.sourcePath), std::ios::binary);
			if(!*in)
				throw std::runtime_error("cannot open " + src.sourcePath);
			handles_.push_back(in);
			file->size = size;
			const std::string name = src.sourcePath;
			file->read = [in, name](uint64_t offset, uint8_t *out, size_t length) {
				in->clear();
				in->seekg(static_cast<std::streamoff>(offset));
				in->read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(length));
				if(!*in)
					throw std::runtime_error("reading " + name + " failed");
			};
		}
		file->compressedSize = file->size;
		dir->files.push_back(std::move(file));
	}

	FsDir *sceSys = root_.childDir("sce_sys");
	FsFile *sfoFile = sceSys ? sceSys->findFile("param.sfo") : nullptr;
	if(!sfoFile)
		throw std::runtime_error("the package has no sce_sys/param.sfo");
	Bytes sfoBytes(static_cast<size_t>(sfoFile->size));
	sfoFile->read(0, sfoBytes.data(), sfoBytes.size());
	std::string error;
	if(!sfo_.parse(sfoBytes, &error))
		throw std::runtime_error(error);

	// Application packages carry a keystone made from the passcode.
	if(!sceSys->findFile("keystone"))
	{
		auto keystone = std::make_shared<Bytes>(createKeystone(req_.passcode));
		auto file = std::make_unique<FsFile>();
		file->name = "keystone";
		file->parent = sceSys;
		file->size = file->compressedSize = keystone->size();
		file->read = [keystone](uint64_t offset, uint8_t *out, size_t length) {
			std::memcpy(out, keystone->data() + offset, length);
		};
		sceSys->files.push_back(std::move(file));
	}

	for(auto &f : sceSys->files)
	{
		const uint32_t id = pkgEntryIdForName(f->name);
		if(id == 0 || f->name == "param.sfo")
			continue;
		Bytes data(static_cast<size_t>(f->size));
		f->read(0, data.data(), data.size());
		sceSysEntries_.emplace_back(id, std::move(data));
	}
}

void Builder::layout(uint64_t pfsSize, uint64_t innerSize, uint64_t chunkShaReserve)
{
	header_ = Header();
	header_.contentId = req_.contentId;
	header_.pfsImageSize = pfsSize;
	header_.packageSize = 0x80000 + pfsSize;

	ParamSfo sfo = sfo_;
	std::string info = "c_date=" + req_.creationDate;
	if(!req_.creationTime.empty())
		info += ",c_time=" + req_.creationTime;
	info += ",img0_l0_size=" + std::to_string((header_.packageSize + 0xFFFFF) / (1024 * 1024))
		+ ",img0_l1_size=0,img0_sc_ksize=512,img0_pc_ksize=832";
	sfo.setString("PUBTOOLINFO", info, 0x200);
	sfo.setInteger("PUBTOOLVER", 0x02890000);
	sfo_ = sfo;

	entries_.clear();
	auto add = [&](uint32_t id, const std::string &name, Bytes data) {
		Entry e;
		e.id = id;
		e.name = name;
		e.data = std::move(data);
		entries_.push_back(std::move(e));
	};
	const Digest ekpfs = computeKeys(req_.contentId, req_.passcode, 1);
	add(kEntryKeys, "", makeEntryKeys(req_.contentId, req_.passcode));
	add(kImageKey, "", rsa2048EncryptKey(kFakeKeysetModulus, ekpfs.data()));
	add(kGeneralDigests, "", Bytes(0x180, 0));
	add(kMetas, "", Bytes());
	add(kDigests, "", Bytes());
	add(kEntryNames, "", Bytes());
	add(kPlaygoChunkDat, "playgo-chunk.dat", makeChunkDat(req_.contentId, 0, innerSize));
	add(kPlaygoChunkSha, "playgo-chunk.sha", Bytes());
	chunkSha_ = entries_.size() - 1;
	add(kPlaygoManifest, "playgo-manifest.xml", playgoManifest());
	add(kLicenseDat, "", makeLicenseDat(req_.contentId));
	add(kLicenseInfo, "", makeLicenseInfo(req_.contentId));
	add(kParamSfo, "param.sfo", sfo.serialize());
	add(kPsReservedDat, "", Bytes(0x2000, 0));
	for(const auto &e : sceSysEntries_)
	{
		// The entry's name is the file's own.
		std::string name;
		for(const auto &f : root_.childDir("sce_sys")->files)
			if(pkgEntryIdForName(f->name) == e.first)
				name = f->name;
		add(e.first, name, e.second);
	}
	const size_t count = entries_.size();
	entries_[3].data.assign(count * 32, 0);  // metas
	entries_[4].data.assign(count * 32, 0);  // digests

	// The name table: names in order, each NUL-terminated after a leading NUL.
	std::vector<std::string> names;
	for(const Entry &e : entries_)
		if(!e.name.empty())
			names.push_back(e.name);
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	std::map<std::string, uint32_t> nameOffsets;
	Bytes nameTable(1, 0);
	for(const std::string &n : names)
	{
		nameOffsets[n] = static_cast<uint32_t>(nameTable.size());
		nameTable.insert(nameTable.end(), n.begin(), n.end());
		nameTable.push_back(0);
	}
	entries_[5].data = nameTable;

	static const std::map<uint32_t, uint32_t> flags1 = {{kDigests, 0x40000000}, {kEntryKeys, 0x60000000},
		{kImageKey, 0xE0000000}, {kGeneralDigests, 0x60000000}, {kMetas, 0x60000000},
		{kEntryNames, 0x40000000}, {kLicenseDat, 0x80000000}, {kLicenseInfo, 0x80000000}};
	static const std::map<uint32_t, uint32_t> flags2 = {
		{kImageKey, 3u << 12}, {kLicenseDat, 3u << 12}, {kLicenseInfo, 2u << 12}};

	uint64_t dataOffset = kBodyOffset;
	for(size_t i = 0; i < count; ++i)
	{
		Entry &e = entries_[i];
		Meta &m = e.meta;
		m.id = e.id;
		m.nameOffset = e.name.empty() ? 0 : nameOffsets.at(e.name);
		m.dataOffset = static_cast<uint32_t>(dataOffset);
		m.dataSize = static_cast<uint32_t>(e.data.size());
		const auto f1 = flags1.find(e.id);
		m.flags1 = f1 == flags1.end() ? 0 : f1->second;
		const auto f2 = flags2.find(e.id);
		m.flags2 = f2 == flags2.end() ? 0 : f2->second;
		if(i == chunkSha_)
		{
			// As LibOrbisPkg estimates it: the metas table only as long as
			// it is so far, the hash table itself not yet.
			uint64_t estimate = kBodyOffset;
			for(size_t k = 0; k < count; ++k)
			{
				if(k == 3)
					estimate += alignUp((i + 1) * 32, 16);
				else if(k != chunkSha_)
					estimate += alignUp(entries_[k].data.size(), 16);
			}
			estimate = alignUp(estimate, 0x80000) + pfsSize;
			estimate += ((estimate + 16) / kChunk) * 4;
			m.dataSize = static_cast<uint32_t>(std::max<uint64_t>((estimate / kChunk) * 4, chunkShaReserve));
		}
		dataOffset = alignUp(dataOffset + m.dataSize, 16);
	}
	const uint64_t bodySize = dataOffset - kBodyOffset;
	metaOrder_.resize(count);
	for(size_t i = 0; i < count; ++i)
		metaOrder_[i] = i;
	std::stable_sort(metaOrder_.begin(), metaOrder_.end(),
		[&](size_t a, size_t b) { return entries_[a].id < entries_[b].id; });

	header_.entryCount = static_cast<uint32_t>(count);
	header_.bodySize = alignUp(kBodyOffset + bodySize, 0x80000) - kBodyOffset;
	header_.mainEntDataSize = 0;
	for(size_t i = 0; i < 5; ++i)
		header_.mainEntDataSize += static_cast<uint32_t>(entries_[i].data.size());
	header_.pfsImageOffset = kBodyOffset + header_.bodySize;
	header_.packageSize = header_.mountImageSize = header_.bodySize + kBodyOffset + pfsSize;

	chunkShaNeeded_ = 4 * (header_.packageSize / kChunk);
	if(chunkShaNeeded_ <= entries_[chunkSha_].meta.dataSize)
	{
		entries_[chunkSha_].data.assign(static_cast<size_t>(chunkShaNeeded_), 0);
		entries_[chunkSha_].meta.dataSize = static_cast<uint32_t>(chunkShaNeeded_);
	}
	entries_[6].data = makeChunkDat(req_.contentId, header_.packageSize, innerSize);
	header_.promoteSize = static_cast<uint32_t>(header_.bodySize + kBodyOffset);
}

void Builder::run(const std::string &outputPath)
{
	if(req_.contentId.size() != 36)
		throw std::runtime_error("the content ID must be 36 characters long");
	if(req_.passcode.size() != 32)
		throw std::runtime_error("the passcode must be 32 characters long");
	step("prepare", 0, 1);
	buildTree();

	const Digest ekpfs = computeKeys(req_.contentId, req_.passcode, 1);
	PfsOptions innerOptions;
	innerOptions.minBlocks = 0x55;
	innerOptions.fileTime = req_.volumeTime;
	PfsImage inner(root_, innerOptions);

	FsDir outerRoot;
	{
		auto pfsc = makePfscFile(inner);
		pfsc->parent = &outerRoot;
		outerRoot.files.push_back(std::move(pfsc));
	}
	PfsOptions outerOptions;
	outerOptions.sign = true;
	outerOptions.encrypt = true;
	outerOptions.ekpfs.assign(ekpfs.begin(), ekpfs.end());
	outerOptions.seed.assign(16, 0);
	outerOptions.fileTime = req_.volumeTime;
	PfsImage outer(outerRoot, outerOptions);

	// The PlayGo hash table is sized from an estimate; if the real package
	// came out bigger, lay it out again with the room it needs.
	const ParamSfo originalSfo = sfo_;
	layout(outer.size(), inner.size(), 0);
	if(chunkShaNeeded_ > entries_[chunkSha_].meta.dataSize)
	{
		const uint64_t needed = chunkShaNeeded_;
		sfo_ = originalSfo;
		layout(outer.size(), inner.size(), needed + 0x10000);
	}
	step("prepare", 1, 1);

	OutFile out(outputPath, header_.packageSize);

	// ── the image: straight from the sources into its place
	const uint64_t pfsOffset = header_.pfsImageOffset;
	Bytes &chunkSha = entries_[chunkSha_].data;
	const uint64_t firstChunk = pfsOffset / kChunk;
	const uint64_t imageSize = outer.size();
	outer.writeFinal(
		[&](uint64_t block, const uint8_t *data, const Digest &sha) {
			out.write(pfsOffset + block * kChunk, data, kChunk);
			std::memcpy(chunkSha.data() + (firstChunk + block) * 4, sha.data(), 4);
			if(block == 0)
				header_.pfsSignedDigest = sha;
		},
		[&](uint64_t done) {
			if(progress_)
				return progress_("image", done, imageSize);
			return true;
		});

	uint64_t hashed = 0;
	header_.pfsImageDigest = out.sha256(pfsOffset, imageSize, [&](uint64_t n) {
		hashed += n;
		if(hashed % (64ull << 20) < n || hashed == imageSize)
			step("digest", hashed, imageSize);
	});

	// ── general digests
	{
		const Bytes h = header_.serialize();
		Sha256 hh;
		hh.update(h.data(), 64);
		hh.update(h.data() + 0x400, 128);
		const Digest headerDigest = hh.finish();

		const Digest majorParam = sha256(majorParamString(sfo_).data(), majorParamString(sfo_).size());
		Bytes content(48, 0);
		std::memcpy(content.data(), req_.contentId.data(), 36);
		uint8_t be[4];
		putBe<uint32_t>(be, kDrmPs4);
		content.insert(content.end(), be, be + 4);
		putBe<uint32_t>(be, kContentGd);
		content.insert(content.end(), be, be + 4);
		content.insert(content.end(), header_.pfsImageDigest.begin(), header_.pfsImageDigest.end());
		content.insert(content.end(), majorParam.begin(), majorParam.end());
		const Digest contentDigest = sha256(content);
		const Bytes sfoBytes = sfo_.serialize();
		const Digest paramDigest = sha256(sfoBytes);

		Bytes &g = entries_[2].data;
		std::fill(g.begin(), g.end(), 0);
		putBe<uint16_t>(g.data(), 0xD256);
		putBe<uint16_t>(g.data() + 2, 0x100);
		putBe<int32_t>(g.data() + 28, 0x2 | 0x4 | 0x8 | 0x20 | 0x40);
		std::memcpy(g.data() + 32, contentDigest.data(), 32);
		std::memcpy(g.data() + 64, header_.pfsImageDigest.data(), 32);
		std::memcpy(g.data() + 96, headerDigest.data(), 32);
		std::memcpy(g.data() + 160, majorParam.data(), 32);
		std::memcpy(g.data() + 192, paramDigest.data(), 32);
	}

	// ── body
	{
		Bytes &metas = entries_[3].data;
		for(size_t i = 0; i < metaOrder_.size(); ++i)
		{
			const Bytes m = entries_[metaOrder_[i]].meta.bytes();
			std::memcpy(metas.data() + 32 * i, m.data(), 32);
		}
	}
	for(Entry &e : entries_)
	{
		Bytes data = e.data;
		if(e.meta.encrypted())
		{
			Bytes seed = e.meta.bytes();
			const Digest k = computeKeys(req_.contentId, req_.passcode, e.meta.keyIndex());
			seed.insert(seed.end(), k.begin(), k.end());
			const Digest ivKey = sha256(seed);
			aes128CbcEncrypt(data.data(), data.size(), ivKey.data() + 16, ivKey.data());
		}
		if(!data.empty())
			out.write(e.meta.dataOffset, data.data(), data.size());
	}

	// ── body digests
	const Meta digestsMeta = entries_[4].meta;
	Bytes &digests = entries_[4].data;
	for(size_t i = 1; i < metaOrder_.size(); ++i)
	{
		const Meta &m = entries_[metaOrder_[i]].meta;
		const Digest d = out.sha256(m.dataOffset, m.dataSize);
		std::memcpy(digests.data() + 32 * i, d.data(), 32);
		out.write(digestsMeta.dataOffset + 32 * i, d.data(), 32);
	}
	step("finish", 0, 1);
	header_.bodyDigest = out.sha256(kBodyOffset, header_.bodySize);
	header_.digestTable = sha256(digests);
	{
		Sha256 sc1, sc2;
		for(size_t i = 0; i < 5; ++i)
		{
			const Meta &m = entries_[i].meta;
			Bytes data(m.dataSize);
			out.read(m.dataOffset, data.data(), data.size());
			sc1.update(data.data(), data.size());
			if(i < 4)
				sc2.update(data.data(), i == 3 ? std::min<size_t>(data.size(), 6 * 0x20) : data.size());
		}
		header_.scEntries1 = sc1.finish();
		header_.scEntries2 = sc2.finish();
	}

	// ── header, its digest and its signature
	const Bytes h = header_.serialize();
	out.write(0, h.data(), h.size());
	const Digest headerSha = out.sha256(0, 0xFE0);
	out.write(0xFE0, headerSha.data(), 32);
	const Digest whole = out.sha256(0, 0x1000);
	const Bytes signature = rsa2048EncryptKey(kPkgPublicKeys[3], whole.data());
	out.write(0x1000, signature.data(), signature.size());
	out.close();
	step("finish", 1, 1);
}

} // namespace

bool buildFakePkg(const PkgRequest &request, const std::string &outputPath,
	const PkgProgress &progress, std::string *error)
{
	try
	{
		Builder builder(request, progress);
		builder.run(outputPath);
		return true;
	}
	catch(const std::exception &e)
	{
		if(error)
			*error = e.what();
		std::error_code ec;
		std::filesystem::remove(std::filesystem::u8path(outputPath), ec);
		return false;
	}
}

} // namespace orbislink::fpkg
