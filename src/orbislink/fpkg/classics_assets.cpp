// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/fpkg/classics_assets.h"

#include <filesystem>

namespace orbislink::fpkg {

namespace fs = std::filesystem;

const char *const kClassicsAssetsUrl =
	"https://github.com/SvenGDK/PS-Classics-fPKG-Builder/releases/download/v1/"
	"PS.Classics.fPKG.Builder.v1.Linux.x64.tar.gz";

std::string classicsAssetPath(const std::string &key)
{
	// The same choice as easy-ps2-fpkg's MapAssetPath, plus the PS1 title list.
	const std::string tools = "Tools/PS4/";
	const size_t at = key.find(tools);
	if(at != std::string::npos)
	{
		const std::string rel = key.substr(at + tools.size());
		if(rel.rfind("emus/", 0) == 0 || rel.rfind("lua_include/", 0) == 0 || rel.rfind("ps2-configs/", 0) == 0)
			return rel.find("..") == std::string::npos ? rel : std::string();
		return {};
	}
	auto endsWith = [&](const std::string &tail) {
		return key.size() >= tail.size() && key.compare(key.size() - tail.size(), tail.size(), tail) == 0;
	};
	if(endsWith("Tools/ps2ids.txt"))
		return "ps2ids.txt";
	if(endsWith("Tools/ps1ids.txt"))
		return "ps1ids.txt";
	return {};
}

bool hasClassicsAssets(const std::string &dir)
{
	std::error_code ec;
	return !dir.empty() && fs::is_directory(fs::u8path(dir) / "emus", ec)
		&& fs::is_regular_file(fs::u8path(dir) / "emus" / "Jak v2" / "eboot.bin", ec);
}

bool installClassicsAssets(const std::string &archive, const std::string &dir,
	const TarProgress &progress, std::string *error)
{
	const fs::path target = fs::u8path(dir);
	const fs::path partial = fs::u8path(dir + ".partial");
	std::error_code ec;
	fs::remove_all(partial, ec);
	fs::create_directories(partial, ec);
	const bool ok = extractTarGz(archive,
		[&](const std::string &path, uint64_t) {
			const std::string rel = classicsAssetPath(path);
			return rel.empty() ? std::string() : (partial / fs::u8path(rel)).u8string();
		},
		progress, error);
	if(!ok || !hasClassicsAssets(partial.u8string()))
	{
		if(ok && error)
			*error = "the archive has no emulators in it";
		fs::remove_all(partial, ec);
		return false;
	}
	fs::remove_all(target, ec);
	fs::rename(partial, target, ec);
	if(ec)
	{
		if(error)
			*error = "could not move the emulator files into place: " + ec.message();
		fs::remove_all(partial, ec);
		return false;
	}
	return true;
}

} // namespace orbislink::fpkg
