// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/net/net_utils.h"

#include "orbislink/common/log.h"
#include "orbislink/net/socket_compat.h"

#include <cstring>
#include <vector>

namespace orbislink {

namespace {

// No Windows o primeiro argumento de select() é ignorado e SOCKET é um
// inteiro de 64 bits, por isso não se converte para int.
inline int selectNfds(socket_t sock)
{
#ifdef _WIN32
	(void)sock;
	return 0;
#else
	return static_cast<int>(sock) + 1;
#endif
}

std::string ipv4ToString(uint32_t networkOrder)
{
	char buf[INET_ADDRSTRLEN] = { 0 };
	struct in_addr addr {};
	addr.s_addr = networkOrder;
	inet_ntop(AF_INET, &addr, buf, sizeof(buf));
	return buf;
}

} // namespace

std::vector<LocalInterface> localInterfaces()
{
	initSocketsOnce();
	std::vector<LocalInterface> result;

#ifdef _WIN32
	ULONG size = 16 * 1024;
	std::vector<uint8_t> buffer(size);
	ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
			| GAA_FLAG_SKIP_DNS_SERVER,
		nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
	if(ret == ERROR_BUFFER_OVERFLOW)
	{
		buffer.resize(size);
		ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
				| GAA_FLAG_SKIP_DNS_SERVER,
			nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
	}
	if(ret != NO_ERROR)
		return result;

	for(auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter;
		adapter = adapter->Next)
	{
		if(adapter->OperStatus != IfOperStatusUp)
			continue;
		for(auto *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
		{
			if(!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
				continue;
			auto *sin = reinterpret_cast<sockaddr_in *>(unicast->Address.lpSockaddr);
			LocalInterface iface;
			iface.name = adapter->AdapterName ? adapter->AdapterName : "";
			iface.address = ipv4ToString(sin->sin_addr.s_addr);
			const ULONG prefix = unicast->OnLinkPrefixLength;
			const uint32_t mask = prefix == 0 ? 0 : htonl(0xFFFFFFFFu << (32 - prefix));
			iface.netmask = ipv4ToString(mask);
			iface.loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
			result.push_back(iface);
		}
	}
#else
	struct ifaddrs *addresses = nullptr;
	if(getifaddrs(&addresses) != 0)
		return result;
	for(struct ifaddrs *it = addresses; it; it = it->ifa_next)
	{
		if(!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
			continue;
		if(!(it->ifa_flags & IFF_UP))
			continue;
		LocalInterface iface;
		iface.name = it->ifa_name ? it->ifa_name : "";
		iface.address = ipv4ToString(reinterpret_cast<sockaddr_in *>(it->ifa_addr)->sin_addr.s_addr);
		iface.netmask = it->ifa_netmask
			? ipv4ToString(reinterpret_cast<sockaddr_in *>(it->ifa_netmask)->sin_addr.s_addr)
			: "255.255.255.0";
		iface.loopback = (it->ifa_flags & IFF_LOOPBACK) != 0;
		result.push_back(iface);
	}
	freeifaddrs(addresses);
#endif
	return result;
}

bool parseIPv4(const std::string &text, uint32_t *outHostOrder)
{
	struct in_addr addr {};
	if(inet_pton(AF_INET, text.c_str(), &addr) != 1)
		return false;
	if(outHostOrder)
		*outHostOrder = ntohl(addr.s_addr);
	return true;
}

bool sameSubnet(const std::string &a, const std::string &b, const std::string &netmask)
{
	uint32_t ia = 0, ib = 0, im = 0;
	if(!parseIPv4(a, &ia) || !parseIPv4(b, &ib) || !parseIPv4(netmask, &im))
		return false;
	return (ia & im) == (ib & im);
}

std::string localAddressForConsole(const std::string &consoleIp)
{
	const auto interfaces = localInterfaces();
	for(const auto &iface : interfaces)
	{
		if(iface.loopback)
			continue;
		if(!consoleIp.empty() && sameSubnet(iface.address, consoleIp, iface.netmask))
			return iface.address;
	}
	for(const auto &iface : interfaces)
	{
		if(!iface.loopback)
			return iface.address;
	}
	return std::string();
}

bool tcpProbe(const std::string &host, uint16_t port, int timeoutMs, std::string *banner)
{
	initSocketsOnce();
	if(banner)
		banner->clear();

	struct addrinfo hints {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo *info = nullptr;
	const std::string portText = std::to_string(port);
	if(getaddrinfo(host.c_str(), portText.c_str(), &hints, &info) != 0 || !info)
		return false;

	bool connected = false;
	socket_t sock = ORBISLINK_INVALID_SOCKET;
	for(struct addrinfo *it = info; it && !connected; it = it->ai_next)
	{
		sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if(sock == ORBISLINK_INVALID_SOCKET)
			continue;
		setSocketNonBlocking(sock, true);
		const int rc = connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen));
		if(rc == 0)
			connected = true;
		else
		{
			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(sock, &writeSet);
			struct timeval tv {};
			tv.tv_sec = timeoutMs / 1000;
			tv.tv_usec = (timeoutMs % 1000) * 1000;
			const int ready = select(selectNfds(sock), nullptr, &writeSet, nullptr, &tv);
			if(ready > 0)
			{
				int soError = 0;
#ifdef _WIN32
				int len = sizeof(soError);
				getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soError), &len);
#else
				socklen_t len = sizeof(soError);
				getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &len);
#endif
				connected = soError == 0;
			}
		}
		if(!connected)
		{
			closeSocketHandle(sock);
			sock = ORBISLINK_INVALID_SOCKET;
		}
	}
	freeaddrinfo(info);

	if(connected && banner)
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(sock, &readSet);
		struct timeval tv {};
		tv.tv_sec = timeoutMs / 1000;
		tv.tv_usec = (timeoutMs % 1000) * 1000;
		if(select(selectNfds(sock), &readSet, nullptr, nullptr, &tv) > 0)
		{
			char buffer[256] = { 0 };
			const int received = static_cast<int>(recv(sock, buffer, sizeof(buffer) - 1, 0));
			if(received > 0)
			{
				banner->assign(buffer, static_cast<size_t>(received));
				const size_t newline = banner->find_first_of("\r\n");
				if(newline != std::string::npos)
					banner->resize(newline);
			}
		}
	}

	if(sock != ORBISLINK_INVALID_SOCKET)
		closeSocketHandle(sock);
	return connected;
}

bool findFreePort(const std::string &bindAddress, uint16_t preferred, uint16_t *outPort,
	int maxAttempts)
{
	initSocketsOnce();
	for(int attempt = 0; attempt < maxAttempts; ++attempt)
	{
		const uint16_t port = static_cast<uint16_t>(preferred + attempt);
		if(port == 0)
			continue;
		socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
		if(sock == ORBISLINK_INVALID_SOCKET)
			return false;
		int reuse = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse),
			sizeof(reuse));
		struct sockaddr_in addr {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		if(bindAddress.empty() || bindAddress == "0.0.0.0")
			addr.sin_addr.s_addr = htonl(INADDR_ANY);
		else if(inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1)
		{
			closeSocketHandle(sock);
			return false;
		}
		const bool ok = bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0;
		closeSocketHandle(sock);
		if(ok)
		{
			if(outPort)
				*outPort = port;
			return true;
		}
	}
	return false;
}

} // namespace orbislink
