// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/update/update_checker.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"

#include <cctype>

namespace orbislink {

namespace {

// Um SHA-256 publicado ao lado do ficheiro pode vir de duas maneiras: num
// anexo "<nome>.sha256", ou escrito no corpo do lançamento. Aqui trata-se
// da segunda: procura-se uma palavra de 64 dígitos hexadecimais na mesma
// linha que o nome do ficheiro.
std::string sha256FromNotes(const std::string &notes, const std::string &assetName)
{
	if(notes.empty() || assetName.empty())
		return std::string();
	for(const std::string &linha : split(notes, '\n', false))
	{
		if(linha.find(assetName) == std::string::npos)
			continue;
		size_t inicio = std::string::npos;
		size_t contados = 0;
		for(size_t i = 0; i <= linha.size(); ++i)
		{
			const bool hex = i < linha.size()
				&& std::isxdigit(static_cast<unsigned char>(linha[i])) != 0;
			if(hex)
			{
				if(contados == 0)
					inicio = i;
				++contados;
				continue;
			}
			if(contados == 64)
				return toLower(linha.substr(inicio, 64));
			contados = 0;
		}
	}
	return std::string();
}

} // namespace

const char *updateChannelName(UpdateChannel channel)
{
	return channel == UpdateChannel::Testing ? "testes" : "estavel";
}

UpdateChannel updateChannelFromName(const std::string &name, UpdateChannel fallback)
{
	if(name == "testes" || name == "testing" || name == "beta")
		return UpdateChannel::Testing;
	if(name == "estavel" || name == "stable")
		return UpdateChannel::Stable;
	return fallback;
}

std::string platformAssetSuffix()
{
#if defined(_WIN32)
	// O instalador que o release.yml publica.
	return "-setup.exe";
#else
	// Em Linux ainda não há um pacote que se instale sozinho (o AppImage
	// está por fazer), por isso só se oferece a página do lançamento.
	return std::string();
#endif
}

std::vector<ReleaseInfo> UpdateChecker::parseReleases(const std::string &json,
	const std::string &assetSuffix)
{
	std::vector<ReleaseInfo> releases;
	std::string error;
	const Json root = Json::parse(json, &error);
	// A API devolve uma lista; /releases/latest devolve um objecto só.
	std::vector<Json> entradas;
	if(root.isArray())
		entradas = root.items();
	else if(root.isObject())
		entradas.push_back(root);
	else
		return releases;

	for(const Json &entrada : entradas)
	{
		if(!entrada.isObject())
			continue;
		// Rascunhos não existem para quem está do lado de fora.
		if(entrada["draft"].toLooseBool(false))
			continue;
		ReleaseInfo info;
		info.tag = entrada["tag_name"].toString();
		if(info.tag.empty())
			continue;
		info.name = entrada["name"].toString(info.tag);
		info.notes = entrada["body"].toString();
		info.pageUrl = entrada["html_url"].toString();
		info.prerelease = entrada["prerelease"].toLooseBool(false);

		const Json &anexos = entrada["assets"];
		for(size_t i = 0; i < anexos.size(); ++i)
		{
			const Json &anexo = anexos.at(i);
			const std::string nome = anexo["name"].toString();
			if(nome.empty())
				continue;
			// O ficheiro dos hashes, publicado ao lado do instalador.
			if(endsWith(toLower(nome), ".sha256"))
			{
				info.assetSha256Url = anexo["browser_download_url"].toString();
				continue;
			}
			if(assetSuffix.empty() || !endsWith(toLower(nome), toLower(assetSuffix)))
				continue;
			info.assetName = nome;
			info.assetUrl = anexo["browser_download_url"].toString();
			info.assetSize = anexo["size"].toInt(0);
		}
		info.assetSha256 = sha256FromNotes(info.notes, info.assetName);
		releases.push_back(info);
	}
	return releases;
}

const ReleaseInfo *UpdateChecker::pick(const std::vector<ReleaseInfo> &releases,
	UpdateChannel channel, const std::string &currentVersion)
{
	const Version atual = parseVersion(currentVersion);
	const ReleaseInfo *melhor = nullptr;
	Version melhorVersao;
	for(const ReleaseInfo &info : releases)
	{
		// No canal estável, uma pré-lançamento não conta.
		if(channel == UpdateChannel::Stable && info.prerelease)
			continue;
		const Version versao = info.version();
		if(!versao.valid)
			continue;
		if(melhor && compareVersions(versao, melhorVersao) <= 0)
			continue;
		melhor = &info;
		melhorVersao = versao;
	}
	if(!melhor)
		return nullptr;
	// Só é novidade se for mesmo posterior ao que está instalado. Uma
	// versão local ilegível conta como antiga, e aí qualquer lançamento
	// válido serve.
	if(atual.valid && compareVersions(melhorVersao, atual) <= 0)
		return nullptr;
	return melhor;
}

std::string UpdateChecker::describeNothingNew(const std::vector<ReleaseInfo> &releases,
	UpdateChannel channel, const std::string &currentVersion)
{
	if(channel == UpdateChannel::Stable)
	{
		// Só com compilações de testes publicadas, o canal estável responderia
		// "estás na versão mais recente" com uma versão nova ao lado.
		const ReleaseInfo *testes = pick(releases, UpdateChannel::Testing, currentVersion);
		if(testes)
			return "Não há versão estável mais recente. Há uma compilação de testes, "
				+ testes->version().toString()
				+ ": para a receber, escolhe o canal \"Testes\" nas definições.";
		return "Estás na versão estável mais recente.";
	}
	return "Estás na versão mais recente, contando com as compilações de testes.";
}

UpdateCheckResult UpdateChecker::check() const
{
	UpdateCheckResult result;
	if(config_.repository.find('/') == std::string::npos)
	{
		result.message = "O repositório de actualizações não está definido "
						 "(espera-se \"dono/nome\").";
		return result;
	}

	const std::string url = config_.apiBase + "/repos/" + config_.repository
		+ "/releases?per_page=20";
	HttpClient client(config_.timeoutMs);
	HttpClient::FetchOptions options;
	options.headers.push_back("Accept: application/vnd.github+json");
	options.headers.push_back("X-GitHub-Api-Version: 2022-11-28");
	const HttpResponse response = client.fetch(url, options);

	if(!response.transportOk)
	{
		result.message = "Não foi possível falar com o GitHub: " + response.error;
		return result;
	}
	if(response.status == 404)
	{
		// O caso que realmente acontece: o repositório é privado, ou mudou
		// de nome. Dizê-lo em vez de um "sem novidades" que mente.
		result.message = "O repositório " + config_.repository
			+ " não respondeu (é privado, ou o nome está errado).";
		return result;
	}
	if(response.status == 403 || response.status == 429)
	{
		result.message = "O GitHub pediu para esperar (limite de pedidos). Tenta mais tarde.";
		return result;
	}
	if(response.status < 200 || response.status >= 300)
	{
		result.message = "O GitHub respondeu " + std::to_string(response.status) + ".";
		return result;
	}

	const std::vector<ReleaseInfo> releases = parseReleases(response.body, config_.assetSuffix);
	if(releases.empty())
	{
		result.ok = true;
		result.message = "Ainda não há lançamentos publicados.";
		return result;
	}

	result.ok = true;
	const ReleaseInfo *novo = pick(releases, config_.channel, config_.currentVersion);
	if(!novo)
	{
		result.message = describeNothingNew(releases, config_.channel, config_.currentVersion);
		return result;
	}
	result.updateAvailable = true;
	result.release = *novo;
	result.message = "Há uma versão nova: " + novo->version().toString() + ".";
	logInfo("Actualização disponível: " + novo->tag + " (instalada: " + config_.currentVersion
		+ ")");
	return result;
}

} // namespace orbislink
