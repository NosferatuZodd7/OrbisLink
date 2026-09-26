// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/sfo_parser.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <sstream>

using namespace orbislink;
using namespace orbislink_test;

ORBISLINK_TEST(reads_text_entries)
{
	const auto data = buildSfo({ { "CATEGORY", "gp" }, { "TITLE", "Accented Game ção" },
		{ "TITLE_ID", "CUSA00123" } });
	Sfo sfo;
	std::string error;
	CHECK(sfo.parse(data, &error));
	CHECK(error.empty());
	CHECK_EQ(sfo.stringValue("TITLE"), std::string("Accented Game ção"));
	CHECK_EQ(sfo.stringValue("TITLE_ID"), std::string("CUSA00123"));
	CHECK_EQ(sfo.stringValue("CATEGORY"), std::string("gp"));
	CHECK_EQ(sfo.stringValue("MISSING", "default"), std::string("default"));
}

ORBISLINK_TEST(rejects_invalid_magic)
{
	auto data = buildSfo({ { "TITLE", "x" } });
	data[1] = 'X';
	Sfo sfo;
	std::string error;
	CHECK(!sfo.parse(data, &error));
	CHECK(!error.empty());
}

ORBISLINK_TEST(rejects_short_file)
{
	std::vector<uint8_t> data = { 0x00, 'P', 'S', 'F' };
	Sfo sfo;
	CHECK(!sfo.parse(data, nullptr));
}

ORBISLINK_TEST(ignores_out_of_bounds_entry)
{
	auto data = buildSfo({ { "TITLE", "Good" }, { "TITLE_ID", "CUSA00001" } });
	// Corrupts the second entry's value_offset to beyond the end of the file.
	putLE32(data, 0x14 + 0x10 + 0x0C, 0x7FFFFFFF);
	Sfo sfo;
	CHECK(sfo.parse(data, nullptr));
	CHECK_EQ(sfo.stringValue("TITLE"), std::string("Good"));
	CHECK(sfo.find("TITLE_ID") == nullptr);
}

ORBISLINK_TEST(implausible_entry_count)
{
	auto data = buildSfo({ { "TITLE", "x" } });
	putLE32(data, 0x10, 999999);
	Sfo sfo;
	std::string error;
	CHECK(!sfo.parse(data, &error));
}

TEST_MAIN()
