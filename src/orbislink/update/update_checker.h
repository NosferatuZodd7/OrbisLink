// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/update/version.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

// Um lançamento tal como o GitHub o descreve.
struct ReleaseInfo
{
	std::string tag;        // "v0.1.8"
	std::string name;       // título do lançamento
	std::string notes;      // corpo em markdown
	std::string pageUrl;    // página do lançamento, para abrir no browser
	bool prerelease = false;

	// O ficheiro a descarregar para esta plataforma, se existir.
	std::string assetName;
	std::string assetUrl;
	int64_t assetSize = 0;
	// SHA-256 publicado ao lado do ficheiro, quando o há. Vazio significa
	// "não foi publicado" — e aí não se verifica nada, o que tem de ser
	// dito a quem carrega no botão.
	std::string assetSha256;
	// Em alternativa, o anexo "<nome>.sha256" de onde o ler. Só se vai
	// buscar na hora de descarregar, para a verificação não custar dois
	// pedidos a quem nem vai actualizar.
	std::string assetSha256Url;

	Version version() const { return parseVersion(tag); }
};

enum class UpdateChannel { Stable, Testing };

const char *updateChannelName(UpdateChannel channel);
UpdateChannel updateChannelFromName(const std::string &name, UpdateChannel fallback);

struct UpdateCheckResult
{
	bool ok = false;              // a verificação em si correu bem
	bool updateAvailable = false;
	std::string message;          // o que dizer a quem está a olhar
	ReleaseInfo release;
};

// Vê se há uma versão mais recente publicada no GitHub.
//
// Não precisa de credenciais: assume um repositório público. O repositório
// é configurável em vez de estar no código, para a app o poder seguir sem
// ser recompilada.
class UpdateChecker
{
public:
	struct Config
	{
		// Vem do ORBISLINK_REPOSITORY do CMake, que o CI preenche com o
		// repositório onde a compilação correu. Vazio numa compilação
		// local: aí a verificação diz que não está configurada, em vez de
		// ir bater ao repositório de outra pessoa.
		std::string repository = ORBISLINK_REPOSITORY_STRING; // "dono/nome"
		UpdateChannel channel = UpdateChannel::Stable;
		std::string currentVersion;
		// Sufixo do ficheiro a procurar nos anexos do lançamento. Em
		// Windows é o instalador; noutros sistemas fica vazio e só se
		// oferece a página do lançamento.
		std::string assetSuffix;
		std::string apiBase = "https://api.github.com";
		int timeoutMs = 15000;
	};

	explicit UpdateChecker(Config config) : config_(std::move(config)) {}

	const Config &config() const { return config_; }

	UpdateCheckResult check() const;

	// Exposto para testes: converte a resposta da API na lista de
	// lançamentos, sem rede pelo meio.
	static std::vector<ReleaseInfo> parseReleases(const std::string &json,
		const std::string &assetSuffix);
	// Escolhe o lançamento a oferecer de entre os que vieram.
	static const ReleaseInfo *pick(const std::vector<ReleaseInfo> &releases,
		UpdateChannel channel, const std::string &currentVersion);
	// O que dizer quando o canal escolhido não tem nada mais recente. Se o
	// outro canal tiver, diz qual é e onde está: "estás actualizado", com
	// uma versão nova publicada ao lado, é uma meia verdade que faz
	// desistir de procurar.
	static std::string describeNothingNew(const std::vector<ReleaseInfo> &releases,
		UpdateChannel channel, const std::string &currentVersion);

private:
	Config config_;
};

// O sufixo do instalador desta plataforma, ou vazio se não houver um.
std::string platformAssetSuffix();

} // namespace orbislink
