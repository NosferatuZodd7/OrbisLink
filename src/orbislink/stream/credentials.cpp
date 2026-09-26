// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/credentials.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/settings/settings_store.h"

#include <chiaki/base64.h>
#include <chiaki/common.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace orbislink {

namespace {

const char kHexDigits[] = "0123456789ABCDEF";

int hexValue(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

} // namespace

std::string bytesToHex(const unsigned char *data, size_t size)
{
	std::string out;
	out.reserve(size * 2);
	for(size_t i = 0; i < size; ++i)
	{
		out.push_back(kHexDigits[(data[i] >> 4) & 0xF]);
		out.push_back(kHexDigits[data[i] & 0xF]);
	}
	return out;
}

bool hexToBytes(const std::string &hex, unsigned char *out, size_t size)
{
	if(hex.size() != size * 2)
		return false;
	for(size_t i = 0; i < size; ++i)
	{
		const int hi = hexValue(hex[i * 2]);
		const int lo = hexValue(hex[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return false;
		out[i] = static_cast<unsigned char>((hi << 4) | lo);
	}
	return true;
}

bool decodeAccountId(const std::string &base64, unsigned char out[8], std::string *error)
{
	const std::string trimmed = trim(base64);
	if(trimmed.empty())
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The PSN Account ID is missing.");
		return false;
	}
	size_t size = 8;
	const ChiakiErrorCode result =
		chiaki_base64_decode(trimmed.c_str(), trimmed.size(), out, &size);
	if(result != CHIAKI_ERR_SUCCESS || size != 8)
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The Account ID is not valid: it must be the base64 "
				"value, 8 bytes long (for example \"AbCdEfGhIjK=\").");
		return false;
	}
	return true;
}

CredentialStore::CredentialStore(std::string path) : path_(std::move(path)) {}

std::string CredentialStore::defaultPath()
{
	const std::string directory = SettingsStore::defaultDirectory();
	const std::string path = joinPath(directory, "registered-consoles.json");
	// Older versions used a Portuguese file name; carry it over once.
	const std::string legacy = joinPath(directory, "consolas-registadas.json");
	if(!std::ifstream(path) && std::ifstream(legacy))
		std::rename(legacy.c_str(), path.c_str());
	return path;
}

std::vector<StreamCredentials> CredentialStore::all() const
{
	std::vector<StreamCredentials> out;
	std::ifstream file(path_, std::ios::binary);
	if(!file)
		return out;
	std::ostringstream buffer;
	buffer << file.rdbuf();

	std::string err;
	const Json root = Json::parse(buffer.str(), &err);
	if(!root.isArray())
	{
		if(!err.empty())
			logWarning("Unreadable Remote Play credentials: " + err);
		return out;
	}

	for(const Json &entry : root.items())
	{
		StreamCredentials credentials;
		credentials.nickname = entry["nickname"].toString();
		credentials.hostId = entry["host_id"].toString();
		credentials.registKey = entry["regist_key"].toString();
		credentials.rpKeyHex = entry["rp_key"].toString();
		credentials.rpKeyType = static_cast<uint32_t>(entry["rp_key_type"].toInt());
		credentials.target = static_cast<int>(entry["target"].toInt());
		// The target already says whether it is a PS5; the stored "ps5" may
		// come from a version that always saved it as false after registration.
		credentials.ps5 = entry["ps5"].toBool()
			|| chiaki_target_is_ps5(static_cast<ChiakiTarget>(credentials.target));
		credentials.valid = !credentials.registKey.empty() && !credentials.rpKeyHex.empty();
		if(credentials.valid)
			out.push_back(credentials);
	}
	return out;
}

StreamCredentials CredentialStore::load(const std::string &hostId) const
{
	const std::vector<StreamCredentials> stored = all();
	for(const StreamCredentials &credentials : stored)
	{
		if(iequals(credentials.hostId, hostId))
			return credentials;
	}
	// Without a host-id (the console may not have answered discovery) the
	// only registered one is used, if there is exactly one.
	if(hostId.empty() && stored.size() == 1)
		return stored.front();
	return {};
}

bool CredentialStore::save(const StreamCredentials &credentials)
{
	if(credentials.registKey.empty() || credentials.rpKeyHex.empty())
		return false;

	std::vector<StreamCredentials> stored = all();
	bool replaced = false;
	for(StreamCredentials &existing : stored)
	{
		if(iequals(existing.hostId, credentials.hostId))
		{
			existing = credentials;
			replaced = true;
			break;
		}
	}
	if(!replaced)
		stored.push_back(credentials);

	Json root = Json::makeArray();
	for(const StreamCredentials &c : stored)
	{
		Json entry = Json::makeObject();
		entry.set("nickname", Json::fromString(c.nickname));
		entry.set("host_id", Json::fromString(c.hostId));
		entry.set("regist_key", Json::fromString(c.registKey));
		entry.set("rp_key", Json::fromString(c.rpKeyHex));
		entry.set("rp_key_type", Json::fromInt(c.rpKeyType));
		entry.set("target", Json::fromInt(c.target));
		entry.set("ps5", Json::fromBool(c.ps5));
		root.push(std::move(entry));
	}

	SettingsStore::ensureDirectory(SettingsStore::defaultDirectory());
	std::ofstream file(path_, std::ios::binary | std::ios::trunc);
	if(!file)
	{
		logError("Could not save the Remote Play credentials to " + path_);
		return false;
	}
	file << root.dump() << "\n";
	return file.good();
}

bool CredentialStore::forget(const std::string &hostId)
{
	std::vector<StreamCredentials> stored = all();
	const size_t before = stored.size();
	for(size_t i = 0; i < stored.size();)
	{
		if(iequals(stored[i].hostId, hostId))
			stored.erase(stored.begin() + static_cast<long>(i));
		else
			++i;
	}
	if(stored.size() == before)
		return false;

	// Rewrites the file without the forgotten console.
	std::remove(path_.c_str());
	for(const StreamCredentials &c : stored)
		save(c);
	return true;
}

} // namespace orbislink
