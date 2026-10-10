// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/library/console_library.h"

#include "orbislink/common/json.h"
#include "orbislink/pkg/sfo_parser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>

namespace orbislink {
namespace library {

namespace {

std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string upper(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return text;
}

std::string extensionOf(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos || dot == 0 ? std::string() : lower(name.substr(dot));
}

bool startsWith(const std::string &text, const std::string &prefix)
{
	return text.compare(0, prefix.size(), prefix) == 0;
}

// "/mnt/usb3/…" → "/mnt/usb3"; empty when `path` is not on such a drive.
std::string mountPointOf(const std::string &path, const std::string &prefix)
{
	if(!startsWith(path, prefix) || path.size() <= prefix.size()
		|| !std::isdigit(static_cast<unsigned char>(path[prefix.size()])))
		return std::string();
	size_t end = prefix.size();
	while(end < path.size() && std::isdigit(static_cast<unsigned char>(path[end])))
		++end;
	return end == path.size() || path[end] == '/' ? path.substr(0, end) : std::string();
}

} // namespace

std::vector<std::string> libraryFolders()
{
	std::vector<std::string> folders = { std::string("/data/") + kFolderName };
	for(int i = 0; i < 8; ++i)
		folders.push_back("/mnt/usb" + std::to_string(i) + "/" + kFolderName);
	for(int i = 0; i < 2; ++i)
		folders.push_back("/mnt/ext" + std::to_string(i) + "/" + kFolderName);
	return folders;
}

Kind kindOf(const std::string &name, bool directory, int64_t size)
{
	if(directory)
		return Kind::Folder;
	const std::string ext = extensionOf(name);
	if(ext == ".pkg")
		return Kind::Package;
	if(ext == ".iso" || ext == ".img" || ext == ".cue")
		return Kind::Disc;
	// A disc's track is hundreds of megabytes; a payload a few.
	if(ext == ".bin")
		return size >= 32ll * 1024 * 1024 ? Kind::Disc : Kind::Payload;
	if(ext == ".ffpkg" || ext == ".exfat" || ext == ".ffpfs" || ext == ".ffpfsc")
		return Kind::Image;
	if(ext == ".elf" || ext == ".self" || ext == ".lua" || ext == ".js" || ext == ".jar")
		return Kind::Payload;
	if(ext == ".zip" || ext == ".7z" || ext == ".rar")
		return Kind::Archive;
	return Kind::Other;
}

const char *kindName(Kind kind)
{
	switch(kind)
	{
		case Kind::Package: return "package";
		case Kind::Disc: return "disc";
		case Kind::Image: return "image";
		case Kind::Folder: return "folder";
		case Kind::Payload: return "payload";
		case Kind::Archive: return "archive";
		case Kind::Other: break;
	}
	return "other";
}

std::string titleIdInName(const std::string &name)
{
	// The prefixes of PS5, PS4, homebrew and PS1/PS2 discs; anything else
	// that looks like four letters and five digits is left alone.
	static const std::regex pattern(
		"(PPSA|CUSA|PCJS|PLJS|PLJM|PCAS|PLAS|ELJM|ELJS|ECAS|ELAS|LAPY|FAKE|SLUS|SCUS|SLES|SCES|SLPM|SLPS|SCPS|"
		"SCPM|SLKA|SCKA|SCAJ|SLAJ|SCED|SLED|PBPX|PAPX|PCPX|SIPS)[-_ ]?([0-9]{3})\\.?([0-9]{2})(?![0-9])");
	std::smatch match;
	const std::string text = upper(name);
	if(!std::regex_search(text, match, pattern))
		return std::string();
	return match[1].str() + match[2].str() + match[3].str();
}

std::string driveOf(const std::string &path)
{
	if(startsWith(path, "/mnt/usb"))
		return "usb";
	if(startsWith(path, "/mnt/ext"))
		return "ext";
	return "internal";
}

std::string mountFolderFor(const std::string &path)
{
	if(startsWith(path, "/data/"))
		return "/data/homebrew";
	for(const char *prefix : { "/mnt/usb", "/mnt/ext" })
	{
		const std::string point = mountPointOf(path, prefix);
		if(!point.empty())
			return point + "/homebrew";
	}
	return std::string();
}

AppParams readParamJson(const std::string &json)
{
	AppParams params;
	const Json root = Json::parse(json);
	if(!root.isObject())
		return params;
	params.titleId = root["titleId"].toString();
	params.contentId = root["contentId"].toString();
	params.version = root["contentVersion"].toString();
	// The name in the default language, else the first there is.
	const Json &localized = root["localizedParameters"];
	if(localized.isObject())
	{
		const std::string language = localized["defaultLanguage"].toString("en-US");
		params.title = localized[language]["titleName"].toString();
		for(const auto &entry : localized.members())
			if(params.title.empty() && entry.second.isObject())
				params.title = entry.second["titleName"].toString();
	}
	params.ok = !params.titleId.empty();
	return params;
}

AppParams readParamSfo(const std::vector<uint8_t> &sfo)
{
	AppParams params;
	Sfo parsed;
	if(!parsed.parse(sfo))
		return params;
	params.titleId = parsed.stringValue("TITLE_ID");
	params.title = parsed.stringValue("TITLE");
	params.version = parsed.stringValue("APP_VER", parsed.stringValue("VERSION"));
	params.contentId = parsed.stringValue("CONTENT_ID");
	params.ok = !params.titleId.empty();
	return params;
}

BlockReader::BlockReader(Fetch fetch, int64_t size, size_t blockSize)
	: fetch_(std::move(fetch)), size_(size), blockSize_(blockSize == 0 ? 64 * 1024 : blockSize)
{
}

const BlockReader::Block *BlockReader::block(int64_t index)
{
	const int64_t offset = index * static_cast<int64_t>(blockSize_);
	for(const Block &kept : cache_)
		if(kept.offset == offset)
			return &kept;
	if(offset >= size_)
		return nullptr;
	Block fresh;
	fresh.offset = offset;
	const size_t length = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(blockSize_), size_ - offset));
	++fetches_;
	if(!fetch_(offset, length, &fresh.bytes) || fresh.bytes.size() < length)
		return nullptr;
	// A few blocks are plenty for a header: the oldest goes.
	if(cache_.size() >= 16)
		cache_.erase(cache_.begin());
	cache_.push_back(std::move(fresh));
	return &cache_.back();
}

bool BlockReader::read(int64_t offset, void *out, size_t length)
{
	if(offset < 0 || offset + static_cast<int64_t>(length) > size_)
		return false;
	auto *target = static_cast<uint8_t *>(out);
	size_t done = 0;
	while(done < length)
	{
		const int64_t at = offset + static_cast<int64_t>(done);
		const Block *found = block(at / static_cast<int64_t>(blockSize_));
		if(!found)
			return false;
		const size_t inside = static_cast<size_t>(at - found->offset);
		const size_t take = std::min(length - done, found->bytes.size() - inside);
		std::memcpy(target + done, found->bytes.data() + inside, take);
		done += take;
	}
	return true;
}

} // namespace library
} // namespace orbislink
