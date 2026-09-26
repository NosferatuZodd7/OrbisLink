// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/chiaki_log_bridge.h"

#include "orbislink/common/log.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace orbislink {

namespace {

std::atomic<uint32_t> motivo { 0 };

void forward(ChiakiLogLevel level, const char *message, void *)
{
	static const char prefixo[] = "Reported Application Reason: ";
	if(std::strncmp(message, prefixo, sizeof(prefixo) - 1) == 0)
		motivo.store(static_cast<uint32_t>(std::strtoul(message + sizeof(prefixo) - 1, nullptr, 16)));
	switch(level)
	{
		case CHIAKI_LOG_DEBUG:
		case CHIAKI_LOG_VERBOSE:
			logDebug(std::string("chiaki: ") + message);
			break;
		case CHIAKI_LOG_INFO:
			logInfo(std::string("chiaki: ") + message);
			break;
		case CHIAKI_LOG_WARNING:
			logWarning(std::string("chiaki: ") + message);
			break;
		case CHIAKI_LOG_ERROR:
			logError(std::string("chiaki: ") + message);
			break;
	}
}

ChiakiLog &instance()
{
	static ChiakiLog log;
	static std::once_flag once;
	std::call_once(once, []() {
		chiaki_log_init(&log, CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR, forward,
			nullptr);
	});
	return log;
}

} // namespace

ChiakiLog *chiakiLog() { return &instance(); }

void setChiakiVerbose(bool verbose)
{
	instance().level_mask = verbose
		? CHIAKI_LOG_ALL
		: (CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR);
}

uint32_t takeApplicationReason() { return motivo.exchange(0); }

} // namespace orbislink
