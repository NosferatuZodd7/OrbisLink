// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

struct LocalInterface
{
	std::string name;
	std::string address; // IPv4 as text
	std::string netmask;
	bool loopback = false;
};

// The machine's active IPv4 interfaces.
std::vector<LocalInterface> localInterfaces();

bool parseIPv4(const std::string &text, uint32_t *outHostOrder);
bool sameSubnet(const std::string &a, const std::string &b, const std::string &netmask);

// Picks the local IP whose subnet contains `consoleIp` (§5.3). If no
// interface matches it returns the first non-loopback one; an empty string if
// there is none.
std::string localAddressForConsole(const std::string &consoleIp);

// TCP connection with a timeout: the basis of the service checks (§5.1).
// `banner`, if not null, receives the first line sent by the server (used to
// confirm FTP's "220").
bool tcpProbe(const std::string &host, uint16_t port, int timeoutMs, std::string *banner = nullptr);

// A free TCP port starting at `preferred` (§5.3: if taken, pick another).
bool findFreePort(const std::string &bindAddress, uint16_t preferred, uint16_t *outPort,
	int maxAttempts = 64);

} // namespace orbislink
