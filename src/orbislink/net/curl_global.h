// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <curl/curl.h>

namespace orbislink {

// libcurl's global set-up, done once for the whole program (HTTP and FTP
// alike) before the first handle. Older libcurls do not make
// curl_global_init safe to call from two threads; a function-local static
// runs it exactly once.
inline void ensureCurlGlobal()
{
	struct CurlGlobal
	{
		CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
		~CurlGlobal() { curl_global_cleanup(); }
	};
	static CurlGlobal global;
	(void)global;
}

} // namespace orbislink
