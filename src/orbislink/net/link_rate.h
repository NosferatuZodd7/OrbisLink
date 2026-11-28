// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace orbislink {

// Turns a network interface's running totals (bytes received and sent, as
// LinkCounters gives them) into rates, one sample at a time. The first
// sample only sets the starting point; a total that goes backwards (the
// interface was reset, or another one answered) starts again instead of
// giving a negative or huge rate.
class LinkRate
{
public:
	// `ms` is a steady time in milliseconds. Returns true once a rate is
	// known (from the second sample on).
	bool sample(uint64_t received, uint64_t sent, int64_t ms);
	void reset();

	bool known() const { return known_; }
	// Bytes per second; -1 while not known.
	double download() const { return known_ ? download_ : -1; }
	double upload() const { return known_ ? upload_ : -1; }

private:
	bool started_ = false;
	bool known_ = false;
	uint64_t lastReceived_ = 0;
	uint64_t lastSent_ = 0;
	int64_t lastMs_ = 0;
	double download_ = 0;
	double upload_ = 0;
};

// The resolutions and frame rates Remote Play asks for (chiaki's presets).
bool validStreamQuality(int resolution, int fps);

// How the answer time reads on the stream bar: "good" up to 20 ms, "fair"
// up to 60 ms, "poor" above, "unknown" when not measured (negative).
const char *latencyGrade(double ms);

} // namespace orbislink
