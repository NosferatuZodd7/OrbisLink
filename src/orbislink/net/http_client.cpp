// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/net/http_client.h"

#include "orbislink/common/log.h"

#include <curl/curl.h>

#include <cstdio>

namespace orbislink {

namespace {

size_t writeCallback(char *data, size_t size, size_t count, void *userdata)
{
	auto *out = static_cast<std::string *>(userdata);
	out->append(data, size * count);
	return size * count;
}

struct CurlGlobal
{
	CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
	~CurlGlobal() { curl_global_cleanup(); }
};

void ensureCurlInitialised()
{
	static CurlGlobal global;
	(void)global;
}

HttpResponse perform(const std::string &url, const std::string *body,
	const std::string &contentType, int timeoutMs)
{
	ensureCurlInitialised();
	HttpResponse response;

	CURL *curl = curl_easy_init();
	if(!curl)
	{
		response.error = "não foi possível inicializar o libcurl";
		return response;
	}

	struct curl_slist *headers = nullptr;
	if(body)
		headers = curl_slist_append(headers, ("Content-Type: " + contentType).c_str());
	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "Expect:"); // evita o 100-continue

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs));
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_PROXY, "");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "OrbisLink/0.1");
	if(body)
	{
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
	}

	const CURLcode code = curl_easy_perform(curl);
	if(code == CURLE_OK)
	{
		response.transportOk = true;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	}
	else
		response.error = curl_easy_strerror(code);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response;
}

struct DownloadState
{
	std::FILE *file = nullptr;
	int64_t written = 0;
	const std::function<bool(int64_t, int64_t)> *progress = nullptr;
	bool cancelled = false;
};

size_t downloadWrite(char *data, size_t size, size_t count, void *userdata)
{
	auto *state = static_cast<DownloadState *>(userdata);
	const size_t bytes = size * count;
	if(std::fwrite(data, 1, bytes, state->file) != bytes)
		return 0; // erro de escrita: o curl aborta
	state->written += static_cast<int64_t>(bytes);
	return bytes;
}

int downloadProgress(void *userdata, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t)
{
	auto *state = static_cast<DownloadState *>(userdata);
	if(!state->progress || !*state->progress)
		return 0;
	if(!(*state->progress)(static_cast<int64_t>(done), static_cast<int64_t>(total)))
	{
		state->cancelled = true;
		return 1; // não-zero aborta a transferência
	}
	return 0;
}

} // namespace

HttpResponse HttpClient::fetch(const std::string &url, const FetchOptions &options) const
{
	logDebug("HTTP GET (externo) " + url);
	ensureCurlInitialised();
	HttpResponse response;

	CURL *curl = curl_easy_init();
	if(!curl)
	{
		response.error = "não foi possível inicializar o libcurl";
		return response;
	}

	struct curl_slist *headers = nullptr;
	for(const std::string &header : options.headers)
		headers = curl_slist_append(headers, header.c_str());

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	if(headers)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "OrbisLink");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, options.followRedirects ? 1L : 0L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	// A verificação do certificado fica como está por omissão, ligada: isto
	// vai à internet, ao contrário do resto do cliente.
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

	const CURLcode code = curl_easy_perform(curl);
	if(code == CURLE_OK)
	{
		response.transportOk = true;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	}
	else
		response.error = curl_easy_strerror(code);

	if(headers)
		curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response;
}

HttpClient::DownloadResult HttpClient::download(const std::string &url,
	const std::string &destinationPath,
	const std::function<bool(int64_t done, int64_t total)> &progress) const
{
	logInfo("A descarregar " + url + " para " + destinationPath);
	ensureCurlInitialised();
	DownloadResult result;

	DownloadState state;
	state.progress = &progress;
#ifdef _WIN32
	if(fopen_s(&state.file, destinationPath.c_str(), "wb") != 0)
		state.file = nullptr;
#else
	state.file = std::fopen(destinationPath.c_str(), "wb");
#endif
	if(!state.file)
	{
		result.error = "não foi possível escrever em " + destinationPath;
		return result;
	}

	CURL *curl = curl_easy_init();
	if(!curl)
	{
		std::fclose(state.file);
		result.error = "não foi possível inicializar o libcurl";
		return result;
	}

	struct curl_slist *headers = curl_slist_append(nullptr, "Accept: application/octet-stream");
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, downloadWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, downloadProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	// Sem TIMEOUT total: um instalador de 80 MB numa ligação fraca demora o
	// que demorar. O que se vigia é a ligação parar de todo.
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "OrbisLink");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

	const CURLcode code = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	std::fclose(state.file);

	result.bytes = state.written;
	if(state.cancelled)
	{
		result.error = "cancelado";
		std::remove(destinationPath.c_str());
		return result;
	}
	if(code != CURLE_OK)
	{
		result.error = curl_easy_strerror(code);
		std::remove(destinationPath.c_str());
		return result;
	}
	if(result.status >= 400)
	{
		result.error = "o servidor respondeu " + std::to_string(result.status);
		std::remove(destinationPath.c_str());
		return result;
	}
	result.ok = true;
	return result;
}

HttpResponse HttpClient::post(const std::string &url, const std::string &body,
	const std::string &contentType) const
{
	logDebug("HTTP POST " + url + " " + body);
	return perform(url, &body, contentType, timeoutMs_);
}

HttpResponse HttpClient::get(const std::string &url) const
{
	logDebug("HTTP GET " + url);
	return perform(url, nullptr, std::string(), timeoutMs_);
}

} // namespace orbislink
