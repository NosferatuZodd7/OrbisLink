// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Verifica o formato dos pedidos e das respostas da API do Remote Package
// Installer sem precisar da consola: o servidor falso responde exatamente
// como o server.c do instalador (incluindo os números em hexadecimal).

#include "orbislink/common/json.h"
#include "orbislink/installer/error_codes.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/net/socket_compat.h"
#include "test_support.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace orbislink;

namespace {

// Servidor HTTP mínimo que responde a POSTs com uma resposta fixa e guarda o
// último pedido recebido.
class FakeInstallerServer
{
public:
	FakeInstallerServer() { initSocketsOnce(); }
	~FakeInstallerServer() { stop(); }

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
		if(::listen(listen_, 8) != 0)
			return false;
		socklen_t length = sizeof(addr);
		getsockname(listen_, reinterpret_cast<struct sockaddr *>(&addr), &length);
		port_ = ntohs(addr.sin_port);
		running_.store(true);
		thread_ = std::thread(&FakeInstallerServer::loop, this);
		return true;
	}

	void stop()
	{
		if(!running_.exchange(false))
			return;
		// Acorda o accept() com uma ligação a si próprio antes de fechar.
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

	void setResponse(std::string body) { response_ = std::move(body); }
	uint16_t port() const { return port_; }
	std::string lastPath() const { return lastPath_; }
	std::string lastBody() const { return lastBody_; }

private:
	void loop()
	{
		while(running_.load())
		{
			socket_t client = accept(listen_, nullptr, nullptr);
			if(client == ORBISLINK_INVALID_SOCKET)
				break;
			if(!running_.load())
			{
				closeSocketHandle(client);
				break;
			}
			std::string request;
			char buffer[2048];
			size_t contentLength = 0;
			size_t headerEnd = std::string::npos;
			while(true)
			{
				const int received = static_cast<int>(recv(client, buffer, sizeof(buffer), 0));
				if(received <= 0)
					break;
				request.append(buffer, static_cast<size_t>(received));
				if(headerEnd == std::string::npos)
				{
					headerEnd = request.find("\r\n\r\n");
					if(headerEnd != std::string::npos)
					{
						const std::string headers = request.substr(0, headerEnd);
						const size_t marker = headers.find("Content-Length:");
						if(marker != std::string::npos)
							contentLength = static_cast<size_t>(
								std::atoi(headers.c_str() + marker + 15));
					}
				}
				if(headerEnd != std::string::npos && request.size() >= headerEnd + 4 + contentLength)
					break;
			}
			if(headerEnd != std::string::npos)
			{
				const size_t firstSpace = request.find(' ');
				const size_t secondSpace = request.find(' ', firstSpace + 1);
				lastPath_ = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
				lastBody_ = request.substr(headerEnd + 4, contentLength);
			}
			const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
										 "Content-Length: "
				+ std::to_string(response_.size()) + "\r\nConnection: close\r\n\r\n" + response_;
			send(client, response.data(), static_cast<int>(response.size()), 0);
			closeSocketHandle(client);
		}
	}

	socket_t listen_ = ORBISLINK_INVALID_SOCKET;
	uint16_t port_ = 0;
	std::atomic<bool> running_ { false };
	std::thread thread_;
	std::string response_ = R"({ "status": "success" })";
	std::string lastPath_;
	std::string lastBody_;
};

RpiClient makeClient(uint16_t port)
{
	RpiClient::Config config;
	config.host = "127.0.0.1";
	config.port = port;
	config.timeoutMs = 3000;
	config.maxAttempts = 1;
	return RpiClient(config);
}

} // namespace

ORBISLINK_TEST(install_envia_o_corpo_esperado)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(R"({ "status": "success", "task_id": 7, "title": "Jogo" })");

	RpiClient client = makeClient(server.port());
	InstallTaskHandle handle;
	const InstallerResult result =
		client.installDirect({ "http://192.168.1.2:8765/f/abc/jogo.pkg" }, &handle);

	CHECK(result.ok);
	CHECK_EQ(handle.taskId, 7);
	CHECK_EQ(handle.title, std::string("Jogo"));
	CHECK_EQ(server.lastPath(), std::string("/api/install"));

	const Json sent = Json::parse(server.lastBody());
	CHECK_EQ(sent["type"].toString(), std::string("direct"));
	CHECK_EQ(sent["packages"].size(), static_cast<size_t>(1));
	CHECK_EQ(sent["packages"].at(0).toString(),
		std::string("http://192.168.1.2:8765/f/abc/jogo.pkg"));
	server.stop();
}

ORBISLINK_TEST(erro_da_consola_e_traduzido)
{
	FakeInstallerServer server;
	CHECK(server.start());
	// Formato real: hexadecimal sem aspas, campo "error_code".
	server.setResponse(R"({ "status": "fail", "error_code": 0x8002001C })");

	RpiClient client = makeClient(server.port());
	InstallTaskHandle handle;
	const InstallerResult result = client.installDirect({ "http://x/y.pkg" }, &handle);

	CHECK(!result.ok);
	CHECK_EQ(result.errorCode, 0x8002001Cu);
	CHECK(result.message.find("Espaço insuficiente") != std::string::npos);
	CHECK(isOutOfSpaceError(result.errorCode));
	server.stop();
}

ORBISLINK_TEST(codigo_desconhecido_aparece_em_hexadecimal)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(R"({ "status": "fail", "error_code": 0x80FF1234 })");

	RpiClient client = makeClient(server.port());
	const InstallerResult result = client.uninstallGame("CUSA12345");
	CHECK(!result.ok);
	CHECK(result.message.find("0x80FF1234") != std::string::npos);
	CHECK_EQ(server.lastPath(), std::string("/api/uninstall_game"));
	server.stop();
}

ORBISLINK_TEST(is_exists_le_booleano_em_texto)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(R"({ "status": "success", "exists": "true", "size": 0x1A2B3C })");

	RpiClient client = makeClient(server.port());
	bool exists = false;
	int64_t size = -1;
	const InstallerResult result = client.isExists("CUSA12345", &exists, &size);
	CHECK(result.ok);
	CHECK(exists);
	CHECK_EQ(size, 0x1A2B3C);
	CHECK_EQ(Json::parse(server.lastBody())["title_id"].toString(), std::string("CUSA12345"));
	server.stop();
}

ORBISLINK_TEST(progresso_le_campos_hexadecimais)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(
		R"({ "status": "success", "bits": 0x1, "error": 0, "length": 0x100000, )"
		R"("transferred": 0x80000, "length_total": 0x100000, "transferred_total": 0x80000, )"
		R"("num_index": 0, "num_total": 1, "rest_sec": 30, "rest_sec_total": 30, )"
		R"("preparing_percent": 100, "local_copy_percent": 0 })");

	RpiClient client = makeClient(server.port());
	TaskProgress progress;
	const InstallerResult result = client.taskProgress(7, &progress);
	CHECK(result.ok);
	CHECK_EQ(progress.lengthTotal, 0x100000);
	CHECK_EQ(progress.transferredTotal, 0x80000);
	CHECK_EQ(progress.restSecTotal, 30u);
	CHECK(progress.percent() > 49.0 && progress.percent() < 51.0);
	CHECK(!progress.finished());
	CHECK_EQ(server.lastPath(), std::string("/api/get_task_progress"));
	CHECK_EQ(Json::parse(server.lastBody())["task_id"].toInt(), 7);
	server.stop();
}

ORBISLINK_TEST(find_task_usa_sub_type)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(R"({ "status": "success", "task_id": 3 })");

	RpiClient client = makeClient(server.port());
	int taskId = -1;
	const InstallerResult result =
		client.findTask("UP0001-CUSA12345_00-ORBISLINKTEST001", TaskSubType::Game, &taskId);
	CHECK(result.ok);
	CHECK_EQ(taskId, 3);
	const Json sent = Json::parse(server.lastBody());
	CHECK_EQ(sent["sub_type"].toInt(), 6); // Game=6 (README do instalador)
	server.stop();
}

ORBISLINK_TEST(comandos_de_tarefa_usam_os_endpoints_certos)
{
	FakeInstallerServer server;
	CHECK(server.start());
	RpiClient client = makeClient(server.port());

	CHECK(client.pauseTask(1).ok);
	CHECK_EQ(server.lastPath(), std::string("/api/pause_task"));
	CHECK(client.resumeTask(1).ok);
	CHECK_EQ(server.lastPath(), std::string("/api/resume_task"));
	CHECK(client.stopTask(1).ok);
	CHECK_EQ(server.lastPath(), std::string("/api/stop_task"));
	CHECK(client.unregisterTask(1).ok);
	CHECK_EQ(server.lastPath(), std::string("/api/unregister_task"));
	CHECK(client.uninstallPatch("CUSA12345").ok);
	CHECK_EQ(server.lastPath(), std::string("/api/uninstall_patch"));
	CHECK(client.uninstallAdditionalContent("UP0001-CUSA12345_00-DLC0000000000001").ok);
	CHECK_EQ(server.lastPath(), std::string("/api/uninstall_ac"));
	server.stop();
}

ORBISLINK_TEST(sem_servidor_a_mensagem_e_a_do_requisito)
{
	// Porta fechada: deve dar a mensagem de §7 para a porta 12800.
	RpiClient::Config config;
	config.host = "127.0.0.1";
	config.port = 1; // nada à escuta
	config.timeoutMs = 500;
	config.maxAttempts = 1;
	RpiClient client(config);
	const InstallerResult result = client.isExists("CUSA12345", nullptr, nullptr);
	CHECK(!result.ok);
	CHECK(result.message.find("Instalador remoto indisponível") != std::string::npos);
	CHECK(!client.probe(nullptr));
}

TEST_MAIN()
