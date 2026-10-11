// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/console/console_manager.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/net/net_utils.h"

#include <algorithm>

namespace orbislink {

namespace {

// How often FTP is looked for on the other ports while the set one is silent.
constexpr int64_t kFtpSearchEveryMs = 60 * 1000;

bool activeConsoleIsPs5(const Settings &settings)
{
	for(const ConsoleEntry &console : settings.consoles)
		if(console.address == settings.consoleAddress)
			return console.type == "ps5";
	return false;
}

} // namespace

const char *serviceStateSymbol(ServiceState state)
{
	switch(state)
	{
		case ServiceState::Available: return "🟢";
		case ServiceState::Checking: return "🟡";
		case ServiceState::Unavailable: return "🔴";
		case ServiceState::Unknown: break;
	}
	return "🟡";
}

std::vector<uint16_t> ftpPortCandidates(uint16_t preferred, bool ps5)
{
	std::vector<uint16_t> ports;
	auto add = [&ports](uint16_t port) {
		if(port != 0 && std::find(ports.begin(), ports.end(), port) == ports.end())
			ports.push_back(port);
	};
	add(preferred);
	add(ps5 ? 1337 : 2121);
	add(ps5 ? 2121 : 1337);
	add(21);
	if(ps5)
		add(9090);
	return ports;
}

std::vector<PortAnswer> askPorts(const std::string &address, const std::vector<uint16_t> &ports,
	int timeoutMs)
{
	std::vector<PortAnswer> answers(ports.size());
	std::vector<std::thread> threads;
	threads.reserve(ports.size());
	for(size_t i = 0; i < ports.size(); ++i)
	{
		answers[i].port = ports[i];
		threads.emplace_back([&address, &answers, i, timeoutMs]() {
			std::string greeting;
			answers[i].open = tcpProbe(address, answers[i].port, timeoutMs, &greeting);
			answers[i].greeting = trim(greeting);
		});
	}
	for(std::thread &thread : threads)
		thread.join();
	return answers;
}

uint16_t pickFtpPort(const std::vector<PortAnswer> &answers, uint16_t preferred)
{
	for(const PortAnswer &answer : answers)
		if(answer.port == preferred && answer.ftp())
			return preferred;
	for(const PortAnswer &answer : answers)
		if(answer.ftp())
			return answer.port;
	for(const PortAnswer &answer : answers)
		if(answer.port == preferred && answer.open && answer.greeting.empty())
			return preferred;
	return 0;
}

std::string knownPortUse(uint16_t port, bool ps5)
{
	switch(port)
	{
		case 21: return "the standard FTP port";
		case 1337: return ps5 ? "etaHEN's FTP (FTP=1 in config.ini)" : "FTP payloads";
		case 2121: return ps5 ? "the ftpsrv payload's FTP" : "GoldHEN's FTP";
		case 9090: return ps5 ? "etaHEN's DPI v1 package installer (DPI=1), not FTP" : "GoldHEN's payload loader";
		case 12800: return ps5 ? "etaHEN's DPI v2 installer" : "Remote Package Installer";
		default: return std::string();
	}
}

ProbeResult probeConsoleServices(const std::string &address, uint16_t ftpPort,
	uint16_t installerPort, int timeoutMs, const std::vector<uint16_t> &otherFtpPorts, bool ps5)
{
	ProbeResult result;
	if(trim(address).empty())
	{
		result.ftpDetail = result.installerDetail = "no console address";
		return result;
	}

	// FTP: a connection to each port and the greeting it sends (§5.1). An
	// FTP server greets with "220"; one that takes the connection and says
	// nothing still counts on the set port (it may be busy), never on another.
	std::vector<uint16_t> ports { ftpPort };
	for(uint16_t port : otherFtpPorts)
		if(port != 0 && std::find(ports.begin(), ports.end(), port) == ports.end())
			ports.push_back(port);
	result.ftpPorts = askPorts(address, ports, timeoutMs);
	result.ftpPort = pickFtpPort(result.ftpPorts, ftpPort);
	result.ftpOk = result.ftpPort != 0;
	const uint16_t shown = result.ftpOk ? result.ftpPort : ftpPort;
	for(const PortAnswer &answer : result.ftpPorts)
		if(answer.port == shown)
			result.ftpDetail = result.ftpOk && answer.greeting.empty() ? "connected, no banner" : answer.greeting;

	// Installer: any HTTP reply on 12800 counts as available.
	RpiClient::Config rpiConfig;
	rpiConfig.host = address;
	rpiConfig.port = installerPort;
	rpiConfig.timeoutMs = timeoutMs + 1000;
	rpiConfig.maxAttempts = 1;
	rpiConfig.ps5 = ps5;
	RpiClient installer(rpiConfig);
	std::string detail;
	result.installerOk = installer.probe(&detail);
	result.installerDetail = detail;
	return result;
}

ConsoleManager::ConsoleManager(Settings settings) : settings_(std::move(settings))
{
	status_.ftp.hint = QT_TRANSLATE_NOOP("Messages", "FTP unavailable — check that GoldHEN is "
		"loaded and FTP is enabled.");
	status_.installer.hint =
		QT_TRANSLATE_NOOP("Messages", "Remote installer unavailable. Open Remote Package Installer on "
			"the console.");
	status_.remotePlay.hint = QT_TRANSLATE_NOOP("Messages", "Remote Play unavailable — turn the "
		"console on and check the registration.");
}

ConsoleManager::~ConsoleManager() { stop(); }

void ConsoleManager::setSettings(const Settings &settings)
{
	std::lock_guard<std::mutex> lock(mutex_);
	settings_ = settings;
}

Settings ConsoleManager::settings() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return settings_;
}

void ConsoleManager::setAddress(const std::string &address)
{
	std::lock_guard<std::mutex> lock(mutex_);
	settings_.consoleAddress = address;
}

std::string ConsoleManager::address() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return settings_.consoleAddress;
}

ConsoleStatus ConsoleManager::status() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return status_;
}

void ConsoleManager::setListener(std::function<void(const ConsoleStatus &)> listener)
{
	std::lock_guard<std::mutex> lock(mutex_);
	listener_ = std::move(listener);
}

void ConsoleManager::setRemotePlayState(RemotePlayState state, const std::string &detail)
{
	ConsoleStatus copy;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		switch(state)
		{
			case RemotePlayState::Ready:
				status_.remotePlay.state = ServiceState::Available;
				status_.remotePlay.hint.clear();
				break;
			case RemotePlayState::Standby:
				status_.remotePlay.state = ServiceState::Unavailable;
				status_.remotePlay.hint = QT_TRANSLATE_NOOP("Messages", "The console is in rest mode — click its "
					"box to wake it.");
				break;
			case RemotePlayState::Offline:
				status_.remotePlay.state = ServiceState::Unavailable;
				status_.remotePlay.hint = QT_TRANSLATE_NOOP("Messages", "The console is off or not on the "
					"network.");
				break;
			case RemotePlayState::Unknown:
				status_.remotePlay.state = ServiceState::Unknown;
				break;
		}
		status_.remotePlay.detail = detail;
		status_.remotePlay.checkedAtMs = monotonicMillis();
		copy = status_;
	}
	publish(copy);
}

ConsoleStatus ConsoleManager::checkNow()
{
	Settings cfg = settings();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		status_.ftp.state = ServiceState::Checking;
		status_.installer.state = ServiceState::Checking;
	}

	ConsoleStatus result = status();
	const int64_t now = monotonicMillis();

	if(cfg.consoleAddress.empty())
	{
		result.ftp.state = ServiceState::Unavailable;
		result.ftp.detail = "no console address";
		result.installer.state = ServiceState::Unavailable;
		result.installer.detail = "no console address";
		result.ftp.checkedAtMs = result.installer.checkedAtMs = now;
		result.ftpPort = 0;
	}
	else
	{
		ProbeResult probe = probeConsoleServices(cfg.consoleAddress, cfg.ftpPort, cfg.installerPort, 2000, {},
			activeConsoleIsPs5(cfg));

		// The set port does not greet as FTP: once a minute at most, the
		// other ports FTP servers use are asked too, and FTP is used where
		// it answers.
		const bool greeted = !probe.ftpPorts.empty() && probe.ftpPorts.front().ftp();
		bool searched = false;
		if(!greeted)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			searched = lastFtpSearchMs_ < 0 || now - lastFtpSearchMs_ >= kFtpSearchEveryMs;
			if(searched)
				lastFtpSearchMs_ = now;
		}
		if(searched)
		{
			std::vector<uint16_t> others = ftpPortCandidates(cfg.ftpPort, activeConsoleIsPs5(cfg));
			others.erase(others.begin());
			std::vector<PortAnswer> answers = probe.ftpPorts;
			const std::vector<PortAnswer> more = askPorts(cfg.consoleAddress, others, 2000);
			answers.insert(answers.end(), more.begin(), more.end());
			const uint16_t found = pickFtpPort(answers, cfg.ftpPort);
			if(found != 0 && found != cfg.ftpPort)
			{
				for(const PortAnswer &answer : answers)
					if(answer.port == found)
						probe.ftpDetail = answer.greeting;
				logInfo("FTP answers on port " + std::to_string(found) + ", not on " + std::to_string(cfg.ftpPort)
					+ " (" + probe.ftpDetail + ")");
				std::lock_guard<std::mutex> lock(mutex_);
				settings_.ftpPort = found;
			}
			probe.ftpPort = found;
			probe.ftpOk = found != 0;
			result.ftpSearch = answers;
			result.ftpSearchAtMs = now;
		}

		result.ftpPort = probe.ftpPort;
		result.ftp.state = probe.ftpOk ? ServiceState::Available : ServiceState::Unavailable;
		result.ftp.detail = probe.ftpDetail;
		result.ftp.hint = probe.ftpOk
			? std::string()
			: QT_TRANSLATE_NOOP("Messages", "FTP unavailable — check that GoldHEN is loaded and FTP is "
				"enabled.");
		result.ftp.checkedAtMs = now;

		result.installer.state =
			probe.installerOk ? ServiceState::Available : ServiceState::Unavailable;
		result.installer.detail = probe.installerDetail;
		result.installer.hint = probe.installerOk
			? std::string()
			: QT_TRANSLATE_NOOP("Messages", "Remote installer unavailable. Open Remote Package Installer "
				"on the console.");
		result.installer.checkedAtMs = now;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		status_.ftp = result.ftp;
		status_.installer = result.installer;
		status_.ftpPort = result.ftpPort;
		status_.ftpSearch = result.ftpSearch;
		status_.ftpSearchAtMs = result.ftpSearchAtMs;
		result = status_;
	}
	publish(result);
	return result;
}

void ConsoleManager::publish(const ConsoleStatus &status)
{
	std::function<void(const ConsoleStatus &)> listener;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		listener = listener_;
	}
	if(listener)
		listener(status);
}

void ConsoleManager::start(int intervalSeconds)
{
	if(running_.exchange(true))
		return;
	thread_ = std::thread(&ConsoleManager::loop, this, intervalSeconds > 0 ? intervalSeconds : 10);
}

void ConsoleManager::stop()
{
	if(!running_.exchange(false))
		return;
	wakeup_.notify_all();
	if(thread_.joinable())
		thread_.join();
}

void ConsoleManager::loop(int intervalSeconds)
{
	while(running_.load())
	{
		checkNow();
		std::unique_lock<std::mutex> lock(wakeupMutex_);
		wakeup_.wait_for(lock, std::chrono::seconds(intervalSeconds),
			[this]() { return !running_.load(); });
	}
}

} // namespace orbislink
