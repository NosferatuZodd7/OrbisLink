// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The Account ID is a 64-bit number written three ways. What these tests
// pin down is the byte order: the base64 is of the 8 bytes in
// little-endian, not the other way round. It is not our choice — it is in
// chiaki-ng's scripts/psn-account-id.py, which does
// base64.b64encode(user_id.to_bytes(8, "little")). Swapping it gives an ID
// the console refuses without saying why, and nobody would spot it by looking.
#include "orbislink/stream/account_id.h"
#include "test_support.h"

using namespace orbislink;

ORBISLINK_TEST(hexadecimal_gives_base64_with_bytes_reversed)
{
	// 0x0123456789ABCDEF in little-endian is EF CD AB 89 67 45 23 01, and those
	// eight bytes in base64 are "782riWdFIwE=". Checked by hand:
	//   >>> base64.b64encode((0x0123456789ABCDEF).to_bytes(8, "little"))
	const AccountId id = parseAccountId("0123456789ABCDEF");
	CHECK(id.valid);
	CHECK_EQ(id.format, std::string("hex"));
	CHECK_EQ(id.base64, std::string("782riWdFIwE="));
	CHECK_EQ(id.hex, std::string("0123456789ABCDEF"));
	CHECK_EQ(id.decimal, std::string("81985529216486895"));
}

ORBISLINK_TEST(base64_gives_back_the_same_hexadecimal)
{
	const AccountId id = parseAccountId("782riWdFIwE=");
	CHECK(id.valid);
	CHECK_EQ(id.format, std::string("base64"));
	CHECK_EQ(id.hex, std::string("0123456789ABCDEF"));
	CHECK_EQ(id.decimal, std::string("81985529216486895"));
}

ORBISLINK_TEST(decimal_and_hexadecimal_give_the_same)
{
	const AccountId byDecimal = parseAccountId("81985529216486895");
	const AccountId byHex = parseAccountId("0x0123456789ABCDEF");
	CHECK(byDecimal.valid);
	CHECK(byHex.valid);
	CHECK_EQ(byDecimal.base64, byHex.base64);
	CHECK_EQ(byDecimal.format, std::string("decimal"));
	CHECK_EQ(byHex.format, std::string("hex"));
}

ORBISLINK_TEST(prefix_0x_forces_hexadecimal_on_an_all_digit_id)
{
	// A 16-digit ID is ambiguous. Without a prefix it is read as decimal,
	// because that is the form PSN gives; with "0x" it is read as hexadecimal.
	// That is why the interface shows all three forms at once.
	const AccountId asDecimal = parseAccountId("1234567890123456");
	const AccountId asHex = parseAccountId("0x1234567890123456");
	CHECK(asDecimal.valid);
	CHECK(asHex.valid);
	CHECK_EQ(asDecimal.format, std::string("decimal"));
	CHECK_EQ(asHex.format, std::string("hex"));
	CHECK(asDecimal.base64 != asHex.base64);
	CHECK_EQ(asHex.decimal, std::string("1311768467284833366"));
}

ORBISLINK_TEST(spaces_and_separators_break_nothing)
{
	// Whoever copies this off a console screen brings spaces and colons.
	const AccountId withSpaces = parseAccountId("  01 23 45 67 89 AB CD EF  ");
	const AccountId withColons = parseAccountId("01:23:45:67:89:AB:CD:EF");
	CHECK(withSpaces.valid);
	CHECK(withColons.valid);
	CHECK_EQ(withSpaces.base64, std::string("782riWdFIwE="));
	CHECK_EQ(withColons.base64, std::string("782riWdFIwE="));
}

ORBISLINK_TEST(reversing_the_bytes_is_reversible)
{
	const AccountId id = parseAccountId("0123456789ABCDEF");
	const AccountId swapped = reverseAccountIdBytes(id);
	CHECK(swapped.valid);
	CHECK_EQ(swapped.hex, std::string("EFCDAB8967452301"));
	CHECK_EQ(reverseAccountIdBytes(swapped).hex, id.hex);
}

ORBISLINK_TEST(lowercase_works_too)
{
	const AccountId id = parseAccountId("0123456789abcdef");
	CHECK(id.valid);
	CHECK_EQ(id.hex, std::string("0123456789ABCDEF"));
}

ORBISLINK_TEST(what_is_not_an_account_id_is_refused_with_a_reason)
{
	const AccountId empty = parseAccountId("   ");
	CHECK(!empty.valid);
	CHECK(!empty.error.empty());

	const AccountId tooShort = parseAccountId("0123ABCD");
	CHECK(!tooShort.valid);
	CHECK(!tooShort.error.empty());

	const AccountId huge = parseAccountId("99999999999999999999999");
	CHECK(!huge.valid);
	CHECK(!huge.error.empty());

	const AccountId nonsense = parseAccountId("my-account-id");
	CHECK(!nonsense.valid);
	CHECK(!nonsense.error.empty());
}

ORBISLINK_TEST(zero_and_the_largest_value_do_not_crash)
{
	const AccountId zero = parseAccountId("0000000000000000");
	CHECK(zero.valid);
	CHECK_EQ(zero.decimal, std::string("0"));
	CHECK_EQ(zero.base64, std::string("AAAAAAAAAAA="));

	const AccountId maximum = parseAccountId("FFFFFFFFFFFFFFFF");
	CHECK(maximum.valid);
	CHECK_EQ(maximum.decimal, std::string("18446744073709551615"));
}

TEST_MAIN()
