// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/util.h"
#include "orbislink/http/local_http_server.h"
#include "orbislink/net/socket_compat.h"
#include "test_support.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <vector>

using namespace orbislink;

namespace {

struct RawResponse
{
	int status = 0;
	std::string headers;
	std::string body;

	std::string header(const std::string &name) const
	{
		const std::string needle = toLower(name) + ":";
		std::istringstream stream(headers);
		std::string line;
		while(std::getline(stream, line))
		{
			if(!line.empty() && line.back() == '\r')
				line.pop_back();
			if(startsWith(toLower(line), needle))
				return trim(line.substr(needle.size()));
		}
		return std::string();
	}
};

// Cliente HTTP cru: é preciso controlar cabeçalhos Range à mão.
RawResponse rawRequest(uint16_t port, const std::string &request)
{
	initSocketsOnce();
	RawResponse response;
	socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == ORBISLINK_INVALID_SOCKET)
		return response;

	struct sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	if(connect(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0)
	{
		closeSocketHandle(sock);
		return response;
	}
	send(sock, request.data(), static_cast<int>(request.size()), 0);

	std::string raw;
	char buffer[4096];
	for(;;)
	{
		const int received = static_cast<int>(recv(sock, buffer, sizeof(buffer), 0));
		if(received <= 0)
			break;
		raw.append(buffer, static_cast<size_t>(received));
	}
	closeSocketHandle(sock);

	const size_t split = raw.find("\r\n\r\n");
	if(split == std::string::npos)
		return response;
	response.headers = raw.substr(0, split);
	response.body = raw.substr(split + 4);
	const size_t firstSpace = response.headers.find(' ');
	if(firstSpace != std::string::npos)
		response.status = std::atoi(response.headers.c_str() + firstSpace + 1);
	return response;
}

std::string writePayload(const std::string &name, const std::string &content)
{
	std::ofstream file(name, std::ios::binary | std::ios::trunc);
	file << content;
	return name;
}

struct ServerFixture
{
	LocalHttpServer server;
	std::string token;
	std::string path;
	std::string content;

	ServerFixture(const std::string &payload, bool allowLoopback = true,
		const std::string &allowedClient = std::string())
		: content(payload)
	{
		path = writePayload(".orbislink-test-http.bin", payload);
		LocalHttpServer::Config config;
		config.bindAddress = "127.0.0.1";
		config.port = 0; // porta atribuída pelo sistema
		config.autoSelectPort = false;
		config.allowLoopback = allowLoopback;
		config.allowedClient = allowedClient;
		config.chunkSize = 16; // força vários blocos
		std::string error;
		if(!server.start(config, &error))
			throw std::runtime_error("servidor não arrancou: " + error);
		token = server.registerFile(path, "jogo teste!.pkg");
	}

	~ServerFixture()
	{
		server.stop();
		std::remove(path.c_str());
	}

	std::string get(const std::string &target, const std::string &extraHeaders = std::string(),
		const std::string &method = "GET") const
	{
		return method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + extraHeaders
			+ "Connection: close\r\n\r\n";
	}
};

const std::string kPayload = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

} // namespace

ORBISLINK_TEST(serve_ficheiro_completo)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/jogo_teste_.pkg";
	const RawResponse response = rawRequest(fixture.server.port(), fixture.get(url));
	CHECK_EQ(response.status, 200);
	CHECK_EQ(response.body, kPayload);
	CHECK_EQ(response.header("Accept-Ranges"), std::string("bytes"));
	CHECK_EQ(response.header("Content-Type"), std::string("application/octet-stream"));
	CHECK_EQ(response.header("Content-Length"), std::to_string(kPayload.size()));
}

ORBISLINK_TEST(url_gerado_tem_token_e_nome_sanitizado)
{
	ServerFixture fixture(kPayload);
	const std::string url = fixture.server.urlForToken(fixture.token);
	CHECK(url.find("/f/" + fixture.token + "/") != std::string::npos);
	CHECK(url.find(' ') == std::string::npos);
	CHECK(url.find("jogo_teste_.pkg") != std::string::npos);
}

ORBISLINK_TEST(range_do_inicio)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	const RawResponse response = rawRequest(fixture.server.port(),
		fixture.get(url, "Range: bytes=0-9\r\n"));
	CHECK_EQ(response.status, 206);
	CHECK_EQ(response.body, kPayload.substr(0, 10));
	CHECK_EQ(response.header("Content-Range"), "bytes 0-9/" + std::to_string(kPayload.size()));
	CHECK_EQ(response.header("Content-Length"), std::string("10"));
}

ORBISLINK_TEST(range_do_meio)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	const RawResponse response = rawRequest(fixture.server.port(),
		fixture.get(url, "Range: bytes=20-29\r\n"));
	CHECK_EQ(response.status, 206);
	CHECK_EQ(response.body, kPayload.substr(20, 10));
}

ORBISLINK_TEST(range_aberto_ate_ao_fim)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	const RawResponse response = rawRequest(fixture.server.port(),
		fixture.get(url, "Range: bytes=50-\r\n"));
	CHECK_EQ(response.status, 206);
	CHECK_EQ(response.body, kPayload.substr(50));
}

ORBISLINK_TEST(range_sufixo)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	const RawResponse response = rawRequest(fixture.server.port(),
		fixture.get(url, "Range: bytes=-8\r\n"));
	CHECK_EQ(response.status, 206);
	CHECK_EQ(response.body, kPayload.substr(kPayload.size() - 8));
}

ORBISLINK_TEST(range_fora_dos_limites_devolve_416)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	const RawResponse response = rawRequest(fixture.server.port(),
		fixture.get(url, "Range: bytes=99999-\r\n"));
	CHECK_EQ(response.status, 416);
	CHECK_EQ(response.header("Content-Range"), "bytes */" + std::to_string(kPayload.size()));
}

ORBISLINK_TEST(head_devolve_cabecalhos_sem_corpo)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	const RawResponse response = rawRequest(fixture.server.port(),
		fixture.get(url, std::string(), "HEAD"));
	CHECK_EQ(response.status, 200);
	CHECK(response.body.empty());
	CHECK_EQ(response.header("Content-Length"), std::to_string(kPayload.size()));
}

ORBISLINK_TEST(token_invalido_devolve_404)
{
	ServerFixture fixture(kPayload);
	CHECK_EQ(rawRequest(fixture.server.port(), fixture.get("/f/token-errado/x.pkg")).status, 404);
	// Nenhuma pasta é exposta: qualquer outro caminho é 404.
	CHECK_EQ(rawRequest(fixture.server.port(), fixture.get("/")).status, 404);
	CHECK_EQ(rawRequest(fixture.server.port(), fixture.get("/../etc/passwd")).status, 404);
}

ORBISLINK_TEST(token_expira_ao_ser_removido)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	CHECK_EQ(rawRequest(fixture.server.port(), fixture.get(url)).status, 200);
	CHECK(fixture.server.unregisterFile(fixture.token));
	CHECK_EQ(rawRequest(fixture.server.port(), fixture.get(url)).status, 404);
}

ORBISLINK_TEST(ip_nao_autorizado_devolve_403)
{
	ServerFixture fixture(kPayload, /*allowLoopback=*/false, /*allowedClient=*/"192.168.1.50");
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	CHECK_EQ(rawRequest(fixture.server.port(), fixture.get(url)).status, 403);
}

ORBISLINK_TEST(metodo_nao_suportado_devolve_405)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	const RawResponse response = rawRequest(fixture.server.port(),
		fixture.get(url, std::string(), "DELETE"));
	CHECK_EQ(response.status, 405);
}

ORBISLINK_TEST(contabiliza_bytes_servidos)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	rawRequest(fixture.server.port(), fixture.get(url, "Range: bytes=0-9\r\n"));
	rawRequest(fixture.server.port(), fixture.get(url, "Range: bytes=10-19\r\n"));
	ServedFileStats stats;
	CHECK(fixture.server.statsForToken(fixture.token, &stats));
	CHECK_EQ(stats.bytesSent, 20);
	CHECK(stats.firstByteAtMs >= 0);
	CHECK_EQ(stats.size, static_cast<int64_t>(kPayload.size()));
}

ORBISLINK_TEST(varias_ligacoes_em_simultaneo)
{
	ServerFixture fixture(kPayload);
	const std::string url = "/f/" + fixture.token + "/x.pkg";
	std::vector<std::thread> threads;
	std::vector<int> statuses(6, 0);
	for(size_t i = 0; i < statuses.size(); ++i)
	{
		threads.emplace_back([&, i]() {
			const RawResponse response = rawRequest(fixture.server.port(), fixture.get(url));
			statuses[i] = response.status == 200 && response.body == kPayload ? 200 : -1;
		});
	}
	for(auto &thread : threads)
		thread.join();
	for(int status : statuses)
		CHECK_EQ(status, 200);
}

TEST_MAIN()
