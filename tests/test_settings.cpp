// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/log.h"
#include "orbislink/settings/settings_store.h"
#include "test_support.h"

#include <cstdio>
#include <sstream>

using namespace orbislink;

ORBISLINK_TEST(ida_e_volta_das_definicoes)
{
	Settings settings;
	settings.consoleName = "PS4 da sala";
	settings.consoleAddress = "192.168.1.42";
	settings.defaultMode = TransferMode::FtpUpload;
	settings.ftpUploadDirectory = "/mnt/usb0/pkg/";
	settings.httpPort = 9000;
	settings.restrictToConsoleIp = false;
	settings.ftpMaxConnections = 2;
	settings.ftpAdvancedMode = true;
	settings.language = "en";

	bool ok = false;
	const Settings restored = Settings::fromJson(settings.toJson(), &ok);
	CHECK(ok);
	CHECK_EQ(restored.consoleName, std::string("PS4 da sala"));
	CHECK_EQ(restored.consoleAddress, std::string("192.168.1.42"));
	CHECK(restored.defaultMode == TransferMode::FtpUpload);
	CHECK_EQ(restored.ftpUploadDirectory, std::string("/mnt/usb0/pkg/"));
	CHECK_EQ(restored.httpPort, static_cast<uint16_t>(9000));
	CHECK(!restored.restrictToConsoleIp);
	CHECK_EQ(restored.ftpMaxConnections, 2);
	CHECK(restored.ftpAdvancedMode);
	CHECK_EQ(restored.language, std::string("en"));
}

ORBISLINK_TEST(valores_por_omissao_seguem_a_especificacao)
{
	const Settings settings;
	CHECK_EQ(settings.ftpPort, static_cast<uint16_t>(2121));
	CHECK_EQ(settings.installerPort, static_cast<uint16_t>(12800));
	CHECK_EQ(settings.httpPort, static_cast<uint16_t>(8765));
	CHECK(settings.defaultMode == TransferMode::DirectInstall);
	CHECK_EQ(settings.ftpUploadDirectory, std::string("/data/pkg/"));
	CHECK(settings.restrictToConsoleIp);
	CHECK(!settings.installAfterUpload);
	CHECK(!settings.ftpAdvancedMode);
	CHECK(!settings.debugLogging);
	CHECK(settings.checkForUpdates);
	CHECK_EQ(settings.ftpMaxConnections, 1);
	// Remote Play
	CHECK_EQ(settings.streamResolution, 720);
	CHECK_EQ(settings.streamFps, 60);
	CHECK_EQ(settings.streamBitrateKbps, 0);
	CHECK(settings.streamHardwareDecode);
	CHECK(!settings.streamFullscreenOnConnect);
	CHECK(settings.streamRumble);
	CHECK_EQ(settings.theme, std::string("escuro"));
	CHECK_EQ(settings.language, std::string("auto"));
}

ORBISLINK_TEST(definicoes_de_stream_fora_do_admissivel_sao_corrigidas)
{
	// Um ficheiro editado à mão não pode pedir o que a consola não conhece.
	bool ok = false;
	const Settings settings = Settings::fromJson(
		R"({"stream_resolution":999,"stream_fps":144,"stream_bitrate_kbps":-5,"theme":"neon"})",
		&ok);
	CHECK(ok);
	CHECK_EQ(settings.streamResolution, 720);
	CHECK_EQ(settings.streamFps, 60);
	CHECK_EQ(settings.streamBitrateKbps, 0);
	CHECK_EQ(settings.theme, std::string("escuro"));
}

ORBISLINK_TEST(definicoes_de_stream_admissiveis_passam)
{
	bool ok = false;
	const Settings settings = Settings::fromJson(
		R"({"stream_resolution":1080,"stream_fps":30,"stream_bitrate_kbps":15000,"theme":"vidro"})",
		&ok);
	CHECK(ok);
	CHECK_EQ(settings.streamResolution, 1080);
	CHECK_EQ(settings.streamFps, 30);
	CHECK_EQ(settings.streamBitrateKbps, 15000);
	CHECK_EQ(settings.theme, std::string("vidro"));
}

ORBISLINK_TEST(json_invalido_nao_estraga_as_definicoes)
{
	bool ok = true;
	const Settings settings = Settings::fromJson("{ lixo", &ok);
	CHECK(!ok);
	CHECK_EQ(settings.ftpPort, static_cast<uint16_t>(2121));
}

ORBISLINK_TEST(limita_ligacoes_ftp_a_duas)
{
	const Settings settings = Settings::fromJson(R"({"ftp_max_connections": 9})");
	CHECK_EQ(settings.ftpMaxConnections, 2);
}

ORBISLINK_TEST(guardar_e_ler_do_disco)
{
	const std::string path = ".orbislink-test-settings.json";
	Settings settings;
	settings.consoleAddress = "10.0.0.5";
	SettingsStore store(path);
	CHECK(store.save(settings));

	Settings loaded;
	CHECK(store.load(&loaded));
	CHECK_EQ(loaded.consoleAddress, std::string("10.0.0.5"));
	std::remove(path.c_str());
}

ORBISLINK_TEST(logs_nao_levam_dados_sensiveis)
{
	// §8/§9: nada de Account ID nem chaves de registo nos logs exportados.
	const std::string redacted =
		redactSensitive(R"({"psn_account_id":"1234567890","rp_key":"abcdef","title":"Jogo"})");
	CHECK(redacted.find("1234567890") == std::string::npos);
	CHECK(redacted.find("abcdef") == std::string::npos);
	CHECK(redacted.find("[REDIGIDO]") != std::string::npos);
	CHECK(redacted.find("Jogo") != std::string::npos);

	const std::string plain = redactSensitive("account_id=AABBCCDD e password=segredo");
	CHECK(plain.find("AABBCCDD") == std::string::npos);
	CHECK(plain.find("segredo") == std::string::npos);
}

ORBISLINK_TEST(assistente_so_aparece_a_primeira_vez)
{
	// Por omissão o assistente tem de aparecer; depois de correr uma vez,
	// nunca mais — e isso tem de sobreviver a guardar e reler.
	Settings settings;
	CHECK(!settings.firstRunDone);

	settings.firstRunDone = true;
	bool ok = false;
	const Settings relido = Settings::fromJson(settings.toJson(), &ok);
	CHECK(ok);
	CHECK(relido.firstRunDone);

	// Um ficheiro antigo, sem o campo, continua a pedir o assistente.
	const Settings antigo = Settings::fromJson(R"({"console_address":"10.0.0.5"})");
	CHECK(!antigo.firstRunDone);
}


// Se o projecto mudar de repositório, quem tem a versão antiga fica com o
// repositório antigo gravado nas definições, e a app nova não pode
// continuar a procurar versões lá.
ORBISLINK_TEST(repositorio_de_actualizacoes_segue_a_compilacao)
{
	const std::string novo = "novo/OrbisLink";
	const std::string antigo = "antigo/repo";
	// Ficheiro de antes da regra: não diz qual era a omissão.
	CHECK_EQ(resolveUpdateRepository(antigo, nullptr, novo), novo);
	// Gravado igual à omissão de então: ninguém o escolheu.
	CHECK_EQ(resolveUpdateRepository(antigo, &antigo, novo), novo);
	// Escrito à mão (diferente da omissão de então): fica.
	const std::string outro = "outra/copia";
	CHECK_EQ(resolveUpdateRepository(outro, &antigo, novo), outro);
	// Vazio nunca serve.
	CHECK_EQ(resolveUpdateRepository("", &antigo, novo), novo);
	// Compilação local, sem repositório: não há com que substituir.
	CHECK_EQ(resolveUpdateRepository(antigo, nullptr, ""), antigo);
}

TEST_MAIN()
