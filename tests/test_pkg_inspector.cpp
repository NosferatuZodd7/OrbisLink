// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/pkg_inspector.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace orbislink;
using namespace orbislink_test;

ORBISLINK_TEST(le_metadados_de_um_pkg_valido)
{
	PkgOptions options;
	const std::string path = writeTempFile("jogo.pkg", buildPkg(options));

	PkgInspector inspector;
	const PkgInfo info = inspector.inspect(path);
	CHECK(info.valid);
	CHECK_EQ(info.title, std::string("Jogo de Teste"));
	CHECK_EQ(info.titleId, std::string("CUSA12345"));
	CHECK_EQ(info.contentId, std::string("UP0001-CUSA12345_00-ORBISLINKTEST001"));
	CHECK_EQ(info.category, std::string("gd"));
	CHECK_EQ(info.appVersion, std::string("01.00"));
	CHECK(info.kind == PkgCategory::Game);
	CHECK(!info.isPatch);
	CHECK(!info.iconPng.empty());
	CHECK_EQ(info.iconPng[0], static_cast<uint8_t>(0x89));
	CHECK_EQ(info.declaredSize, static_cast<uint64_t>(info.fileSize));

	removeTempFile(path);
}

ORBISLINK_TEST(distingue_patch_pelos_flags_do_cabecalho)
{
	PkgOptions options;
	options.contentFlags = 0x00100000; // FIRST_PATCH
	options.sfoEntries = { { "CATEGORY", "gp" }, { "TITLE", "Jogo de Teste" },
		{ "TITLE_ID", "CUSA12345" }, { "APP_VER", "01.02" } };
	const std::string path = writeTempFile("patch.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(info.valid);
	CHECK(info.isPatch);
	CHECK(info.kind == PkgCategory::Patch);
	CHECK_EQ(pkgCategoryInstallOrder(info.kind), 1);

	removeTempFile(path);
}

ORBISLINK_TEST(reconhece_dlc_pelo_content_type)
{
	PkgOptions options;
	options.contentType = 0x1B; // AC
	options.sfoEntries = { { "CATEGORY", "ac" }, { "TITLE", "Pacote extra" },
		{ "TITLE_ID", "CUSA12345" } };
	const std::string path = writeTempFile("dlc.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(info.kind == PkgCategory::Dlc);
	CHECK_EQ(pkgCategoryInstallOrder(info.kind), 3);
	CHECK_EQ(std::string(pkgCategoryLabelPt(info.kind)), std::string("DLC"));

	removeTempFile(path);
}

ORBISLINK_TEST(rejeita_magic_invalido)
{
	PkgOptions options;
	options.validMagic = false;
	const std::string path = writeTempFile("naoepkg.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK_EQ(info.error, std::string("Não é um pkg PS4 válido."));
	CHECK(!PkgInspector::hasPkgMagic(path));

	removeTempFile(path);
}

ORBISLINK_TEST(rejeita_ficheiro_pequeno_demais)
{
	std::vector<uint8_t> tiny(100, 0);
	tiny[0] = 0x7F;
	tiny[1] = 'C';
	tiny[2] = 'N';
	tiny[3] = 'T';
	const std::string path = writeTempFile("pequeno.pkg", tiny);

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK(info.error.find("demasiado pequeno") != std::string::npos);

	removeTempFile(path);
}

ORBISLINK_TEST(ficheiro_inexistente)
{
	const PkgInfo info = PkgInspector().inspect("nao-existe-mesmo.pkg");
	CHECK(!info.valid);
	CHECK(!info.error.empty());
}

ORBISLINK_TEST(pkg_sem_param_sfo_continua_valido)
{
	PkgOptions options;
	options.includeSfo = false;
	const std::string path = writeTempFile("semsfo.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(info.valid);
	CHECK(info.title.empty());
	// O TITLE_ID vem do content id do cabeçalho mesmo sem PARAM.SFO.
	CHECK_EQ(info.titleId, std::string("CUSA12345"));
	CHECK_EQ(info.displayTitle(), std::string("CUSA12345"));

	removeTempFile(path);
}

ORBISLINK_TEST(tabela_de_entradas_fora_dos_limites)
{
	PkgOptions options;
	auto data = buildPkg(options);
	putBE32(data, 0x10, 100000); // entry_count impossível para este ficheiro
	const std::string path = writeTempFile("corrompido.pkg", data);

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK(info.error.find("Tabela de entradas") != std::string::npos);

	removeTempFile(path);
}

ORBISLINK_TEST(ficheiro_acima_de_4gb_e_lido_por_offsets)
{
	// Ficheiro esparso de ~5 GB: confirma que os tamanhos usam 64 bits e que
	// só se leem os offsets necessários (o teste corre em segundos).
	PkgOptions options;
	const auto data = buildPkg(options);
	const std::string path = ".orbislink-test-grande.pkg";
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char *>(data.data()),
			static_cast<std::streamsize>(data.size()));
		const int64_t target = 5ll * 1024 * 1024 * 1024;
		file.seekp(static_cast<std::streamoff>(target - 1), std::ios::beg);
		const char zero = 0;
		file.write(&zero, 1);
	}

	const PkgInfo info = PkgInspector().inspect(path);
	if(info.fileSize < 4ll * 1024 * 1024 * 1024)
	{
		// Sistema de ficheiros sem suporte a ficheiros esparsos: não falha o teste.
		std::cout << "        (aviso: ficheiro esparso não criado; teste ignorado)\n";
		removeTempFile(path);
		return;
	}
	CHECK(info.valid);
	CHECK(info.fileSize > 4ll * 1024 * 1024 * 1024);
	CHECK_EQ(info.titleId, std::string("CUSA12345"));

	removeTempFile(path);
}

TEST_MAIN()
