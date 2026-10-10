// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/fpkg/disc_scanner.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

namespace orbislink::fpkg {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string s)
{
	for(char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while(a < b && std::isspace(static_cast<unsigned char>(s[a])))
		++a;
	while(b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

uint32_t le32(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Reads the 2048-byte user data of a sector, whatever the image's sector
// size: plain ISO (2048), raw CD (2352, mode 1 or mode 2) or 2336.
class SectorReader
{
public:
	explicit SectorReader(ByteReader read) : read_(std::move(read)) {}

	bool detect()
	{
		static const struct
		{
			uint32_t size, offset;
		} layouts[] = {{2048, 0}, {2352, 24}, {2352, 16}, {2336, 8}};
		for(const auto &l : layouts)
		{
			sectorSize_ = l.size;
			dataOffset_ = l.offset;
			uint8_t pvd[2048];
			if(read(16, pvd) && pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0)
			{
				std::memcpy(pvd_, pvd, sizeof pvd_);
				return true;
			}
		}
		return false;
	}

	bool read(uint64_t lba, uint8_t *out) { return read_(lba * sectorSize_ + dataOffset_, out, 2048); }

	const uint8_t *pvd() const { return pvd_; }
	uint32_t sectorSize() const { return sectorSize_; }

private:
	ByteReader read_;
	uint32_t sectorSize_ = 2048;
	uint32_t dataOffset_ = 0;
	uint8_t pvd_[2048] = {};
};

// SYSTEM.CNF from the root directory, or empty.
std::string readSystemCnf(SectorReader &reader)
{
	const uint8_t *root = reader.pvd() + 156;
	const uint32_t extent = le32(root + 2);
	const uint32_t length = std::min<uint32_t>(le32(root + 10), 64 * 2048);
	uint8_t sector[2048];
	for(uint32_t s = 0; s * 2048 < length; ++s)
	{
		if(!reader.read(extent + s, sector))
			return {};
		for(size_t pos = 0; pos < 2048;)
		{
			const uint8_t len = sector[pos];
			if(len == 0 || pos + len > 2048)
				break;
			const uint8_t nameLen = sector[pos + 32];
			std::string name(reinterpret_cast<const char *>(sector + pos + 33),
				std::min<size_t>(nameLen, len > 33 ? len - 33 : 0));
			const size_t semicolon = name.find(';');
			if(semicolon != std::string::npos)
				name.resize(semicolon);
			if(lower(name) == "system.cnf")
			{
				const uint32_t fileExtent = le32(sector + pos + 2);
				const uint32_t fileSize = std::min<uint32_t>(le32(sector + pos + 10), 4096);
				std::string text;
				uint8_t data[2048];
				for(uint32_t k = 0; k * 2048 < fileSize; ++k)
				{
					if(!reader.read(fileExtent + k, data))
						break;
					text.append(reinterpret_cast<const char *>(data),
						std::min<uint32_t>(2048, fileSize - k * 2048));
				}
				return text;
			}
			pos += len;
		}
	}
	return {};
}

std::string firstCueFile(const std::string &cuePath)
{
	std::ifstream in(fs::u8path(cuePath));
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const std::string name = cueImageName(text);
	return name.empty() ? std::string() : (fs::u8path(cuePath).parent_path() / fs::u8path(name)).u8string();
}

} // namespace

std::string cueImageName(const std::string &cueText)
{
	size_t pos = 0;
	while(pos < cueText.size())
	{
		size_t end = cueText.find('\n', pos);
		if(end == std::string::npos)
			end = cueText.size();
		const std::string line = cueText.substr(pos, end - pos);
		pos = end + 1;
		const std::string t = trim(line);
		if(lower(t.substr(0, 5)) != "file ")
			continue;
		std::string name;
		const size_t q1 = t.find('"');
		const size_t q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
		if(q1 != std::string::npos && q2 != std::string::npos)
			name = t.substr(q1 + 1, q2 - q1 - 1);
		else
		{
			name = trim(t.substr(5));
			const size_t space = name.rfind(' ');
			if(space != std::string::npos)
				name.resize(space);
		}
		return name;
	}
	return {};
}

std::string normaliseSerial(const std::string &text)
{
	std::string letters, digits;
	for(char c : text)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if(letters.size() < 4)
		{
			if(std::isalpha(u))
				letters += static_cast<char>(std::toupper(u));
			else if(!letters.empty())
				return {};
			continue;
		}
		if(std::isdigit(u))
		{
			digits += c;
			if(digits.size() == 5)
				break;
		}
		else if(c != '_' && c != '-' && c != '.' && c != ' ')
			return {};
	}
	if(letters.size() != 4 || digits.size() != 5)
		return {};
	return letters + "-" + digits;
}

std::string regionOfSerial(const std::string &serial)
{
	const std::string p = serial.substr(0, 4);
	if(p == "SLUS" || p == "SCUS" || p == "LSP-" || p == "PAPX" || p == "SLUD")
		return "USA";
	if(p == "SLES" || p == "SCES" || p == "SCED" || p == "SLED")
		return "Europe";
	if(p == "SLPS" || p == "SLPM" || p == "SCPS" || p == "SCAJ" || p == "SLAJ" || p == "SCPM"
		|| p == "SIPS" || p == "PBPX")
		return "Japan";
	if(p == "SLKA" || p == "SCKA")
		return "Asia";
	return {};
}

std::string titleFromFileName(const std::string &fileName, int *discNumber)
{
	std::string name = fs::u8path(fileName).stem().u8string();
	if(discNumber)
		*discNumber = 0;
	// "SLUS_209.05.Some Game": the serial in front is not part of the name.
	{
		static const std::regex serialPrefix(R"(^[A-Za-z]{4}[_-]?\d{3}\.?\d{2}[\s._-]*)");
		const std::string rest = std::regex_replace(name, serialPrefix, "");
		if(!rest.empty())
			name = rest;
	}
	std::string out;
	for(size_t i = 0; i < name.size(); ++i)
	{
		const char c = name[i];
		if(c == '(' || c == '[')
		{
			const char close = c == '(' ? ')' : ']';
			const size_t end = name.find(close, i);
			if(end == std::string::npos)
				break;
			const std::string inside = lower(name.substr(i + 1, end - i - 1));
			if(discNumber && inside.rfind("disc ", 0) == 0)
				*discNumber = std::atoi(inside.c_str() + 5);
			i = end;
			continue;
		}
		out += c == '_' ? ' ' : c;
	}
	std::string collapsed;
	for(char c : trim(out))
	{
		if(c == ' ' && !collapsed.empty() && collapsed.back() == ' ')
			continue;
		collapsed += c;
	}
	return collapsed.empty() ? name : collapsed;
}

DiscInfo inspectDisc(const std::string &path)
{
	std::string image = path;
	if(lower(fs::u8path(path).extension().u8string()) == ".cue")
	{
		image = firstCueFile(path);
		if(image.empty())
		{
			DiscInfo info;
			info.listedPath = path;
			info.path = path;
			info.fileName = fs::u8path(path).filename().u8string();
			info.title = titleFromFileName(info.fileName, &info.discNumber);
			info.problem = "the cue sheet names no file";
			return info;
		}
	}
	std::error_code ec;
	const uint64_t size = fs::file_size(fs::u8path(image), ec);
	auto file = std::make_shared<std::ifstream>(fs::u8path(image), std::ios::binary);
	DiscInfo info = inspectDisc(fs::u8path(image).filename().u8string(), ec ? 0 : size,
		[file](uint64_t offset, uint8_t *out, size_t length) {
			file->clear();
			file->seekg(static_cast<std::streamoff>(offset));
			file->read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(length));
			return static_cast<bool>(*file);
		});
	info.listedPath = path;
	info.path = image;
	// The name shown is the one found in the folder (the .cue's).
	info.fileName = fs::u8path(path).filename().u8string();
	info.title = titleFromFileName(info.fileName, &info.discNumber);
	if(ec)
		info.problem = "the file cannot be read";
	return info;
}

DiscInfo inspectDisc(const std::string &fileName, uint64_t size, const ByteReader &read)
{
	DiscInfo info;
	info.listedPath = fileName;
	info.path = fileName;
	info.fileName = fileName;
	info.title = titleFromFileName(info.fileName, &info.discNumber);
	const std::string ext = lower(fs::u8path(fileName).extension().u8string());
	info.format = ext.empty() ? std::string() : ext.substr(1);
	info.size = size;
	if(size == 0)
	{
		info.problem = "the file cannot be read";
		return info;
	}

	SectorReader reader(read);
	if(!reader.detect())
	{
		info.problem = "not a PlayStation disc image";
		return info;
	}
	const std::string cnf = readSystemCnf(reader);
	size_t pos = 0;
	while(pos < cnf.size())
	{
		size_t end = cnf.find_first_of("\r\n", pos);
		if(end == std::string::npos)
			end = cnf.size();
		const std::string line = cnf.substr(pos, end - pos);
		pos = end + 1;
		const size_t eq = line.find('=');
		if(eq == std::string::npos)
			continue;
		const std::string key = lower(trim(line.substr(0, eq)));
		if(key != "boot2" && key != "boot")
			continue;
		std::string value = trim(line.substr(eq + 1));
		const size_t cut = value.find_last_of("\\/:");
		if(cut != std::string::npos)
			value = value.substr(cut + 1);
		info.serial = normaliseSerial(value);
		info.platform = key == "boot2" ? "ps2" : "ps1";
		break;
	}
	if(info.platform.empty())
	{
		// A disc without SYSTEM.CNF boots PSX.EXE: an early PS1 game.
		const std::string system(reinterpret_cast<const char *>(reader.pvd() + 8), 32);
		if(system.find("PLAYSTATION") != std::string::npos)
			info.platform = reader.sectorSize() == 2048 && info.size > 900ull * 1024 * 1024 ? "ps2" : "ps1";
		else
			info.problem = "no PlayStation boot file on the disc";
	}
	if(!info.serial.empty())
	{
		info.titleId = info.serial.substr(0, 4) + info.serial.substr(5);
		info.region = regionOfSerial(info.serial);
	}
	return info;
}

std::vector<DiscInfo> scanFolder(const std::string &folder,
	const std::function<bool(const std::string &)> &progress)
{
	std::vector<std::string> cues, images;
	std::error_code ec;
	fs::recursive_directory_iterator it(fs::u8path(folder),
		fs::directory_options::skip_permission_denied, ec);
	for(; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
	{
		if(it.depth() > 3)
		{
			it.disable_recursion_pending();
			continue;
		}
		if(!it->is_regular_file(ec))
			continue;
		const std::string ext = lower(it->path().extension().u8string());
		if(ext == ".cue")
			cues.push_back(it->path().u8string());
		else if(ext == ".iso" || ext == ".bin" || ext == ".img")
			images.push_back(it->path().u8string());
	}

	std::vector<DiscInfo> found;
	std::set<std::string> covered;
	for(const std::string &cue : cues)
	{
		if(progress && !progress(cue))
			return found;
		DiscInfo info = inspectDisc(cue);
		covered.insert(fs::u8path(info.path).lexically_normal().u8string());
		if(!info.platform.empty())
			found.push_back(std::move(info));
	}
	for(const std::string &image : images)
	{
		if(covered.count(fs::u8path(image).lexically_normal().u8string()))
			continue;
		if(progress && !progress(image))
			return found;
		DiscInfo info = inspectDisc(image);
		if(!info.platform.empty())
			found.push_back(std::move(info));
	}
	std::sort(found.begin(), found.end(), [](const DiscInfo &a, const DiscInfo &b) {
		const std::string la = lower(a.title), lb = lower(b.title);
		return la != lb ? la < lb : a.discNumber < b.discNumber;
	});
	return found;
}

} // namespace orbislink::fpkg
