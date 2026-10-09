// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PS5 homebrew catalog of homebrew.page (the one ProsperoStore reads),
// through its store API: static, signed JSON files. The catalog is used
// only once its signature and every file's SHA-256 check out.
// https://github.com/blackbearreloaded/ps5-homebrew-catalog/blob/main/docs/api.md
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace orbislink::store {

// One app as the list gives it (index.json).
struct StoreApp
{
	std::string titleId;
	std::string status;          // "available" or "coming_soon"
	std::string name;
	std::string kind;            // "app", "game", "tool"…
	std::string author;
	std::string version;         // the developer's tag, for display only
	std::string contentVersion;  // NN.NNN.NNN, what updates are decided on
	std::string format;          // "zip" (installable), or an image format
	int64_t size = -1;
	std::string released;
	std::string updated;
	std::string iconSmall;
	std::string iconHash;
	std::string sandbox;         // "stays", "leaves", "unclear" or ""

	bool available() const { return status == "available"; }
	bool installable() const { return available() && format == "zip"; }
};

// Everything about one app (apps/<TITLEID>.json).
struct StoreAppDetail : StoreApp
{
	std::string description;
	std::string license;
	std::string sourceRepo;
	std::string artifactUrl;
	std::string artifactName;
	std::string sha256;
	std::string icon;
	std::string page;
	std::string releaseUrl;
	std::string releaseNotes;
	bool releaseNotesTruncated = false;
	bool prerelease = false;
	// What the catalog's scan of the release found (unknown when not scanned).
	bool safetyKnown = false;
	std::vector<std::string> safetyRoutes;
	int safetyHelpers = 0;
	int safetyHelpersUnapproved = 0;
	bool safetyNetwork = false;
	std::string safetyBuild;
};

struct CatalogManifest
{
	int64_t sequence = -1;
	std::string commit;
	std::map<std::string, std::string> files; // path under /api/v1/ → SHA-256
};

using PublicKey = std::array<uint8_t, 32>;

// The two keys that sign the catalog (docs/api.md, "The public keys").
const std::vector<PublicKey> &catalogKeys();

// Ed25519: valid when any of the keys verifies the signature (64 bytes)
// over the exact bytes.
bool verifySignature(const std::string &bytes, const std::string &signature, const std::vector<PublicKey> &keys);

bool parseManifest(const std::string &bytes, CatalogManifest *out, std::string *error);
bool parseIndex(const std::string &bytes, std::vector<StoreApp> *apps, std::string *error);
bool parseAppDetail(const std::string &bytes, StoreAppDetail *out, std::string *error);

// Compares two NN.NNN.NNN versions: <0, 0, >0; or kNotComparable when
// either is not in that form.
constexpr int kNotComparable = -1000;
int compareContentVersions(const std::string &a, const std::string &b);

// Fetching it: the main site first, the mirror when it does not answer.
// Kept in `cacheDir`: the last catalog that checked out (shown when offline,
// checked again on reading), the highest sequence ever accepted (an older
// catalog is refused) and the icons.
class StoreCatalog
{
public:
	StoreCatalog(std::string cacheDir, std::string userAgent);

	// The list, verified. When the network fails, the last one kept is used
	// (offline() then says so).
	bool refresh(std::string *error);
	const std::vector<StoreApp> &apps() const { return apps_; }
	bool offline() const { return offline_; }
	int64_t sequence() const { return manifest_.sequence; }

	// One app's file, checked against the manifest.
	bool details(const std::string &titleId, StoreAppDetail *out, std::string *error);
	// A local copy of the app's small icon, downloaded once per picture; ""
	// when it has none or it cannot be had.
	std::string iconFile(const StoreApp &app);

	// For the tests: other keys and addresses.
	void setKeys(std::vector<PublicKey> keys) { keys_ = std::move(keys); }
	void setBases(std::vector<std::string> bases) { bases_ = std::move(bases); }

private:
	bool fetch(const std::string &url, std::string *body, std::string *error) const;
	bool load(const std::string &base, std::string *error);
	bool accept(const std::string &manifest, const std::string &signature, const std::string &index,
		std::string *error);
	bool hashMatches(const std::string &path, const std::string &bytes) const;

	std::string cacheDir_;
	std::string userAgent_;
	std::vector<PublicKey> keys_;
	std::vector<std::string> bases_;
	std::string base_;
	CatalogManifest manifest_;
	std::vector<StoreApp> apps_;
	bool offline_ = false;
};

} // namespace orbislink::store
