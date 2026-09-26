// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/discovery.h"

#include "orbislink/common/log.h"
#include "orbislink/stream/chiaki_log_bridge.h"
#include "orbislink/stream/stream_trace.h"

#include <chiaki/discovery.h>
#include <chiaki/thread.h>

#include <cstring>
#include <mutex>
#include <optional>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
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

// O callback do chiaki corre na thread da descoberta; guarda-se o que
// chegar aqui, protegido.
struct Colheita
{
	std::mutex mutex;
	std::vector<HostInfo> hosts;
	bool único = false;
};

void colher(ChiakiDiscoveryHost *host, void *user)
{
	auto *colheita = static_cast<Colheita *>(user);
	std::lock_guard<std::mutex> lock(colheita->mutex);
	HostInfo info = fromChiaki(host);
	for(HostInfo &existente : colheita->hosts)
	{
		// A mesma consola responde a mais do que um pedido.
		if(existente.id == info.id && !info.id.empty())
		{
			existente = info;
			return;
		}
	}
	colheita->hosts.push_back(info);
}

// Resolve um endereço para sockaddr. Devolve false se não for possível.
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

// A pergunta à consola, com ou sem registo no diário da tentativa.
HostInfo perguntar(const std::string &address, int timeoutMs, uint16_t ps4Port, bool diario)
{
	HostInfo vazio;
	vazio.address = address;
	if(address.empty())
		return vazio;

	std::optional<StreamStep> passo;
	if(diario)
	{
		StreamTrace::instance().addressIfUnset(address);
		passo.emplace("descoberta", address);
	}
	auto falhar = [&passo](const std::string &detalhe) {
		if(passo)
			passo->fail(detalhe);
	};

	struct sockaddr_storage addr {};
	socklen_t addrLen = 0;
	if(!resolve(address, &addr, &addrLen))
	{
		falhar("não consegui resolver o endereço \"" + address + "\"");
		return vazio;
	}

	ChiakiDiscovery discovery {};
	if(chiaki_discovery_init(&discovery, chiakiLog(), addr.ss_family) != CHIAKI_ERR_SUCCESS)
	{
		falhar("não consegui abrir o socket de descoberta (portas 9303-9319 ocupadas?)");
		return vazio;
	}

	Colheita colheita;
	ChiakiDiscoveryThread thread {};
	if(chiaki_discovery_thread_start_oneshot(&thread, &discovery, colher, &colheita)
		!= CHIAKI_ERR_SUCCESS)
	{
		chiaki_discovery_fini(&discovery);
		return vazio;
	}

	// Pergunta-se nos dois protocolos: a mesma função serve PS4 e PS5.
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

	// O thread do chiaki só termina sozinho quando recebe resposta; se
	// passar o tempo, pára-se à força.
	const ChiakiErrorCode joined = chiaki_thread_timedjoin(&thread.thread, nullptr,
		static_cast<uint64_t>(timeoutMs > 0 ? timeoutMs : 2000));
	if(joined != CHIAKI_ERR_SUCCESS)
		chiaki_discovery_thread_stop(&thread);
	chiaki_discovery_fini(&discovery);

	std::lock_guard<std::mutex> lock(colheita.mutex);
	if(colheita.hosts.empty())
	{
		falhar("a consola não respondeu em " + std::to_string(timeoutMs)
			+ " ms (Remote Play desligado nas definições da consola? IP errado? "
			  "outra rede?)");
		return vazio;
	}
	HostInfo info = colheita.hosts.front();
	if(info.address.empty())
		info.address = address;
	if(passo)
		passo->ok(std::string(hostStateName(info.state)) + ", " + info.name + ", sistema "
			+ info.systemVersion + ", alvo " + std::to_string(info.target));
	return info;
}

} // namespace

HostInfo StreamDiscovery::probe(const std::string &address, int timeoutMs, uint16_t ps4Port)
{
	return perguntar(address, timeoutMs, ps4Port, true);
}

HostInfo StreamDiscovery::peek(const std::string &address, int timeoutMs, uint16_t ps4Port)
{
	return perguntar(address, timeoutMs, ps4Port, false);
}

std::vector<HostInfo> StreamDiscovery::scan(int timeoutMs)
{
	ChiakiDiscovery discovery {};
	if(chiaki_discovery_init(&discovery, chiakiLog(), AF_INET) != CHIAKI_ERR_SUCCESS)
		return {};

	Colheita colheita;
	ChiakiDiscoveryThread thread {};
	if(chiaki_discovery_thread_start(&thread, &discovery, colher, &colheita) != CHIAKI_ERR_SUCCESS)
	{
		chiaki_discovery_fini(&discovery);
		return {};
	}

	struct sockaddr_in broadcast {};
	broadcast.sin_family = AF_INET;
	broadcast.sin_addr.s_addr = INADDR_BROADCAST;

	ChiakiDiscoveryPacket packet {};
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
	packet.protocol_version = const_cast<char *>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4);
	broadcast.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
	chiaki_discovery_send(&discovery, &packet, reinterpret_cast<struct sockaddr *>(&broadcast),
		sizeof(broadcast));

	packet.protocol_version = const_cast<char *>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5);
	broadcast.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
	chiaki_discovery_send(&discovery, &packet, reinterpret_cast<struct sockaddr *>(&broadcast),
		sizeof(broadcast));

	// Aqui não se espera pelo fim do thread: ele fica à escuta. Dá-se tempo
	// às consolas para responderem e depois pára-se.
	chiaki_thread_timedjoin(&thread.thread, nullptr,
		static_cast<uint64_t>(timeoutMs > 0 ? timeoutMs : 3000));
	chiaki_discovery_thread_stop(&thread);
	chiaki_discovery_fini(&discovery);

	std::lock_guard<std::mutex> lock(colheita.mutex);
	return colheita.hosts;
}

bool StreamDiscovery::wakeup(const std::string &address, uint64_t credential, bool ps5,
	std::string *error)
{
	if(address.empty() || credential == 0)
	{
		if(error)
			*error = "Falta o endereço ou a consola ainda não foi registada.";
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
	logInfo("Remote Play: pedido de acordar enviado para " + address);
	return true;
}

} // namespace orbislink
