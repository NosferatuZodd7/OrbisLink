// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/sfo_parser.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <sstream>

using namespace orbislink;
using namespace orbislink_test;

ORBISLINK_TEST(le_entradas_de_texto)
{
	const auto data = buildSfo({ { "CATEGORY", "gp" }, { "TITLE", "Jogo Acentuado ção" },
		{ "TITLE_ID", "CUSA00123" } });
	Sfo sfo;
	std::string error;
	CHECK(sfo.parse(data, &error));
	CHECK(error.empty());
	CHECK_EQ(sfo.stringValue("TITLE"), std::string("Jogo Acentuado ção"));
	CHECK_EQ(sfo.stringValue("TITLE_ID"), std::string("CUSA00123"));
	CHECK_EQ(sfo.stringValue("CATEGORY"), std::string("gp"));
	CHECK_EQ(sfo.stringValue("NAO_EXISTE", "omissao"), std::string("omissao"));
}

ORBISLINK_TEST(rejeita_magic_invalido)
{
	auto data = buildSfo({ { "TITLE", "x" } });
	data[1] = 'X';
	Sfo sfo;
	std::string error;
	CHECK(!sfo.parse(data, &error));
	CHECK(!error.empty());
}

ORBISLINK_TEST(rejeita_ficheiro_curto)
{
	std::vector<uint8_t> data = { 0x00, 'P', 'S', 'F' };
	Sfo sfo;
	CHECK(!sfo.parse(data, nullptr));
}

ORBISLINK_TEST(ignora_entrada_fora_dos_limites)
{
	auto data = buildSfo({ { "TITLE", "Bom" }, { "TITLE_ID", "CUSA00001" } });
	// Corrompe o value_offset da segunda entrada para lá do fim do ficheiro.
	putLE32(data, 0x14 + 0x10 + 0x0C, 0x7FFFFFFF);
	Sfo sfo;
	CHECK(sfo.parse(data, nullptr));
	CHECK_EQ(sfo.stringValue("TITLE"), std::string("Bom"));
	CHECK(sfo.find("TITLE_ID") == nullptr);
}

ORBISLINK_TEST(numero_de_entradas_implausivel)
{
	auto data = buildSfo({ { "TITLE", "x" } });
	putLE32(data, 0x10, 999999);
	Sfo sfo;
	std::string error;
	CHECK(!sfo.parse(data, &error));
}

TEST_MAIN()
