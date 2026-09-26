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

ORBISLINK_TEST(hexadecimal_da_o_base64_com_os_bytes_ao_contrario)
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

ORBISLINK_TEST(base64_volta_a_dar_o_mesmo_hexadecimal)
{
	const AccountId id = parseAccountId("782riWdFIwE=");
	CHECK(id.valid);
	CHECK_EQ(id.format, std::string("base64"));
	CHECK_EQ(id.hex, std::string("0123456789ABCDEF"));
	CHECK_EQ(id.decimal, std::string("81985529216486895"));
}

ORBISLINK_TEST(decimal_e_hexadecimal_dao_o_mesmo)
{
	const AccountId porDecimal = parseAccountId("81985529216486895");
	const AccountId porHex = parseAccountId("0x0123456789ABCDEF");
	CHECK(porDecimal.valid);
	CHECK(porHex.valid);
	CHECK_EQ(porDecimal.base64, porHex.base64);
	CHECK_EQ(porDecimal.format, std::string("decimal"));
	CHECK_EQ(porHex.format, std::string("hex"));
}

ORBISLINK_TEST(o_prefixo_0x_forca_hexadecimal_num_id_so_de_algarismos)
{
	// A 16-digit ID is ambiguous. Without a prefix it is read as decimal,
	// because that is the form PSN gives; with "0x" it is read as hexadecimal.
	// That is why the interface shows all three forms at once.
	const AccountId comoDecimal = parseAccountId("1234567890123456");
	const AccountId comoHex = parseAccountId("0x1234567890123456");
	CHECK(comoDecimal.valid);
	CHECK(comoHex.valid);
	CHECK_EQ(comoDecimal.format, std::string("decimal"));
	CHECK_EQ(comoHex.format, std::string("hex"));
	CHECK(comoDecimal.base64 != comoHex.base64);
	CHECK_EQ(comoHex.decimal, std::string("1311768467284833366"));
}

ORBISLINK_TEST(espacos_e_separadores_nao_estragam_nada)
{
	// Whoever copies this off a console screen brings spaces and colons.
	const AccountId comEspacos = parseAccountId("  01 23 45 67 89 AB CD EF  ");
	const AccountId comDoisPontos = parseAccountId("01:23:45:67:89:AB:CD:EF");
	CHECK(comEspacos.valid);
	CHECK(comDoisPontos.valid);
	CHECK_EQ(comEspacos.base64, std::string("782riWdFIwE="));
	CHECK_EQ(comDoisPontos.base64, std::string("782riWdFIwE="));
}

ORBISLINK_TEST(inverter_os_bytes_e_reversivel)
{
	const AccountId id = parseAccountId("0123456789ABCDEF");
	const AccountId trocado = reverseAccountIdBytes(id);
	CHECK(trocado.valid);
	CHECK_EQ(trocado.hex, std::string("EFCDAB8967452301"));
	CHECK_EQ(reverseAccountIdBytes(trocado).hex, id.hex);
}

ORBISLINK_TEST(minusculas_servem_na_mesma)
{
	const AccountId id = parseAccountId("0123456789abcdef");
	CHECK(id.valid);
	CHECK_EQ(id.hex, std::string("0123456789ABCDEF"));
}

ORBISLINK_TEST(o_que_nao_e_account_id_e_recusado_com_uma_razao)
{
	const AccountId vazio = parseAccountId("   ");
	CHECK(!vazio.valid);
	CHECK(!vazio.error.empty());

	const AccountId curto = parseAccountId("0123ABCD");
	CHECK(!curto.valid);
	CHECK(!curto.error.empty());

	const AccountId enorme = parseAccountId("99999999999999999999999");
	CHECK(!enorme.valid);
	CHECK(!enorme.error.empty());

	const AccountId disparate = parseAccountId("o-meu-account-id");
	CHECK(!disparate.valid);
	CHECK(!disparate.error.empty());
}

ORBISLINK_TEST(zero_e_o_maior_valor_nao_rebentam)
{
	const AccountId zero = parseAccountId("0000000000000000");
	CHECK(zero.valid);
	CHECK_EQ(zero.decimal, std::string("0"));
	CHECK_EQ(zero.base64, std::string("AAAAAAAAAAA="));

	const AccountId maximo = parseAccountId("FFFFFFFFFFFFFFFF");
	CHECK(maximo.valid);
	CHECK_EQ(maximo.decimal, std::string("18446744073709551615"));
}

TEST_MAIN()
