// SPDX-License-Identifier: AGPL-3.0-or-later
//
// As credenciais do Remote Play são o que autentica este PC na consola.
// Se forem mal guardadas ou mal lidas, o sintoma na consola é "sessão
// recusada" sem mais explicação — por isso vale a pena testá-las aqui.

#include "orbislink/stream/credentials.h"
#include "test_support.h"

#include <cstdio>
#include <string>

using namespace orbislink;

namespace {

std::string ficheiroTemporario()
{
	static int contador = 0;
	return std::string(".orbislink-test-credenciais-") + std::to_string(++contador) + ".json";
}

StreamCredentials exemplo(const std::string &hostId, const std::string &nome)
{
	StreamCredentials c;
	c.valid = true;
	c.nickname = nome;
	c.hostId = hostId;
	c.registKey = "1a2b3c4d";
	c.rpKeyHex = "000102030405060708090A0B0C0D0E0F";
	c.rpKeyType = 2;
	c.target = 1000;
	c.ps5 = false;
	return c;
}

} // namespace

ORBISLINK_TEST(hexadecimal_ida_e_volta)
{
	const unsigned char original[4] = { 0x00, 0x7F, 0x80, 0xFF };
	const std::string hex = bytesToHex(original, sizeof(original));
	CHECK_EQ(hex, std::string("007F80FF"));

	unsigned char devolta[4] {};
	CHECK(hexToBytes(hex, devolta, sizeof(devolta)));
	for(size_t i = 0; i < sizeof(original); ++i)
		CHECK_EQ(int(devolta[i]), int(original[i]));
}

ORBISLINK_TEST(hexadecimal_recusa_o_que_nao_e_hexadecimal)
{
	unsigned char destino[4] {};
	// Comprimento errado.
	CHECK(!hexToBytes("00FF", destino, sizeof(destino)));
	// Caracteres que não são dígitos hexadecimais.
	CHECK(!hexToBytes("00ZZ80FF", destino, sizeof(destino)));
}

ORBISLINK_TEST(account_id_tem_de_ser_base64_de_oito_bytes)
{
	unsigned char id[8] {};
	std::string erro;

	// Oito bytes em base64.
	CHECK(decodeAccountId("AQIDBAUGBwg=", id, &erro));
	CHECK(erro.empty());
	CHECK_EQ(int(id[0]), 1);
	CHECK_EQ(int(id[7]), 8);

	// Vazio, texto que não é base64, e base64 com o tamanho errado: os três
	// têm de ser recusados com uma explicação.
	for(const char *mau : { "", "isto não é base64", "AQID" })
	{
		erro.clear();
		CHECK(!decodeAccountId(mau, id, &erro));
		CHECK(!erro.empty());
	}
}

ORBISLINK_TEST(guarda_e_le_uma_consola)
{
	const std::string caminho = ficheiroTemporario();
	CredentialStore store(caminho);

	CHECK(store.save(exemplo("AABBCCDDEEFF", "PS4 da sala")));

	const StreamCredentials lida = store.load("AABBCCDDEEFF");
	CHECK(lida.valid);
	CHECK_EQ(lida.nickname, std::string("PS4 da sala"));
	CHECK_EQ(lida.registKey, std::string("1a2b3c4d"));
	CHECK_EQ(lida.rpKeyHex, std::string("000102030405060708090A0B0C0D0E0F"));
	CHECK_EQ(int(lida.rpKeyType), 2);
	CHECK_EQ(lida.target, 1000);

	std::remove(caminho.c_str());
}

ORBISLINK_TEST(guarda_varias_consolas_sem_as_confundir)
{
	const std::string caminho = ficheiroTemporario();
	CredentialStore store(caminho);

	CHECK(store.save(exemplo("AABBCCDDEEFF", "Sala")));
	StreamCredentials segunda = exemplo("112233445566", "Quarto");
	segunda.registKey = "ffffffff";
	CHECK(store.save(segunda));

	CHECK_EQ(store.all().size(), size_t(2));
	CHECK_EQ(store.load("AABBCCDDEEFF").registKey, std::string("1a2b3c4d"));
	CHECK_EQ(store.load("112233445566").registKey, std::string("ffffffff"));
	// Uma consola desconhecida não devolve a de outra pessoa.
	CHECK(!store.load("999999999999").valid);

	std::remove(caminho.c_str());
}

ORBISLINK_TEST(voltar_a_registar_substitui_em_vez_de_duplicar)
{
	const std::string caminho = ficheiroTemporario();
	CredentialStore store(caminho);

	CHECK(store.save(exemplo("AABBCCDDEEFF", "Nome antigo")));
	StreamCredentials nova = exemplo("AABBCCDDEEFF", "Nome novo");
	nova.registKey = "deadbeef";
	CHECK(store.save(nova));

	CHECK_EQ(store.all().size(), size_t(1));
	CHECK_EQ(store.load("AABBCCDDEEFF").registKey, std::string("deadbeef"));
	CHECK_EQ(store.load("AABBCCDDEEFF").nickname, std::string("Nome novo"));

	std::remove(caminho.c_str());
}

ORBISLINK_TEST(esquecer_apaga_so_a_consola_pedida)
{
	const std::string caminho = ficheiroTemporario();
	CredentialStore store(caminho);
	store.save(exemplo("AABBCCDDEEFF", "Sala"));
	store.save(exemplo("112233445566", "Quarto"));

	CHECK(store.forget("AABBCCDDEEFF"));
	CHECK(!store.load("AABBCCDDEEFF").valid);
	CHECK(store.load("112233445566").valid);
	// Esquecer o que já não lá está não é um erro silencioso: devolve false.
	CHECK(!store.forget("AABBCCDDEEFF"));

	std::remove(caminho.c_str());
}

ORBISLINK_TEST(a_credencial_para_acordar_sai_da_chave_de_registo)
{
	StreamCredentials c = exemplo("AABBCCDDEEFF", "Sala");
	// A chave é lida como número hexadecimal, que é o que o pacote de
	// wakeup leva (chiaki_discovery_wakeup, campo user_credential).
	CHECK_EQ(c.wakeupCredential(), uint64_t(0x1a2b3c4d));

	c.registKey.clear();
	CHECK_EQ(c.wakeupCredential(), uint64_t(0));
}

ORBISLINK_TEST(um_ficheiro_estragado_nao_deita_a_aplicacao_abaixo)
{
	const std::string caminho = ficheiroTemporario();
	{
		FILE *f = std::fopen(caminho.c_str(), "wb");
		CHECK(f != nullptr);
		std::fputs("isto não é json {{{", f);
		std::fclose(f);
	}

	CredentialStore store(caminho);
	CHECK(store.all().empty());
	CHECK(!store.load("AABBCCDDEEFF").valid);

	std::remove(caminho.c_str());
}

TEST_MAIN()
