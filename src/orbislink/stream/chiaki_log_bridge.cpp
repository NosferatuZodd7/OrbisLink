// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/chiaki_log_bridge.h"

#include "orbislink/common/log.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace orbislink {

namespace {

std::atomic<uint32_t> reason { 0 };

void write(ChiakiLogLevel level, const std::string &line)
{
	switch(level)
	{
		case CHIAKI_LOG_DEBUG:
		case CHIAKI_LOG_VERBOSE:
			logDebug(line);
			break;
		case CHIAKI_LOG_INFO:
			logInfo(line);
			break;
		case CHIAKI_LOG_WARNING:
			logWarning(line);
			break;
		case CHIAKI_LOG_ERROR:
			logError(line);
			break;
	}
}

// A bad moment on the network makes chiaki say the same thing hundreds of
// times a second ("Takion Send Buffer overflow", a frame number after
// another): a few of each are kept, then how many more there were, so the
// log — and the diagnostics' last lines — still say what else happened.
// Messages count as the same with their numbers left out.
struct Burst
{
	std::chrono::steady_clock::time_point start;
	int shown = 0;
	int skipped = 0;
	ChiakiLogLevel level = CHIAKI_LOG_INFO;
	std::string example;
};

std::string shapeOf(const char *message)
{
	std::string shape;
	for(const char *p = message; *p; ++p)
	{
		if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && std::isxdigit(static_cast<unsigned char>(p[2])))
		{
			p += 2;
			while(std::isxdigit(static_cast<unsigned char>(p[1])))
				++p;
			shape += '#';
		}
		else if(std::isdigit(static_cast<unsigned char>(*p)))
		{
			while(std::isdigit(static_cast<unsigned char>(p[1])))
				++p;
			shape += '#';
		}
		else
			shape += *p;
	}
	return shape;
}

void forward(ChiakiLogLevel level, const char *message, void *)
{
	static const char prefix[] = "Reported Application Reason: ";
	if(std::strncmp(message, prefix, sizeof(prefix) - 1) == 0)
		reason.store(static_cast<uint32_t>(std::strtoul(message + sizeof(prefix) - 1, nullptr, 16)));

	constexpr int kShown = 3;
	constexpr auto kWindow = std::chrono::seconds(5);
	static std::mutex mutex;
	static std::map<std::string, Burst> bursts;
	const auto now = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> lock(mutex);
	// The bursts that ended: how many were left out.
	for(auto it = bursts.begin(); it != bursts.end();)
	{
		if(now - it->second.start < kWindow)
		{
			++it;
			continue;
		}
		if(it->second.skipped > 0)
			write(it->second.level, "chiaki: " + it->second.example + " (and " + std::to_string(it->second.skipped)
				+ " more like it in " + std::to_string(kWindow.count()) + " s)");
		it = bursts.erase(it);
	}
	Burst &burst = bursts[shapeOf(message)];
	if(burst.shown == 0 && burst.skipped == 0)
		burst.start = now;
	if(burst.shown < kShown)
	{
		++burst.shown;
		write(level, std::string("chiaki: ") + message);
		return;
	}
	++burst.skipped;
	burst.level = level;
	burst.example = message;
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

uint32_t takeApplicationReason() { return reason.exchange(0); }

} // namespace orbislink
