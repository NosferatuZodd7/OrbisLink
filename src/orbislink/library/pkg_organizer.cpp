// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/library/pkg_organizer.h"

#include "orbislink/common/util.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace orbislink {
namespace library {

namespace {

// How deep the folders of the folder organised are looked in.
constexpr int kMaxDepth = 4;
constexpr size_t kMaxNameBytes = 200;

// In file names, a package's kind in plain words (not translated: a file
// name is the same for everyone).
std::string kindWord(PkgCategory kind)
{
	switch(kind)
	{
		case PkgCategory::Game: return "Game";
		case PkgCategory::Patch:
		case PkgCategory::DeltaPatch: return "Update";
		case PkgCategory::Dlc: return "DLC";
		case PkgCategory::Theme: return "Theme";
		case PkgCategory::Unknown: break;
	}
	return std::string();
}

// "00 - " the game, "01 - " its update, "02 - " its add-ons.
std::string orderPrefix(PkgCategory kind)
{
	switch(kind)
	{
		case PkgCategory::Game: return "00 - ";
		case PkgCategory::Patch:
		case PkgCategory::DeltaPatch: return "01 - ";
		case PkgCategory::Dlc: return "02 - ";
		default: return std::string();
	}
}

void replaceAll(std::string &text, const std::string &from, const std::string &to)
{
	if(from.empty())
		return;
	size_t at = 0;
	while((at = text.find(from, at)) != std::string::npos)
	{
		text.replace(at, from.size(), to);
		at += to.size();
	}
}

// A placeholder left empty leaves its brackets: "[]", "(v)"… go.
std::string tidy(std::string text)
{
	bool changed = true;
	while(changed)
	{
		const std::string before = text;
		for(const char *empty : { "[]", "()", "[v]", "(v)", "[ ]", "( )" })
			replaceAll(text, empty, "");
		replaceAll(text, "  ", " ");
		// "… v" when the version is not known.
		if(text.size() >= 2 && text.compare(text.size() - 2, 2, " v") == 0)
			text.resize(text.size() - 2);
		changed = text != before;
	}
	// No dangling separators at either end.
	auto edge = [](char c) { return c == ' ' || c == '-' || c == '_' || c == '.'; };
	while(!text.empty() && edge(text.front()))
		text.erase(text.begin());
	while(!text.empty() && edge(text.back()))
		text.pop_back();
	return text;
}

bool isPkg(const fs::path &path)
{
	return toLower(path.extension().u8string()) == ".pkg";
}

void collect(const fs::path &dir, int depth, std::vector<fs::path> *found, const std::atomic<bool> *cancel)
{
	std::error_code ec;
	fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	if(ec)
		return;
	for(; it != fs::directory_iterator(); it.increment(ec))
	{
		if(ec || (cancel && cancel->load()))
			return;
		const fs::directory_entry &entry = *it;
		if(entry.is_symlink(ec))
			continue;
		if(entry.is_directory(ec))
		{
			if(depth < kMaxDepth)
				collect(entry.path(), depth + 1, found, cancel);
		}
		else if(entry.is_regular_file(ec) && isPkg(entry.path()))
		{
			found->push_back(entry.path());
		}
	}
}

// `path`, or "<stem> (2)<ext>", "<stem> (3)<ext>"… the first not in `taken`
// and not on the disk (unless it is `self`).
fs::path freeName(const fs::path &path, const std::set<std::string> &taken, const fs::path &self)
{
	auto usable = [&](const fs::path &candidate) {
		if(taken.count(toLower(candidate.u8string())))
			return false;
		std::error_code ec;
		if(!fs::exists(candidate, ec))
			return true;
		return fs::equivalent(candidate, self, ec);
	};
	if(usable(path))
		return path;
	const std::string stem = path.stem().u8string();
	const std::string ext = path.extension().u8string();
	for(int n = 2; n < 1000; ++n)
	{
		const fs::path candidate = path.parent_path() / fs::u8path(stem + " (" + std::to_string(n) + ")" + ext);
		if(usable(candidate))
			return candidate;
	}
	return path;
}

} // namespace

const std::vector<std::string> &organizePatterns()
{
	static const std::vector<std::string> patterns = {
		"{TITLE} [{TITLE_ID}] [{CATEGORY}] [v{VERSION}]",
		"{TITLE_ID} - {TITLE} ({CATEGORY} {VERSION})",
		"{TITLE} ({REGION}) v{VERSION}",
		"{CONTENT_ID}",
	};
	return patterns;
}

std::string regionOf(const std::string &contentId)
{
	const std::string prefix = contentId.size() >= 2 ? contentId.substr(0, 2) : std::string();
	if(prefix == "UP")
		return "USA";
	if(prefix == "EP")
		return "EUR";
	if(prefix == "JP")
		return "JPN";
	if(prefix == "HP")
		return "ASIA";
	if(prefix == "KP")
		return "KOR";
	if(prefix == "IP")
		return "INT";
	return std::string();
}

std::string expandPattern(const std::string &pattern, const PkgInfo &info)
{
	std::string text = pattern;
	const std::string version = !info.appVersion.empty() ? info.appVersion : info.version;
	replaceAll(text, "{TITLE}", info.displayTitle());
	replaceAll(text, "{TITLE_ID}", info.titleId);
	replaceAll(text, "{CONTENT_ID}", info.contentId);
	replaceAll(text, "{VERSION}", version);
	replaceAll(text, "{CATEGORY}", kindWord(info.kind));
	replaceAll(text, "{REGION}", regionOf(info.contentId));
	replaceAll(text, "{FIRMWARE}", firmwareText(info.requiredFirmware));
	replaceAll(text, "{TYPE}", info.official ? "Official" : "Fake");
	return tidy(text);
}

std::string safeFileName(const std::string &name)
{
	std::string out;
	out.reserve(name.size());
	for(size_t i = 0; i < name.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if(c == ':')
			out += " -";
		else if(c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<'
			|| c == '>' || c == '|')
			out += ' ';
		else
			out += static_cast<char>(c);
	}
	replaceAll(out, "  ", " ");
	replaceAll(out, "  ", " ");
	// Not past the limit, nor in the middle of a character.
	if(out.size() > kMaxNameBytes)
	{
		size_t cut = kMaxNameBytes;
		while(cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
			--cut;
		out.resize(cut);
	}
	while(!out.empty() && (out.back() == ' ' || out.back() == '.'))
		out.pop_back();
	while(!out.empty() && out.front() == ' ')
		out.erase(out.begin());
	// Names Windows keeps for devices.
	const std::string lower = toLower(out);
	static const std::set<std::string> reserved = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
		"com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8",
		"lpt9" };
	if(reserved.count(lower))
		out += "_";
	return out;
}

std::vector<OrganizeStep> planOrganize(const std::string &folder, const OrganizeOptions &options,
	const std::atomic<bool> *cancel)
{
	std::vector<fs::path> files;
	collect(fs::u8path(folder), 0, &files, cancel);
	std::vector<PkgInfo> packages;
	PkgInspector::Options read;
	read.extractIcon = false;
	const PkgInspector inspector(read);
	for(const fs::path &file : files)
	{
		if(cancel && cancel->load())
			return {};
		PkgInfo info = inspector.inspect(file.u8string());
		if(info.valid)
			packages.push_back(std::move(info));
	}
	return planOrganize(folder, packages, options);
}

std::vector<OrganizeStep> planOrganize(const std::string &folder, const std::vector<PkgInfo> &packages,
	const OrganizeOptions &options)
{
	std::vector<const PkgInfo *> sorted;
	for(const PkgInfo &info : packages)
		sorted.push_back(&info);
	std::sort(sorted.begin(), sorted.end(), [](const PkgInfo *a, const PkgInfo *b) { return a->path < b->path; });

	// A game's folder is named after the game, which its update says too
	// (an add-on says its own name): the game's, else its update's, else
	// an add-on's.
	std::map<std::string, std::pair<int, std::string>> gameTitles; // title ID → (rank, title)
	for(const PkgInfo *info : sorted)
	{
		const int rank = info->kind == PkgCategory::Game ? 0 : info->isPatch ? 1 : 2;
		auto &best = gameTitles[info->titleId];
		if(best.second.empty() || rank < best.first)
			best = { rank, info->displayTitle() };
	}

	const fs::path root = fs::u8path(folder);
	std::set<std::string> sources;
	for(const PkgInfo *info : sorted)
		sources.insert(toLower(fs::u8path(info->path).u8string()));

	std::map<std::string, std::string> firstOf; // same package → the first file
	std::set<std::string> taken;
	std::vector<OrganizeStep> plan;
	for(const PkgInfo *info : sorted)
	{
		OrganizeStep step;
		step.from = info->path;
		step.to = info->path;
		step.title = info->displayTitle();
		const std::string same = info->contentId + "|" + pkgCategoryCode(info->kind) + "|" + info->appVersion
			+ "|" + info->version;
		const auto first = firstOf.find(same);
		if(first != firstOf.end() && !info->contentId.empty())
		{
			step.duplicate = true;
			step.sameAs = first->second;
			plan.push_back(step);
			continue;
		}
		firstOf[same] = info->path;

		const fs::path from = fs::u8path(info->path);
		std::string name = safeFileName(expandPattern(options.pattern, *info));
		if(name.empty())
			name = from.stem().u8string();
		if(options.installOrder)
			name = orderPrefix(info->kind) + name;
		fs::path dir = from.parent_path();
		if(options.folderPerGame && !info->titleId.empty())
		{
			const std::string game = gameTitles[info->titleId].second;
			dir = root / fs::u8path(safeFileName(game + " (" + info->titleId + ")"));
		}
		fs::path to = dir / fs::u8path(name + ".pkg");
		// Not over another package's new name, nor over a file that stays.
		const bool samePlace = toLower(to.u8string()) == toLower(from.u8string());
		if(!samePlace)
		{
			std::error_code ec;
			const bool otherFile = fs::exists(to, ec) && !fs::equivalent(to, from, ec)
				&& !sources.count(toLower(to.u8string()));
			if(taken.count(toLower(to.u8string())) || otherFile)
				to = freeName(to, taken, from);
		}
		taken.insert(toLower(to.u8string()));
		step.to = to.u8string();
		plan.push_back(step);
	}
	return plan;
}

OrganizeResult applyOrganize(const std::string &folder, const std::vector<OrganizeStep> &plan,
	const std::function<void(int done, int total)> &progress)
{
	OrganizeResult result;
	const fs::path root = fs::u8path(folder);
	int total = 0;
	for(const OrganizeStep &step : plan)
		if(step.changes())
			++total;
	std::set<std::string> taken;
	std::set<fs::path> left; // folders a package left
	int done = 0;
	for(const OrganizeStep &step : plan)
	{
		if(!step.changes())
			continue;
		const fs::path from = fs::u8path(step.from);
		fs::path to = fs::u8path(step.to);
		std::error_code ec;
		fs::create_directories(to.parent_path(), ec);
		// Taken since the plan was made: the next free name.
		if(toLower(to.u8string()) != toLower(from.u8string()))
			to = freeName(to, taken, from);
		fs::rename(from, to, ec);
		if(ec)
		{
			result.failed.push_back(from.filename().u8string() + ": " + ec.message());
		}
		else
		{
			++result.moved;
			taken.insert(toLower(to.u8string()));
			if(from.parent_path() != to.parent_path())
				left.insert(from.parent_path());
		}
		if(progress)
			progress(++done, total);
	}
	// The folders moved out of, when nothing is left in them (never the
	// folder organised itself, nor above it).
	for(const fs::path &dir : left)
	{
		fs::path at = dir;
		std::error_code ec;
		while(!at.empty() && at != root && at.u8string().size() > root.u8string().size()
			&& fs::is_directory(at, ec) && fs::is_empty(at, ec))
		{
			fs::remove(at, ec);
			at = at.parent_path();
		}
	}
	return result;
}

} // namespace library
} // namespace orbislink
