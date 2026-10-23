// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/library/folder_package.h"

#include "orbislink/pkg/sfo_parser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>

namespace fs = std::filesystem;

namespace orbislink {
namespace library {

namespace {

std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string readText(const fs::path &file, size_t limit)
{
	std::ifstream in(file, std::ios::binary);
	if(!in)
		return std::string();
	std::string text(limit, '\0');
	in.read(&text[0], static_cast<std::streamsize>(limit));
	text.resize(static_cast<size_t>(in.gcount()));
	return text;
}

// An attribute of an XML tag, as written ("&amp;" and the like undone).
std::string attribute(const std::string &tag, const std::string &name)
{
	const std::regex pattern("\\b" + name + "\\s*=\\s*\"([^\"]*)\"");
	std::smatch match;
	if(!std::regex_search(tag, match, pattern))
		return std::string();
	std::string value = match[1].str();
	static const std::pair<const char *, const char *> entities[] = { { "&quot;", "\"" }, { "&apos;", "'" },
		{ "&lt;", "<" }, { "&gt;", ">" }, { "&amp;", "&" } };
	for(const auto &entity : entities)
	{
		size_t at = 0;
		while((at = value.find(entity.first, at)) != std::string::npos)
		{
			value.replace(at, std::strlen(entity.first), entity.second);
			at += std::strlen(entity.second);
		}
	}
	return value;
}

// The tools' own leftovers, never part of an app.
bool leftover(const std::string &name)
{
	const std::string low = lower(name);
	return low.empty() || low[0] == '.' || low == "thumbs.db" || low == "desktop.ini"
		|| (low.size() > 4 && low.compare(low.size() - 4, 4, ".gp4") == 0)
		|| (low.size() > 9 && low.compare(low.size() - 9, 9, ".complete") == 0);
}

std::string slashes(std::string path)
{
	std::replace(path.begin(), path.end(), '\\', '/');
	while(!path.empty() && path.front() == '/')
		path.erase(path.begin());
	return path;
}

// The file a .gp4 names: where it says, else beside the .gp4, else in the
// folder by its place in the package.
fs::path locate(const std::string &orig, const std::string &target, const fs::path &gp4Dir, const fs::path &folder)
{
	std::error_code ec;
	const fs::path given = fs::u8path(orig);
	if(!orig.empty() && given.is_absolute() && fs::is_regular_file(given, ec))
		return given;
	if(!orig.empty())
	{
		const fs::path beside = gp4Dir / fs::u8path(slashes(orig));
		if(fs::is_regular_file(beside, ec))
			return beside;
	}
	const fs::path inFolder = folder / fs::u8path(slashes(target));
	if(fs::is_regular_file(inFolder, ec))
		return inFolder;
	return fs::path();
}

} // namespace

std::string findGp4(const std::string &folder)
{
	std::error_code ec;
	const fs::path dir = fs::u8path(folder);
	const std::string name = dir.filename().u8string().empty() ? dir.parent_path().filename().u8string()
															   : dir.filename().u8string();
	std::vector<fs::path> inside;
	for(fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		if(it->is_regular_file(ec) && lower(it->path().extension().u8string()) == ".gp4")
			inside.push_back(it->path());
	for(const fs::path &gp4 : inside)
		if(lower(gp4.stem().u8string()) == lower(name))
			return gp4.u8string();
	if(inside.size() == 1)
		return inside.front().u8string();
	const fs::path beside = dir.parent_path() / fs::u8path(name + ".gp4");
	if(fs::is_regular_file(beside, ec))
		return beside.u8string();
	return std::string();
}

PackPlan planFolderPackage(const std::string &folder)
{
	PackPlan plan;
	std::error_code ec;
	const fs::path dir = fs::u8path(folder);
	if(!fs::is_directory(dir, ec))
	{
		plan.error = "the folder is not there";
		return plan;
	}

	// What the app says about itself.
	const fs::path sfoPath = dir / "sce_sys" / "param.sfo";
	const std::string sfoText = readText(sfoPath, 1024 * 1024);
	Sfo sfo;
	if(sfoText.empty() || !sfo.parse(std::vector<uint8_t>(sfoText.begin(), sfoText.end())))
	{
		plan.error = "no sce_sys/param.sfo in it: it is not an app or game folder";
		return plan;
	}
	plan.titleId = sfo.stringValue("TITLE_ID");
	plan.title = sfo.stringValue("TITLE");
	plan.version = sfo.stringValue("APP_VER", sfo.stringValue("VERSION"));
	plan.contentId = sfo.stringValue("CONTENT_ID");

	std::map<std::string, PackFile> files;
	plan.gp4 = findGp4(folder);
	if(!plan.gp4.empty())
	{
		const std::string xml = readText(fs::u8path(plan.gp4), 16 * 1024 * 1024);
		const fs::path gp4Dir = fs::u8path(plan.gp4).parent_path();
		static const std::regex packageTag("<package\\b[^>]*>");
		std::smatch match;
		if(std::regex_search(xml, match, packageTag))
		{
			const std::string id = attribute(match.str(), "content_id");
			if(id.size() == 36)
				plan.contentId = id;
			const std::string passcode = attribute(match.str(), "passcode");
			if(passcode.size() == 32)
				plan.passcode = passcode;
		}
		static const std::regex fileTag("<file\\b[^>]*>");
		std::vector<std::string> missing;
		for(std::sregex_iterator it(xml.begin(), xml.end(), fileTag), end; it != end; ++it)
		{
			const std::string tag = it->str();
			const std::string target = slashes(attribute(tag, "targ_path"));
			if(target.empty())
				continue;
			const fs::path source = locate(attribute(tag, "orig_path"), target, gp4Dir, dir);
			if(source.empty())
			{
				missing.push_back(target);
				continue;
			}
			PackFile file;
			file.target = target;
			file.source = source.u8string();
			file.size = static_cast<uint64_t>(fs::file_size(source, ec));
			files[lower(target)] = file;
		}
		if(!missing.empty())
		{
			plan.error = "the recipe (" + fs::u8path(plan.gp4).filename().u8string() + ") names files that are not there: "
				+ missing.front() + (missing.size() > 1 ? " and " + std::to_string(missing.size() - 1) + " more" : "");
			return plan;
		}
	}
	if(files.empty())
	{
		plan.gp4.clear();
		for(fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
			!ec && it != end; it.increment(ec))
		{
			if(!it->is_regular_file(ec) || leftover(it->path().filename().u8string()))
				continue;
			const std::string target = slashes(fs::relative(it->path(), dir, ec).generic_u8string());
			if(target.empty())
				continue;
			PackFile file;
			file.target = target;
			file.source = it->path().u8string();
			file.size = static_cast<uint64_t>(it->file_size(ec));
			files[lower(target)] = file;
		}
	}
	if(!files.count("sce_sys/param.sfo"))
	{
		plan.error = "the package would have no sce_sys/param.sfo";
		return plan;
	}
	if(plan.contentId.size() != 36)
	{
		plan.error = "no content ID: neither the recipe nor param.sfo has one";
		return plan;
	}
	if(plan.titleId.empty())
		plan.titleId = plan.contentId.substr(7, 9);
	if(plan.title.empty())
		plan.title = dir.filename().u8string();
	for(auto &entry : files)
	{
		plan.bytes += entry.second.size;
		plan.files.push_back(std::move(entry.second));
	}
	plan.ok = true;
	return plan;
}

} // namespace library
} // namespace orbislink
