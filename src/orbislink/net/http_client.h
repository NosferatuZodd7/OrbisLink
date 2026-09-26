// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {

struct HttpResponse
{
	bool transportOk = false; // houve resposta HTTP (mesmo que 4xx/5xx)
	long status = 0;
	std::string body;
	std::string error; // mensagem do libcurl quando transportOk == false
};

// Cliente HTTP minimalista sobre libcurl, usado pelo RpiClient e pelas
// verificações de serviço. Não segue redireções e não usa proxies do sistema:
// os pedidos são sempre para a consola na rede local.
class HttpClient
{
public:
	explicit HttpClient(int timeoutMs = 10000) : timeoutMs_(timeoutMs) {}

	void setTimeoutMs(int timeoutMs) { timeoutMs_ = timeoutMs; }
	int timeoutMs() const { return timeoutMs_; }

	HttpResponse post(const std::string &url, const std::string &body,
		const std::string &contentType = "application/json") const;
	HttpResponse get(const std::string &url) const;

	// Pedidos para fora da rede local (a API do GitHub, na verificação de
	// actualizações). Ao contrário do get() acima, este segue redireções —
	// um repositório que mudou de nome responde 301 — e aceita cabeçalhos
	// à medida.
	struct FetchOptions
	{
		bool followRedirects = true;
		std::vector<std::string> headers;
	};
	HttpResponse fetch(const std::string &url, const FetchOptions &options) const;

	struct DownloadResult
	{
		bool ok = false;
		long status = 0;
		std::string error;
		int64_t bytes = 0;
	};
	// Descarrega para um ficheiro. O progresso é chamado ao longo do
	// caminho e devolver false cancela. Segue redireções sempre: os anexos
	// dos lançamentos do GitHub vivem noutro domínio.
	DownloadResult download(const std::string &url, const std::string &destinationPath,
		const std::function<bool(int64_t done, int64_t total)> &progress = nullptr) const;

private:
	int timeoutMs_;
};

} // namespace orbislink
