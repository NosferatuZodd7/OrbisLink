// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/store/store_catalog.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"
#include "orbislink/update/sha256.h"

#include <openssl/evp.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace orbislink::store {

namespace {

const char *const kMainBase = "https://homebrew.page/api/v1/";
const char *const kMirrorBase = "https://blackbearreloaded.github.io/ps5-homebrew-catalog/api/v1/";

PublicKey keyFromHex(const char *hex)
{
	PublicKey key {};
	for(size_t i = 0; i < key.size(); ++i)
	{
		auto digit = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
		key[i] = static_cast<uint8_t>((digit(hex[i * 2]) << 4) | digit(hex[i * 2 + 1]));
	}
	return key;
}

bool readFile(const std::string &path, std::string *out)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	if(!in)
		return false;
	out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

bool writeFile(const std::string &path, const std::string &bytes)
{
	std::error_code ignored;
	fs::create_directories(fs::u8path(path).parent_path(), ignored);
	std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
	out << bytes;
	return static_cast<bool>(out);
}

// A title ID as the catalog uses it: four capital letters and five digits.
bool plainTitleId(const std::string &id)
{
	if(id.size() != 9)
		return false;
	for(size_t i = 0; i < 9; ++i)
		if(i < 4 ? !std::isupper(static_cast<unsigned char>(id[i])) : !std::isdigit(static_cast<unsigned char>(id[i])))
			return false;
	return true;
}

std::string text(const Json &value)
{
	return value.isString() ? value.toString() : std::string();
}

void readApp(const Json &item, StoreApp *app)
{
	app->titleId = text(item["titleid"]);
	app->status = text(item["status"]);
	app->name = text(item["name"]);
	app->kind = text(item["kind"]);
	app->author = text(item["author"]);
	app->version = item["version"].isNumber() ? std::to_string(item["version"].toInt()) : text(item["version"]);
	app->contentVersion = text(item["content_version"]);
	app->format = text(item["format"]);
	app->size = item["size"].isNumber() ? item["size"].toInt() : -1;
	app->released = text(item["released"]);
	app->updated = text(item["updated"]);
	app->iconSmall = text(item["icon_small"]);
	app->iconHash = text(item["icon_hash"]);
	app->sandbox = text(item["sandbox"]);
}

} // namespace

const std::vector<PublicKey> &catalogKeys()
{
	static const std::vector<PublicKey> keys = {
		keyFromHex("87391bf1698ecef101bf5e29dc8585ee5947d571e19470de7411c5d3b137b5cf"),
		keyFromHex("509bcfab7edfb4e5ed23639488517c6ef2657c13b6b7f2bf699c8989d9b0dd7b"),
	};
	return keys;
}

bool verifySignature(const std::string &bytes, const std::string &signature, const std::vector<PublicKey> &keys)
{
	if(signature.size() != 64)
		return false;
	for(const PublicKey &raw : keys)
	{
		EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, raw.data(), raw.size());
		if(!key)
			continue;
		EVP_MD_CTX *context = EVP_MD_CTX_new();
		const bool ok = context && EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1
			&& EVP_DigestVerify(context, reinterpret_cast<const unsigned char *>(signature.data()), signature.size(),
				   reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()) == 1;
		EVP_MD_CTX_free(context);
		EVP_PKEY_free(key);
		if(ok)
			return true;
	}
	return false;
}

bool parseManifest(const std::string &bytes, CatalogManifest *out, std::string *error)
{
	std::string why;
	const Json root = Json::parse(bytes, &why);
	if(!root.isObject() || !root["files"].isObject() || !root["sequence"].isNumber())
	{
		if(error)
			*error = "the catalog's manifest is not readable" + (why.empty() ? std::string() : ": " + why);
		return false;
	}
	out->sequence = root["sequence"].toInt();
	out->commit = text(root["commit"]);
	out->files.clear();
	for(const auto &pair : root["files"].members())
		if(pair.second.isString())
			out->files[pair.first] = toLower(pair.second.toString());
	return true;
}

bool parseIndex(const std::string &bytes, std::vector<StoreApp> *apps, std::string *error)
{
	std::string why;
	const Json root = Json::parse(bytes, &why);
	if(!root.isObject() || !root["apps"].isArray())
	{
		if(error)
			*error = "the catalog's list is not readable" + (why.empty() ? std::string() : ": " + why);
		return false;
	}
	apps->clear();
	for(const Json &item : root["apps"].items())
	{
		StoreApp app;
		readApp(item, &app);
		if(plainTitleId(app.titleId) && !app.name.empty())
			apps->push_back(app);
	}
	return true;
}

bool parseAppDetail(const std::string &bytes, StoreAppDetail *out, std::string *error)
{
	std::string why;
	const Json root = Json::parse(bytes, &why);
	if(!root.isObject() || !plainTitleId(text(root["titleid"])))
	{
		if(error)
			*error = "the app's file is not readable" + (why.empty() ? std::string() : ": " + why);
		return false;
	}
	readApp(root, out);
	out->description = text(root["description"]);
	out->license = text(root["license"]);
	out->sourceRepo = text(root["source_repo"]);
	out->artifactUrl = text(root["artifact_url"]);
	out->artifactName = text(root["artifact_name"]);
	out->sha256 = toLower(text(root["sha256"]));
	out->icon = text(root["icon"]);
	out->page = text(root["page"]);
	out->releaseUrl = text(root["release_url"]);
	out->releaseNotes = text(root["release_notes"]);
	out->releaseNotesTruncated = root["release_notes_truncated"].toBool();
	out->prerelease = root["prerelease"].toBool();
	const Json &safety = root["safety"];
	out->safetyKnown = safety.isObject();
	if(out->safetyKnown)
	{
		out->sandbox = text(safety["sandbox"]);
		out->safetyRoutes.clear();
		for(const Json &route : safety["routes"].items())
			if(route.isString())
				out->safetyRoutes.push_back(route.toString());
		out->safetyHelpers = static_cast<int>(safety["helpers"].toInt());
		out->safetyHelpersUnapproved = static_cast<int>(safety["helpers_unapproved"].toInt());
		out->safetyNetwork = safety["network"].toBool();
		out->safetyBuild = text(safety["build"]);
	}
	return true;
}

int compareContentVersions(const std::string &a, const std::string &b)
{
	auto parse = [](const std::string &v, int out[3]) {
		if(v.size() != 10 || v[2] != '.' || v[6] != '.')
			return false;
		for(size_t i = 0; i < v.size(); ++i)
			if(i != 2 && i != 6 && !std::isdigit(static_cast<unsigned char>(v[i])))
				return false;
		out[0] = std::stoi(v.substr(0, 2));
		out[1] = std::stoi(v.substr(3, 3));
		out[2] = std::stoi(v.substr(7, 3));
		return true;
	};
	int x[3];
	int y[3];
	if(!parse(a, x) || !parse(b, y))
		return kNotComparable;
	for(int i = 0; i < 3; ++i)
		if(x[i] != y[i])
			return x[i] < y[i] ? -1 : 1;
	return 0;
}

StoreCatalog::StoreCatalog(std::string cacheDir, std::string userAgent)
	: cacheDir_(std::move(cacheDir)), userAgent_(std::move(userAgent)), keys_(catalogKeys()),
	  bases_({ kMainBase, kMirrorBase })
{
}

bool StoreCatalog::fetch(const std::string &url, std::string *body, std::string *error) const
{
	HttpClient http(20000);
	HttpClient::FetchOptions options;
	// The site's CDN turns away requests that do not say who they are.
	options.headers.push_back("User-Agent: " + userAgent_);
	const HttpResponse response = http.fetch(url, options);
	if(!response.transportOk || response.status != 200)
	{
		if(error)
			*error = response.transportOk ? "HTTP " + std::to_string(response.status) : response.error;
		return false;
	}
	*body = response.body;
	return true;
}

bool StoreCatalog::hashMatches(const std::string &path, const std::string &bytes) const
{
	const auto expected = manifest_.files.find(path);
	return expected != manifest_.files.end() && expected->second == sha256Hex(bytes);
}

bool StoreCatalog::accept(const std::string &manifestBytes, const std::string &signature, const std::string &index,
	std::string *error)
{
	if(!verifySignature(manifestBytes, signature, keys_))
	{
		if(error)
			*error = "the catalog's signature does not check out";
		return false;
	}
	CatalogManifest manifest;
	if(!parseManifest(manifestBytes, &manifest, error))
		return false;
	// Never an older catalog than one already accepted: a replayed one could
	// bring back a release that was taken down.
	std::string stored;
	int64_t highest = -1;
	if(readFile(joinPath(cacheDir_, "catalog-sequence"), &stored))
		highest = std::atoll(stored.c_str());
	if(manifest.sequence < highest)
	{
		if(error)
			*error = "the catalog is older than one already seen (" + std::to_string(manifest.sequence) + " < "
				+ std::to_string(highest) + ")";
		return false;
	}
	CatalogManifest previous = manifest_;
	manifest_ = manifest;
	if(!hashMatches("index.json", index))
	{
		manifest_ = previous;
		if(error)
			*error = "the catalog's list does not match its manifest";
		return false;
	}
	std::vector<StoreApp> apps;
	if(!parseIndex(index, &apps, error))
	{
		manifest_ = previous;
		return false;
	}
	apps_ = apps;
	writeFile(joinPath(cacheDir_, "catalog-sequence"), std::to_string(manifest.sequence));
	return true;
}

bool StoreCatalog::load(const std::string &base, std::string *error)
{
	std::string manifest;
	std::string signature;
	std::string index;
	if(!fetch(base + "manifest.json", &manifest, error) || !fetch(base + "manifest.sig", &signature, error)
		|| !fetch(base + "index.json", &index, error))
		return false;
	if(!accept(manifest, signature, index, error))
		return false;
	base_ = base;
	offline_ = false;
	// Kept, to show while offline (checked again then).
	const std::string kept = joinPath(cacheDir_, "catalog");
	writeFile(joinPath(kept, "base"), base);
	writeFile(joinPath(kept, "manifest.json"), manifest);
	writeFile(joinPath(kept, "manifest.sig"), signature);
	writeFile(joinPath(kept, "index.json"), index);
	return true;
}

bool StoreCatalog::refresh(std::string *error)
{
	std::string why;
	for(const std::string &base : bases_)
	{
		std::string attempt;
		if(load(base, &attempt))
			return true;
		logWarning("Store: " + base + " — " + attempt);
		if(why.empty())
			why = attempt;
	}
	// Offline: the last catalog kept, if it still checks out.
	const std::string kept = joinPath(cacheDir_, "catalog");
	std::string base;
	std::string manifest;
	std::string signature;
	std::string index;
	if(readFile(joinPath(kept, "base"), &base) && readFile(joinPath(kept, "manifest.json"), &manifest)
		&& readFile(joinPath(kept, "manifest.sig"), &signature) && readFile(joinPath(kept, "index.json"), &index)
		&& accept(manifest, signature, index, nullptr))
	{
		base_ = base;
		offline_ = true;
		return true;
	}
	if(error)
		*error = why;
	return false;
}

bool StoreCatalog::details(const std::string &titleId, StoreAppDetail *out, std::string *error)
{
	if(!plainTitleId(titleId) || base_.empty())
	{
		if(error)
			*error = "the catalog is not loaded";
		return false;
	}
	const std::string path = "apps/" + titleId + ".json";
	const std::string cached = joinPath(joinPath(cacheDir_, "catalog"), titleId + ".json");
	std::string bytes;
	// The one kept, while it still matches the manifest; else fetched again.
	if(!(readFile(cached, &bytes) && hashMatches(path, bytes)))
	{
		if(!fetch(base_ + path, &bytes, error))
			return false;
		if(!hashMatches(path, bytes))
		{
			if(error)
				*error = "the app's file does not match the catalog's manifest";
			return false;
		}
		writeFile(cached, bytes);
	}
	return parseAppDetail(bytes, out, error);
}

std::string StoreCatalog::iconFile(const StoreApp &app)
{
	if(app.iconSmall.empty() || app.iconHash.empty() || !plainTitleId(app.titleId))
		return std::string();
	std::string hash;
	for(char c : app.iconHash)
		if(std::isalnum(static_cast<unsigned char>(c)))
			hash += c;
	const std::string path = joinPath(joinPath(cacheDir_, "icons"), app.titleId + "-" + hash + ".png");
	if(fileSize(path) > 0)
		return path;
	std::string bytes;
	std::string why;
	if(!fetch(app.iconSmall, &bytes, &why) || bytes.size() < 8 || bytes.compare(0, 4, "\x89PNG") != 0)
		return std::string();
	return writeFile(path, bytes) ? path : std::string();
}

} // namespace orbislink::store
