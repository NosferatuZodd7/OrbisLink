// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/update/version.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

// A release as GitHub describes it.
struct ReleaseInfo
{
	std::string tag;        // "v0.1.8"
	std::string name;       // release title
	std::string notes;      // corpo em markdown
	std::string pageUrl;    // release page, to open in the browser
	bool prerelease = false;

	// O ficheiro a descarregar para esta plataforma, se existir.
	std::string assetName;
	std::string assetUrl;
	int64_t assetSize = 0;
	// SHA-256 published next to the file, when there is one. Empty means
	// "not published" — and then nothing is verified, which has to be
	// told to whoever presses the button.
	std::string assetSha256;
	// Alternatively, the "<name>.sha256" asset to read it from. It is only
	// fetched at download time, so verification does not cost two requests
	// for someone who is not even going to update.
	std::string assetSha256Url;

	Version version() const { return parseVersion(tag); }
};

enum class UpdateChannel { Stable, Testing };

const char *updateChannelName(UpdateChannel channel);
UpdateChannel updateChannelFromName(const std::string &name, UpdateChannel fallback);

struct UpdateCheckResult
{
	bool ok = false;              // the check itself went well
	bool updateAvailable = false;
	std::string message;          // what to tell whoever is looking
	ReleaseInfo release;
};

// Checks whether a newer version is published on GitHub.
//
// It needs no credentials: it assumes a public repository. The repository
// is configurable instead of being in the code, so the app can follow it
// without being recompiled.
class UpdateChecker
{
public:
	struct Config
	{
		// Comes from CMake's ORBISLINK_REPOSITORY, which CI fills with the
		// repository the build ran in. Empty in a local build: then the
		// check says it is not configured, instead of hitting someone
		// else's repository.
		std::string repository = ORBISLINK_REPOSITORY_STRING; // "dono/nome"
		UpdateChannel channel = UpdateChannel::Stable;
		std::string currentVersion;
		// Suffix of the file to look for in the release assets. On
		// Windows it is the installer; on other systems it is empty and
		// only the release page is offered.
		std::string assetSuffix;
		std::string apiBase = "https://api.github.com";
		int timeoutMs = 15000;
	};

	explicit UpdateChecker(Config config) : config_(std::move(config)) {}

	const Config &config() const { return config_; }

	UpdateCheckResult check() const;

	// Exposed for tests: turns the API reply into the list of
	// releases, with no network involved.
	static std::vector<ReleaseInfo> parseReleases(const std::string &json,
		const std::string &assetSuffix);
	// Picks the release to offer from those that came back.
	static const ReleaseInfo *pick(const std::vector<ReleaseInfo> &releases,
		UpdateChannel channel, const std::string &currentVersion);
	// What to say when the chosen channel has nothing newer. If the other
	// channel does, say which and where: "you are up to date", with a new
	// version published right next to it, is a half-truth that makes
	// people stop looking.
	static std::string describeNothingNew(const std::vector<ReleaseInfo> &releases,
		UpdateChannel channel, const std::string &currentVersion);

private:
	Config config_;
};

// This platform's installer suffix, or empty if there is none.
std::string platformAssetSuffix();

} // namespace orbislink
