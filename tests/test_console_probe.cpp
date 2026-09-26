// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A verificação automática do endereço nas definições assenta nesta função.
// Aqui confirma-se contra servidores falsos: FTP a responder com o banner do
// GoldHEN e instalador remoto a responder HTTP, cada um podendo faltar.

#include "orbislink/console/console_manager.h"
#include "orbislink/net/socket_compat.h"
#include "test_support.h"

#include <atomic>
#include <string>
#include <thread>

using namespace orbislink;

namespace {

// Servidor TCP mínimo: aceita uma ligação, envia `greeting` e fecha. O FTP
// cumprimenta primeiro; o instalador só responde depois de ler o pedido HTTP.
class FakeService
{
public:
	explicit FakeService(std::string greeting, bool waitForRequest = false)
		: greeting_(std::move(greeting)), waitForRequest_(waitForRequest)
	{
		initSocketsOnce();
	}
	~FakeService() { stop(); }

	bool start()
	{
		listen_ = socket(AF_INET, SOCK_STREAM, 0);
		if(listen_ == ORBISLINK_INVALID_SOCKET)
			return false;
		int reuse = 1;
		setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse),
			sizeof(reuse));
		struct sockaddr_in addr {};
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
		if(bind(listen_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0)
			return false;
		if(::listen(listen_, 4) != 0)
			return false;
		socklen_t length = sizeof(addr);
		getsockname(listen_, reinterpret_cast<struct sockaddr *>(&addr), &length);
		port_ = ntohs(addr.sin_port);
		running_.store(true);
		thread_ = std::thread(&FakeService::loop, this);
		return true;
	}

	void stop()
	{
		if(!running_.exchange(false))
			return;
		socket_t waker = socket(AF_INET, SOCK_STREAM, 0);
		if(waker != ORBISLINK_INVALID_SOCKET)
		{
			struct sockaddr_in addr {};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port_);
			inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
			connect(waker, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
			closeSocketHandle(waker);
		}
		if(thread_.joinable())
			thread_.join();
		closeSocketHandle(listen_);
		listen_ = ORBISLINK_INVALID_SOCKET;
	}

	uint16_t port() const { return port_; }

private:
	void loop()
	{
		while(running_.load())
		{
			socket_t client = accept(listen_, nullptr, nullptr);
			if(client == ORBISLINK_INVALID_SOCKET)
				continue;
			if(!running_.load())
			{
				closeSocketHandle(client);
				break;
			}
			if(!greeting_.empty())
			{
				if(waitForRequest_)
				{
					char scratch[2048];
					recv(client, scratch, sizeof(scratch), 0);
				}
				send(client, greeting_.c_str(), static_cast<int>(greeting_.size()), 0);
			}
			closeSocketHandle(client);
		}
	}

	std::string greeting_;
	bool waitForRequest_ = false;
	socket_t listen_ = ORBISLINK_INVALID_SOCKET;
	uint16_t port_ = 0;
	std::atomic<bool> running_ { false };
	std::thread thread_;
};

// Uma porta que ninguém está a ouvir: abre-se e fecha-se um socket para
// ficar com um número que o sistema acabou de libertar.
uint16_t closedPort()
{
	FakeService service("");
	service.start();
	const uint16_t port = service.port();
	service.stop();
	return port;
}

const char *kHttpOk = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";

} // namespace

ORBISLINK_TEST(devolve_os_dois_servicos_quando_ambos_respondem)
{
	FakeService ftp("220 GoldHEN FTP Server\r\n");
	FakeService installer(kHttpOk, true);
	CHECK(ftp.start());
	CHECK(installer.start());

	const ProbeResult result =
		probeConsoleServices("127.0.0.1", ftp.port(), installer.port(), 1500);

	CHECK(result.ftpOk);
	CHECK(result.installerOk);
	CHECK(result.allOk());
	CHECK(result.ftpDetail.find("220") != std::string::npos);
}

ORBISLINK_TEST(distingue_o_caso_de_so_um_servico_responder)
{
	FakeService ftp("220 GoldHEN FTP Server\r\n");
	CHECK(ftp.start());
	const uint16_t dead = closedPort();

	const ProbeResult result = probeConsoleServices("127.0.0.1", ftp.port(), dead, 1500);

	CHECK(result.ftpOk);
	CHECK(!result.installerOk);
	CHECK(result.anyOk());
	CHECK(!result.allOk());
}

ORBISLINK_TEST(falha_sem_ninguem_do_outro_lado)
{
	const uint16_t deadFtp = closedPort();
	const uint16_t deadInstaller = closedPort();

	const ProbeResult result =
		probeConsoleServices("127.0.0.1", deadFtp, deadInstaller, 1000);

	CHECK(!result.ftpOk);
	CHECK(!result.installerOk);
	CHECK(!result.anyOk());
}

ORBISLINK_TEST(endereco_vazio_nao_vai_a_rede)
{
	const ProbeResult result = probeConsoleServices("   ", 2121, 12800, 1000);

	CHECK(!result.anyOk());
	CHECK(result.ftpDetail == "sem endereço de consola");
	CHECK(result.installerDetail == "sem endereço de consola");
}

TEST_MAIN()
