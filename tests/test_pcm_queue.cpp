// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The queue between the decoder and the sound card. What is tested here is
// the accounting: how much went in, how much came out, and how much was lost
// on the way. That is what the diagnostics use to say at which stretch the
// sound dies, and a wrong counter sends us looking for the fault in the wrong place.
#include "orbislink/qt/audio_output.h"
#include "test_support.h"

#include <vector>

using namespace orbislink;

namespace {

std::vector<char> block(int bytes, char value)
{
	return std::vector<char>(static_cast<size_t>(bytes), value);
}

} // namespace

ORBISLINK_TEST(queue_counts_what_goes_in_and_out)
{
	PcmQueue queue;
	CHECK(queue.open(QIODevice::ReadOnly));
	CHECK_EQ(queue.pushedBytes(), 0);
	CHECK_EQ(queue.pulledBytes(), 0);

	const std::vector<char> data = block(1000, 'a');
	queue.push(data.data(), static_cast<qint64>(data.size()));
	CHECK_EQ(queue.pushedBytes(), 1000);
	CHECK_EQ(queue.queuedBytes(), 1000);
	CHECK_EQ(queue.pulledBytes(), 0);

	std::vector<char> destination(400, 0);
	const qint64 parsed = queue.take(destination.data(), 400);
	CHECK_EQ(parsed, 400);
	CHECK_EQ(destination[0], 'a');
	CHECK_EQ(queue.pulledBytes(), 400);
	CHECK_EQ(queue.queuedBytes(), 600);
}

ORBISLINK_TEST(take_does_not_invent_bytes)
{
	PcmQueue queue;
	CHECK(queue.open(QIODevice::ReadOnly));
	const std::vector<char> data = block(64, 'b');
	queue.push(data.data(), 64);

	std::vector<char> destination(500, 0x7f);
	// Unlike the mode where the card pulls, here the rest is not padded with
	// silence: the application does the writing, and writing silence that
	// does not exist only fills the card's buffer with nothing.
	CHECK_EQ(queue.take(destination.data(), 500), 64);
	CHECK_EQ(destination[64], 0x7f);
	CHECK_EQ(queue.take(destination.data(), 500), 0);
}

ORBISLINK_TEST(read_fills_the_rest_with_silence)
{
	PcmQueue queue;
	CHECK(queue.open(QIODevice::ReadOnly));
	const std::vector<char> data = block(10, 'c');
	queue.push(data.data(), 10);

	// In the mode where the card pulls, returning less than it asked for puts
	// QAudioSink to sleep. So the rest is zero-filled.
	std::vector<char> destination(100, 0x5a);
	CHECK_EQ(queue.read(destination.data(), 100), 100);
	CHECK_EQ(destination[9], 'c');
	CHECK_EQ(destination[10], 0);
	CHECK_EQ(queue.pulledBytes(), 10);
}

ORBISLINK_TEST(queue_drops_the_oldest_when_full)
{
	PcmQueue queue;
	CHECK(queue.open(QIODevice::ReadOnly));
	queue.setLimit(100);

	const std::vector<char> stale = block(80, 'v');
	const std::vector<char> fresh = block(80, 'n');
	queue.push(stale.data(), 80);
	queue.push(fresh.data(), 80);

	// Delaying the sound is worse than making it skip: the most recent stays.
	CHECK_EQ(queue.queuedBytes(), 100);
	std::vector<char> destination(100, 0);
	CHECK_EQ(queue.take(destination.data(), 100), 100);
	CHECK_EQ(destination[0], 'v');
	CHECK_EQ(destination[99], 'n');
	// The input counter counts everything that arrived, even what was thrown
	// away: that is how you see the decoder was delivering.
	CHECK_EQ(queue.pushedBytes(), 160);
}

ORBISLINK_TEST(every_push_signals_that_something_arrived)
{
	PcmQueue queue;
	CHECK(queue.open(QIODevice::ReadOnly));
	int notices = 0;
	QObject::connect(&queue, &QIODevice::readyRead, &queue, [&notices]() { ++notices; });

	// Without this signal some Qt audio backends never pull a single
	// sample, and the stream stays silent with no error at all.
	const std::vector<char> data = block(16, 'd');
	queue.push(data.data(), 16);
	queue.push(data.data(), 16);
	CHECK_EQ(notices, 2);
}

ORBISLINK_TEST(clear_resets_the_counters)
{
	PcmQueue queue;
	CHECK(queue.open(QIODevice::ReadOnly));
	const std::vector<char> data = block(32, 'e');
	queue.push(data.data(), 32);
	std::vector<char> destination(32, 0);
	queue.take(destination.data(), 32);

	queue.clear();
	CHECK_EQ(queue.pushedBytes(), 0);
	CHECK_EQ(queue.pulledBytes(), 0);
	CHECK_EQ(queue.queuedBytes(), 0);
}

TEST_MAIN()
