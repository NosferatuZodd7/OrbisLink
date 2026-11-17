// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Following a console the router gave another address: by its host-id and
// hardware address only, never by a guess.
#include "orbislink/net/console_locator.h"
#include "orbislink/net/net_utils.h"
#include "test_support.h"

using namespace orbislink;

ORBISLINK_TEST(hardware_ids_compare_in_any_form)
{
	CHECK_EQ(normaliseHardwareId("a2:4f:10:7c:3e:91"), std::string("A24F107C3E91"));
	CHECK_EQ(normaliseHardwareId("A2-4F-10-7C-3E-91"), std::string("A24F107C3E91"));
	CHECK_EQ(normaliseHardwareId("a24f107c3e91"), std::string("A24F107C3E91"));
	CHECK_EQ(normaliseHardwareId("00:00:00:00:00:00"), std::string());
	CHECK_EQ(normaliseHardwareId("A24F107C3E"), std::string());
	CHECK_EQ(normaliseHardwareId("not a mac!!!"), std::string());
}

ORBISLINK_TEST(the_neighbour_table_is_read_from_linux)
{
	const std::string text =
		"IP address       HW type     Flags       HW address            Mask     Device\n"
		"10.0.0.70        0x1         0x2         aa:bb:cc:00:11:22     *        eth0\n"
		"10.0.0.71        0x1         0x0         00:00:00:00:00:00     *        eth0\n"
		"10.0.0.1         0x1         0x6         de:ad:be:ef:00:01     *        eth0\n";
	const auto table = parseProcNetArp(text);
	CHECK_EQ(table.size(), static_cast<size_t>(2));
	CHECK_EQ(table.at("10.0.0.70"), std::string("AABBCC001122"));
	CHECK_EQ(table.at("10.0.0.1"), std::string("DEADBEEF0001"));
	CHECK(table.find("10.0.0.71") == table.end());
}

ORBISLINK_TEST(a_console_is_present_taken_or_silent_at_its_address)
{
	SoughtConsole console { "10.0.0.66", { "AA:BB:CC:00:11:22" } };
	CHECK(checkAddress(console, { { "10.0.0.66", "AABBCC001122" } }) == AddressCheck::Present);
	CHECK(checkAddress(console, { { "10.0.0.66", "112233445566" } }) == AddressCheck::Taken);
	CHECK(checkAddress(console, { { "10.0.0.70", "AABBCC001122" } }) == AddressCheck::Silent);
}

ORBISLINK_TEST(a_console_found_elsewhere_by_its_host_id_has_moved)
{
	// The PS5 was at .66; Remote Play's search hears it at .70.
	const std::vector<SoughtConsole> sought { { "10.0.0.66", { "A24F107C3E91" } } };
	const auto moved = locateMoved(sought, { { "10.0.0.70", "a24f107c3e91" } }, {});
	CHECK_EQ(moved.size(), static_cast<size_t>(1));
	CHECK_EQ(moved.at("10.0.0.66"), std::string("10.0.0.70"));
}

ORBISLINK_TEST(a_console_that_never_answers_the_search_is_found_by_its_mac)
{
	// A PS4 that does not answer discovery: only the neighbour table knows it.
	const std::vector<SoughtConsole> sought { { "10.0.0.90", { "", "02:00:5E:10:00:01" } } };
	const auto moved = locateMoved(sought, {}, { { "10.0.0.95", "02005E100001" }, { "10.0.0.1", "DEADBEEF0001" } });
	CHECK_EQ(moved.at("10.0.0.90"), std::string("10.0.0.95"));
}

ORBISLINK_TEST(nothing_moves_on_a_guess)
{
	// Still at its address (only asleep or slow): stays.
	CHECK(locateMoved({ { "10.0.0.66", { "AABBCC001122" } } }, { { "10.0.0.66", "AABBCC001122" } }, {}).empty());
	// Seen at two other addresses at once: no way to know which.
	CHECK(locateMoved({ { "10.0.0.66", { "AABBCC001122" } } },
		{ { "10.0.0.70", "AABBCC001122" } }, { { "10.0.0.71", "AABBCC001122" } }).empty());
	// Nobody with its ids: stays where it is.
	CHECK(locateMoved({ { "10.0.0.66", { "AABBCC001122" } } }, { { "10.0.0.70", "112233445566" } }, {}).empty());
	// No ids at all: never moved.
	CHECK(locateMoved({ { "10.0.0.66", {} } }, { { "10.0.0.70", "AABBCC001122" } }, {}).empty());
	// Two saved consoles claiming the same new address: neither moves.
	const std::vector<SoughtConsole> twins { { "10.0.0.66", { "AABBCC001122" } },
		{ "10.0.0.67", { "AABBCC001122" } } };
	CHECK(locateMoved(twins, { { "10.0.0.70", "AABBCC001122" } }, {}).empty());
}

TEST_MAIN()
