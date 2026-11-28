// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace orbislink {

struct LocalInterface
{
	std::string name;
	std::string address; // IPv4 as text
	std::string netmask;
	bool loopback = false;
	// The system's number for the interface (Windows; 0 elsewhere).
	uint32_t index = 0;
	// The name people see ("Ethernet", "Wi-Fi" on Windows; the same as
	// `name` elsewhere).
	std::string label;
};

// The machine's active IPv4 interfaces.
std::vector<LocalInterface> localInterfaces();

bool parseIPv4(const std::string &text, uint32_t *outHostOrder);
bool sameSubnet(const std::string &a, const std::string &b, const std::string &netmask);

// Picks the local IP whose subnet contains `consoleIp` (§5.3). If no
// interface matches it returns the first non-loopback one; an empty string if
// there is none.
std::string localAddressForConsole(const std::string &consoleIp);

// The bytes this PC's network interface has received and sent so far, for
// the interface that reaches `consoleIp` (the one in its subnet): the whole
// link, not one program. `ok` false where the system does not say.
struct LinkCounters
{
	bool ok = false;
	std::string name;
	uint64_t received = 0;
	uint64_t sent = 0;
};
LinkCounters linkCountersForConsole(const std::string &consoleIp);

// TCP connection with a timeout: the basis of the service checks (§5.1).
// `banner`, if not null, receives the first line sent by the server (used to
// confirm FTP's "220").
bool tcpProbe(const std::string &host, uint16_t port, int timeoutMs, std::string *banner = nullptr);

// This PC's neighbour table (ARP): the hardware address each IPv4 address
// of its own networks last answered from, as 12 upper-case hex digits
// ("A24F107C3E91"). Only entries that did answer. Empty where the system
// does not say (macOS for now). An address only gets an entry after this PC
// tried to reach it — tcpProbe() to any port is enough.
std::map<std::string, std::string> neighbourMacs();
// The same, from the text of Linux's /proc/net/arp.
std::map<std::string, std::string> parseProcNetArp(const std::string &text);

// A free TCP port starting at `preferred` (§5.3: if taken, pick another).
bool findFreePort(const std::string &bindAddress, uint16_t preferred, uint16_t *outPort,
	int maxAttempts = 64);

} // namespace orbislink
