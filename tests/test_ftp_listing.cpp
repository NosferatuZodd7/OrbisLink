// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/ftp/ftp_client.h"
#include "test_support.h"

#include <algorithm>
#include <sstream>

using namespace orbislink;

ORBISLINK_TEST(le_listagem_estilo_unix)
{
	const std::string listing =
		"drwxr-xr-x   2 root  root      4096 Jan  1 00:00 pkg\r\n"
		"-rw-r--r--   1 root  root  5368709120 Mar 15 12:34 jogo grande.pkg\r\n"
		"-rw-r--r--   1 root  root       512 Mar 15 12:35 nota.txt\r\n";
	const auto entries = FtpClient::parseListing(listing, "/data");
	CHECK_EQ(entries.size(), static_cast<size_t>(3));
	// Pastas primeiro, depois ficheiros por ordem alfabética.
	CHECK_EQ(entries[0].name, std::string("pkg"));
	CHECK(entries[0].isDirectory);
	CHECK_EQ(entries[0].path, std::string("/data/pkg"));
	CHECK_EQ(entries[1].name, std::string("jogo grande.pkg")); // nome com espaços
	CHECK_EQ(entries[1].size, 5368709120ll);                   // > 4 GB
	CHECK(!entries[1].isDirectory);
	CHECK_EQ(entries[2].name, std::string("nota.txt"));
	CHECK_EQ(entries[2].path, std::string("/data/nota.txt"));
}

ORBISLINK_TEST(ignora_ponto_e_ponto_ponto)
{
	const std::string listing =
		"drwxr-xr-x 2 root root 4096 Jan 1 00:00 .\n"
		"drwxr-xr-x 2 root root 4096 Jan 1 00:00 ..\n"
		"-rw-r--r-- 1 root root  10 Jan 1 00:00 a.bin\n";
	const auto entries = FtpClient::parseListing(listing, "/");
	CHECK_EQ(entries.size(), static_cast<size_t>(1));
	CHECK_EQ(entries[0].name, std::string("a.bin"));
	CHECK_EQ(entries[0].path, std::string("/a.bin"));
}

ORBISLINK_TEST(le_ligacoes_simbolicas)
{
	const std::string listing = "lrwxrwxrwx 1 root root 7 Jan 1 00:00 atalho -> /data/pkg\n";
	const auto entries = FtpClient::parseListing(listing, "/mnt");
	CHECK_EQ(entries.size(), static_cast<size_t>(1));
	CHECK(entries[0].isSymlink);
	CHECK_EQ(entries[0].name, std::string("atalho"));
}

ORBISLINK_TEST(le_listagem_estilo_ms_dos)
{
	const std::string listing =
		"01-15-24  10:22AM       <DIR>          pkg\r\n"
		"01-15-24  10:23AM             1048576 jogo.pkg\r\n";
	const auto entries = FtpClient::parseListing(listing, "/data/");
	CHECK_EQ(entries.size(), static_cast<size_t>(2));
	CHECK(entries[0].isDirectory);
	CHECK_EQ(entries[1].size, 1048576);
}

ORBISLINK_TEST(linha_desconhecida_nao_se_perde)
{
	const auto entries = FtpClient::parseListing("qualquer-coisa-estranha\n", "/data");
	CHECK_EQ(entries.size(), static_cast<size_t>(1));
	CHECK_EQ(entries[0].name, std::string("qualquer-coisa-estranha"));
}

ORBISLINK_TEST(zonas_protegidas_do_sistema)
{
	CHECK(FtpClient::isProtectedPath("/system"));
	CHECK(FtpClient::isProtectedPath("/system/priv_data"));
	CHECK(FtpClient::isProtectedPath("/system_ex/app"));
	CHECK(FtpClient::isProtectedPath("/preinst/x"));
	CHECK(!FtpClient::isProtectedPath("/data/pkg"));
	CHECK(!FtpClient::isProtectedPath("/mnt/usb0"));

	FtpClient::Config config;
	config.host = "192.168.1.10";
	FtpClient client(config);
	CHECK(!client.isWriteAllowed("/system/x"));
	CHECK(client.isWriteAllowed("/data/pkg/x.pkg"));

	config.advancedMode = true;
	client.setConfig(config);
	CHECK(client.isWriteAllowed("/system/x"));
}

ORBISLINK_TEST(escreve_urls_ftp_corretos)
{
	FtpClient::Config config;
	config.host = "192.168.1.10";
	config.port = 2121;
	FtpClient client(config);
	CHECK_EQ(client.urlFor("/data/pkg/jogo teste.pkg"),
		std::string("ftp://192.168.1.10:2121/data/pkg/jogo%20teste.pkg"));
}

ORBISLINK_TEST(atalhos_sugeridos)
{
	const auto shortcuts = FtpClient::shortcutPaths();
	CHECK(std::find(shortcuts.begin(), shortcuts.end(), "/data/pkg/") != shortcuts.end());
	CHECK(std::find(shortcuts.begin(), shortcuts.end(), "/mnt/usb0/") != shortcuts.end());
}

TEST_MAIN()
