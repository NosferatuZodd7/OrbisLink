// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace orbislink {

// Estado da consola tal como responde ao pedido de descoberta (porta
// 987/UDP no PS4 — ver docs/validation.md).
enum class HostState { Unknown, Ready, Standby };

const char *hostStateName(HostState state);

// What the console says about itself when it answers discovery.
struct HostInfo
{
	bool found = false;
	HostState state = HostState::Unknown;
	bool ps5 = false;
	std::string address;
	std::string name;           // name the user gave the console
	std::string id;             // host-id, o MAC sem separadores
	std::string systemVersion;  // ex.: "09000000"
	std::string runningAppName;
	std::string runningAppTitleId;
	uint16_t requestPort = 0;
	// ChiakiTarget value matching the system version: it tells chiaki
	// which protocol to speak.
	int target = 0;
};

// What is stored after registering the console. Without it there is no
// session: the registration key and the "morning" are what authenticate the PC.
struct StreamCredentials
{
	bool valid = false;
	std::string nickname;
	std::string hostId;       // MAC em hexadecimal, para casar com a descoberta
	std::string registKey;    // rp_regist_key, texto
	std::string rpKeyHex;     // rp_key (16 bytes) em hexadecimal
	uint32_t rpKeyType = 0;
	int target = 0;
	bool ps5 = false;
	// 64-bit credential used to wake a console in rest mode: it is the
	// registration key itself read as hexadecimal.
	uint64_t wakeupCredential() const;
};

} // namespace orbislink
