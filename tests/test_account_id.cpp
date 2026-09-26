// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O Account ID é um número de 64 bits escrito de três maneiras. O que estes
// testes prendem é a ordem dos bytes: o base64 é dos 8 bytes em
// little-endian, e não o contrário. Não é escolha nossa — está no
// scripts/psn-account-id.py do chiaki-ng, que faz
// base64.b64encode(user_id.to_bytes(8, "little")). Trocá-la dá um ID que a
// consola recusa sem dizer porquê, e ninguém descobriria isso a olhar.
#include "orbislink/stream/account_id.h"
#include "test_support.h"

using namespace orbislink;

ORBISLINK_TEST(hexadecimal_da_o_base64_com_os_bytes_ao_contrario)
{
	// 0x0123456789ABCDEF em little-endian é EF CD AB 89 67 45 23 01, e esses
	// oito bytes em base64 são "782riWdFIwE=". Conferido à mão:
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
	// Um ID com 16 algarismos é ambíguo. Sem prefixo lê-se como decimal,
	// porque é a forma em que a PSN o dá; com "0x" lê-se em hexadecimal.
	// É por isso que a interface mostra as três formas ao mesmo tempo.
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
	// Quem copia isto de um ecrã de consola traz espaços e dois-pontos.
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
