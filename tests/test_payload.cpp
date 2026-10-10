// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Payloads go to a jailbroken console's loader whole, and what the payload
// prints comes back over the same connection. Here the loader is a fake one
// that behaves like the PS5's: it reads the ELF, then the payload talks.

#include "orbislink/net/payload_sender.h"
#include "orbislink/payloads/payload_catalog.h"
#include "orbislink/payloads/payload_layout.h"
#include "orbislink/net/socket_compat.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace orbislink;

namespace {

// Accepts one connection, reads `expect` bytes, answers `reply` and, unless
// told to hold on, closes.
class FakeLoader
{
public:
	FakeLoader(size_t expect, std::string reply, bool holdOpen = false)
		: expect_(expect), reply_(std::move(reply)), holdOpen_(holdOpen)
	{
		initSocketsOnce();
	}
	~FakeLoader()
	{
		release_.store(true);
		if(thread_.joinable())
			thread_.join();
		closeSocketHandle(listen_);
	}

	bool start()
	{
		listen_ = socket(AF_INET, SOCK_STREAM, 0);
		if(listen_ == ORBISLINK_INVALID_SOCKET)
			return false;
		struct sockaddr_in addr {};
		addr.sin_family = AF_INET;
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
		if(bind(listen_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0
			|| ::listen(listen_, 1) != 0)
			return false;
		socklen_t length = sizeof(addr);
		getsockname(listen_, reinterpret_cast<struct sockaddr *>(&addr), &length);
		port_ = ntohs(addr.sin_port);
		thread_ = std::thread([this]() {
			socket_t client = accept(listen_, nullptr, nullptr);
			if(client == ORBISLINK_INVALID_SOCKET)
				return;
			std::vector<char> buffer(4096);
			while(received_.size() < expect_)
			{
				const auto n = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
				if(n <= 0)
					break;
				received_.insert(received_.end(), buffer.begin(), buffer.begin() + n);
			}
			send(client, reply_.data(), static_cast<int>(reply_.size()), 0);
			while(holdOpen_ && !release_.load())
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			closeSocketHandle(client);
		});
		return true;
	}

	uint16_t port() const { return port_; }
	const std::vector<char> &received() const { return received_; }
	void join()
	{
		release_.store(true);
		if(thread_.joinable())
			thread_.join();
	}

private:
	size_t expect_;
	std::string reply_;
	bool holdOpen_;
	socket_t listen_ = ORBISLINK_INVALID_SOCKET;
	uint16_t port_ = 0;
	std::thread thread_;
	std::vector<char> received_;
	std::atomic<bool> release_ { false };
};

std::vector<uint8_t> fakeElf(size_t size)
{
	std::vector<uint8_t> elf(size, 0xAB);
	const uint8_t header[] = { 0x7f, 'E', 'L', 'F', 2 };
	std::copy(std::begin(header), std::end(header), elf.begin());
	return elf;
}

const char *kPayloadSays =
	"[payload] started\n"
	"listening on 2323\r\n"
	"version 1.0\n"
	"done\n";

} // namespace

ORBISLINK_TEST(sends_the_whole_payload_and_reads_what_it_prints)
{
	const std::vector<uint8_t> elf = fakeElf(200000);
	FakeLoader loader(elf.size(), kPayloadSays);
	CHECK(loader.start());

	std::vector<std::string> lines;
	PayloadSender::Options options;
	options.listenMs = 5000;
	options.onLine = [&lines](const std::string &line) {
		lines.push_back(line);
		return true;
	};
	const PayloadSender::Result result = PayloadSender::send("127.0.0.1", loader.port(), elf, options);
	loader.join();

	CHECK(result.sent);
	CHECK(result.closedByConsole);
	CHECK_EQ(loader.received().size(), elf.size());
	CHECK_EQ(lines.size(), size_t(4));
	CHECK_EQ(lines[0], std::string("[payload] started"));
	CHECK_EQ(lines[1], std::string("listening on 2323"));
}

ORBISLINK_TEST(stops_listening_when_told_to)
{
	const std::vector<uint8_t> elf = fakeElf(1000);
	FakeLoader loader(elf.size(), "first\nsecond\nthird\n", true);
	CHECK(loader.start());

	std::vector<std::string> lines;
	PayloadSender::Options options;
	options.listenMs = 20000;
	options.onLine = [&lines](const std::string &line) {
		lines.push_back(line);
		return line != "second";
	};
	const auto started = std::chrono::steady_clock::now();
	const PayloadSender::Result result = PayloadSender::send("127.0.0.1", loader.port(), elf, options);
	const auto took = std::chrono::steady_clock::now() - started;
	loader.join();

	CHECK(result.sent);
	CHECK_EQ(lines.size(), size_t(2));
	CHECK(took < std::chrono::seconds(5));
}

ORBISLINK_TEST(says_when_no_loader_answers)
{
	// A port that was just released: nobody listens there.
	initSocketsOnce();
	socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr {};
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	CHECK(bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0);
	socklen_t length = sizeof(addr);
	getsockname(sock, reinterpret_cast<struct sockaddr *>(&addr), &length);
	const uint16_t port = ntohs(addr.sin_port);
	closeSocketHandle(sock);

	PayloadSender::Options options;
	options.connectTimeoutMs = 800;
	const PayloadSender::Result result = PayloadSender::send("127.0.0.1", port, fakeElf(64), options);
	CHECK(!result.sent);
	CHECK(!result.error.empty());
}

ORBISLINK_TEST(knows_where_each_console_keeps_its_payloads)
{
	using namespace payloads;
	const std::vector<Folder> ps4 = folders(Kind::Ps4);
	CHECK_EQ(ps4.size(), size_t(3));
	CHECK_EQ(ps4[0].criticalFile, std::string("goldhen.bin"));
	CHECK(ps4[2].autoStart == AutoStart::Ini);
	const std::vector<Folder> ps5 = folders(Kind::Ps5);
	CHECK_EQ(ps5[0].path, std::string("/data/etaHEN/payloads"));
	CHECK(ps5[0].autoStart == AutoStart::Marker);
	CHECK_EQ(ps5[2].configPath, std::string("/data/ps5_autoloader/autoload.txt"));
	// PLK's Payload Manager: a folder per payload, its own autoload list.
	CHECK_EQ(ps5.size(), size_t(4));
	CHECK(ps5[3].nested);
	CHECK_EQ(ps5[3].configPath, std::string("/data/pldmgr/autoload.txt"));

	CHECK_EQ(loaderPort(Kind::Ps4, "goldhen.bin"), 9090);
	CHECK_EQ(loaderPort(Kind::Ps5, "kstuff.ELF"), 9021);
	CHECK_EQ(loaderPort(Kind::Ps5, "etaHEN.bin"), 9020);
	CHECK_EQ(loaderPort(Kind::Ps5, "loader.lua"), 9026);
	CHECK(sendable(Kind::Ps5, "ftpsrv.elf"));
	CHECK(!sendable(Kind::Ps4, "game_patch.prx"));
	CHECK(!sendable(Kind::Ps5, "config.ini"));
}

ORBISLINK_TEST(keeps_the_autoloader_list_in_order)
{
	using namespace payloads;
	const std::string text = "!5000\nkstuff.elf\n!1000\nftpsrv.elf\nwebsrv.elf\n";
	CHECK(autoloadListed(text, "ftpsrv.elf"));
	CHECK(!autoloadListed(text, "FTPSRV.elf")); // names are case-sensitive
	CHECK_EQ(autoloadEntries(text).size(), size_t(3));

	// Out goes the name and the wait before it.
	CHECK_EQ(autoloadSet(text, "ftpsrv.elf", false), std::string("!5000\nkstuff.elf\nwebsrv.elf\n"));
	CHECK_EQ(autoloadSet(text, "kstuff.elf", false), std::string("!1000\nftpsrv.elf\nwebsrv.elf\n"));
	// In at the end, blank lines kept out of the way.
	CHECK_EQ(autoloadSet("kstuff.elf\n\n", "shsrv.elf", true), std::string("kstuff.elf\nshsrv.elf\n"));
	CHECK_EQ(autoloadSet(text, "kstuff.elf", true), text);
	CHECK_EQ(autoloadSet("", "a.elf", true), std::string("a.elf\n"));
	// Windows line endings stay Windows line endings.
	CHECK_EQ(autoloadSet("a.elf\r\n", "b.elf", true), std::string("a.elf\r\nb.elf\r\n"));
	CHECK_EQ(autoloadRename(text, "websrv.elf", "websrv-2.elf"),
		std::string("!5000\nkstuff.elf\n!1000\nftpsrv.elf\nwebsrv-2.elf\n"));
}

ORBISLINK_TEST(turns_goldhen_plugins_on_and_off)
{
	using namespace payloads;
	const std::string ini =
		"; plugins\n"
		"[default]\n"
		"/data/GoldHEN/plugins/game_patch.prx=true\n"
		"/data/GoldHEN/plugins/afr.prx=false\n"
		"/data/GoldHEN/plugins/old.prx\n"
		"\n"
		"[CUSA00001]\n"
		"/data/GoldHEN/plugins/afr.prx=true\n";
	CHECK(pluginEnabled(ini, "/data/GoldHEN/plugins/game_patch.prx"));
	CHECK(!pluginEnabled(ini, "/data/GoldHEN/plugins/afr.prx")); // on for one game only
	CHECK(pluginEnabled(ini, "/data/GoldHEN/plugins/old.prx")); // no value: on
	CHECK(!pluginEnabled(ini, "/data/GoldHEN/plugins/missing.prx"));

	const std::string on = pluginSet(ini, "/data/GoldHEN/plugins/afr.prx", true);
	CHECK(pluginEnabled(on, "/data/GoldHEN/plugins/afr.prx"));
	CHECK(on.find("[CUSA00001]\n/data/GoldHEN/plugins/afr.prx=true") != std::string::npos);

	// A new one goes at the end of [default], before the next section.
	const std::string added = pluginSet(ini, "/data/GoldHEN/plugins/fps.prx", true);
	CHECK(added.find("old.prx\n/data/GoldHEN/plugins/fps.prx=true\n\n[CUSA00001]") != std::string::npos);
	CHECK_EQ(pluginSet(ini, "/data/GoldHEN/plugins/fps.prx", false), ini);

	// No [default] yet: it is made at the top.
	CHECK_EQ(pluginSet("[CUSA1]\n/x.prx=true\n", "/y.prx", true),
		std::string("[default]\n/y.prx=true\n\n[CUSA1]\n/x.prx=true\n"));

	const std::string renamed = pluginRename(ini, "/data/GoldHEN/plugins/afr.prx", "/data/GoldHEN/plugins/afr2.prx");
	CHECK(renamed.find("afr2.prx=false") != std::string::npos);
	CHECK(renamed.find("afr2.prx=true") != std::string::npos);
	const std::string removed = pluginRemove(ini, "/data/GoldHEN/plugins/afr.prx");
	CHECK(removed.find("afr.prx") == std::string::npos);
	CHECK(removed.find("game_patch.prx=true") != std::string::npos);
}

ORBISLINK_TEST(reads_the_payload_catalog)
{
	using namespace payloads;
	const std::string json = R"([
		{"name":"kstuff-lite","filename":"kstuff-lite_v1.11.elf",
		 "url":"https://example.org/kstuff-lite_v1.11.elf","source":"https://example.org/releases",
		 "description":"Lite version","last_update":"2026-09-20","version":"v1.11",
		 "category":"System & Jailbreak","checksum":"AB9A"},
		{"name":"no-url","filename":"x.elf"},
		{"name":"sneaky","filename":"../../etc/passwd","url":"https://example.org/x"},
		{"name":"plain","filename":"plain.elf","url":"http://example.org/plain.elf"},
		{"name":"ftpsrv","filename":"ftpsrv_v0.21.1.elf","url":"https://example.org/ftpsrv.elf"}
	])";
	std::string error;
	const std::vector<CatalogPayload> list = parseCatalog(json, &error);
	CHECK_EQ(list.size(), size_t(2)); // no address, a path, or not https: left out
	CHECK_EQ(list[0].checksum, std::string("ab9a"));
	CHECK_EQ(list[1].category, std::string("Uncategorized"));
	CHECK(parseCatalog("not json", &error).empty());
	CHECK(!error.empty());

	CHECK(isFileOf(list[0], "kstuff-lite_v1.11.elf"));
	CHECK(isFileOf(list[0], "KSTUFF-LITE_v1.10.elf"));
	CHECK(isFileOf(list[0], "kstuff-lite.elf"));
	CHECK(isFileOf(list[1], "ftpsrv.elf"));
	CHECK(!isFileOf(list[1], "ftpsrv-drakmor_1.16.elf"));
	CHECK(!isFileOf(list[1], "ftpsrvx.elf"));

	// What PLK's Payload Manager keeps beside a payload reads back.
	std::string name;
	std::string version;
	readDetails(detailsJson(list[0], "2026-10-10T12:00:00Z"), &name, &version);
	CHECK_EQ(name, std::string("kstuff-lite"));
	CHECK_EQ(version, std::string("v1.11"));
}

ORBISLINK_TEST(edits_an_autoload_sequence)
{
	using namespace payloads;
	const std::string text = "# boot\n!3000\nkstuff.elf\n!500\n!500\nftpsrv.elf\nwebsrv.elf\n";
	const std::vector<AutoloadStep> steps = autoloadSteps(text);
	CHECK_EQ(steps.size(), size_t(3));
	CHECK_EQ(steps[0].delayMs, 3000);
	CHECK_EQ(steps[1].delayMs, 1000); // two waits in a row add up
	CHECK_EQ(steps[2].delayMs, 0);

	// Reordered and a wait changed: the comment stays on top.
	std::vector<AutoloadStep> moved = { steps[1], steps[0], steps[2] };
	moved[0].delayMs = 0;
	CHECK_EQ(autoloadText(moved, text), std::string("# boot\nftpsrv.elf\n!3000\nkstuff.elf\nwebsrv.elf\n"));
	CHECK_EQ(autoloadText({ { "a.elf", 0 } }, "x.elf\r\n"), std::string("a.elf\r\n"));
}

TEST_MAIN()
