// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/stream_types.h"

#include <string>
#include <vector>

namespace orbislink {

// Stores what registration returned. It lives in a file separate from the
// settings because it is sensitive material: the registration key and the
// rp_key authenticate this PC on the console.
//
// The file never appears in the exported diagnostics and the values are
// masked in the log (see redactSensitive).
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

// Conversions used by registration and by the session.
std::string bytesToHex(const unsigned char *data, size_t size);
bool hexToBytes(const std::string &hex, unsigned char *out, size_t size);
// The PSN Account ID is given by the user in base64 (8 bytes).
bool decodeAccountId(const std::string &base64, unsigned char out[8], std::string *error);

} // namespace orbislink
