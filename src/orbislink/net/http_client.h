// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {

struct HttpResponse
{
	bool transportOk = false; // there was an HTTP reply (even if 4xx/5xx)
	long status = 0;
	std::string body;
	std::string error; // libcurl's message when transportOk == false
};

// Minimal HTTP client on top of libcurl, used by RpiClient and the service
// checks. It does not follow redirects or use the system proxies: requests
// always go to the console on the local network.
class HttpClient
{
public:
	explicit HttpClient(int timeoutMs = 10000) : timeoutMs_(timeoutMs) {}

	void setTimeoutMs(int timeoutMs) { timeoutMs_ = timeoutMs; }
	int timeoutMs() const { return timeoutMs_; }

	HttpResponse post(const std::string &url, const std::string &body,
		const std::string &contentType = "application/json") const;
	HttpResponse get(const std::string &url) const;

	// Requests outside the local network (the GitHub API, for the update
	// check). Unlike get() above, this one follows redirects — a renamed
	// repository answers 301 — and takes custom headers.
	struct FetchOptions
	{
		bool followRedirects = true;
		std::vector<std::string> headers;
		// Set: the request stops ("cancelled") once it turns true.
		const std::atomic<bool> *cancel = nullptr;
	};
	HttpResponse fetch(const std::string &url, const FetchOptions &options) const;

	struct DownloadResult
	{
		bool ok = false;
		long status = 0;
		std::string error;
		int64_t bytes = 0;
	};
	// Downloads to a file. Progress is called along the way and returning
	// false cancels. It always follows redirects: GitHub release assets live
	// on another domain.
	DownloadResult download(const std::string &url, const std::string &destinationPath,
		const std::function<bool(int64_t done, int64_t total)> &progress = nullptr) const;

private:
	int timeoutMs_;
};

} // namespace orbislink
