// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Installing a homebrew app from the catalog on a jailbroken PS5: its ZIP
// holds the app folder (<TITLEID>/ with eboot.bin and sce_sys/), which goes to
// a folder ShadowMountPlus scans (/data/homebrew by default); ShadowMountPlus
// then adds it to the home screen by itself. No package is built: the
// folder is the install.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {
struct FtpEntry;
}

namespace orbislink::store {

// What the installer needs from the console: its FTP.
class StoreRemote
{
public:
	virtual ~StoreRemote() = default;
	// Empty and true for a folder that is not there.
	virtual bool list(const std::string &dir, std::vector<FtpEntry> *entries, std::string *error) = 0;
	virtual bool download(const std::string &remote, const std::string &local, std::string *error) = 0;
	virtual bool upload(const std::string &local, const std::string &remote, std::string *error) = 0;
	virtual bool makeDirectory(const std::string &dir) = 0;
	virtual bool rename(const std::string &from, const std::string &to, std::string *error) = 0;
	virtual bool removeFile(const std::string &path, std::string *error) = 0;
	virtual bool removeDirectory(const std::string &path, std::string *error) = 0;
	// False when the server does not do SITE CHMOD.
	virtual bool chmod(const std::string &path, const std::string &mode, std::string *error) = 0;
};

// What an install did beyond putting the app in place.
struct InstallReport
{
	// False when the console's FTP could not set permissions: the app may
	// not start (CE-107750-0) until its folder is set to 777 by hand.
	bool permissionsSet = true;
	// An earlier copy was replaced; it is kept here until the next update.
	bool replaced = false;
	std::string earlierCopy;
	// The user's files moved on into the new folder, and those left behind.
	std::vector<std::string> carried;
	std::vector<std::string> left;
};

struct InstalledApp
{
	std::string titleId;
	std::string contentVersion; // from its sce_sys/param.json, "" if unknown
	std::string folder;
};

class StoreInstaller
{
public:
	// The stage ("download", "verify", "unpack", "finish") and how far.
	// Returning false cancels.
	using Progress = std::function<bool(const std::string &stage, double fraction)>;

	explicit StoreInstaller(std::string installRoot = "/data/homebrew");
	const std::string &installRoot() const { return installRoot_; }

	// The apps in the install folder, with their content version.
	std::vector<InstalledApp> installed(StoreRemote &remote, const std::string &scratchDir, std::string *error);

	// Puts the app folder from the ZIP in place: unpacked into a staging
	// folder outside what ShadowMountPlus scans, opened to everyone (the
	// console starts an app only with permissions 777), then moved in. The
	// copy it replaces is kept in /data/orbislink/previous/<TITLEID> until
	// the next update; what the user put in it (what the new archive does
	// not have: games, saves, settings) moves on into the new folder, as
	// copying the new version over the old one would keep it. Nothing in
	// place changes before the end.
	bool install(StoreRemote &remote, const std::string &zipPath, const std::string &titleId,
		const std::string &scratchDir, const Progress &progress, std::string *error,
		InstallReport *report = nullptr);

	// Takes the app folder away (its saves stay).
	bool uninstall(StoreRemote &remote, const std::string &titleId, std::string *error);

	static std::string stagingRoot() { return "/data/orbislink/staging"; }
	static std::string previousRoot() { return "/data/orbislink/previous"; }

private:
	std::string installRoot_;
};

// Removes a folder on the console and all inside it.
bool removeRemoteTree(StoreRemote &remote, const std::string &path, std::string *error);

} // namespace orbislink::store
