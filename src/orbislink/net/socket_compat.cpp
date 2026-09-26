// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/net/socket_compat.h"

#include <cstring>
#include <mutex>

namespace orbislink {

void initSocketsOnce()
{
#ifdef _WIN32
	static std::once_flag flag;
	std::call_once(flag, []() {
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
	});
#endif
}

void closeSocketHandle(socket_t sock)
{
	if(sock == ORBISLINK_INVALID_SOCKET)
		return;
#ifdef _WIN32
	closesocket(sock);
#else
	::close(sock);
#endif
}

bool setSocketNonBlocking(socket_t sock, bool nonBlocking)
{
#ifdef _WIN32
	u_long mode = nonBlocking ? 1 : 0;
	return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
	int flags = fcntl(sock, F_GETFL, 0);
	if(flags < 0)
		return false;
	flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
	return fcntl(sock, F_SETFL, flags) == 0;
#endif
}

int lastSocketError()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

std::string socketErrorString(int error)
{
#ifdef _WIN32
	char *buffer = nullptr;
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, static_cast<DWORD>(error), 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
	std::string message = buffer ? buffer : "erro desconhecido";
	if(buffer)
		LocalFree(buffer);
	while(!message.empty() && (message.back() == '\n' || message.back() == '\r'))
		message.pop_back();
	return message;
#else
	return std::strerror(error);
#endif
}

} // namespace orbislink
