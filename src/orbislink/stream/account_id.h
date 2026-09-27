// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>

namespace orbislink {

// The PSN Account ID, in the three forms it is usually found in.
//
// It is always the same 64-bit number. What changes is how it is written:
//
//   decimal      the "user_id" as the PSN account has it
//   hexadecimal  the same number in base 16, most significant byte on the
//                left — this is how most tools on a jailbroken console
//                show it
//   base64       the number's 8 bytes in little-endian, encoded — the only
//                form Remote Play accepts
//
// The byte order is not this project's choice: it is in chiaki-ng's
// scripts/psn-account-id.py, which does
// base64.b64encode(user_id.to_bytes(8, "little")). Writing it the other way
// round gives an ID the console refuses without saying why.
struct AccountId
{
	bool valid = false;
	std::string base64;
	std::string hex;     // 16 digits, without the "0x"
	std::string decimal;
	// How the text was read: "base64", "hex" or "decimal".
	std::string format;
	// Why it failed, when valid is false.
	std::string error;
};

// Reads the Account ID in any of the three forms and returns the other two.
//
// There is one ambiguity that does not resolve itself: an all-digit text of
// 16 characters could be decimal or hexadecimal. It is read as decimal,
// because that is the form PSN gives; to force hexadecimal, write it with
// "0x" in front. That is why the interface shows all three forms at once —
// whoever has it in front of them sees straight away whether it matches.
AccountId parseAccountId(const std::string &message);

// Reverses the byte order. Some tools show the raw bytes instead of the
// number, and then the hexadecimal comes out backwards.
AccountId reverseAccountIdBytes(const AccountId &id);

} // namespace orbislink
