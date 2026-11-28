// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/net/link_rate.h"

namespace orbislink {

bool LinkRate::sample(uint64_t received, uint64_t sent, int64_t ms)
{
	if(!started_ || ms <= lastMs_ || received < lastReceived_ || sent < lastSent_)
	{
		// The first sample, a clock that did not move on, or totals that went
		// back: a new starting point, and nothing said until the next.
		const bool wentBack = started_ && (received < lastReceived_ || sent < lastSent_);
		if(!started_ || wentBack || ms < lastMs_)
		{
			started_ = true;
			known_ = false;
			lastReceived_ = received;
			lastSent_ = sent;
			lastMs_ = ms;
		}
		return known_;
	}
	const double seconds = static_cast<double>(ms - lastMs_) / 1000.0;
	download_ = static_cast<double>(received - lastReceived_) / seconds;
	upload_ = static_cast<double>(sent - lastSent_) / seconds;
	lastReceived_ = received;
	lastSent_ = sent;
	lastMs_ = ms;
	known_ = true;
	return true;
}

void LinkRate::reset()
{
	*this = LinkRate();
}

bool validStreamQuality(int resolution, int fps)
{
	const bool resolutionOk = resolution == 360 || resolution == 540 || resolution == 720 || resolution == 1080;
	return resolutionOk && (fps == 30 || fps == 60);
}

const char *latencyGrade(double ms)
{
	if(ms < 0)
		return "unknown";
	if(ms <= 20)
		return "good";
	if(ms <= 60)
		return "fair";
	return "poor";
}

} // namespace orbislink
