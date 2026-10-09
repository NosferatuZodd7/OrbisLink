// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The Remote Play credentials are what authenticates this PC on the console.
// If they are stored or read wrongly, the symptom on the console is "session
// refused" with no further explanation — so they are worth testing here.

#include "orbislink/stream/credentials.h"
#include "test_support.h"

#include <cstdio>
#include <string>

using namespace orbislink;

namespace {

std::string tempFile()
{
	static int counter = 0;
	return std::string(".orbislink-test-credentials-") + std::to_string(++counter) + ".json";
}

StreamCredentials sample(const std::string &hostId, const std::string &name)
{
	StreamCredentials c;
	c.valid = true;
	c.nickname = name;
	c.hostId = hostId;
	c.registKey = "1a2b3c4d";
	c.rpKeyHex = "000102030405060708090A0B0C0D0E0F";
	c.rpKeyType = 2;
	c.target = 1000;
	c.ps5 = false;
	return c;
}

} // namespace

ORBISLINK_TEST(hexadecimal_round_trip)
{
	const unsigned char original[4] = { 0x00, 0x7F, 0x80, 0xFF };
	const std::string hex = bytesToHex(original, sizeof(original));
	CHECK_EQ(hex, std::string("007F80FF"));

	unsigned char roundTrip[4] {};
	CHECK(hexToBytes(hex, roundTrip, sizeof(roundTrip)));
	for(size_t i = 0; i < sizeof(original); ++i)
		CHECK_EQ(int(roundTrip[i]), int(original[i]));
}

ORBISLINK_TEST(hexadecimal_refuses_what_is_not_hexadecimal)
{
	unsigned char destination[4] {};
	// Comprimento errado.
	CHECK(!hexToBytes("00FF", destination, sizeof(destination)));
	// Characters that are not hexadecimal digits.
	CHECK(!hexToBytes("00ZZ80FF", destination, sizeof(destination)));
}

ORBISLINK_TEST(account_id_must_be_base64_of_eight_bytes)
{
	unsigned char id[8] {};
	std::string err;

	// Eight bytes in base64.
	CHECK(decodeAccountId("AQIDBAUGBwg=", id, &err));
	CHECK(err.empty());
	CHECK_EQ(int(id[0]), 1);
	CHECK_EQ(int(id[7]), 8);

	// Empty, text that is not base64, and base64 of the wrong size: all three
	// must be refused with an explanation.
	for(const char *bad : { "", "this is not base64", "AQID" })
	{
		err.clear();
		CHECK(!decodeAccountId(bad, id, &err));
		CHECK(!err.empty());
	}
}

ORBISLINK_TEST(saves_and_reads_a_console)
{
	const std::string path = tempFile();
	CredentialStore store(path);

	CHECK(store.save(sample("AABBCCDDEEFF", "Living room PS4")));

	const StreamCredentials loaded = store.load("AABBCCDDEEFF");
	CHECK(loaded.valid);
	CHECK_EQ(loaded.nickname, std::string("Living room PS4"));
	CHECK_EQ(loaded.registKey, std::string("1a2b3c4d"));
	CHECK_EQ(loaded.rpKeyHex, std::string("000102030405060708090A0B0C0D0E0F"));
	CHECK_EQ(int(loaded.rpKeyType), 2);
	CHECK_EQ(loaded.target, 1000);

	std::remove(path.c_str());
}

ORBISLINK_TEST(a_ps5_stored_without_the_flag_stays_a_ps5)
{
	const std::string path = tempFile();
	CredentialStore store(path);

	// What was stored after registering a PS5: a PS5 target, but "ps5"
	// false. Uncorrected, the session spoke the PS4 protocol.
	StreamCredentials ps5 = sample("0A1B2C3D4E5F", "Bedroom PS5");
	ps5.target = 1000100;
	ps5.ps5 = false;
	CHECK(store.save(ps5));

	CHECK(store.load("0A1B2C3D4E5F").ps5);
	CHECK(!store.load("0A1B2C3D4E5F").nickname.empty());
	CHECK(store.save(sample("AABBCCDDEEFF", "Living room PS4")));
	CHECK(!store.load("AABBCCDDEEFF").ps5);

	std::remove(path.c_str());
}

ORBISLINK_TEST(saves_several_consoles_without_mixing_them_up)
{
	const std::string path = tempFile();
	CredentialStore store(path);

	CHECK(store.save(sample("AABBCCDDEEFF", "Living room")));
	StreamCredentials second = sample("112233445566", "Bedroom");
	second.registKey = "ffffffff";
	CHECK(store.save(second));

	CHECK_EQ(store.all().size(), size_t(2));
	CHECK_EQ(store.load("AABBCCDDEEFF").registKey, std::string("1a2b3c4d"));
	CHECK_EQ(store.load("112233445566").registKey, std::string("ffffffff"));
	// An unknown console does not return someone else's.
	CHECK(!store.load("999999999999").valid);

	std::remove(path.c_str());
}

ORBISLINK_TEST(registering_again_replaces_instead_of_duplicating)
{
	const std::string path = tempFile();
	CredentialStore store(path);

	CHECK(store.save(sample("AABBCCDDEEFF", "Old name")));
	StreamCredentials fresh = sample("AABBCCDDEEFF", "New name");
	fresh.registKey = "deadbeef";
	CHECK(store.save(fresh));

	CHECK_EQ(store.all().size(), size_t(1));
	CHECK_EQ(store.load("AABBCCDDEEFF").registKey, std::string("deadbeef"));
	CHECK_EQ(store.load("AABBCCDDEEFF").nickname, std::string("New name"));

	std::remove(path.c_str());
}

ORBISLINK_TEST(forget_deletes_only_the_requested_console)
{
	const std::string path = tempFile();
	CredentialStore store(path);
	store.save(sample("AABBCCDDEEFF", "Living room"));
	store.save(sample("112233445566", "Bedroom"));

	CHECK(store.forget("AABBCCDDEEFF"));
	CHECK(!store.load("AABBCCDDEEFF").valid);
	CHECK(store.load("112233445566").valid);
	// Forgetting what is no longer there is not a silent error: it returns false.
	CHECK(!store.forget("AABBCCDDEEFF"));

	std::remove(path.c_str());
}

// Two PSN accounts on one console: each has its own registration, and
// asking for one never hands back the other's.
ORBISLINK_TEST(each_account_keeps_its_own_registration_on_a_console)
{
	const std::string path = tempFile();
	CredentialStore store(path);
	StreamCredentials first = sample("AABBCCDDEEFF", "Living room");
	first.accountId = "AQIDBAUGBwg=";
	StreamCredentials second = sample("AABBCCDDEEFF", "Living room");
	second.accountId = "CAcGBQQDAgE=";
	second.registKey = "5e6f7a8b";
	CHECK(store.save(first));
	CHECK(store.save(second));

	CHECK_EQ(store.all().size(), size_t(2));
	CHECK_EQ(store.forHost("aabbccddeeff").size(), size_t(2));
	CHECK_EQ(store.load("AABBCCDDEEFF", "AQIDBAUGBwg=").registKey, std::string("1a2b3c4d"));
	CHECK_EQ(store.load("AABBCCDDEEFF", "CAcGBQQDAgE=").registKey, std::string("5e6f7a8b"));
	CHECK_EQ(store.load("AABBCCDDEEFF", "CAcGBQQDAgE=").accountId, std::string("CAcGBQQDAgE="));
	// An account that never registered there gets nothing, not another's.
	CHECK(!store.load("AABBCCDDEEFF", "AAAAAAAAAAA=").valid);

	// Registering the second account again replaces only its own.
	second.registKey = "99999999";
	CHECK(store.save(second));
	CHECK_EQ(store.all().size(), size_t(2));
	CHECK_EQ(store.load("AABBCCDDEEFF", "AQIDBAUGBwg=").registKey, std::string("1a2b3c4d"));
	CHECK_EQ(store.load("AABBCCDDEEFF", "CAcGBQQDAgE=").registKey, std::string("99999999"));

	// Forgetting one account leaves the other; forgetting the console, all.
	CHECK(store.forget("AABBCCDDEEFF", "AQIDBAUGBwg="));
	CHECK(!store.load("AABBCCDDEEFF", "AQIDBAUGBwg=").valid);
	CHECK(store.load("AABBCCDDEEFF", "CAcGBQQDAgE=").valid);
	CHECK(store.forget("AABBCCDDEEFF"));
	CHECK(store.all().empty());

	std::remove(path.c_str());
}

// A registration stored before accounts were told apart goes to the
// account it was made with, once, and stays readable as before meanwhile.
ORBISLINK_TEST(an_older_registration_is_adopted_by_its_account)
{
	const std::string path = tempFile();
	CredentialStore store(path);
	CHECK(store.save(sample("AABBCCDDEEFF", "Living room")));

	CHECK(store.load("AABBCCDDEEFF").valid);
	CHECK(!store.load("AABBCCDDEEFF", "AQIDBAUGBwg=").valid);

	CHECK(store.adopt("AABBCCDDEEFF", "AQIDBAUGBwg="));
	CHECK(store.load("AABBCCDDEEFF", "AQIDBAUGBwg=").valid);
	// Only once, and never for a second account.
	CHECK(!store.adopt("AABBCCDDEEFF", "AQIDBAUGBwg="));
	CHECK(!store.adopt("AABBCCDDEEFF", "CAcGBQQDAgE="));
	CHECK(!store.load("AABBCCDDEEFF", "CAcGBQQDAgE=").valid);
	// Read back from the file as well.
	CHECK_EQ(CredentialStore(path).load("AABBCCDDEEFF", "AQIDBAUGBwg=").registKey, std::string("1a2b3c4d"));

	std::remove(path.c_str());
}

ORBISLINK_TEST(wakeup_credential_comes_from_the_registration_key)
{
	StreamCredentials c = sample("AABBCCDDEEFF", "Living room");
	// The key is read as a hexadecimal number, which is what the wakeup
	// packet carries (chiaki_discovery_wakeup, user_credential field).
	CHECK_EQ(c.wakeupCredential(), uint64_t(0x1a2b3c4d));

	c.registKey.clear();
	CHECK_EQ(c.wakeupCredential(), uint64_t(0));
}

ORBISLINK_TEST(a_broken_file_does_not_bring_the_app_down)
{
	const std::string path = tempFile();
	{
		FILE *f = std::fopen(path.c_str(), "wb");
		CHECK(f != nullptr);
		std::fputs("this is not json {{{", f);
		std::fclose(f);
	}

	CredentialStore store(path);
	CHECK(store.all().empty());
	CHECK(!store.load("AABBCCDDEEFF").valid);

	std::remove(path.c_str());
}

TEST_MAIN()
