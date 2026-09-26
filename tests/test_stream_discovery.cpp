// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Descoberta de consolas sem ter uma PS4 à mão: um servidor UDP responde
// exactamente como a consola responde ao pedido SRCH (uma resposta no
// formato HTTP, ver lib/src/discovery.c do chiaki-ng), e verifica-se que o
// que sai do StreamDiscovery é o que lá estava.

#include "orbislink/net/socket_compat.h"
#include "orbislink/stream/discovery.h"
#include "orbislink/stream/stream_trace.h"
#include "test_support.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

using namespace orbislink;

namespace {

// Consola falsa: escuta em UDP e responde a quem lhe perguntar.
class FakeConsole
{
public:
	explicit FakeConsole(std::string response) : response_(std::move(response))
	{
		initSocketsOnce();
	}
	~FakeConsole() { stop(); }

	bool start()
	{
		socket_ = socket(AF_INET, SOCK_DGRAM, 0);
		if(socket_ == ORBISLINK_INVALID_SOCKET)
			return false;
		struct sockaddr_in addr {};
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
		if(bind(socket_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0)
			return false;
		socklen_t length = sizeof(addr);
		getsockname(socket_, reinterpret_cast<struct sockaddr *>(&addr), &length);
		port_ = ntohs(addr.sin_port);
		running_.store(true);
		thread_ = std::thread(&FakeConsole::loop, this);
		return true;
	}

	void stop()
	{
		if(!running_.exchange(false))
			return;
		// Acorda o recvfrom com um datagrama para si própria.
		socket_t waker = socket(AF_INET, SOCK_DGRAM, 0);
		if(waker != ORBISLINK_INVALID_SOCKET)
		{
			struct sockaddr_in addr {};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port_);
			inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
			const char nada = 0;
			sendto(waker, &nada, 1, 0, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
			closeSocketHandle(waker);
		}
		if(thread_.joinable())
			thread_.join();
		closeSocketHandle(socket_);
		socket_ = ORBISLINK_INVALID_SOCKET;
	}

	uint16_t port() const { return port_; }
	std::string lastRequest() const { return lastRequest_; }

private:
	void loop()
	{
		while(running_.load())
		{
			char buffer[2048];
			struct sockaddr_in from {};
			socklen_t fromLen = sizeof(from);
			const int n = recvfrom(socket_, buffer, sizeof(buffer) - 1, 0,
				reinterpret_cast<struct sockaddr *>(&from), &fromLen);
			if(n <= 1 || !running_.load())
				continue;
			buffer[n] = '\0';
			lastRequest_ = std::string(buffer, static_cast<size_t>(n));
			sendto(socket_, response_.c_str(), static_cast<int>(response_.size()), 0,
				reinterpret_cast<struct sockaddr *>(&from), fromLen);
		}
	}

	std::string response_;
	socket_t socket_ = ORBISLINK_INVALID_SOCKET;
	uint16_t port_ = 0;
	std::atomic<bool> running_ { false };
	std::thread thread_;
	std::string lastRequest_;
};

// Resposta de uma consola ligada, com um jogo a correr.
const char *kPronta =
	"HTTP/1.1 200 Ok\r\n"
	"host-id:1122334455AA\r\n"
	"host-type:PS4\r\n"
	"host-name:PS4 da sala\r\n"
	"host-request-port:997\r\n"
	"device-discovery-protocol-version:00020020\r\n"
	"system-version:09000000\r\n"
	"running-app-name:Bloodborne\r\n"
	"running-app-titleid:CUSA00207\r\n";

// Consola em repouso: mesmo formato, código 620.
const char *kEmRepouso =
	"HTTP/1.1 620 Server Standby\r\n"
	"host-id:1122334455AA\r\n"
	"host-type:PS4\r\n"
	"host-name:PS4 da sala\r\n"
	"host-request-port:997\r\n"
	"device-discovery-protocol-version:00020020\r\n"
	"system-version:09000000\r\n";

} // namespace

ORBISLINK_TEST(le_uma_consola_pronta)
{
	FakeConsole console(kPronta);
	CHECK(console.start());

	const HostInfo info = StreamDiscovery::probe("127.0.0.1", 2000, console.port());

	CHECK(info.found);
	CHECK(info.state == HostState::Ready);
	CHECK(!info.ps5);
	CHECK_EQ(info.name, std::string("PS4 da sala"));
	CHECK_EQ(info.id, std::string("1122334455AA"));
	CHECK_EQ(info.systemVersion, std::string("09000000"));
	CHECK_EQ(info.runningAppName, std::string("Bloodborne"));
	CHECK_EQ(info.runningAppTitleId, std::string("CUSA00207"));
	CHECK_EQ(int(info.requestPort), 997);
	// O alvo tem de sair da versão de sistema, senão o chiaki fala o
	// protocolo errado com a consola.
	CHECK(info.target != 0);
}

ORBISLINK_TEST(distingue_consola_em_repouso)
{
	FakeConsole console(kEmRepouso);
	CHECK(console.start());

	const HostInfo info = StreamDiscovery::probe("127.0.0.1", 2000, console.port());

	CHECK(info.found);
	CHECK(info.state == HostState::Standby);
	CHECK(info.runningAppName.empty());
}

ORBISLINK_TEST(envia_um_pedido_de_procura)
{
	FakeConsole console(kPronta);
	CHECK(console.start());
	StreamDiscovery::probe("127.0.0.1", 2000, console.port());

	// O que a consola recebeu tem de ser mesmo um SRCH do protocolo do PS4.
	const std::string pedido = console.lastRequest();
	CHECK(pedido.find("SRCH") != std::string::npos);
	CHECK(pedido.find("device-discovery-protocol-version:00020020") != std::string::npos);
}

ORBISLINK_TEST(sem_ninguem_a_responder_nao_inventa_consola)
{
	FakeConsole console(kPronta);
	CHECK(console.start());
	const uint16_t porta = console.port();
	console.stop();

	const HostInfo info = StreamDiscovery::probe("127.0.0.1", 600, porta);

	CHECK(!info.found);
	CHECK(info.state == HostState::Unknown);
}

ORBISLINK_TEST(verificacao_periodica_nao_mexe_na_tentativa)
{
	FakeConsole console(kPronta);
	CHECK(console.start());

	// Uma tentativa a meio: a verificação periódica das outras consolas
	// não pode fechar este passo nem acrescentar os seus.
	StreamTrace::instance().begin("192.0.2.1");
	StreamTrace::instance().step("primeiro fotograma");
	const HostInfo info = StreamDiscovery::peek("127.0.0.1", 2000, console.port());

	CHECK(info.found);
	const auto passos = StreamTrace::instance().steps();
	CHECK_EQ(passos.size(), std::size_t(1));
	CHECK_EQ(passos.front().name, std::string("primeiro fotograma"));
	CHECK(passos.front().result == StreamTrace::Result::Running);
	StreamTrace::instance().end();
}

ORBISLINK_TEST(acordar_sem_credencial_recusa_em_vez_de_enviar_lixo)
{
	std::string erro;
	CHECK(!StreamDiscovery::wakeup("127.0.0.1", 0, false, &erro));
	CHECK(!erro.empty());
}

TEST_MAIN()
