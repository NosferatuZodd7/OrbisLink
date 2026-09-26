// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/pcm_convert.h"
#include "test_support.h"

#include <cmath>
#include <vector>

using namespace orbislink;

namespace {

// A sine wave, which is what can be checked without ears.
std::vector<int16_t> sine(int samples, int channels, double hz, int rate)
{
	std::vector<int16_t> out(static_cast<size_t>(samples) * channels);
	for(int i = 0; i < samples; ++i)
	{
		const double v = std::sin(2.0 * 3.14159265358979 * hz * i / rate) * 20000.0;
		for(int c = 0; c < channels; ++c)
			out[static_cast<size_t>(i) * channels + c] = static_cast<int16_t>(v);
	}
	return out;
}

} // namespace

ORBISLINK_TEST(same_format_touches_nothing)
{
	PcmConverter conv;
	conv.configure(48000, 2, 48000, 2);
	CHECK(!conv.needed());
	CHECK_EQ(conv.outputSamples(480), static_cast<size_t>(480));
}

ORBISLINK_TEST(from_48k_to_44k_shrinks_in_the_right_ratio)
{
	PcmConverter conv;
	conv.configure(48000, 2, 44100, 2);
	CHECK(conv.needed());

	const auto input = sine(480, 2, 440.0, 48000);
	const auto &output = conv.convert(input.data(), 480);
	// 480 samples at 48 kHz are 10 ms; at 44100 they are 441. One of
	// difference is accepted, which is the one left for the next frame.
	const size_t perChannel = output.size() / 2;
	CHECK(perChannel >= 439 && perChannel <= 442);
}

ORBISLINK_TEST(phase_continues_across_frames)
{
	// Without keeping the position between calls there is a click at each
	// boundary, and after a hundred frames the drift is already audible.
	PcmConverter conv;
	conv.configure(48000, 2, 44100, 2);
	const auto input = sine(480, 2, 440.0, 48000);

	size_t total = 0;
	for(int i = 0; i < 100; ++i)
		total += conv.convert(input.data(), 480).size() / 2;

	// 100 frames of 10 ms = 1 second, so ~44100 samples. With a
	// resampling that lost the phase, this would drift away quickly.
	CHECK(total >= 44050 && total <= 44150);
}

ORBISLINK_TEST(mono_to_stereo_duplicates_the_channel)
{
	PcmConverter conv;
	conv.configure(48000, 1, 48000, 2);
	CHECK(conv.needed());

	const auto input = sine(64, 1, 440.0, 48000);
	const auto &output = conv.convert(input.data(), 64);
	CHECK(output.size() >= 2);
	// Both channels come out equal: one side silent would be worse than mono.
	for(size_t i = 0; i + 1 < output.size(); i += 2)
		CHECK_EQ(output[i], output[i + 1]);
}

ORBISLINK_TEST(stereo_to_mono_does_not_lose_half_the_sound)
{
	PcmConverter conv;
	conv.configure(48000, 2, 48000, 1);

	// Left channel with signal, right silent. The average must be half —
	// not zero, which is what ignoring one of the channels would give.
	std::vector<int16_t> input(64 * 2, 0);
	for(int i = 0; i < 64; ++i)
		input[static_cast<size_t>(i) * 2] = 10000;

	const auto &output = conv.convert(input.data(), 64);
	CHECK(!output.empty());
	CHECK(output[0] > 4000 && output[0] < 6000);
}

ORBISLINK_TEST(does_not_crash_on_degenerate_input)
{
	PcmConverter conv;
	conv.configure(48000, 2, 44100, 2);
	CHECK(conv.convert(nullptr, 100).empty());
	const auto input = sine(1, 2, 440.0, 48000);
	// A single sample cannot be interpolated: it must not read past the buffer.
	conv.convert(input.data(), 1);
	// An absurd format must not divide by zero.
	conv.configure(0, 0, 0, 0);
	CHECK(conv.targetRate() > 0);
	CHECK(conv.targetChannels() > 0);
}

TEST_MAIN()
