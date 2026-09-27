// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/update/update_checker.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"

#include <cctype>
#include <map>

namespace orbislink {

namespace {

// A SHA-256 published next to the file can come in two ways: in an
// "<name>.sha256" asset, or written in the release body. This handles
// the second: look for a 64-hex-digit word on the same line as the
// file name.
std::string sha256FromNotes(const std::string &notes, const std::string &assetName)
{
	if(notes.empty() || assetName.empty())
		return std::string();
	for(const std::string &line : split(notes, '\n', false))
	{
		if(line.find(assetName) == std::string::npos)
			continue;
		size_t start = std::string::npos;
		size_t counted = 0;
		for(size_t i = 0; i <= line.size(); ++i)
		{
			const bool hex = i < line.size()
				&& std::isxdigit(static_cast<unsigned char>(line[i])) != 0;
			if(hex)
			{
				if(counted == 0)
					start = i;
				++counted;
				continue;
			}
			if(counted == 64)
				return toLower(line.substr(start, 64));
			counted = 0;
		}
	}
	return std::string();
}

} // namespace

const char *updateChannelName(UpdateChannel channel)
{
	return channel == UpdateChannel::Testing ? "testing" : "stable";
}

UpdateChannel updateChannelFromName(const std::string &name, UpdateChannel fallback)
{
	if(name == "testes" || name == "testing" || name == "beta")
		return UpdateChannel::Testing;
	if(name == "estavel" || name == "stable")
		return UpdateChannel::Stable;
	return fallback;
}

std::string platformAssetSuffix()
{
#if defined(_WIN32)
	// The installer release.yml publishes.
	return "-setup.exe";
#else
	// On Linux there is no self-installing package yet (the AppImage
	// is still to do), so only the release page is offered.
	return std::string();
#endif
}

std::vector<ReleaseInfo> UpdateChecker::parseReleases(const std::string &json,
	const std::string &assetSuffix)
{
	std::vector<ReleaseInfo> releases;
	std::string error;
	const Json root = Json::parse(json, &error);
	// The API returns a list; /releases/latest returns a single object.
	std::vector<Json> entries;
	if(root.isArray())
		entries = root.items();
	else if(root.isObject())
		entries.push_back(root);
	else
		return releases;

	for(const Json &input : entries)
	{
		if(!input.isObject())
			continue;
		// Drafts do not exist for anyone on the outside.
		if(input["draft"].toLooseBool(false))
			continue;
		ReleaseInfo info;
		info.tag = input["tag_name"].toString();
		if(info.tag.empty())
			continue;
		info.name = input["name"].toString(info.tag);
		info.notes = input["body"].toString();
		info.pageUrl = input["html_url"].toString();
		info.prerelease = input["prerelease"].toLooseBool(false);

		// Every published file has its own "<name>.sha256" next to it; what
		// matters is the one for the file being downloaded, not just any
		// (the zip's does not verify the installer).
		std::map<std::string, std::string> hashes;
		const Json &assets = input["assets"];
		for(size_t i = 0; i < assets.size(); ++i)
		{
			const Json &asset = assets.at(i);
			const std::string name = asset["name"].toString();
			if(name.empty())
				continue;
			if(endsWith(toLower(name), ".sha256"))
			{
				hashes[toLower(name)] = asset["browser_download_url"].toString();
				continue;
			}
			if(assetSuffix.empty() || !endsWith(toLower(name), toLower(assetSuffix)))
				continue;
			info.assetName = name;
			info.assetUrl = asset["browser_download_url"].toString();
			info.assetSize = asset["size"].toInt(0);
		}
		if(!info.assetName.empty())
		{
			const auto hash = hashes.find(toLower(info.assetName) + ".sha256");
			if(hash != hashes.end())
				info.assetSha256Url = hash->second;
		}
		info.assetSha256 = sha256FromNotes(info.notes, info.assetName);
		releases.push_back(info);
	}
	return releases;
}

const ReleaseInfo *UpdateChecker::pick(const std::vector<ReleaseInfo> &releases,
	UpdateChannel channel, const std::string &currentVersion)
{
	const Version current = parseVersion(currentVersion);
	const ReleaseInfo *best = nullptr;
	Version bestVersion;
	for(const ReleaseInfo &info : releases)
	{
		// On the stable channel, a prerelease does not count.
		if(channel == UpdateChannel::Stable && info.prerelease)
			continue;
		const Version version = info.version();
		if(!version.valid)
			continue;
		if(best && compareVersions(version, bestVersion) <= 0)
			continue;
		best = &info;
		bestVersion = version;
	}
	if(!best)
		return nullptr;
	// It is only news if it is really newer than what is installed. An
	// unreadable local version counts as old, and then any valid release
	// will do.
	if(current.valid && compareVersions(bestVersion, current) <= 0)
		return nullptr;
	return best;
}

std::string UpdateChecker::describeNothingNew(const std::vector<ReleaseInfo> &releases,
	UpdateChannel channel, const std::string &currentVersion)
{
	if(channel == UpdateChannel::Stable)
	{
		// With only testing builds published, the stable channel would answer
		// "you are on the latest version" with a new version right next to it.
		const ReleaseInfo *testing = pick(releases, UpdateChannel::Testing, currentVersion);
		if(testing)
			return std::string(QT_TRANSLATE_NOOP("Messages",
					   "There is no newer stable version, but there is a test build (to get it, choose the "
					   	"\"Testing\" channel in the settings)"))
				+ ": " + testing->version().toString();
		return QT_TRANSLATE_NOOP("Messages", "You are on the latest stable version.");
	}
	return QT_TRANSLATE_NOOP("Messages", "You are on the latest version, including test builds.");
}

UpdateCheckResult UpdateChecker::check() const
{
	UpdateCheckResult result;
	if(config_.repository.find('/') == std::string::npos)
	{
		result.message = QT_TRANSLATE_NOOP("Messages", "The update repository is not set (expected "
			"\"owner/name\").");
		return result;
	}

	const std::string url = config_.apiBase + "/repos/" + config_.repository
		+ "/releases?per_page=20";
	HttpClient client(config_.timeoutMs);
	HttpClient::FetchOptions options;
	options.headers.push_back("Accept: application/vnd.github+json");
	options.headers.push_back("X-GitHub-Api-Version: 2022-11-28");
	const HttpResponse response = client.fetch(url, options);

	if(!response.transportOk)
	{
		result.message = std::string(QT_TRANSLATE_NOOP("Messages", "Could not reach GitHub"))
			+ ": " + response.error;
		return result;
	}
	if(response.status == 404)
	{
		// The case that actually happens: the repository is private, or was
		// renamed. Say so instead of a misleading "nothing new".
		result.message = std::string(QT_TRANSLATE_NOOP("Messages",
							 "The update repository did not respond (it is private, or the name is wrong)"))
			+ ": " + config_.repository;
		return result;
	}
	if(response.status == 403 || response.status == 429)
	{
		result.message = QT_TRANSLATE_NOOP("Messages", "GitHub asked to wait (rate limit). Try again "
			"later.");
		return result;
	}
	if(response.status < 200 || response.status >= 300)
	{
		result.message = std::string(QT_TRANSLATE_NOOP("Messages", "GitHub responded with an error"))
			+ ": " + std::to_string(response.status);
		return result;
	}

	const std::vector<ReleaseInfo> releases = parseReleases(response.body, config_.assetSuffix);
	if(releases.empty())
	{
		result.ok = true;
		result.message = QT_TRANSLATE_NOOP("Messages", "No releases have been published yet.");
		return result;
	}

	result.ok = true;
	const ReleaseInfo *fresh = pick(releases, config_.channel, config_.currentVersion);
	if(!fresh)
	{
		result.message = describeNothingNew(releases, config_.channel, config_.currentVersion);
		return result;
	}
	result.updateAvailable = true;
	result.release = *fresh;
	result.message = std::string(QT_TRANSLATE_NOOP("Messages", "A new version is available"))
		+ ": " + fresh->version().toString();
	logInfo("Update available: " + fresh->tag + " (installed: " + config_.currentVersion
		+ ")");
	return result;
}

} // namespace orbislink
