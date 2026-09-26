// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/update/sha256.h"
#include "orbislink/update/update_checker.h"
#include "orbislink/update/version.h"
#include "test_support.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace orbislink;

ORBISLINK_TEST(sha256_matches_the_public_vectors)
{
	// Vectores do NIST/RFC 6234.
	CHECK_EQ(sha256Hex(""),
		std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(sha256Hex("abc"),
		std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

	// A million "a": exercises chained blocks, not just the padding.
	Sha256 tooLong;
	const std::string block(1000, 'a');
	for(int i = 0; i < 1000; ++i)
		tooLong.update(block);
	CHECK_EQ(tooLong.hex(),
		std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

ORBISLINK_TEST(sha256_of_a_file_matches_the_in_memory_one)
{
	const std::string path = ".orbislink-test-sha.bin";
	std::string content;
	for(int i = 0; i < 100000; ++i)
		content.push_back(static_cast<char>(i % 251));
	{
		std::ofstream out(path, std::ios::binary);
		out.write(content.data(), static_cast<std::streamsize>(content.size()));
	}
	CHECK_EQ(sha256File(path), sha256Hex(content));
	// A file that does not exist must not return the empty hash: that
	// would let an integrity check pass.
	CHECK(sha256File(".orbislink-missing.bin").empty());
	std::remove(path.c_str());
}

ORBISLINK_TEST(reads_versions_with_and_without_v)
{
	const Version a = parseVersion("0.1.8");
	CHECK(a.valid);
	CHECK_EQ(a.major, 0);
	CHECK_EQ(a.minor, 1);
	CHECK_EQ(a.patch, 8);
	CHECK(a.pre.empty());

	const Version b = parseVersion("v1.2.3-dev.42");
	CHECK(b.valid);
	CHECK_EQ(b.major, 1);
	CHECK_EQ(b.minor, 2);
	CHECK_EQ(b.patch, 3);
	CHECK_EQ(b.pre, std::string("dev.42"));
	CHECK_EQ(b.toString(), std::string("1.2.3-dev.42"));

	// What the app uses when CI gives it a strange name.
	CHECK(!parseVersion("").valid);
	CHECK(!parseVersion("not-a-version").valid);
	// Build metadata does not count.
	CHECK_EQ(compareVersions("0.1.8+abc", "0.1.8"), 0);
}

ORBISLINK_TEST(orders_versions_like_semver)
{
	CHECK(compareVersions("0.1.7", "0.1.8") < 0);
	CHECK(compareVersions("0.1.8", "0.1.8") == 0);
	CHECK(compareVersions("0.2.0", "0.1.9") > 0);
	CHECK(compareVersions("1.0.0", "0.9.9") > 0);

	// What makes the testing channel work: a dev comes before the final.
	CHECK(compareVersions("0.1.8-dev.2", "0.1.8") < 0);
	CHECK(compareVersions("0.1.8", "0.1.8-dev.2") > 0);
	CHECK(compareVersions("0.1.8-dev.2", "0.1.8-dev.3") < 0);
	// By value, not as text: 10 comes after 9.
	CHECK(compareVersions("0.1.8-dev.9", "0.1.8-dev.10") < 0);
	// Someone on a dev build gets the next one and then the final.
	CHECK(compareVersions("0.1.8-dev.42", "0.1.9") < 0);
}

ORBISLINK_TEST(unreadable_version_never_wins)
{
	// If GitHub returns garbage in tag_name, the app must not conclude there
	// is a new version — it would be an update to nowhere.
	CHECK(compareVersions("garbage", "0.1.8") < 0);
	CHECK(compareVersions("0.1.8", "garbage") > 0);
}


namespace {

// A reply like the GitHub API gives, trimmed to what is read.
const char *kGitHubReply = R"([
  {
    "tag_name": "v0.1.9-dev.3",
    "name": "Testing build 3",
    "body": "Changes of the day.\n\nOrbisLink-0.1.9-dev.3-setup.exe  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
    "html_url": "https://github.com/example/app/releases/tag/v0.1.9-dev.3",
    "draft": false,
    "prerelease": true,
    "assets": [
      { "name": "OrbisLink-0.1.9-dev.3-setup.exe",
        "browser_download_url": "https://exemplo/dev3-setup.exe", "size": 12345 }
    ]
  },
  {
    "tag_name": "v0.1.8",
    "name": "v0.1.8",
    "body": "Remote Play.",
    "html_url": "https://github.com/example/app/releases/tag/v0.1.8",
    "draft": false,
    "prerelease": false,
    "assets": [
      { "name": "OrbisLink-0.1.8-setup.exe",
        "browser_download_url": "https://exemplo/018-setup.exe", "size": 999 },
      { "name": "OrbisLink-0.1.8-setup.exe.sha256",
        "browser_download_url": "https://exemplo/018.sha256", "size": 80 },
      { "name": "OrbisLink-0.1.8-portable.zip",
        "browser_download_url": "https://exemplo/018.zip", "size": 888 }
    ]
  },
  {
    "tag_name": "v0.2.0",
    "name": "draft, not published yet",
    "body": "",
    "draft": true,
    "prerelease": false,
    "assets": []
  }
])";

} // namespace

ORBISLINK_TEST(reads_the_github_reply)
{
	const auto releases = UpdateChecker::parseReleases(kGitHubReply, "-setup.exe");
	// The draft does not count: for anyone outside, it does not exist.
	CHECK_EQ(releases.size(), static_cast<size_t>(2));

	CHECK_EQ(releases[0].tag, std::string("v0.1.9-dev.3"));
	CHECK(releases[0].prerelease);
	CHECK_EQ(releases[0].assetName, std::string("OrbisLink-0.1.9-dev.3-setup.exe"));
	CHECK_EQ(releases[0].assetUrl, std::string("https://exemplo/dev3-setup.exe"));
	CHECK_EQ(releases[0].assetSize, static_cast<int64_t>(12345));
	// The hash was written in the body, on the file's line.
	CHECK_EQ(releases[0].assetSha256,
		std::string("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));

	CHECK_EQ(releases[1].tag, std::string("v0.1.8"));
	CHECK(!releases[1].prerelease);
	// It picked the installer and not the zip or the .sha256.
	CHECK_EQ(releases[1].assetName, std::string("OrbisLink-0.1.8-setup.exe"));
	CHECK_EQ(releases[1].assetSha256Url, std::string("https://exemplo/018.sha256"));
	CHECK(releases[1].assetSha256.empty());
}

ORBISLINK_TEST(hash_is_the_downloaded_files_one)
{
	// One .sha256 per file, in the order the API returns them: the zip's
	// comes after the installer's. The installer's is what counts.
	const char *reply = R"([{
	  "tag_name": "v1.0.0", "draft": false, "prerelease": false, "body": "",
	  "assets": [
	    { "name": "orbislink-1.0.0-linux-x86_64.tar.gz.sha256", "browser_download_url": "https://x/linux.sha256" },
	    { "name": "OrbisLink-1.0.0-setup.exe", "browser_download_url": "https://x/setup.exe", "size": 10 },
	    { "name": "OrbisLink-1.0.0-setup.exe.sha256", "browser_download_url": "https://x/setup.sha256" },
	    { "name": "OrbisLink-1.0.0-windows-x64.zip.sha256", "browser_download_url": "https://x/zip.sha256" }
	  ]}])";
	const auto releases = UpdateChecker::parseReleases(reply, "-setup.exe");
	CHECK_EQ(releases.size(), static_cast<size_t>(1));
	CHECK_EQ(releases[0].assetName, std::string("OrbisLink-1.0.0-setup.exe"));
	CHECK_EQ(releases[0].assetSha256Url, std::string("https://x/setup.sha256"));

	// The notes carry one line per file, in the format release.yml
	// writes. The installer's counts, not the zip's or the Linux one.
	const char *withNotes = R"([{
	  "tag_name": "v1.0.0", "draft": false, "prerelease": false,
	  "body": "Notes.\n\n### SHA-256\n\n`096da89a3a9f3624311e907d24af4c9b907b9bfabcd0af7dec2f5fdc935f2777`  OrbisLink-1.0.0-setup.exe\n\n`d2f6e324971ce63f55db4e66ce5e15be2359de673111619213c9b81f42f512cc`  OrbisLink-1.0.0-windows-x64.zip\n\n`e85c6cf68dfaa7d1f48a0a02a5b7eb43747adc7c074819547b38276ed8abd337`  orbislink-1.0.0-linux-x86_64.tar.gz\n",
	  "assets": [
	    { "name": "OrbisLink-1.0.0-setup.exe", "browser_download_url": "https://x/setup.exe", "size": 10 }
	  ]}])";
	CHECK_EQ(UpdateChecker::parseReleases(withNotes, "-setup.exe")[0].assetSha256,
		std::string("096da89a3a9f3624311e907d24af4c9b907b9bfabcd0af7dec2f5fdc935f2777"));

	// Without the file's own .sha256, another's is not used.
	const char *noHash = R"([{
	  "tag_name": "v1.0.0", "draft": false, "prerelease": false, "body": "",
	  "assets": [
	    { "name": "OrbisLink-1.0.0-setup.exe", "browser_download_url": "https://x/setup.exe", "size": 10 },
	    { "name": "OrbisLink-1.0.0-windows-x64.zip.sha256", "browser_download_url": "https://x/zip.sha256" }
	  ]}])";
	CHECK(UpdateChecker::parseReleases(noHash, "-setup.exe")[0].assetSha256Url.empty());
}

ORBISLINK_TEST(stable_channel_ignores_prereleases)
{
	const auto releases = UpdateChecker::parseReleases(kGitHubReply, "-setup.exe");

	// Someone on 0.1.7 and the stable channel gets 0.1.8, not the dev.
	const ReleaseInfo *stable =
		UpdateChecker::pick(releases, UpdateChannel::Stable, "0.1.7");
	CHECK(stable != nullptr);
	CHECK_EQ(stable->tag, std::string("v0.1.8"));

	// On the testing channel they get the dev, which is newer.
	const ReleaseInfo *testing =
		UpdateChecker::pick(releases, UpdateChannel::Testing, "0.1.7");
	CHECK(testing != nullptr);
	CHECK_EQ(testing->tag, std::string("v0.1.9-dev.3"));
}

ORBISLINK_TEST(does_not_offer_what_is_installed)
{
	const auto releases = UpdateChecker::parseReleases(kGitHubReply, "-setup.exe");

	// Already on 0.1.8, stable channel: nothing to do.
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Stable, "0.1.8") == nullptr);
	// And someone ahead is never pushed back.
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Stable, "0.3.0") == nullptr);
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Testing, "0.3.0") == nullptr);
	// Someone on a dev gets the next dev.
	const ReleaseInfo *next =
		UpdateChecker::pick(releases, UpdateChannel::Testing, "0.1.9-dev.2");
	CHECK(next != nullptr);
	CHECK_EQ(next->tag, std::string("v0.1.9-dev.3"));
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Testing, "0.1.9-dev.3") == nullptr);
}

ORBISLINK_TEST(stable_channel_mentions_a_newer_testing_build)
{
	const auto releases = UpdateChecker::parseReleases(kGitHubReply, "-setup.exe");

	// On 0.1.8 and the stable channel there is nothing — but there is a newer
	// dev, and that has to be said instead of "you are up to date".
	const std::string notice =
		UpdateChecker::describeNothingNew(releases, UpdateChannel::Stable, "0.1.8");
	CHECK(notice.find("0.1.9-dev.3") != std::string::npos);
	CHECK(notice.find("Testing") != std::string::npos);

	// With nothing anywhere, it just says it is up to date.
	const std::string nothing =
		UpdateChecker::describeNothingNew(releases, UpdateChannel::Stable, "0.3.0");
	CHECK(nothing.find("dev") == std::string::npos);
	CHECK(!UpdateChecker::describeNothingNew(releases, UpdateChannel::Testing, "0.3.0").empty());
}

ORBISLINK_TEST(unreadable_reply_invents_no_releases)
{
	CHECK(UpdateChecker::parseReleases("", "-setup.exe").empty());
	CHECK(UpdateChecker::parseReleases("this is not json", "-setup.exe").empty());
	CHECK(UpdateChecker::parseReleases("[]", "-setup.exe").empty());
	// An entry without a tag gives no version to compare.
	CHECK(UpdateChecker::parseReleases(R"([{"name":"no tag"}])", "-setup.exe").empty());
}

ORBISLINK_TEST(without_a_platform_asset_there_is_still_a_page)
{
	// On Linux there is no published installer: the release still counts,
	// it just has no file to download.
	const auto releases = UpdateChecker::parseReleases(kGitHubReply, "");
	CHECK_EQ(releases.size(), static_cast<size_t>(2));
	CHECK(releases[1].assetName.empty());
	CHECK(!releases[1].pageUrl.empty());
}

ORBISLINK_TEST(badly_set_repository_fails_saying_why)
{
	UpdateChecker::Config config;
	config.repository = "no-slash";
	config.currentVersion = "0.1.8";
	const UpdateCheckResult result = UpdateChecker(config).check();
	CHECK(!result.ok);
	CHECK(!result.updateAvailable);
	CHECK(result.message.find("owner/name") != std::string::npos);
}

ORBISLINK_TEST(channel_is_read_and_written_by_name)
{
	CHECK_EQ(std::string(updateChannelName(UpdateChannel::Stable)), std::string("stable"));
	CHECK_EQ(std::string(updateChannelName(UpdateChannel::Testing)), std::string("testing"));
	CHECK(updateChannelFromName("testes", UpdateChannel::Stable) == UpdateChannel::Testing);
	CHECK(updateChannelFromName("garbage", UpdateChannel::Stable) == UpdateChannel::Stable);
}

TEST_MAIN()
