// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/pcm_convert.h"

#include <algorithm>
#include <cmath>

namespace orbislink {

namespace {

// Reads one channel of a source sample, handling mono and stereo.
inline int16_t sampleAt(const int16_t *pcm, size_t frame, int channels, int channel)
{
	if(channels <= 1)
		return pcm[frame];
	return pcm[frame * static_cast<size_t>(channels) + static_cast<size_t>(channel)];
}

inline int16_t clamp16(double value)
{
	if(value > 32767.0)
		return 32767;
	if(value < -32768.0)
		return -32768;
	return static_cast<int16_t>(std::lround(value));
}

} // namespace

void PcmConverter::configure(int sourceRate, int sourceChannels, int targetRate,
	int targetChannels)
{
	sourceRate_ = sourceRate > 0 ? sourceRate : 48000;
	sourceChannels_ = sourceChannels > 0 ? sourceChannels : 2;
	targetRate_ = targetRate > 0 ? targetRate : sourceRate_;
	targetChannels_ = targetChannels > 0 ? targetChannels : sourceChannels_;
	needed_ = sourceRate_ != targetRate_ || sourceChannels_ != targetChannels_;
	position_ = 0.0;
	out_.clear();
}

size_t PcmConverter::outputSamples(size_t inputSamples) const
{
	if(!needed_ || sourceRate_ == targetRate_)
		return inputSamples;
	return static_cast<size_t>(static_cast<double>(inputSamples) * targetRate_ / sourceRate_);
}

const std::vector<int16_t> &PcmConverter::convert(const int16_t *pcm, size_t samples)
{
	out_.clear();
	if(!pcm || samples == 0)
		return out_;

	const double step = static_cast<double>(sourceRate_) / static_cast<double>(targetRate_);
	// While there is a pair of source samples to interpolate. The frame's
	// last sample is carried to the next one, through position_.
	out_.reserve(outputSamples(samples) * static_cast<size_t>(targetChannels_) + 8);

	while(position_ < static_cast<double>(samples) - 1.0)
	{
		const size_t idx = static_cast<size_t>(position_);
		const double fraction = position_ - static_cast<double>(idx);

		for(int channel = 0; channel < targetChannels_; ++channel)
		{
			// More channels out than in: the last one is repeated
			// (mono to stereo gives the same sound on both sides).
			const int sourceChannel = std::min(channel, sourceChannels_ - 1);
			const double a = sampleAt(pcm, idx, sourceChannels_, sourceChannel);
			const double b = sampleAt(pcm, idx + 1, sourceChannels_, sourceChannel);
			double value = a + (b - a) * fraction;

			// Stereo to mono: add and divide, otherwise half of the
			// sound would be lost.
			if(targetChannels_ == 1 && sourceChannels_ > 1)
			{
				double total = 0.0;
				for(int c = 0; c < sourceChannels_; ++c)
				{
					const double x = sampleAt(pcm, idx, sourceChannels_, c);
					const double y = sampleAt(pcm, idx + 1, sourceChannels_, c);
					total += x + (y - x) * fraction;
				}
				value = total / sourceChannels_;
			}
			out_.push_back(clamp16(value));
		}
		position_ += step;
	}

	// What is left of this frame counts towards the next one.
	position_ -= static_cast<double>(samples);
	if(position_ < 0.0)
		position_ = 0.0;
	return out_;
}

} // namespace orbislink
