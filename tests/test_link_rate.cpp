// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The stream bar's connection readout: rates from an interface's running
// totals, the Remote Play qualities it may ask for, and the ping's grade.
#include "orbislink/net/link_rate.h"
#include "test_support.h"

#include <string>

using namespace orbislink;

ORBISLINK_TEST(nothing_is_known_from_one_sample)
{
	LinkRate rate;
	CHECK(!rate.sample(1000, 500, 10000));
	CHECK(!rate.known());
	CHECK(rate.download() < 0);
	CHECK(rate.upload() < 0);
}

ORBISLINK_TEST(two_samples_give_bytes_per_second)
{
	LinkRate rate;
	rate.sample(1000, 500, 10000);
	// 2 MB in and 40 KB out over two seconds.
	CHECK(rate.sample(1000 + 2000000, 500 + 40000, 12000));
	CHECK(rate.known());
	CHECK(rate.download() > 999999.0 && rate.download() < 1000001.0);
	CHECK(rate.upload() > 19999.0 && rate.upload() < 20001.0);
	// Nothing more moved in the next second.
	CHECK(rate.sample(1000 + 2000000, 500 + 40000, 13000));
	CHECK(rate.download() == 0.0);
	CHECK(rate.upload() == 0.0);
}

ORBISLINK_TEST(totals_going_back_start_again)
{
	// The interface was reset (or another one answered): no negative or huge
	// rate, nothing until the next sample.
	LinkRate rate;
	rate.sample(5000000, 5000000, 1000);
	rate.sample(6000000, 5100000, 2000);
	CHECK(rate.known());
	CHECK(!rate.sample(100, 100, 3000));
	CHECK(!rate.known());
	CHECK(rate.download() < 0);
	CHECK(rate.sample(100 + 1024, 100 + 512, 4000));
	CHECK(rate.download() > 1023.0 && rate.download() < 1025.0);
	CHECK(rate.upload() > 511.0 && rate.upload() < 513.0);
}

ORBISLINK_TEST(a_clock_that_does_not_move_divides_by_nothing)
{
	LinkRate rate;
	rate.sample(0, 0, 5000);
	// The same instant twice: no rate (no division by zero), the start kept.
	CHECK(!rate.sample(1000, 1000, 5000));
	CHECK(rate.sample(2000, 2000, 6000));
	CHECK(rate.download() > 1999.0 && rate.download() < 2001.0);
	// A clock going back starts again.
	CHECK(!rate.sample(3000, 3000, 1000));
	CHECK(!rate.known());
}

ORBISLINK_TEST(reset_forgets_everything)
{
	LinkRate rate;
	rate.sample(0, 0, 0);
	rate.sample(1000, 1000, 1000);
	CHECK(rate.known());
	rate.reset();
	CHECK(!rate.known());
	CHECK(!rate.sample(5000, 5000, 2000));
}

ORBISLINK_TEST(big_totals_do_not_overflow)
{
	// Counters of a long-running PC (tens of terabytes).
	LinkRate rate;
	const uint64_t start = 40000000000000ull;
	rate.sample(start, start, 100000);
	CHECK(rate.sample(start + 3000000, start + 1000, 101000));
	CHECK(rate.download() > 2999999.0 && rate.download() < 3000001.0);
}

ORBISLINK_TEST(only_chiaki_s_presets_are_asked_for)
{
	for(int resolution : { 360, 540, 720, 1080 })
		for(int fps : { 30, 60 })
			CHECK(validStreamQuality(resolution, fps));
	CHECK(!validStreamQuality(480, 60));
	CHECK(!validStreamQuality(1440, 60));
	CHECK(!validStreamQuality(2160, 60));
	CHECK(!validStreamQuality(720, 120));
	CHECK(!validStreamQuality(720, 0));
	CHECK(!validStreamQuality(0, 60));
	CHECK(!validStreamQuality(-720, 60));
}

ORBISLINK_TEST(the_ping_reads_as_a_word)
{
	CHECK_EQ(std::string(latencyGrade(-1)), std::string("unknown"));
	CHECK_EQ(std::string(latencyGrade(0)), std::string("good"));
	CHECK_EQ(std::string(latencyGrade(4.3)), std::string("good"));
	CHECK_EQ(std::string(latencyGrade(20)), std::string("good"));
	CHECK_EQ(std::string(latencyGrade(20.5)), std::string("fair"));
	CHECK_EQ(std::string(latencyGrade(60)), std::string("fair"));
	CHECK_EQ(std::string(latencyGrade(61)), std::string("poor"));
	CHECK_EQ(std::string(latencyGrade(900)), std::string("poor"));
}

TEST_MAIN()
