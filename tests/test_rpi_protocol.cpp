// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Checks the format of the Remote Package Installer API requests and replies
// without needing the console: the fake server answers exactly like the
// installer's server.c (including the hexadecimal numbers).

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

// Minimal HTTP server that answers POSTs with a fixed reply and keeps the
// last request received.
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
		// Wakes accept() with a connection to itself before closing.
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
	// Answers like etaHEN's DPI v2: its page at "/", a form POST gets the
	// reply set, and a JSON POST is dropped without a word.
	void setDpiV2(bool on) { dpiV2_.store(on); }
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
			std::string reply = response_;
			if(headerEnd != std::string::npos)
			{
				const size_t firstSpace = request.find(' ');
				const size_t secondSpace = request.find(' ', firstSpace + 1);
				const std::string method = request.substr(0, firstSpace);
				const std::string path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
				const std::string body = request.substr(headerEnd + 4, contentLength);
				if(dpiV2_.load())
				{
					if(method == "GET")
						reply = "<html><head><title>etaHEN DPIv2</title></head></html>";
					else if(request.find("application/x-www-form-urlencoded") == std::string::npos)
					{
						closeSocketHandle(client);
						continue;
					}
				}
				if(method == "POST")
				{
					lastPath_ = path;
					lastBody_ = body;
				}
			}
			const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
										 "Content-Length: "
				+ std::to_string(reply.size()) + "\r\nConnection: close\r\n\r\n" + reply;
			send(client, response.data(), static_cast<int>(response.size()), 0);
			closeSocketHandle(client);
		}
	}

	socket_t listen_ = ORBISLINK_INVALID_SOCKET;
	uint16_t port_ = 0;
	std::atomic<bool> running_ { false };
	std::atomic<bool> dpiV2_ { false };
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

ORBISLINK_TEST(install_sends_the_expected_body)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(R"({ "status": "success", "task_id": 7, "title": "Game" })");

	RpiClient client = makeClient(server.port());
	InstallTaskHandle handle;
	const InstallerResult result =
		client.installDirect({ "http://192.168.1.2:8765/f/abc/game.pkg" }, &handle);

	CHECK(result.ok);
	CHECK_EQ(handle.taskId, 7);
	CHECK_EQ(handle.title, std::string("Game"));
	CHECK_EQ(server.lastPath(), std::string("/api/install"));

	const Json sent = Json::parse(server.lastBody());
	CHECK_EQ(sent["type"].toString(), std::string("direct"));
	CHECK_EQ(sent["packages"].size(), static_cast<size_t>(1));
	CHECK_EQ(sent["packages"].at(0).toString(),
		std::string("http://192.168.1.2:8765/f/abc/game.pkg"));
	server.stop();
}

ORBISLINK_TEST(console_error_is_translated)
{
	FakeInstallerServer server;
	CHECK(server.start());
	// Real format: hexadecimal without quotes, "error_code" field.
	server.setResponse(R"({ "status": "fail", "error_code": 0x8002001C })");

	RpiClient client = makeClient(server.port());
	InstallTaskHandle handle;
	const InstallerResult result = client.installDirect({ "http://x/y.pkg" }, &handle);

	CHECK(!result.ok);
	CHECK_EQ(result.errorCode, 0x8002001Cu);
	CHECK(result.message.find("Not enough space") != std::string::npos);
	CHECK(isOutOfSpaceError(result.errorCode));
	server.stop();
}

ORBISLINK_TEST(bgft_codes_are_described)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(R"({ "status": "fail", "error_code": 0x80990015 })");

	RpiClient client = makeClient(server.port());
	InstallTaskHandle handle;
	const InstallerResult result = client.installDirect({ "http://x/y.pkg" }, &handle);
	CHECK(!result.ok);
	CHECK_EQ(result.errorCode, kBgftTaskDuplicated);
	CHECK(result.message.find("TASK_DUPLICATED") != std::string::npos);
	CHECK(result.message.find("0x80990015") != std::string::npos);
	CHECK(!isOutOfSpaceError(result.errorCode));
	CHECK(isOutOfSpaceError(0x80990039u));
	server.stop();
}

ORBISLINK_TEST(speaks_etahen_dpi_v2_on_a_ps5)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setDpiV2(true);
	server.setResponse("SUCCESS: Direct install console Task started for URL: /data/pkg/My Game.pkg");

	RpiClient::Config config;
	config.host = "127.0.0.1";
	config.port = server.port();
	config.timeoutMs = 2000;
	config.maxAttempts = 1;
	config.ps5 = true;
	RpiClient client(config);
	CHECK(client.installsFromConsole());

	InstallTaskHandle handle;
	const InstallerResult started = client.installDirect({ "/data/pkg/My Game.pkg" }, &handle);
	CHECK(started.ok);
	CHECK_EQ(handle.taskId, -1);
	CHECK(!client.followsTasks());
	// A form, with the path as the "url".
	CHECK_EQ(server.lastBody(), std::string("url=/data/pkg/My%20Game.pkg"));

	server.setResponse("FAILED: Install failed with error SCE_BGFT_ERROR_TASK_DUPLICATED, code -2137456619 "
		"(0x80990015) for URL: http://x/y.pkg");
	const InstallerResult refused = client.installDirect({ "http://x/y.pkg" }, &handle);
	CHECK(!refused.ok);
	CHECK_EQ(refused.errorCode, kBgftTaskDuplicated);
	CHECK(refused.message.find("TASK_DUPLICATED") != std::string::npos);

	// What it does not do is said at once, without asking it.
	bool exists = true;
	CHECK(!client.isExists("CUSA00001", &exists, nullptr).ok);
	server.stop();
}

ORBISLINK_TEST(a_ps5_with_the_json_api_keeps_it)
{
	FakeInstallerServer server;
	CHECK(server.start());
	server.setResponse(R"({ "status": "success", "task_id": 0x2A, "title": "Game" })");

	RpiClient::Config config;
	config.host = "127.0.0.1";
	config.port = server.port();
	config.timeoutMs = 2000;
	config.maxAttempts = 1;
	config.ps5 = true;
	RpiClient client(config);
	InstallTaskHandle handle;
	CHECK(client.installDirect({ "/data/pkg/game.pkg" }, &handle).ok);
	CHECK_EQ(handle.taskId, 42);
	CHECK(client.followsTasks());
	CHECK_EQ(server.lastPath(), std::string("/api/install"));
	server.stop();
}

ORBISLINK_TEST(unknown_code_shows_in_hexadecimal)
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

ORBISLINK_TEST(is_exists_reads_boolean_text)
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

ORBISLINK_TEST(progress_reads_hexadecimal_fields)
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

ORBISLINK_TEST(find_task_uses_sub_type)
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

ORBISLINK_TEST(task_commands_use_the_right_endpoints)
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

ORBISLINK_TEST(without_server_the_message_is_the_requirement)
{
	// Closed port: must give the §7 message for port 12800.
	RpiClient::Config config;
	config.host = "127.0.0.1";
	config.port = 1; // nothing listening
	config.timeoutMs = 500;
	config.maxAttempts = 1;
	RpiClient client(config);
	const InstallerResult result = client.isExists("CUSA12345", nullptr, nullptr);
	CHECK(!result.ok);
	CHECK(result.message.find("Remote installer unavailable") != std::string::npos);
	CHECK(!client.probe(nullptr));
}

TEST_MAIN()
