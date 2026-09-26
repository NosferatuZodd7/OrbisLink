// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orbislink {

// Converts interleaved 16-bit PCM between formats.
//
// It exists because the console does not negotiate: it always sends 48 kHz
// stereo. The PC's sound card may not accept exactly that — many run at
// 44100, others are mono — and without conversion everything would be silent.
//
// Resampling is linear interpolation. It is not a studio resampler, but for a
// game's voice and effects the difference is inaudible, and the cost is
// negligible next to decoding video.
class PcmConverter
{
public:
	void configure(int sourceRate, int sourceChannels, int targetRate, int targetChannels);

	bool needed() const { return needed_; }
	int targetRate() const { return targetRate_; }
	int targetChannels() const { return targetChannels_; }

	// Returns the converted samples, interleaved. `samples` is the number of
	// samples per channel coming in.
	const std::vector<int16_t> &convert(const int16_t *pcm, size_t samples);

	// How many samples per channel come out for a given number going in.
	size_t outputSamples(size_t inputSamples) const;

private:
	int sourceRate_ = 48000;
	int sourceChannels_ = 2;
	int targetRate_ = 48000;
	int targetChannels_ = 2;
	bool needed_ = false;
	// The phase is kept between calls: without it there would be a click at
	// every frame boundary.
	double position_ = 0.0;
	std::vector<int16_t> out_;
};

} // namespace orbislink
