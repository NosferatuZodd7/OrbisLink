// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/stream_types.h"

#include <string>
#include <vector>

namespace orbislink {

// Console discovery, on top of chiaki-lib.
//
// Two uses: asking a specific address (the normal case, the user already
// knows the console's IP) and sweeping the local network for consoles
// (for the first-run wizard).
class StreamDiscovery
{
public:
	// Asks `address` directly. Returns found=false if nobody answers
	// within `timeoutMs`.
	//
	// `ps4Port` exists only so tests can put a fake console on a high port:
	// 987 is privileged and CI does not run as root. In normal use it stays
	// 0, which means the protocol's port.
	static HostInfo probe(const std::string &address, int timeoutMs = 2000, uint16_t ps4Port = 0);

	// The same as probe(), but without an entry in the attempt trace or the
	// log: this is what the periodic console check uses, which runs every
	// few seconds, also in the middle of a session.
	static HostInfo peek(const std::string &address, int timeoutMs = 2000, uint16_t ps4Port = 0);

	// Varre a rede local. Devolve todas as consolas que responderem.
	static std::vector<HostInfo> scan(int timeoutMs = 3000);

	// Acorda uma consola em repouso. Precisa da credencial que vem do
	// registo; sem ela a consola ignora o pacote.
	static bool wakeup(const std::string &address, uint64_t credential, bool ps5,
		std::string *error = nullptr);
};

} // namespace orbislink
