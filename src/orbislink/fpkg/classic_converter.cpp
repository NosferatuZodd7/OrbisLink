// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/fpkg/classic_converter.h"

#include "orbislink/fpkg/fself.h"
#include "orbislink/fpkg/param_sfo.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
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

constexpr uint64_t kLargestEmulatorFile = 256ull * 1024 * 1024;

// Whether a file of the Classics folder (path inside it, lower case) goes
// into the package. A dump of a game or a builder's work folder holds more
// than the emulator: the donor's disc, earlier packages, notes, and the
// files that tie it to its own licence, which the builder makes anew.
bool isEmulatorFile(const std::string &low)
{
	static const char *const skippedDirs[] = {"image/", "docs/", "data/disc", "sce_sys/trophy/"};
	for(const char *d : skippedDirs)
		if(low.rfind(d, 0) == 0)
			return false;
	static const char *const madeAnew[] = {"keystone", "license.dat", "license.info", "playgo-chunk.dat",
		"playgo-chunk.sha", "playgo-manifest.xml", "psreserved.dat", "pubtoolinfo.dat", "selfinfo.dat",
		"imageinfo.dat", "target-deltainfo.dat", "origin-deltainfo.dat"};
	for(const char *n : madeAnew)
		if(low == std::string("sce_sys/") + n)
			return false;
	const std::string ext = fs::path(low).extension().string();
	static const char *const skippedExtensions[] = {".pkg", ".gp4", ".iso", ".cue", ".img", ".mdf",
		".mds", ".chd", ".zip", ".rar", ".7z", ".fself", ".elf", ".log"};
	for(const char *e : skippedExtensions)
		if(ext == e)
			return false;
	return true;
}

// Whether `path` is `dir` or below it (case aside, as on Windows).
bool isInside(const fs::path &path, const fs::path &dir)
{
	auto key = [](const fs::path &p) {
		std::string s = lower(p.lexically_normal().generic_u8string());
		if(s.empty() || s.back() != '/')
			s += '/';
		return s;
	};
	return key(path).rfind(key(dir), 0) == 0;
}

bool looksLikeElf(const std::string &path)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	uint8_t head[0x40] = {};
	in.read(reinterpret_cast<char *>(head), sizeof head);
	return in && isPlainElf(head, sizeof head);
}

Bytes readAll(const std::string &path)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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
	EmulatorInfo info;
	if(folder.empty())
		return info;
	const fs::path root = fs::u8path(folder);
	std::vector<fs::path> dirs;
	std::error_code ec;
	if(fileExists(root / "eboot.bin"))
		dirs.push_back(root);
	for(fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
		!ec && it != end; it.increment(ec))
	{
		if(it.depth() > 4)
		{
			it.disable_recursion_pending();
			continue;
		}
		if(it->is_directory(ec))
		{
			const std::string name = lower(it->path().filename().u8string());
			if(name == "lua_include" && info.luaInclude.empty())
				info.luaInclude = it->path().u8string();
			if(fileExists(it->path() / "eboot.bin"))
				dirs.push_back(it->path());
		}
		else if(lower(it->path().filename().u8string()) == "ps2ids.txt" && info.titleDatabase.empty())
			info.titleDatabase = it->path().u8string();
	}
	std::sort(dirs.begin(), dirs.end());
	for(const fs::path &d : dirs)
	{
		const std::string name = lower(d.filename().u8string());
		if(fileExists(d / "ps2-emu-compiler.self"))
		{
			// "Jak v2" is the one most games are happy with.
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
	std::ifstream in(fs::u8path(databasePath));
	std::string line;
	const std::string key = lower(titleId) + ";";
	while(std::getline(in, line))
	{
		if(lower(line.substr(0, key.size())) == key)
		{
			std::string title = line.substr(key.size());
			while(!title.empty() && (title.back() == '\r' || title.back() == ' '))
				title.pop_back();
			return title;
		}
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
		return fail(ps2 ? "no PS2 emulator in the emulator folder" : "no PS1 emulator in the emulator folder");

	std::string title = options.title;
	if(title.empty() && ps2)
		title = lookupTitle(emulators.titleDatabase, disc.titleId);
	if(title.empty())
		title = disc.title;

	const std::string contentId = "UP9000-" + disc.titleId + "_00-" + disc.titleId
		+ (ps2 ? "0000001" : "PS1FPKG");

	PkgRequest req;
	req.contentId = contentId;
	req.volumeTime = options.now;
	req.creationDate = utcDate(options.now);

	// The emulator, minus what is made for this game.
	const std::string configName = ps2 ? "config-emu-ps4.txt" : "config-title.txt";
	std::vector<std::string> config;
	bool hasLua = false;
	// Packages made into a folder inside the emulator's must not end up in
	// the next one.
	const fs::path outputDir = fs::u8path(options.outputDir);
	const bool outputBelowEmulator = !options.outputDir.empty() && isInside(outputDir, fs::u8path(emuDir))
		&& !isInside(fs::u8path(emuDir), outputDir);
	for(const auto &f : listFiles(fs::u8path(emuDir)))
	{
		const std::string rel = f.first;
		const std::string low = lower(rel);
		if(low == "sce_sys/param.sfo" || !isEmulatorFile(low))
			continue;
		std::error_code ec;
		const uint64_t size = fs::file_size(fs::u8path(f.second), ec);
		// Empty files are leftovers of a failed step; very large ones are
		// discs or packages that do not belong to the emulator.
		if(ec || size == 0 || size > kLargestEmulatorFile)
			continue;
		if(outputBelowEmulator && isInside(fs::u8path(f.second), outputDir))
			continue;
		if(fs::u8path(f.second).lexically_normal() == fs::u8path(disc.path).lexically_normal())
			continue;
		if(low == lower(configName))
		{
			if(ps2)
				config = readLines(fs::u8path(f.second));
			continue;
		}
		if(!options.icon.empty() && low == "sce_sys/icon0.png")
			continue;
		if(!options.background.empty() && (low == "sce_sys/pic0.png" || low == "sce_sys/pic1.png"))
			continue;
		// A disc that came with the emulator's donor game stays out.
		if(low.rfind("image/", 0) == 0 || low.rfind("data/disc", 0) == 0)
			continue;
		if(low.rfind("lua_include/", 0) == 0)
			hasLua = true;
		// A dump holds the executables decrypted; the console wants them
		// fake-signed.
		if(looksLikeElf(f.second))
		{
			std::string why;
			Bytes fself = makeFself(readAll(f.second), &why);
			if(fself.empty())
				return fail(rel + ": " + why);
			addMemory(req, rel, std::move(fself));
			continue;
		}
		addDisk(req, rel, f.second);
	}
	if(ps2 && !hasLua && !emulators.luaInclude.empty())
		for(const auto &f : listFiles(fs::u8path(emulators.luaInclude)))
			if(isEmulatorFile("lua_include/" + lower(f.first)))
				addDisk(req, "lua_include/" + f.first, f.second);

	if(ps2)
	{
		if(config.empty())
			config = {"--path-vmc=\"/tmp/vmc\"", "--config-local-lua=\"\"", "--host-audio=1",
				"--rom=\"PS20220WD20050620.crack\"", "--verbose-cdvd-reads=0",
				"--host-display-mode=normal", "--gs-uprender=2x2", "--gs-upscale=none"};
		setOrAdd(config, "--ps2-title-id=", disc.serial);
		setOrAdd(config, "--max-disc-num=", "1");
	}
	else
	{
		config = {"--ps4-trophies=0", "--ps5-uds=0", "--trophies=0", "--image=\"data/disc1.bin\"",
			"--scale=2"};
	}
	addMemory(req, configName, joinLines(config));

	ParamSfo sfo;
	sfo.setInteger("APP_TYPE", 1);
	sfo.setString("APP_VER", "01.00", 8);
	sfo.setInteger("ATTRIBUTE", 0);
	sfo.setString("CATEGORY", "gd", 4);
	sfo.setString("CONTENT_ID", contentId, 48);
	sfo.setInteger("DOWNLOAD_DATA_SIZE", 0);
	sfo.setString("FORMAT", "obs", 4);
	sfo.setInteger("PARENTAL_LEVEL", 5);
	if(ps2)
		sfo.setInteger("REMOTE_PLAY_KEY_ASSIGN", 0);
	sfo.setInteger("SYSTEM_VER", 0);
	sfo.setString("TITLE", title, 128);
	sfo.setString("TITLE_ID", disc.titleId, 12);
	sfo.setString("VERSION", "01.00", 8);
	addMemory(req, "sce_sys/param.sfo", sfo.serialize());
	if(!options.icon.empty())
		addMemory(req, "sce_sys/icon0.png", options.icon);
	if(!options.background.empty())
	{
		addMemory(req, "sce_sys/pic0.png", options.background);
		addMemory(req, "sce_sys/pic1.png", options.background);
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
