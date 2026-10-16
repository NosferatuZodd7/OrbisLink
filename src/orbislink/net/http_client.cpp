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
		response.error = "could not initialise libcurl";
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

std::FILE *openFile(const std::string &path, const char *mode)
{
	std::FILE *file = nullptr;
#ifdef _WIN32
	if(fopen_s(&file, path.c_str(), mode) != 0)
		file = nullptr;
#else
	file = std::fopen(path.c_str(), mode);
#endif
	return file;
}

struct DownloadState
{
	std::FILE *file = nullptr;
	const std::string *path = nullptr;
	CURL *curl = nullptr;
	int64_t written = 0;
	// What the file had before (a download going on from there), until the
	// server shows it sends the whole file again.
	int64_t from = 0;
	bool answerSeen = false;
	bool writeFailed = false;
	const std::function<bool(int64_t, int64_t)> *progress = nullptr;
	bool cancelled = false;
};

size_t downloadWrite(char *data, size_t size, size_t count, void *userdata)
{
	auto *state = static_cast<DownloadState *>(userdata);
	const size_t bytes = size * count;
	if(!state->answerSeen)
	{
		state->answerSeen = true;
		long status = 0;
		curl_easy_getinfo(state->curl, CURLINFO_RESPONSE_CODE, &status);
		// Asked for the rest, it sends everything: from the start.
		if(state->from > 0 && status == 200)
		{
			std::fclose(state->file);
			state->file = openFile(*state->path, "wb");
			state->from = 0;
			if(!state->file)
			{
				state->writeFailed = true;
				return 0;
			}
		}
	}
	if(std::fwrite(data, 1, bytes, state->file) != bytes)
	{
		state->writeFailed = true;
		return 0; // the write failed: curl stops
	}
	state->written += static_cast<int64_t>(bytes);
	return bytes;
}

int downloadProgress(void *userdata, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t)
{
	auto *state = static_cast<DownloadState *>(userdata);
	if(!state->progress || !*state->progress)
		return 0;
	// Until the server answered, what the file had is not known to stay.
	const int64_t had = state->answerSeen ? state->from : 0;
	const int64_t whole = total > 0 ? static_cast<int64_t>(total) + had : 0;
	if(!(*state->progress)(static_cast<int64_t>(done) + had, whole))
	{
		state->cancelled = true;
		return 1; // non-zero aborts the transfer
	}
	return 0;
}

int fetchProgress(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto *cancel = static_cast<const std::atomic<bool> *>(userdata);
	return cancel->load() ? 1 : 0; // non-zero aborts the transfer
}

} // namespace

HttpResponse HttpClient::fetch(const std::string &url, const FetchOptions &options) const
{
	logDebug("HTTP GET (external) " + url);
	ensureCurlInitialised();
	HttpResponse response;

	CURL *curl = curl_easy_init();
	if(!curl)
	{
		response.error = "could not initialise libcurl";
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
	// Certificate verification stays at its default, on: this goes to the
	// internet, unlike the rest of the client.
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	if(options.cancel)
	{
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, fetchProgress);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool> *>(options.cancel));
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	}

	const CURLcode code = curl_easy_perform(curl);
	if(code == CURLE_OK)
	{
		response.transportOk = true;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	}
	else
		response.error = options.cancel && options.cancel->load() ? "cancelled" : curl_easy_strerror(code);

	if(headers)
		curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response;
}

HttpClient::DownloadResult HttpClient::download(const std::string &url,
	const std::string &destinationPath,
	const std::function<bool(int64_t done, int64_t total)> &progress) const
{
	return download(url, destinationPath, progress, DownloadOptions());
}

HttpClient::DownloadResult HttpClient::download(const std::string &url, const std::string &destinationPath,
	const std::function<bool(int64_t done, int64_t total)> &progress, const DownloadOptions &options) const
{
	logInfo("Downloading " + url + " to " + destinationPath);
	ensureCurlInitialised();
	DownloadResult result;

	DownloadState state;
	state.progress = &progress;
	state.path = &destinationPath;
	if(options.resume)
	{
		std::FILE *existing = openFile(destinationPath, "rb");
		if(existing)
		{
			std::fseek(existing, 0, SEEK_END);
			state.from = static_cast<int64_t>(std::ftell(existing));
			std::fclose(existing);
		}
	}
	state.file = openFile(destinationPath, state.from > 0 ? "ab" : "wb");
	if(!state.file)
	{
		result.error = "could not write to " + destinationPath;
		return result;
	}

	CURL *curl = curl_easy_init();
	if(!curl)
	{
		std::fclose(state.file);
		result.error = "could not initialise libcurl";
		return result;
	}
	state.curl = curl;

	struct curl_slist *headers = curl_slist_append(nullptr, "Accept: application/octet-stream");
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, downloadWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, downloadProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	// No total TIMEOUT: an 80 MB installer on a weak connection takes as long
	// as it takes. What is watched is the connection stopping altogether.
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, options.userAgent.empty() ? "OrbisLink" : options.userAgent.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	if(state.from > 0)
		curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(state.from));

	const CURLcode code = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if(state.file)
		std::fclose(state.file);

	result.bytes = state.from + state.written;
	if(state.cancelled)
	{
		result.error = "cancelled";
		// Kept when it can go on from there next time.
		if(!options.resume)
			std::remove(destinationPath.c_str());
		return result;
	}
	// What was there already is the whole file or more than the server has
	// (416), or the server cannot send a part (curl stops at its 200): from
	// the start.
	if(state.from > 0 && (result.status == 416 || code == CURLE_RANGE_ERROR))
	{
		std::remove(destinationPath.c_str());
		DownloadOptions again = options;
		again.resume = false;
		return download(url, destinationPath, progress, again);
	}
	if(code != CURLE_OK)
	{
		result.error = state.writeFailed ? "could not write to " + destinationPath : curl_easy_strerror(code);
		// What came is kept to go on from, unless it is not the file at all.
		if(!options.resume || result.status >= 400 || state.writeFailed)
			std::remove(destinationPath.c_str());
		return result;
	}
	if(result.status >= 400)
	{
		result.error = "the server answered " + std::to_string(result.status);
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
