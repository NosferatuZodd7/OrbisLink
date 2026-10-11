// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/library/installed_titles.h"

#include "orbislink/common/json.h"
#include "orbislink/common/util.h"
#include "orbislink/pkg/sfo_parser.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace orbislink {
namespace library {

namespace {

bool titleIdLike(const std::string &name)
{
	if(name.size() != 9)
		return false;
	for(size_t i = 0; i < 4; ++i)
		if(!std::isupper(static_cast<unsigned char>(name[i])))
			return false;
	for(size_t i = 4; i < 9; ++i)
		if(!std::isdigit(static_cast<unsigned char>(name[i])))
			return false;
	return true;
}

// A PS5 title ID: PPSA (and the region and test series that go with it).
bool ps5TitleId(const std::string &id)
{
	static const std::set<std::string> prefixes { "PPSA", "ECAS", "ELAS", "ELJM", "ELJS", "PCAS", "PLAS" };
	return id.size() >= 4 && prefixes.count(id.substr(0, 4)) > 0;
}

// Plain letters, digits and dashes: a name every file system takes.
std::string cleanName(const std::string &text)
{
	std::string clean;
	bool dash = false;
	for(char c : text)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if((std::isalnum(u) && u < 128) || c == '.')
		{
			if(dash && !clean.empty())
				clean += '-';
			clean += c;
			dash = false;
		}
		else
			dash = true;
	}
	if(clean.size() > 60)
		clean.resize(60);
	return clean;
}

// The packages directly in a folder.
std::vector<ShelfEntry> packagesIn(FileSource &source, const std::string &dir)
{
	std::vector<ShelfEntry> entries;
	std::vector<ShelfEntry> found;
	if(!source.list(dir, &entries, nullptr))
		return found;
	for(const ShelfEntry &entry : entries)
		if(!entry.directory && toLower(fileExtensionLower(entry.name)) == ".pkg")
			found.push_back(entry);
	std::sort(found.begin(), found.end(), [](const ShelfEntry &a, const ShelfEntry &b) { return a.name < b.name; });
	return found;
}

// The names of the folders in a folder.
std::set<std::string> foldersIn(FileSource &source, const std::string &dir)
{
	std::vector<ShelfEntry> entries;
	std::set<std::string> names;
	if(source.list(dir, &entries, nullptr))
		for(const ShelfEntry &entry : entries)
			if(entry.directory)
				names.insert(entry.name);
	return names;
}

// Name, version and category from the title's metadata.
void readMeta(FileSource &source, const std::string &dir, InstalledTitle *title)
{
	std::vector<ShelfEntry> entries;
	if(!source.list(dir, &entries, nullptr))
		return;
	std::map<std::string, const ShelfEntry *> byName;
	for(const ShelfEntry &entry : entries)
		byName[toLower(entry.name)] = &entry;
	if(byName.count("icon0.png"))
		title->iconPath = byName["icon0.png"]->path;

	std::vector<uint8_t> bytes;
	if(byName.count("param.sfo") && source.read(byName["param.sfo"]->path, 0, 256 * 1024, &bytes))
	{
		Sfo sfo;
		if(sfo.parse(bytes))
		{
			title->title = trim(sfo.stringValue("TITLE"));
			title->version = trim(sfo.stringValue("APP_VER", sfo.stringValue("VERSION")));
			title->category = trim(sfo.stringValue("CATEGORY"));
			if(title->platform.empty())
				title->platform = "ps4";
		}
		return;
	}
	if(byName.count("param.json") && source.read(byName["param.json"]->path, 0, 1024 * 1024, &bytes))
	{
		const Json root = Json::parse(std::string(bytes.begin(), bytes.end()));
		if(!root.isObject())
			return;
		const Json &local = root["localizedParameters"];
		if(local.isObject())
		{
			const std::string language = local["defaultLanguage"].toString("en-US");
			std::string name = local[language]["titleName"].toString();
			if(name.empty())
				for(const auto &pair : local.members())
					if(pair.second.isObject() && !pair.second["titleName"].toString().empty())
					{
						name = pair.second["titleName"].toString();
						break;
					}
			title->title = trim(name);
		}
		title->version = trim(root["contentVersion"].toString());
		title->platform = "ps5";
	}
}

} // namespace

int64_t InstalledTitle::size() const
{
	int64_t total = 0;
	for(const InstalledPart &part : parts)
		total += part.size;
	return total;
}

bool InstalledTitle::copyable() const
{
	return !parts.empty();
}

std::vector<InstalledTitle> readInstalled(FileSource &source, const std::string &root, const std::atomic<bool> *cancel,
	const std::function<void(const InstalledTitle &)> &found)
{
	auto at = [&](const std::string &relative) {
		return root.empty() ? "/" + relative : source.join(root, relative);
	};
	auto stop = [cancel]() { return cancel && cancel->load(); };

	std::vector<InstalledTitle> titles;
	const std::set<std::string> apps = foldersIn(source, at("user/app"));
	if(apps.empty() || stop())
		return titles;
	// Which have an update or add-ons: one look at each folder, not one per game.
	const std::set<std::string> patched = foldersIn(source, at("user/patch"));
	const std::set<std::string> extended = foldersIn(source, at("user/addcont"));

	for(const std::string &id : apps)
	{
		if(stop())
			break;
		if(!titleIdLike(id))
			continue;
		InstalledTitle title;
		title.titleId = id;
		title.platform = ps5TitleId(id) ? "ps5" : "";
		readMeta(source, source.join(at("user/appmeta"), id), &title);
		if(title.title.empty())
			title.title = id;
		if(title.platform.empty())
			title.platform = "ps4";

		for(const ShelfEntry &entry : packagesIn(source, source.join(at("user/app"), id)))
			title.parts.push_back({ "app", entry.path, entry.name, std::string(), entry.size });
		if(patched.count(id))
			for(const ShelfEntry &entry : packagesIn(source, source.join(at("user/patch"), id)))
				title.parts.push_back({ "patch", entry.path, entry.name, std::string(), entry.size });
		if(extended.count(id))
		{
			const std::string dir = source.join(at("user/addcont"), id);
			for(const std::string &label : foldersIn(source, dir))
				for(const ShelfEntry &entry : packagesIn(source, source.join(dir, label)))
					title.parts.push_back({ "addon", entry.path, entry.name, label, entry.size });
		}
		if(found)
			found(title);
		titles.push_back(std::move(title));
	}
	return titles;
}

std::string copyFolderName(const InstalledTitle &title)
{
	const std::string name = cleanName(title.title);
	if(name.empty() || name == title.titleId)
		return title.titleId;
	return name + " [" + title.titleId + "]";
}

std::string copyFileName(const InstalledTitle &title, const InstalledPart &part)
{
	std::string base = cleanName(title.title);
	if(base.empty() || base == title.titleId)
		base = title.titleId;
	else
		base += "_" + title.titleId;
	if(part.kind == "patch")
		return base + "_patch" + (title.version.empty() ? std::string() : "_" + cleanName(title.version)) + ".pkg";
	if(part.kind == "addon")
		return base + "_" + (part.label.empty() ? cleanName(part.name) : cleanName(part.label)) + ".pkg";
	return base + ".pkg";
}

} // namespace library
} // namespace orbislink
