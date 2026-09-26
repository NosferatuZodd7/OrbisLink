// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/stream_types.h"

#include <string>
#include <vector>

namespace orbislink {

// Guarda o que o registo devolveu. Fica num ficheiro à parte das
// definições porque é material sensível: a chave de registo e a rp_key
// autenticam este PC na consola.
//
// O ficheiro nunca aparece no diagnóstico exportado e os valores são
// mascarados no registo (ver redactSensitive).
class CredentialStore
{
public:
	explicit CredentialStore(std::string path);

	static std::string defaultPath();

	// Devolve as credenciais do host-id pedido, ou valid=false.
	StreamCredentials load(const std::string &hostId) const;
	// Todas as consolas registadas.
	std::vector<StreamCredentials> all() const;
	bool save(const StreamCredentials &credentials);
	bool forget(const std::string &hostId);

	const std::string &path() const { return path_; }

private:
	std::string path_;
};

// Conversões usadas pelo registo e pela sessão.
std::string bytesToHex(const unsigned char *data, size_t size);
bool hexToBytes(const std::string &hex, unsigned char *out, size_t size);
// O Account ID da PSN é dado pelo utilizador em base64 (8 bytes).
bool decodeAccountId(const std::string &base64, unsigned char out[8], std::string *error);

} // namespace orbislink
