// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {

// Sends a payload to a jailbroken console's loader port (the PS5's ELF loader
// on 9021, GoldHEN's BinLoader on 9090…) and, while the loader keeps the
// connection open, reads what the payload prints: the PS5 ELF loader hands
// the payload's stdout and stderr to that same connection.
class PayloadSender
{
public:
	struct Options
	{
		int connectTimeoutMs = 3000;
		// How long to keep reading after the payload went out (0: not at all).
		int listenMs = 0;
		// Each line the payload prints, as it arrives. Returning false stops
		// listening (the connection closes).
		std::function<bool(const std::string &line)> onLine;
		// Set from another thread to stop waiting.
		const std::atomic<bool> *cancel = nullptr;
	};

	struct Result
	{
		bool sent = false;
		// The console closed the connection (the payload ended) before the
		// listening time was up.
		bool closedByConsole = false;
		std::string error;
		// Everything read back, as it came.
		std::string output;
	};

	static Result send(const std::string &host, uint16_t port, const std::vector<uint8_t> &payload,
		const Options &options);
};

} // namespace orbislink
