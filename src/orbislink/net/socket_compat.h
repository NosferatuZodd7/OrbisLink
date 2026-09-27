// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Thin layer over BSD sockets/Winsock so the rest of the core has no
// scattered #ifdefs. Windows is the main platform (§1); Linux/macOS use the
// same POSIX paths.

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
using socket_t = SOCKET;
#	define ORBISLINK_INVALID_SOCKET INVALID_SOCKET
#else
#	include <arpa/inet.h>
#	include <errno.h>
#	include <fcntl.h>
#	include <ifaddrs.h>
#	include <net/if.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <sys/select.h>
#	include <sys/socket.h>
#	include <unistd.h>
using socket_t = int;
#	define ORBISLINK_INVALID_SOCKET (-1)
#endif

#include <string>

namespace orbislink {

// Initialises Winsock only once (no-op outside Windows).
void initSocketsOnce();

void closeSocketHandle(socket_t sock);
bool setSocketNonBlocking(socket_t sock, bool nonBlocking);
int lastSocketError();
std::string socketErrorString(int error);

} // namespace orbislink
