// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/stream_types.h"

#include <cstdlib>

namespace orbislink {

const char *hostStateName(HostState state)
{
	switch(state)
	{
		case HostState::Ready: return "ready";
		case HostState::Standby: return "rest mode";
		case HostState::Unknown: break;
	}
	return "unknown";
}

uint64_t StreamCredentials::wakeupCredential() const
{
	// The wakeup packet carries the registration key read as a
	// hexadecimal number — that is how chiaki sends it
	// (chiaki_discovery_wakeup, user_credential field).
	if(registKey.empty())
		return 0;
	return strtoull(registKey.c_str(), nullptr, 16);
}

} // namespace orbislink
