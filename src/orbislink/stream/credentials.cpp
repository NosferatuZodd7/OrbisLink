// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/credentials.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
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
			*error = "Falta o Account ID da PSN.";
		return false;
	}
	size_t size = 8;
	const ChiakiErrorCode result =
		chiaki_base64_decode(trimmed.c_str(), trimmed.size(), out, &size);
	if(result != CHIAKI_ERR_SUCCESS || size != 8)
	{
		if(error)
			*error = "O Account ID não é válido: tem de ser o valor em base64, "
					 "com 8 bytes (por exemplo \"AbCdEfGhIjK=\").";
		return false;
	}
	return true;
}

CredentialStore::CredentialStore(std::string path) : path_(std::move(path)) {}

std::string CredentialStore::defaultPath()
{
	return joinPath(SettingsStore::defaultDirectory(), "consolas-registadas.json");
}

std::vector<StreamCredentials> CredentialStore::all() const
{
	std::vector<StreamCredentials> out;
	std::ifstream file(path_, std::ios::binary);
	if(!file)
		return out;
	std::ostringstream buffer;
	buffer << file.rdbuf();

	std::string erro;
	const Json root = Json::parse(buffer.str(), &erro);
	if(!root.isArray())
	{
		if(!erro.empty())
			logWarning("Credenciais do Remote Play ilegíveis: " + erro);
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
		// O alvo já diz se é PS5; o "ps5" guardado pode vir de uma versão
		// que o gravava sempre a falso depois do registo.
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
	const std::vector<StreamCredentials> todas = all();
	for(const StreamCredentials &credentials : todas)
	{
		if(iequals(credentials.hostId, hostId))
			return credentials;
	}
	// Sem host-id (a consola pode não ter respondido à descoberta) usa-se a
	// única registada, se houver só uma.
	if(hostId.empty() && todas.size() == 1)
		return todas.front();
	return {};
}

bool CredentialStore::save(const StreamCredentials &credentials)
{
	if(credentials.registKey.empty() || credentials.rpKeyHex.empty())
		return false;

	std::vector<StreamCredentials> todas = all();
	bool substituída = false;
	for(StreamCredentials &existente : todas)
	{
		if(iequals(existente.hostId, credentials.hostId))
		{
			existente = credentials;
			substituída = true;
			break;
		}
	}
	if(!substituída)
		todas.push_back(credentials);

	Json root = Json::makeArray();
	for(const StreamCredentials &c : todas)
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
		logError("Não consegui guardar as credenciais do Remote Play em " + path_);
		return false;
	}
	file << root.dump() << "\n";
	return file.good();
}

bool CredentialStore::forget(const std::string &hostId)
{
	std::vector<StreamCredentials> todas = all();
	const size_t antes = todas.size();
	for(size_t i = 0; i < todas.size();)
	{
		if(iequals(todas[i].hostId, hostId))
			todas.erase(todas.begin() + static_cast<long>(i));
		else
			++i;
	}
	if(todas.size() == antes)
		return false;

	// Reescreve o ficheiro sem a consola esquecida.
	std::remove(path_.c_str());
	for(const StreamCredentials &c : todas)
		save(c);
	return true;
}

} // namespace orbislink
