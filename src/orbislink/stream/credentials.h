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

	// The registration on that console for that account (base64), or
	// valid=false. Never another account's: each account registers on its
	// own. With no account, the console's registration as before.
	StreamCredentials load(const std::string &hostId, const std::string &accountId = std::string()) const;
	// All registrations, every console and account.
	std::vector<StreamCredentials> all() const;
	// The registrations on one console, one per account.
	std::vector<StreamCredentials> forHost(const std::string &hostId) const;
	// Stores it, replacing the one for the same console and account.
	bool save(const StreamCredentials &credentials);
	// A registration made before accounts were told apart (no account) is
	// given the account it was made with. False when there is none, or that
	// account already has its own.
	bool adopt(const std::string &hostId, const std::string &accountId);
	// Forgets that account's registration on the console; with no account,
	// every registration on it.
	bool forget(const std::string &hostId, const std::string &accountId = std::string());

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
