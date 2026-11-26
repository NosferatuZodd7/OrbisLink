// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/payloads/payload_catalog.h"

#include "orbislink/common/json.h"
#include "orbislink/common/util.h"

#include <algorithm>
#include <cctype>

namespace orbislink {
namespace payloads {

namespace {

bool plainFileName(const std::string &name)
{
	return !name.empty() && name.find('/') == std::string::npos && name.find('\\') == std::string::npos
		&& name != "." && name != "..";
}

} // namespace

std::vector<CatalogPayload> parseCatalog(const std::string &json, std::string *error)
{
	std::vector<CatalogPayload> list;
	std::string why;
	const Json root = Json::parse(json, &why);
	const Json &items = root.isObject() && root.contains("payloads") ? root["payloads"] : root;
	if(!items.isArray())
	{
		if(error)
			*error = why.empty() ? "not a list of payloads" : why;
		return list;
	}
	for(const Json &item : items.items())
	{
		CatalogPayload p;
		p.name = trim(item["name"].toString());
		p.filename = trim(item["filename"].toString());
		p.url = trim(item["url"].toString());
		if(p.name.empty() || !plainFileName(p.filename) || p.url.rfind("https://", 0) != 0)
			continue;
		p.source = item["source"].toString();
		p.sourceDirect = item["source_direct"].toString();
		p.description = item["description"].toString();
		p.lastUpdate = item["last_update"].toString();
		p.version = item["version"].toString();
		p.category = item["category"].toString();
		if(p.category.empty())
			p.category = "Uncategorized";
		p.checksum = toLower(trim(item["checksum"].toString()));
		list.push_back(p);
	}
	return list;
}

std::string detailsJson(const CatalogPayload &p, const std::string &downloadedAt)
{
	Json details = Json::makeObject();
	details.set("name", Json::fromString(p.name));
	details.set("filename", Json::fromString(p.filename));
	details.set("url", Json::fromString(p.url));
	details.set("source", Json::fromString(p.source));
	details.set("source_direct", Json::fromString(p.sourceDirect));
	details.set("description", Json::fromString(p.description));
	details.set("last_update", Json::fromString(p.lastUpdate));
	details.set("version", Json::fromString(p.version));
	details.set("checksum", Json::fromString(p.checksum));
	details.set("category", Json::fromString(p.category));
	details.set("downloaded_at", Json::fromString(downloadedAt));
	details.set("install_source", Json::fromString("repository"));
	details.set("install_source_detail", Json::fromString("OrbisLink"));
	details.set("source_name", Json::fromString(""));
	return details.dump() + "\n";
}

void readDetails(const std::string &json, std::string *name, std::string *version)
{
	const Json details = Json::parse(json);
	if(name)
		*name = details["name"].toString();
	if(version)
		*version = details["version"].toString();
}

bool isFileOf(const CatalogPayload &payload, const std::string &fileName)
{
	const std::string file = toLower(fileName);
	if(file == toLower(payload.filename))
		return true;
	const std::string name = toLower(payload.name);
	if(file.size() <= name.size() || file.compare(0, name.size(), name) != 0)
		return false;
	// The name, then a version or the extension: "kstuff-lite" is not "kstuff".
	const char next = file[name.size()];
	if(next == '.')
		return true;
	if((next == '_' || next == '-') && file.size() > name.size() + 1)
	{
		const char after = file[name.size() + 1];
		return after == 'v' || std::isdigit(static_cast<unsigned char>(after));
	}
	return false;
}

std::vector<AutoloadStep> autoloadSteps(const std::string &text)
{
	std::vector<AutoloadStep> steps;
	int wait = 0;
	size_t start = 0;
	while(start <= text.size())
	{
		size_t end = text.find('\n', start);
		if(end == std::string::npos)
			end = text.size();
		const std::string line = trim(text.substr(start, end - start));
		start = end + 1;
		if(line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		if(line[0] == '!')
		{
			const std::string digits = line.substr(1);
			if(!digits.empty() && std::all_of(digits.begin(), digits.end(),
					[](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
				wait += std::stoi(digits.substr(0, 9));
			continue;
		}
		steps.push_back({ line, wait });
		wait = 0;
	}
	return steps;
}

std::string autoloadText(const std::vector<AutoloadStep> &steps, const std::string &previous)
{
	const bool crlf = previous.find("\r\n") != std::string::npos;
	const char *eol = crlf ? "\r\n" : "\n";
	std::string out;
	size_t start = 0;
	while(start < previous.size())
	{
		size_t end = previous.find('\n', start);
		if(end == std::string::npos)
			end = previous.size();
		const std::string line = trim(previous.substr(start, end - start));
		start = end + 1;
		if(!line.empty() && (line[0] == '#' || line[0] == ';'))
			out += line + eol;
	}
	for(const AutoloadStep &step : steps)
	{
		if(step.name.empty())
			continue;
		if(step.delayMs > 0)
			out += "!" + std::to_string(step.delayMs) + eol;
		out += step.name + eol;
	}
	return out;
}

namespace {

// The release itself, or the newest of a list of them (pre-releases are
// left out of "latest").
Json releaseOf(const std::string &json)
{
	Json parsed = Json::parse(json);
	if(parsed.isArray())
		return parsed.items().empty() ? Json() : parsed.items().front();
	return parsed;
}

const Json *titanAsset(const Json &release, bool elf)
{
	for(const Json &asset : release["assets"].items())
	{
		const std::string name = asset["name"].toString();
		const std::string lowered = toLower(name);
		if(elf ? (lowered.size() > 4 && lowered.compare(lowered.size() - 4, 4, ".elf") == 0)
			   : lowered == "sha256sums" || lowered == "sha256sums.txt")
			return &asset;
	}
	return nullptr;
}

} // namespace

std::string titanCoreChecksumsUrl(const std::string &releaseJson)
{
	const Json release = releaseOf(releaseJson);
	const Json *sums = release.isObject() ? titanAsset(release, false) : nullptr;
	return sums ? (*sums)["browser_download_url"].toString() : std::string();
}

bool titanCoreFromRelease(const std::string &releaseJson, const std::string &sha256sums, CatalogPayload *out,
	std::string *error)
{
	const Json release = releaseOf(releaseJson);
	const Json *elf = release.isObject() ? titanAsset(release, true) : nullptr;
	if(!elf)
	{
		if(error)
			*error = "the release has no .elf";
		return false;
	}
	const std::string file = (*elf)["name"].toString();
	const std::string url = (*elf)["browser_download_url"].toString();
	// "<sha256>  <file>" (or "<sha256> *<file>"), one per line.
	std::string checksum;
	size_t start = 0;
	while(start < sha256sums.size())
	{
		size_t end = sha256sums.find('\n', start);
		if(end == std::string::npos)
			end = sha256sums.size();
		const std::string line = trim(sha256sums.substr(start, end - start));
		start = end + 1;
		const size_t space = line.find_first_of(" \t");
		if(space != 64 || line.size() <= space)
			continue;
		std::string named = trim(line.substr(space));
		if(!named.empty() && named.front() == '*')
			named.erase(0, 1);
		if(named == file)
			checksum = toLower(line.substr(0, 64));
	}
	if(checksum.size() != 64 || checksum.find_first_not_of("0123456789abcdef") != std::string::npos)
	{
		if(error)
			*error = "SHA256SUMS does not list " + file;
		return false;
	}
	CatalogPayload payload;
	payload.name = kTitanCoreName;
	payload.filename = file;
	payload.url = url;
	payload.source = release["html_url"].toString("https://github.com/devchmod/TITAN-CORE/releases");
	payload.sourceDirect = url;
	payload.description = "Starts installed PS5 fake packages on firmware up to 13.60 (kstuff-lite and "
						  "ShadowMountPlus inside). Experimental.";
	payload.lastUpdate = release["published_at"].toString().substr(0, 10);
	std::string version = release["tag_name"].toString();
	if(!version.empty() && (version.front() == 'v' || version.front() == 'V'))
		version.erase(0, 1);
	payload.version = version;
	payload.category = "Jailbreak";
	payload.checksum = checksum;
	*out = payload;
	return true;
}

} // namespace payloads
} // namespace orbislink
