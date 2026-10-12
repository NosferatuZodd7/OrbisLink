// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/discovery.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/net/lan_scan.h"
#include "orbislink/stream/chiaki_log_bridge.h"
#include "orbislink/stream/stream_trace.h"

#include <chiaki/discovery.h>
#include <chiaki/thread.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace orbislink {

namespace {

HostState stateFrom(ChiakiDiscoveryHostState state)
{
	switch(state)
	{
		case CHIAKI_DISCOVERY_HOST_STATE_READY: return HostState::Ready;
		case CHIAKI_DISCOVERY_HOST_STATE_STANDBY: return HostState::Standby;
		case CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN: break;
	}
	return HostState::Unknown;
}

std::string safe(const char *text) { return text ? std::string(text) : std::string(); }

HostInfo fromChiaki(ChiakiDiscoveryHost *host)
{
	HostInfo info;
	info.found = true;
	info.state = stateFrom(host->state);
	info.ps5 = chiaki_discovery_host_is_ps5(host);
	info.address = safe(host->host_addr);
	info.name = safe(host->host_name);
	info.id = safe(host->host_id);
	info.systemVersion = safe(host->system_version);
	info.runningAppName = safe(host->running_app_name);
	info.runningAppTitleId = safe(host->running_app_titleid);
	info.requestPort = host->host_request_port;
	info.target = static_cast<int>(chiaki_discovery_host_system_version_target(host));
	return info;
}

// chiaki's callback runs on the discovery thread; whatever arrives here
// is stored, under a lock.
struct Harvest
{
	std::mutex mutex;
	std::vector<HostInfo> hosts;
	bool single = false;
};

void collect(ChiakiDiscoveryHost *host, void *user)
{
	auto *harvest = static_cast<Harvest *>(user);
	std::lock_guard<std::mutex> lock(harvest->mutex);
	HostInfo info = fromChiaki(host);
	for(HostInfo &existing : harvest->hosts)
	{
		// The same console answers more than one request.
		if(existing.id == info.id && !info.id.empty())
		{
			existing = info;
			return;
		}
	}
	harvest->hosts.push_back(info);
}

// Resolves an address to a sockaddr. Returns false if that is not possible.
bool resolve(const std::string &address, struct sockaddr_storage *out, socklen_t *outLen)
{
	struct addrinfo hints {};
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_family = address.find(':') != std::string::npos ? AF_INET6 : AF_INET;

	struct addrinfo *results = nullptr;
	if(getaddrinfo(address.c_str(), nullptr, &hints, &results) != 0 || !results)
		return false;

	bool ok = false;
	for(struct addrinfo *ai = results; ai; ai = ai->ai_next)
	{
		if(ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
			continue;
		std::memcpy(out, ai->ai_addr, ai->ai_addrlen);
		*outLen = static_cast<socklen_t>(ai->ai_addrlen);
		ok = true;
		break;
	}
	freeaddrinfo(results);
	return ok;
}

void setPort(struct sockaddr_storage *addr, uint16_t port)
{
	if(addr->ss_family == AF_INET)
		reinterpret_cast<struct sockaddr_in *>(addr)->sin_port = htons(port);
	else
		reinterpret_cast<struct sockaddr_in6 *>(addr)->sin6_port = htons(port);
}

// The query to the console, with or without an entry in the attempt trace.
HostInfo ask(const std::string &address, int timeoutMs, uint16_t ps4Port, bool trace)
{
	HostInfo empty;
	empty.address = address;
	if(address.empty())
		return empty;

	std::optional<StreamStep> step;
	if(trace)
	{
		StreamTrace::instance().addressIfUnset(address);
		step.emplace("discovery", address);
	}
	auto failed = [&step](const std::string &detail) {
		if(step)
			step->fail(detail);
	};

	struct sockaddr_storage addr {};
	socklen_t addrLen = 0;
	if(!resolve(address, &addr, &addrLen))
	{
		failed("could not resolve the address \"" + address + "\"");
		return empty;
	}

	ChiakiDiscovery discovery {};
	if(chiaki_discovery_init(&discovery, chiakiLog(), addr.ss_family) != CHIAKI_ERR_SUCCESS)
	{
		failed("could not open the discovery socket (ports 9303-9319 in use?)");
		return empty;
	}

	Harvest harvest;
	ChiakiDiscoveryThread thread {};
	if(chiaki_discovery_thread_start_oneshot(&thread, &discovery, collect, &harvest)
		!= CHIAKI_ERR_SUCCESS)
	{
		chiaki_discovery_fini(&discovery);
		return empty;
	}

	// Ask in both protocols: the same function serves PS4 and PS5.
	ChiakiDiscoveryPacket packet {};
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
	packet.protocol_version = const_cast<char *>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4);
	setPort(&addr, ps4Port != 0 ? ps4Port : CHIAKI_DISCOVERY_PORT_PS4);
	chiaki_discovery_send(&discovery, &packet, reinterpret_cast<struct sockaddr *>(&addr), addrLen);

	if(ps4Port == 0)
	{
		packet.protocol_version = const_cast<char *>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5);
		setPort(&addr, CHIAKI_DISCOVERY_PORT_PS5);
		chiaki_discovery_send(&discovery, &packet, reinterpret_cast<struct sockaddr *>(&addr),
			addrLen);
	}

	// chiaki's thread only ends by itself when it gets a reply; if the
	// time runs out, it is stopped by force.
	const ChiakiErrorCode joined = chiaki_thread_timedjoin(&thread.thread, nullptr,
		static_cast<uint64_t>(timeoutMs > 0 ? timeoutMs : 2000));
	if(joined != CHIAKI_ERR_SUCCESS)
		chiaki_discovery_thread_stop(&thread);
	chiaki_discovery_fini(&discovery);

	std::lock_guard<std::mutex> lock(harvest.mutex);
	if(harvest.hosts.empty())
	{
		failed("the console did not answer within " + std::to_string(timeoutMs)
			+ " ms (Remote Play turned off in the console settings? wrong IP? "
			  "another network?)");
		return empty;
	}
	HostInfo info = harvest.hosts.front();
	if(info.address.empty())
		info.address = address;
	if(step)
		step->ok(std::string(hostStateName(info.state)) + ", " + info.name + ", system "
			+ info.systemVersion + ", target " + std::to_string(info.target));
	return info;
}

} // namespace

HostInfo StreamDiscovery::probe(const std::string &address, int timeoutMs, uint16_t ps4Port)
{
	return ask(address, timeoutMs, ps4Port, true);
}

HostInfo StreamDiscovery::peek(const std::string &address, int timeoutMs, uint16_t ps4Port)
{
	return ask(address, timeoutMs, ps4Port, false);
}

std::vector<HostInfo> StreamDiscovery::scan(int timeoutMs)
{
	ChiakiDiscovery discovery {};
	if(chiaki_discovery_init(&discovery, chiakiLog(), AF_INET) != CHIAKI_ERR_SUCCESS)
		return {};

	Harvest harvest;
	ChiakiDiscoveryThread thread {};
	if(chiaki_discovery_thread_start(&thread, &discovery, collect, &harvest) != CHIAKI_ERR_SUCCESS)
	{
		chiaki_discovery_fini(&discovery);
		return {};
	}

	// Every broadcast address this PC has and, on a small home network,
	// every address of it (see net/lan_scan.h): a PS4 answers on 987, a
	// PS5 on 9302, each in its own protocol.
	const LanTargets targets = lanTargets(localInterfaces());
	auto ask = [&discovery](const std::string &address) {
		struct sockaddr_in to {};
		to.sin_family = AF_INET;
		if(inet_pton(AF_INET, address.c_str(), &to.sin_addr) != 1)
			return;
		ChiakiDiscoveryPacket packet {};
		packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
		packet.protocol_version = const_cast<char *>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4);
		to.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
		chiaki_discovery_send(&discovery, &packet, reinterpret_cast<struct sockaddr *>(&to), sizeof(to));
		packet.protocol_version = const_cast<char *>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5);
		to.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
		chiaki_discovery_send(&discovery, &packet, reinterpret_cast<struct sockaddr *>(&to), sizeof(to));
	};
	auto round = [&](bool everyHost) {
		for(const std::string &address : targets.broadcasts)
			ask(address);
		if(!everyHost)
			return;
		for(size_t i = 0; i < targets.hosts.size(); ++i)
		{
			ask(targets.hosts[i]);
			// A breath now and then: a burst of hundreds can overflow a
			// Wi-Fi adapter's queue.
			if(i % 32 == 31)
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	};

	// UDP can be lost and a console just woken can be slow: three rounds
	// over the time given, the whole network asked in the first and last.
	const int total = timeoutMs > 0 ? timeoutMs : 3000;
	const auto start = std::chrono::steady_clock::now();
	const auto at = [start, total](int third) { return start + std::chrono::milliseconds(total * third / 3); };
	logInfo("Remote Play: searching the network (" + std::to_string(targets.broadcasts.size()) + " broadcast addresses, "
		+ std::to_string(targets.hosts.size()) + " hosts)");
	round(true);
	std::this_thread::sleep_until(at(1));
	round(false);
	std::this_thread::sleep_until(at(2));
	round(true);
	std::this_thread::sleep_until(at(3));
	chiaki_discovery_thread_stop(&thread);
	chiaki_discovery_fini(&discovery);

	std::lock_guard<std::mutex> lock(harvest.mutex);
	return harvest.hosts;
}

bool StreamDiscovery::wakeup(const std::string &address, uint64_t credential, bool ps5,
	std::string *error)
{
	if(address.empty() || credential == 0)
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The address is missing or the console has not been "
				"registered yet.");
		return false;
	}
	const ChiakiErrorCode result =
		chiaki_discovery_wakeup(chiakiLog(), nullptr, address.c_str(), credential, ps5);
	if(result != CHIAKI_ERR_SUCCESS)
	{
		if(error)
			*error = chiaki_error_string(result);
		return false;
	}
	logInfo("Remote Play: wake-up request sent to " + address);
	return true;
}

} // namespace orbislink
