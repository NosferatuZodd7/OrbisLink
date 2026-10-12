// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Looking for consoles on this PC's own network.
//
// A single broadcast to 255.255.255.255 leaves through one network
// interface only — on a PC with a VPN, a virtual machine's adapter or Wi-Fi
// and cable together, often not the console's — and some routers do not
// pass broadcasts between Wi-Fi and cable at all. So a search asks each
// interface's own broadcast address (as chiaki-ng does) and, on the small
// private networks a home has, every address of it one by one.

#include "orbislink/net/net_utils.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

struct LanTargets
{
	// 255.255.255.255 first, then each interface's (192.168.1.255…).
	std::vector<std::string> broadcasts;
	// Every other address of the interfaces' networks that are private
	// (10/8, 172.16/12, 192.168/16) and small (a /23 or smaller): at most
	// `maxHosts` of them, this PC's own left out.
	std::vector<std::string> hosts;
};

// What to ask, from these interfaces (localInterfaces(), or made up in tests).
LanTargets lanTargets(const std::vector<LocalInterface> &interfaces, size_t maxHosts = 1024);
bool privateIPv4(uint32_t hostOrder);

// Which of `hosts` accept a TCP connection on `port`: tried all at once,
// for at most `timeoutMs`, in groups of `batch` sockets.
std::vector<std::string> hostsWithPort(const std::vector<std::string> &hosts, uint16_t port, int timeoutMs,
	const std::atomic<bool> *cancel = nullptr, size_t batch = 256);

} // namespace orbislink
