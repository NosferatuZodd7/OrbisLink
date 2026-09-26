// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/stream_types.h"

#include <cstdlib>

namespace orbislink {

const char *hostStateName(HostState state)
{
	switch(state)
	{
		case HostState::Ready: return "pronta";
		case HostState::Standby: return "em repouso";
		case HostState::Unknown: break;
	}
	return "desconhecido";
}

uint64_t StreamCredentials::wakeupCredential() const
{
	// O pacote de wakeup leva a chave de registo interpretada como um
	// número hexadecimal — é assim que o chiaki a envia
	// (chiaki_discovery_wakeup, campo user_credential).
	if(registKey.empty())
		return 0;
	return strtoull(registKey.c_str(), nullptr, 16);
}

} // namespace orbislink
