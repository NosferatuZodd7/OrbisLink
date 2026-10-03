// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/fpkg/classic_converter.h"

#include "orbislink/fpkg/param_sfo.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace orbislink::fpkg {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string s)
{
	for(char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool fileExists(const fs::path &p)
{
	std::error_code ec;
	return fs::is_regular_file(p, ec);
}

std::vector<std::string> readLines(const fs::path &p)
{
	std::vector<std::string> lines;
	std::ifstream in(p, std::ios::binary);
	std::string line;
	while(std::getline(in, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(line);
	}
	return lines;
}

void setOrAdd(std::vector<std::string> &lines, const std::string &key, const std::string &value)
{
	for(std::string &l : lines)
	{
		if(l.rfind(key, 0) == 0)
		{
			l = key + value;
			return;
		}
	}
	lines.push_back(key + value);
}

Bytes joinLines(const std::vector<std::string> &lines)
{
	std::string text;
	for(const std::string &l : lines)
		text += l + "\n";
	return Bytes(text.begin(), text.end());
}

// Every file under `dir`, as (path inside, path on disk), in a stable order.
std::vector<std::pair<std::string, std::string>> listFiles(const fs::path &dir)
{
	std::vector<std::pair<std::string, std::string>> out;
	std::error_code ec;
	for(fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
		!ec && it != end; it.increment(ec))
	{
		if(!it->is_regular_file(ec))
			continue;
		std::string rel = fs::relative(it->path(), dir, ec).generic_u8string();
		out.emplace_back(rel, it->path().u8string());
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::string utcDate(int64_t now)
{
	const std::time_t t = static_cast<std::time_t>(now);
	std::tm tm {};
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buffer[16];
	std::strftime(buffer, sizeof buffer, "%Y%m%d", &tm);
	return buffer;
}

void addMemory(PkgRequest &request, const std::string &target, Bytes data)
{
	PkgSource s;
	s.targetPath = target;
	s.data = std::move(data);
	request.files.push_back(std::move(s));
}

void addDisk(PkgRequest &request, const std::string &target, const std::string &path)
{
	PkgSource s;
	s.targetPath = target;
	s.sourcePath = path;
	request.files.push_back(std::move(s));
}

} // namespace

EmulatorInfo findEmulators(const std::string &folder)
{
	// The layout of the downloaded files (see classics_assets.h):
	// emus/<name>/eboot.bin, lua_include/, ps2ids.txt, ps1ids.txt.
	EmulatorInfo info;
	if(folder.empty())
		return info;
	const fs::path root = fs::u8path(folder);
	std::error_code ec;
	if(fs::is_directory(root / "lua_include", ec))
		info.luaInclude = (root / "lua_include").u8string();
	if(fileExists(root / "ps2ids.txt"))
		info.titleDatabase = (root / "ps2ids.txt").u8string();
	if(fileExists(root / "ps1ids.txt"))
		info.ps1TitleDatabase = (root / "ps1ids.txt").u8string();
	std::vector<fs::path> dirs;
	for(fs::directory_iterator it(root / "emus", ec), end; !ec && it != end; it.increment(ec))
		if(it->is_directory(ec) && fileExists(it->path() / "eboot.bin"))
			dirs.push_back(it->path());
	std::sort(dirs.begin(), dirs.end());
	for(const fs::path &d : dirs)
	{
		const std::string name = lower(d.filename().u8string());
		if(fileExists(d / "ps2-emu-compiler.self"))
		{
			// "Jak v2" is easy-ps2-fpkg's default: most games are happy with it.
			if(info.ps2Dir.empty() || name == "jak v2")
			{
				info.ps2Dir = d.u8string();
				info.ps2Name = d.filename().u8string();
			}
		}
		else if(name.find("psp") == std::string::npos && info.ps1Dir.empty())
			info.ps1Dir = d.u8string();
	}
	return info;
}

std::string lookupTitle(const std::string &databasePath, const std::string &titleId)
{
	if(databasePath.empty() || titleId.empty())
		return {};
	// "SLUS20946;Title" in ps2ids.txt, "SLPS-00965;Title" in ps1ids.txt.
	auto key = [](const std::string &id) {
		std::string k;
		for(char c : id)
			if(c != '-' && c != '_' && c != '.')
				k += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return k;
	};
	const std::string wanted = key(titleId);
	std::ifstream in(fs::u8path(databasePath));
	std::string line;
	while(std::getline(in, line))
	{
		const size_t semicolon = line.find(';');
		if(semicolon == std::string::npos || key(line.substr(0, semicolon)) != wanted)
			continue;
		std::string title = line.substr(semicolon + 1);
		while(!title.empty() && (title.back() == '\r' || title.back() == ' '))
			title.pop_back();
		// Some names are quoted, for their commas: "Incredibles, The".
		if(title.size() >= 2 && title.front() == '"' && title.back() == '"')
			title = title.substr(1, title.size() - 2);
		return title;
	}
	return {};
}

std::string packageFileName(const std::string &title, const std::string &titleId)
{
	// Plain letters, digits and dashes: the name survives FTP servers and
	// the console's file browser as it is.
	std::string clean;
	bool dash = false;
	for(char c : title)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if(std::isalnum(u) && u < 128)
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
	if(clean.empty())
		clean = "Game";
	return clean + "_" + titleId + ".pkg";
}

bool prepareClassic(const DiscInfo &disc, const EmulatorInfo &emulators,
	const ClassicOptions &options, PkgRequest *request, ClassicResult *result, std::string *error)
{
	auto fail = [&](const std::string &message) {
		if(error)
			*error = message;
		return false;
	};
	const bool ps2 = disc.platform == "ps2";
	if(disc.platform != "ps1" && disc.platform != "ps2")
		return fail("this is not a PS1 or PS2 disc");
	if(disc.titleId.size() != 9)
		return fail("the disc's serial could not be read");
	const std::string emuDir = ps2 ? emulators.ps2Dir : emulators.ps1Dir;
	if(emuDir.empty())
		return fail(ps2 ? "the PS2 emulator files are not there" : "the PS1 emulator files are not there");

	// The name: the one given, else the emulator's title list, else the
	// file's name.
	std::string title = options.title;
	if(title.empty())
		title = lookupTitle(ps2 ? emulators.titleDatabase : emulators.ps1TitleDatabase, disc.titleId);
	if(title.empty())
		title = disc.title;

	const std::string contentId = "UP9000-" + disc.titleId + "_00-" + disc.titleId
		+ (ps2 ? "0000001" : "PS1FPKG");

	PkgRequest req;
	req.contentId = contentId;
	req.volumeTime = options.now;
	req.creationDate = utcDate(options.now);

	// PS2: easy-ps2-fpkg's recipe. The emulator folder is copied as it is,
	// then the shared lua_include over it; its config gets the game's
	// serial; its own param.sfo gets the game's IDs and name.
	// PS1: PS Classics fPKG Builder's — the emulator, a config and a
	// param.sfo written for the game.
	const std::string configName = ps2 ? "config-emu-ps4.txt" : "config-title.txt";
	std::map<std::string, PkgSource> files;
	auto put = [&](const std::string &target, const std::string &path) {
		PkgSource s;
		s.targetPath = target;
		s.sourcePath = path;
		files[target] = std::move(s);
	};
	std::vector<std::string> config;
	Bytes templateSfo;
	for(const auto &f : listFiles(fs::u8path(emuDir)))
	{
		if(f.first == configName)
		{
			if(ps2)
				config = readLines(fs::u8path(f.second));
			continue;
		}
		if(f.first == "sce_sys/param.sfo")
		{
			std::ifstream in(fs::u8path(f.second), std::ios::binary);
			templateSfo.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			continue;
		}
		put(f.first, f.second);
	}
	if(ps2 && !emulators.luaInclude.empty())
		for(const auto &f : listFiles(fs::u8path(emulators.luaInclude)))
			put("lua_include/" + f.first, f.second);
	for(auto &f : files)
		req.files.push_back(std::move(f.second));

	if(ps2)
	{
		if(config.empty())
			config = {"--path-vmc=\"/tmp/vmc\"", "--host-audio=1", "--host-display-mode=full",
				"--rom=\"PS20220WD20050620.crack\""};
		setOrAdd(config, "--ps2-title-id=", disc.serial);
		if(std::none_of(config.begin(), config.end(),
			   [](const std::string &l) { return l.rfind("--max-disc-num=", 0) == 0; }))
			config.push_back("--max-disc-num=1");
	}
	else
		config = {"--ps4-trophies=0", "--ps5-uds=0", "--trophies=0", "--image=\"data/disc1.bin\""};
	addMemory(req, configName, joinLines(config));

	ParamSfo sfo;
	if(ps2)
	{
		std::string why;
		if(templateSfo.empty() || !sfo.parse(templateSfo, &why))
			return fail("the emulator has no usable sce_sys/param.sfo");
		sfo.setString("CONTENT_ID", contentId, 48);
		sfo.setString("TITLE", title, 128);
		sfo.setString("TITLE_ID", disc.titleId, 12);
	}
	else
	{
		sfo.setInteger("APP_TYPE", 1);
		sfo.setString("APP_VER", "01.00", 8);
		sfo.setInteger("ATTRIBUTE", 0);
		sfo.setString("CATEGORY", "gd", 4);
		sfo.setString("CONTENT_ID", contentId, 48);
		sfo.setInteger("DOWNLOAD_DATA_SIZE", 0);
		sfo.setString("FORMAT", "obs", 4);
		sfo.setInteger("PARENTAL_LEVEL", 5);
		sfo.setInteger("SYSTEM_VER", 0);
		sfo.setString("TITLE", title, 128);
		sfo.setString("TITLE_ID", disc.titleId, 12);
		sfo.setString("VERSION", "01.00", 8);
	}
	addMemory(req, "sce_sys/param.sfo", sfo.serialize());

	// Art: the game's cover when there is one, else the emulator's own.
	auto replace = [&](const std::string &target, const Bytes &data) {
		req.files.erase(std::remove_if(req.files.begin(), req.files.end(),
							[&](const PkgSource &s) { return s.targetPath == target; }),
			req.files.end());
		addMemory(req, target, data);
	};
	if(!options.icon.empty())
		replace("sce_sys/icon0.png", options.icon);
	if(!options.background.empty())
	{
		replace("sce_sys/pic1.png", options.background);
		replace("sce_sys/pic0.png", options.background);
	}

	// The disc last: it is most of the package.
	if(ps2)
	{
		addDisk(req, "image/disc01.iso", disc.path);
	}
	else
	{
		std::string cue;
		if(lower(fs::u8path(disc.listedPath).extension().u8string()) == ".cue")
		{
			int files = 0;
			for(std::string line : readLines(fs::u8path(disc.listedPath)))
			{
				std::string t = line;
				t.erase(0, t.find_first_not_of(" \t"));
				if(lower(t.substr(0, 5)) == "file ")
				{
					if(++files > 1)
						return fail("this disc has one file per track; join them into a single .bin first");
					line = "FILE \"disc1.bin\" BINARY";
				}
				cue += line + "\n";
			}
		}
		else
		{
			const bool raw = disc.size % 2352 == 0;
			cue = std::string("FILE \"disc1.bin\" BINARY\n  TRACK 01 ")
				+ (raw ? "MODE2/2352" : "MODE1/2048") + "\n    INDEX 01 00:00:00\n";
		}
		addMemory(req, "data/disc1.cue", Bytes(cue.begin(), cue.end()));
		addDisk(req, "data/disc1.bin", disc.path);
	}

	ClassicResult res;
	res.contentId = contentId;
	res.titleId = disc.titleId;
	res.title = title;
	res.pkgPath = (fs::u8path(options.outputDir) / fs::u8path(packageFileName(title, disc.titleId))).u8string();
	if(request)
		*request = std::move(req);
	if(result)
		*result = res;
	return true;
}

bool convertClassic(const DiscInfo &disc, const EmulatorInfo &emulators,
	const ClassicOptions &options, const PkgProgress &progress, ClassicResult *result,
	std::string *error)
{
	PkgRequest request;
	ClassicResult res;
	if(!prepareClassic(disc, emulators, options, &request, &res, error))
		return false;

	std::error_code ec;
	fs::create_directories(fs::u8path(options.outputDir), ec);
	// Room for the disc, the emulator and some slack, checked before
	// spending minutes on it.
	uint64_t needed = 32ull << 20;
	for(const PkgSource &s : request.files)
		needed += s.sourcePath.empty() ? s.data.size() : fs::file_size(fs::u8path(s.sourcePath), ec);
	const fs::space_info space = fs::space(fs::u8path(options.outputDir), ec);
	if(!ec && space.available < needed)
	{
		if(error)
		{
			std::ostringstream m;
			m << "not enough free space in the output folder (needs about " << (needed >> 20)
			  << " MB, has " << (space.available >> 20) << " MB)";
			*error = m.str();
		}
		return false;
	}
	if(!buildFakePkg(request, res.pkgPath, progress, error))
		return false;
	if(result)
		*result = res;
	return true;
}

} // namespace orbislink::fpkg
