// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/net/payload_sender.h"

#include "orbislink/common/tr.h"
#include "orbislink/net/socket_compat.h"

#include <chrono>

namespace orbislink {

namespace {

inline int selectNfds(socket_t sock)
{
#ifdef _WIN32
	(void)sock;
	return 0;
#else
	return static_cast<int>(sock) + 1;
#endif
}

// Connected and back in blocking mode, or ORBISLINK_INVALID_SOCKET.
socket_t connectWithTimeout(const std::string &host, uint16_t port, int timeoutMs)
{
	struct addrinfo hints {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo *info = nullptr;
	const std::string portText = std::to_string(port);
	if(getaddrinfo(host.c_str(), portText.c_str(), &hints, &info) != 0 || !info)
		return ORBISLINK_INVALID_SOCKET;

	socket_t connected = ORBISLINK_INVALID_SOCKET;
	for(struct addrinfo *it = info; it && connected == ORBISLINK_INVALID_SOCKET; it = it->ai_next)
	{
		socket_t sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if(sock == ORBISLINK_INVALID_SOCKET)
			continue;
		setSocketNonBlocking(sock, true);
		bool ok = connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0;
		if(!ok)
		{
			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(sock, &writeSet);
			struct timeval tv {};
			tv.tv_sec = timeoutMs / 1000;
			tv.tv_usec = (timeoutMs % 1000) * 1000;
			if(select(selectNfds(sock), nullptr, &writeSet, nullptr, &tv) > 0)
			{
				int soError = 0;
#ifdef _WIN32
				int len = sizeof(soError);
				getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soError), &len);
#else
				socklen_t len = sizeof(soError);
				getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &len);
#endif
				ok = soError == 0;
			}
		}
		if(ok)
		{
			setSocketNonBlocking(sock, false);
			connected = sock;
		}
		else
			closeSocketHandle(sock);
	}
	freeaddrinfo(info);
	return connected;
}

bool sendAll(socket_t sock, const uint8_t *data, size_t size)
{
	size_t sent = 0;
	while(sent < size)
	{
		const size_t chunk = size - sent < 65536 ? size - sent : 65536;
#ifdef MSG_NOSIGNAL
		// A console that drops the connection must not take the app with it.
		const int flags = MSG_NOSIGNAL;
#else
		const int flags = 0;
#endif
		const auto n = ::send(sock, reinterpret_cast<const char *>(data + sent), static_cast<int>(chunk), flags);
		if(n <= 0)
			return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

} // namespace

PayloadSender::Result PayloadSender::send(const std::string &host, uint16_t port,
	const std::vector<uint8_t> &payload, const Options &options)
{
	initSocketsOnce();
	Result result;
	if(payload.empty())
	{
		result.error = QT_TRANSLATE_NOOP("Messages", "The payload file is empty.");
		return result;
	}

	const socket_t sock = connectWithTimeout(host, port, options.connectTimeoutMs);
	if(sock == ORBISLINK_INVALID_SOCKET)
	{
		result.error = QT_TRANSLATE_NOOP("Messages", "The console's payload loader did not answer.");
		return result;
	}
	if(!sendAll(sock, payload.data(), payload.size()))
	{
		closeSocketHandle(sock);
		result.error = QT_TRANSLATE_NOOP("Messages", "The connection dropped while sending the payload.");
		return result;
	}
	result.sent = true;

	// The loader knows the payload's size from its own headers, so the
	// connection stays open both ways: what comes back is the payload talking.
	using Clock = std::chrono::steady_clock;
	const auto deadline = Clock::now() + std::chrono::milliseconds(options.listenMs);
	std::string pending;
	bool listening = options.listenMs > 0;
	while(listening && Clock::now() < deadline)
	{
		if(options.cancel && options.cancel->load())
			break;
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(sock, &readSet);
		struct timeval tv {};
		tv.tv_usec = 250 * 1000;
		const int ready = select(selectNfds(sock), &readSet, nullptr, nullptr, &tv);
		if(ready < 0)
			break;
		if(ready == 0)
			continue;
		char buffer[1024];
		const auto n = recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0);
		if(n <= 0)
		{
			result.closedByConsole = true;
			break;
		}
		result.output.append(buffer, static_cast<size_t>(n));
		pending.append(buffer, static_cast<size_t>(n));
		size_t end;
		while((end = pending.find('\n')) != std::string::npos)
		{
			std::string line = pending.substr(0, end);
			pending.erase(0, end + 1);
			while(!line.empty() && (line.back() == '\r' || line.back() == '\0'))
				line.pop_back();
			if(options.onLine && !options.onLine(line))
			{
				listening = false;
				break;
			}
		}
	}
	if(listening && !pending.empty() && options.onLine)
		options.onLine(pending);
	closeSocketHandle(sock);
	return result;
}

} // namespace orbislink
