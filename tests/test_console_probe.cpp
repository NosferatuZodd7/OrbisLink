// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The automatic address check in the settings relies on this function.
// Here it is checked against fake servers: FTP answering with the GoldHEN
// banner and the remote installer answering HTTP, either of which may be missing.

#include "orbislink/console/console_manager.h"
#include "orbislink/net/lan_scan.h"
#include "orbislink/net/socket_compat.h"
#include "test_support.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace orbislink;

namespace {

// Minimal TCP server: accepts a connection, sends `greeting` and closes. FTP
// greets first; the installer only answers after reading the HTTP request.
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

// A port nobody is listening on: a socket is opened and closed to get
// a number the system has just released.
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

ORBISLINK_TEST(returns_both_services_when_both_answer)
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

ORBISLINK_TEST(tells_apart_only_one_service_answering)
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

ORBISLINK_TEST(fails_with_nobody_on_the_other_side)
{
	const uint16_t deadFtp = closedPort();
	const uint16_t deadInstaller = closedPort();

	const ProbeResult result =
		probeConsoleServices("127.0.0.1", deadFtp, deadInstaller, 1000);

	CHECK(!result.ftpOk);
	CHECK(!result.installerOk);
	CHECK(!result.anyOk());
}

ORBISLINK_TEST(finds_ftp_on_another_port_when_the_set_one_is_closed)
{
	FakeService ftp("220 etaHEN FTP\r\n");
	CHECK(ftp.start());
	const uint16_t dead = closedPort();

	const ProbeResult result = probeConsoleServices("127.0.0.1", dead, closedPort(), 1500, { ftp.port() });

	CHECK(result.ftpOk);
	CHECK(result.ftpPort == ftp.port());
	CHECK(result.ftpDetail == "220 etaHEN FTP");
	CHECK(result.ftpPorts.size() == 2);
	CHECK(result.ftpPorts[0].port == dead);
	CHECK(!result.ftpPorts[0].open);
}

ORBISLINK_TEST(a_silent_port_is_not_taken_for_ftp_unless_it_is_the_set_one)
{
	// Takes the connection and says nothing, as etaHEN's DPI v1 does.
	FakeService silent("");
	CHECK(silent.start());

	const ProbeResult elsewhere = probeConsoleServices("127.0.0.1", closedPort(), closedPort(), 1000, { silent.port() });
	CHECK(!elsewhere.ftpOk);
	CHECK(elsewhere.ftpPort == 0);
	CHECK(elsewhere.ftpPorts.size() == 2);
	CHECK(elsewhere.ftpPorts[1].open);
	CHECK(!elsewhere.ftpPorts[1].ftp());

	// On the set port it may be a server busy with a transfer: still FTP.
	const ProbeResult set = probeConsoleServices("127.0.0.1", silent.port(), closedPort(), 1000);
	CHECK(set.ftpOk);
	CHECK(set.ftpPort == silent.port());
	CHECK(set.ftpDetail == "connected, no banner");
}

ORBISLINK_TEST(a_port_that_greets_as_something_else_is_not_ftp)
{
	FakeService other("HTTP/1.1 400 Bad Request\r\n\r\n");
	FakeService ftp("220 GoldHEN FTP Server\r\n");
	CHECK(other.start());
	CHECK(ftp.start());

	const ProbeResult alone = probeConsoleServices("127.0.0.1", other.port(), closedPort(), 1000);
	CHECK(!alone.ftpOk);
	CHECK(alone.ftpDetail == "HTTP/1.1 400 Bad Request");

	const ProbeResult found = probeConsoleServices("127.0.0.1", other.port(), closedPort(), 1000, { ftp.port() });
	CHECK(found.ftpOk);
	CHECK(found.ftpPort == ftp.port());
}

ORBISLINK_TEST(the_set_port_wins_when_several_greet)
{
	FakeService set("220 one\r\n");
	FakeService other("220 two\r\n");
	CHECK(set.start());
	CHECK(other.start());

	const ProbeResult result = probeConsoleServices("127.0.0.1", set.port(), closedPort(), 1000, { other.port() });
	CHECK(result.ftpOk);
	CHECK(result.ftpPort == set.port());
	CHECK(result.ftpDetail == "220 one");
}

ORBISLINK_TEST(ftp_port_candidates_start_with_the_set_port)
{
	const std::vector<uint16_t> ps4 = ftpPortCandidates(2121, false);
	CHECK(ps4.size() == 3);
	CHECK(ps4[0] == 2121);
	CHECK(ps4[1] == 1337);
	CHECK(ps4[2] == 21);

	// A PS5 tries 9090 as well; a PS4 never knocks on its payload loader.
	const std::vector<uint16_t> ps5 = ftpPortCandidates(1337, true);
	CHECK(ps5.size() == 4);
	CHECK(ps5[0] == 1337);
	CHECK(ps5[1] == 2121);
	CHECK(ps5[3] == 9090);

	const std::vector<uint16_t> custom = ftpPortCandidates(5000, true);
	CHECK(custom.size() == 5);
	CHECK(custom[0] == 5000);

	CHECK(knownPortUse(9090, true).find("DPI v1") != std::string::npos);
	CHECK(knownPortUse(1337, true).find("etaHEN") != std::string::npos);
	CHECK(knownPortUse(4444, true).empty());
}

ORBISLINK_TEST(a_search_asks_every_interfaces_network)
{
	std::vector<LocalInterface> interfaces;
	interfaces.push_back({ "lo", "127.0.0.1", "255.0.0.0", true });
	interfaces.push_back({ "wifi", "192.168.1.20", "255.255.255.0", false });
	// Cable and Wi-Fi on the same network: asked once.
	interfaces.push_back({ "eth", "192.168.1.21", "255.255.255.0", false });
	// A VPN's big network: its broadcast only.
	interfaces.push_back({ "vpn", "10.8.0.5", "255.255.0.0", false });
	// A public address: its broadcast, never a sweep.
	interfaces.push_back({ "pub", "203.0.113.7", "255.255.255.0", false });
	// An address the PC gave itself: nothing.
	interfaces.push_back({ "ll", "169.254.10.10", "255.255.0.0", false });
	const LanTargets targets = lanTargets(interfaces);
	CHECK_EQ(targets.broadcasts.size(), static_cast<size_t>(4));
	CHECK_EQ(targets.broadcasts[0], std::string("255.255.255.255"));
	CHECK_EQ(targets.broadcasts[1], std::string("192.168.1.255"));
	CHECK_EQ(targets.broadcasts[2], std::string("10.8.255.255"));
	CHECK_EQ(targets.broadcasts[3], std::string("203.0.113.255"));
	// 192.168.1.1 to .254 but the PC's own two.
	CHECK_EQ(targets.hosts.size(), static_cast<size_t>(252));
	CHECK_EQ(targets.hosts.front(), std::string("192.168.1.1"));
	CHECK_EQ(targets.hosts.back(), std::string("192.168.1.254"));
	for(const std::string &host : targets.hosts)
		CHECK(host != "192.168.1.20" && host != "192.168.1.21");
	CHECK_EQ(lanTargets(interfaces, 10).hosts.size(), static_cast<size_t>(10));
	CHECK(privateIPv4(0xAC1F0001)); // 172.31.0.1
	CHECK(!privateIPv4(0xAC200001)); // 172.32.0.1
}

ORBISLINK_TEST(finds_the_hosts_with_a_port_open)
{
	FakeService ftp("220 FTP ready\r\n");
	CHECK(ftp.start());
	// 127.0.0.2 is this machine too, on another address: nothing listens there.
	const std::vector<std::string> open = hostsWithPort({ "127.0.0.1", "127.0.0.2", "not an address" }, ftp.port(), 800);
	CHECK_EQ(open.size(), static_cast<size_t>(1));
	CHECK_EQ(open.front(), std::string("127.0.0.1"));
	CHECK(hostsWithPort({ "127.0.0.1" }, closedPort(), 500).empty());
}

ORBISLINK_TEST(reads_the_jailbreak_version_from_a_greeting_that_gives_it)
{
	CHECK_EQ(henVersionInGreeting("220 GoldHEN v2.4b18.3 FTP Server", "GoldHEN"), std::string("2.4b18.3"));
	CHECK_EQ(henVersionInGreeting("220 goldhen_V2.4b17. ready", "GoldHEN"), std::string("2.4b17"));
	CHECK_EQ(henVersionInGreeting("220 etaHEN 2.5B by LM", "etaHEN"), std::string("2.5B"));
	// No version, another name, or a number that is not a version.
	CHECK(henVersionInGreeting("220 GoldHEN FTP Server", "GoldHEN").empty());
	CHECK(henVersionInGreeting("220 FTP server 1.0A for PS5 by LM.", "etaHEN").empty());
	CHECK(henVersionInGreeting("220 GoldHEN 2121", "GoldHEN").empty());
	CHECK(henVersionInGreeting("220 GoldHEN v2.4", "").empty());
}

ORBISLINK_TEST(empty_address_does_not_go_to_the_network)
{
	const ProbeResult result = probeConsoleServices("   ", 2121, 12800, 1000);

	CHECK(!result.anyOk());
	CHECK(result.ftpDetail == "no console address");
	CHECK(result.installerDetail == "no console address");
}

TEST_MAIN()
