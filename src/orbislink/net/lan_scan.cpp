// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/net/lan_scan.h"

#include "orbislink/net/socket_compat.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>

#ifndef _WIN32
#include <poll.h>
#endif

namespace orbislink {

namespace {

std::string ipv4Text(uint32_t hostOrder)
{
	char text[INET_ADDRSTRLEN] = { 0 };
	struct in_addr addr {};
	addr.s_addr = htonl(hostOrder);
	inet_ntop(AF_INET, &addr, text, sizeof(text));
	return text;
}

int prefixLength(uint32_t mask)
{
	int bits = 0;
	while(bits < 32 && (mask & (0x80000000u >> bits)))
		++bits;
	return bits;
}

#ifdef _WIN32
using PollFd = WSAPOLLFD;
int pollSockets(PollFd *fds, size_t count, int timeoutMs)
{
	return WSAPoll(fds, static_cast<ULONG>(count), timeoutMs);
}
#else
using PollFd = struct pollfd;
int pollSockets(PollFd *fds, size_t count, int timeoutMs)
{
	return ::poll(fds, static_cast<nfds_t>(count), timeoutMs);
}
#endif

bool connectionUp(socket_t sock)
{
	int error = 0;
#ifdef _WIN32
	int length = sizeof(error);
	if(getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length) != 0)
		return false;
#else
	socklen_t length = sizeof(error);
	if(getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &length) != 0)
		return false;
#endif
	return error == 0;
}

} // namespace

bool privateIPv4(uint32_t ip)
{
	return (ip >> 24) == 10 || (ip >> 20) == ((172u << 4) | 1u) || (ip >> 16) == ((192u << 8) | 168u);
}

LanTargets lanTargets(const std::vector<LocalInterface> &interfaces, size_t maxHosts)
{
	LanTargets targets;
	std::set<std::string> seenBroadcasts;
	std::set<uint32_t> seenHosts;
	std::set<uint32_t> own;
	targets.broadcasts.push_back("255.255.255.255");
	seenBroadcasts.insert("255.255.255.255");
	for(const LocalInterface &iface : interfaces)
	{
		uint32_t address = 0;
		if(!iface.loopback && parseIPv4(iface.address, &address))
			own.insert(address);
	}
	for(const LocalInterface &iface : interfaces)
	{
		uint32_t address = 0;
		uint32_t mask = 0;
		if(iface.loopback || !parseIPv4(iface.address, &address) || !parseIPv4(iface.netmask, &mask))
			continue;
		// No address yet, or a link-local one the PC gave itself.
		if(address == 0 || (address >> 16) == ((169u << 8) | 254u))
			continue;
		const int prefix = prefixLength(mask);
		if(prefix == 0 || prefix >= 31)
			continue;
		const uint32_t network = address & mask;
		const uint32_t broadcast = network | ~mask;
		const std::string text = ipv4Text(broadcast);
		if(seenBroadcasts.insert(text).second)
			targets.broadcasts.push_back(text);
		if(prefix < 23 || !privateIPv4(address))
			continue;
		for(uint32_t host = network + 1; host < broadcast && targets.hosts.size() < maxHosts; ++host)
			if(!own.count(host) && seenHosts.insert(host).second)
				targets.hosts.push_back(ipv4Text(host));
	}
	return targets;
}

std::vector<std::string> hostsWithPort(const std::vector<std::string> &hosts, uint16_t port, int timeoutMs,
	const std::atomic<bool> *cancel, size_t batch)
{
	initSocketsOnce();
	std::vector<std::string> open;
	batch = std::max<size_t>(1, batch);
	for(size_t start = 0; start < hosts.size(); start += batch)
	{
		if(cancel && cancel->load())
			break;
		const size_t end = std::min(hosts.size(), start + batch);
		std::vector<PollFd> fds;
		std::vector<std::string> names;
		for(size_t i = start; i < end; ++i)
		{
			struct sockaddr_in addr {};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			if(inet_pton(AF_INET, hosts[i].c_str(), &addr.sin_addr) != 1)
				continue;
			const socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(sock == ORBISLINK_INVALID_SOCKET)
				continue;
			setSocketNonBlocking(sock, true);
			const int rc = connect(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
			PollFd fd {};
			fd.fd = sock;
			fd.events = POLLOUT;
			if(rc == 0)
			{
				open.push_back(hosts[i]);
				closeSocketHandle(sock);
				continue;
			}
			fds.push_back(fd);
			names.push_back(hosts[i]);
		}
		// Wait for the connections, until each has answered or the time is up.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		std::vector<bool> done(fds.size(), false);
		size_t pending = fds.size();
		while(pending > 0 && !(cancel && cancel->load()))
		{
			const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - std::chrono::steady_clock::now()).count();
			if(left <= 0)
				break;
			std::vector<PollFd> waiting;
			std::vector<size_t> which;
			for(size_t i = 0; i < fds.size(); ++i)
				if(!done[i])
				{
					PollFd fd = fds[i];
					fd.revents = 0;
					waiting.push_back(fd);
					which.push_back(i);
				}
			const int ready = pollSockets(waiting.data(), waiting.size(), static_cast<int>(std::min<long long>(left, 100)));
			if(ready < 0)
				break;
			for(size_t k = 0; k < waiting.size(); ++k)
			{
				if(!waiting[k].revents)
					continue;
				const size_t i = which[k];
				done[i] = true;
				--pending;
				if((waiting[k].revents & POLLOUT) && !(waiting[k].revents & (POLLERR | POLLHUP))
					&& connectionUp(static_cast<socket_t>(fds[i].fd)))
					open.push_back(names[i]);
			}
		}
		for(const PollFd &fd : fds)
			closeSocketHandle(static_cast<socket_t>(fd.fd));
	}
	return open;
}

} // namespace orbislink
