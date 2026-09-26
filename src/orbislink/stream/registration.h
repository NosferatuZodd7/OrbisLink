// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/stream_types.h"

#include <functional>
#include <memory>
#include <string>

namespace orbislink {

// Registering the PC on the console (chiaki §5.2: "Add Device").
//
// On the console: Settings → Remote Play Connection Settings →
// Add Device. An 8-digit PIN appears. Here that PIN is needed, plus the
// PSN Account ID in base64 — the console only accepts the registration if
// both match.
//
// What comes out is the registration key and the rp_key, which are stored:
// from then on the connection needs no PIN.
class StreamRegistration
{
public:
	struct Request
	{
		std::string address;
		std::string accountIdBase64; // Account ID da PSN, 8 bytes em base64
		uint32_t pin = 0;            // the 8 digits the console shows
		int target = 0;              // ChiakiTarget, vindo da descoberta
		bool ps5 = false;
	};

	using Finished = std::function<void(bool ok, StreamCredentials credentials, std::string error)>;

	StreamRegistration();
	~StreamRegistration();

	// Public because chiaki's C callback needs to reach it.
	struct Impl;

	// Starts registration. `finished` is called on a chiaki thread.
	bool start(const Request &request, Finished finished, std::string *error = nullptr);
	void cancel();
	bool running() const;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace orbislink
