// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/update/sha256.h"
#include "orbislink/update/update_checker.h"
#include "orbislink/update/version.h"
#include "test_support.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace orbislink;

ORBISLINK_TEST(sha256_bate_com_os_vectores_publicos)
{
	// Vectores do NIST/RFC 6234.
	CHECK_EQ(sha256Hex(""),
		std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(sha256Hex("abc"),
		std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

	// Um milhão de "a": exercita os blocos em cadeia, não só o padding.
	Sha256 longo;
	const std::string bloco(1000, 'a');
	for(int i = 0; i < 1000; ++i)
		longo.update(bloco);
	CHECK_EQ(longo.hex(),
		std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

ORBISLINK_TEST(sha256_de_um_ficheiro_bate_com_o_da_memoria)
{
	const std::string path = ".orbislink-test-sha.bin";
	std::string conteudo;
	for(int i = 0; i < 100000; ++i)
		conteudo.push_back(static_cast<char>(i % 251));
	{
		std::ofstream out(path, std::ios::binary);
		out.write(conteudo.data(), static_cast<std::streamsize>(conteudo.size()));
	}
	CHECK_EQ(sha256File(path), sha256Hex(conteudo));
	// Um ficheiro que não existe não pode devolver o hash do vazio: isso
	// deixaria passar uma verificação de integridade.
	CHECK(sha256File(".orbislink-nao-existe.bin").empty());
	std::remove(path.c_str());
}

ORBISLINK_TEST(le_versoes_com_e_sem_v)
{
	const Version a = parseVersion("0.1.8");
	CHECK(a.valid);
	CHECK_EQ(a.major, 0);
	CHECK_EQ(a.minor, 1);
	CHECK_EQ(a.patch, 8);
	CHECK(a.pre.empty());

	const Version b = parseVersion("v1.2.3-dev.42");
	CHECK(b.valid);
	CHECK_EQ(b.major, 1);
	CHECK_EQ(b.minor, 2);
	CHECK_EQ(b.patch, 3);
	CHECK_EQ(b.pre, std::string("dev.42"));
	CHECK_EQ(b.toString(), std::string("1.2.3-dev.42"));

	// O que a app usa quando o CI lhe dá um nome estranho.
	CHECK(!parseVersion("").valid);
	CHECK(!parseVersion("nao-e-uma-versao").valid);
	// Metadados de build não contam.
	CHECK_EQ(compareVersions("0.1.8+abc", "0.1.8"), 0);
}

ORBISLINK_TEST(ordena_versoes_como_o_semver)
{
	CHECK(compareVersions("0.1.7", "0.1.8") < 0);
	CHECK(compareVersions("0.1.8", "0.1.8") == 0);
	CHECK(compareVersions("0.2.0", "0.1.9") > 0);
	CHECK(compareVersions("1.0.0", "0.9.9") > 0);

	// O que faz o canal de testes funcionar: uma dev vem antes do final.
	CHECK(compareVersions("0.1.8-dev.2", "0.1.8") < 0);
	CHECK(compareVersions("0.1.8", "0.1.8-dev.2") > 0);
	CHECK(compareVersions("0.1.8-dev.2", "0.1.8-dev.3") < 0);
	// Por valor e não por texto: a 10 é posterior à 9.
	CHECK(compareVersions("0.1.8-dev.9", "0.1.8-dev.10") < 0);
	// Quem está numa dev recebe a seguinte e depois a final.
	CHECK(compareVersions("0.1.8-dev.42", "0.1.9") < 0);
}

ORBISLINK_TEST(versao_ilegivel_nunca_ganha)
{
	// Se o GitHub devolver lixo no tag_name, a app não pode concluir que há
	// uma versão nova — seria um update para nenhures.
	CHECK(compareVersions("lixo", "0.1.8") < 0);
	CHECK(compareVersions("0.1.8", "lixo") > 0);
}


namespace {

// Uma resposta como a que a API do GitHub dá, encurtada ao que se lê.
const char *kRespostaGitHub = R"([
  {
    "tag_name": "v0.1.9-dev.3",
    "name": "Build de testes 3",
    "body": "Mudanças do dia.\n\nOrbisLink-0.1.9-dev.3-setup.exe  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
    "html_url": "https://github.com/exemplo/app/releases/tag/v0.1.9-dev.3",
    "draft": false,
    "prerelease": true,
    "assets": [
      { "name": "OrbisLink-0.1.9-dev.3-setup.exe",
        "browser_download_url": "https://exemplo/dev3-setup.exe", "size": 12345 }
    ]
  },
  {
    "tag_name": "v0.1.8",
    "name": "v0.1.8",
    "body": "Remote Play.",
    "html_url": "https://github.com/exemplo/app/releases/tag/v0.1.8",
    "draft": false,
    "prerelease": false,
    "assets": [
      { "name": "OrbisLink-0.1.8-setup.exe",
        "browser_download_url": "https://exemplo/018-setup.exe", "size": 999 },
      { "name": "OrbisLink-0.1.8-setup.exe.sha256",
        "browser_download_url": "https://exemplo/018.sha256", "size": 80 },
      { "name": "OrbisLink-0.1.8-portable.zip",
        "browser_download_url": "https://exemplo/018.zip", "size": 888 }
    ]
  },
  {
    "tag_name": "v0.2.0",
    "name": "rascunho, ainda não publicado",
    "body": "",
    "draft": true,
    "prerelease": false,
    "assets": []
  }
])";

} // namespace

ORBISLINK_TEST(le_a_resposta_do_github)
{
	const auto releases = UpdateChecker::parseReleases(kRespostaGitHub, "-setup.exe");
	// O rascunho não conta: para quem está de fora, não existe.
	CHECK_EQ(releases.size(), static_cast<size_t>(2));

	CHECK_EQ(releases[0].tag, std::string("v0.1.9-dev.3"));
	CHECK(releases[0].prerelease);
	CHECK_EQ(releases[0].assetName, std::string("OrbisLink-0.1.9-dev.3-setup.exe"));
	CHECK_EQ(releases[0].assetUrl, std::string("https://exemplo/dev3-setup.exe"));
	CHECK_EQ(releases[0].assetSize, static_cast<int64_t>(12345));
	// O hash vinha escrito no corpo, na linha do ficheiro.
	CHECK_EQ(releases[0].assetSha256,
		std::string("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));

	CHECK_EQ(releases[1].tag, std::string("v0.1.8"));
	CHECK(!releases[1].prerelease);
	// Escolheu o instalador e não o zip nem o .sha256.
	CHECK_EQ(releases[1].assetName, std::string("OrbisLink-0.1.8-setup.exe"));
	CHECK_EQ(releases[1].assetSha256Url, std::string("https://exemplo/018.sha256"));
	CHECK(releases[1].assetSha256.empty());
}

ORBISLINK_TEST(canal_estavel_ignora_pre_lancamentos)
{
	const auto releases = UpdateChecker::parseReleases(kRespostaGitHub, "-setup.exe");

	// Quem está na 0.1.7 e no canal estável recebe a 0.1.8, não a dev.
	const ReleaseInfo *estavel =
		UpdateChecker::pick(releases, UpdateChannel::Stable, "0.1.7");
	CHECK(estavel != nullptr);
	CHECK_EQ(estavel->tag, std::string("v0.1.8"));

	// No canal de testes recebe a dev, que é posterior.
	const ReleaseInfo *testes =
		UpdateChecker::pick(releases, UpdateChannel::Testing, "0.1.7");
	CHECK(testes != nullptr);
	CHECK_EQ(testes->tag, std::string("v0.1.9-dev.3"));
}

ORBISLINK_TEST(nao_oferece_o_que_ja_esta_instalado)
{
	const auto releases = UpdateChecker::parseReleases(kRespostaGitHub, "-setup.exe");

	// Já na 0.1.8, canal estável: nada a fazer.
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Stable, "0.1.8") == nullptr);
	// E nunca se empurra para trás quem está à frente.
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Stable, "0.3.0") == nullptr);
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Testing, "0.3.0") == nullptr);
	// Quem está numa dev recebe a dev seguinte.
	const ReleaseInfo *seguinte =
		UpdateChecker::pick(releases, UpdateChannel::Testing, "0.1.9-dev.2");
	CHECK(seguinte != nullptr);
	CHECK_EQ(seguinte->tag, std::string("v0.1.9-dev.3"));
	CHECK(UpdateChecker::pick(releases, UpdateChannel::Testing, "0.1.9-dev.3") == nullptr);
}

ORBISLINK_TEST(canal_estavel_avisa_quando_ha_uma_de_testes)
{
	const auto releases = UpdateChecker::parseReleases(kRespostaGitHub, "-setup.exe");

	// Na 0.1.8 e no canal estável não há nada — mas há uma dev posterior, e
	// isso tem de ser dito em vez de "estás actualizado".
	const std::string aviso =
		UpdateChecker::describeNothingNew(releases, UpdateChannel::Stable, "0.1.8");
	CHECK(aviso.find("0.1.9-dev.3") != std::string::npos);
	CHECK(aviso.find("Testes") != std::string::npos);

	// Sem nada em lado nenhum, diz só que está actualizado.
	const std::string nada =
		UpdateChecker::describeNothingNew(releases, UpdateChannel::Stable, "0.3.0");
	CHECK(nada.find("dev") == std::string::npos);
	CHECK(!UpdateChecker::describeNothingNew(releases, UpdateChannel::Testing, "0.3.0").empty());
}

ORBISLINK_TEST(resposta_ilegivel_nao_inventa_lancamentos)
{
	CHECK(UpdateChecker::parseReleases("", "-setup.exe").empty());
	CHECK(UpdateChecker::parseReleases("isto nao e json", "-setup.exe").empty());
	CHECK(UpdateChecker::parseReleases("[]", "-setup.exe").empty());
	// Uma entrada sem tag não dá uma versão para comparar.
	CHECK(UpdateChecker::parseReleases(R"([{"name":"sem tag"}])", "-setup.exe").empty());
}

ORBISLINK_TEST(sem_anexo_para_a_plataforma_ainda_ha_pagina)
{
	// Em Linux não há instalador publicado: o lançamento continua a contar,
	// só não tem ficheiro para descarregar.
	const auto releases = UpdateChecker::parseReleases(kRespostaGitHub, "");
	CHECK_EQ(releases.size(), static_cast<size_t>(2));
	CHECK(releases[1].assetName.empty());
	CHECK(!releases[1].pageUrl.empty());
}

ORBISLINK_TEST(repositorio_mal_definido_falha_a_dizer_porque)
{
	UpdateChecker::Config config;
	config.repository = "sem-barra";
	config.currentVersion = "0.1.8";
	const UpdateCheckResult result = UpdateChecker(config).check();
	CHECK(!result.ok);
	CHECK(!result.updateAvailable);
	CHECK(result.message.find("dono/nome") != std::string::npos);
}

ORBISLINK_TEST(canal_le_se_e_escreve_se_por_nome)
{
	CHECK_EQ(std::string(updateChannelName(UpdateChannel::Stable)), std::string("estavel"));
	CHECK_EQ(std::string(updateChannelName(UpdateChannel::Testing)), std::string("testes"));
	CHECK(updateChannelFromName("testes", UpdateChannel::Stable) == UpdateChannel::Testing);
	CHECK(updateChannelFromName("lixo", UpdateChannel::Stable) == UpdateChannel::Stable);
}

TEST_MAIN()
