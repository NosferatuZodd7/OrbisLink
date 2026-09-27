// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/console/console_manager.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/net/net_utils.h"

namespace orbislink {

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

ProbeResult probeConsoleServices(const std::string &address, uint16_t ftpPort,
	uint16_t installerPort, int timeoutMs)
{
	ProbeResult result;
	if(trim(address).empty())
	{
		result.ftpDetail = result.installerDetail = "no console address";
		return result;
	}

	// FTP: TCP connection to the FTP port + banner (§5.1). The connection alone
	// says the service is up; the banner only enriches the message.
	std::string banner;
	result.ftpOk = tcpProbe(address, ftpPort, timeoutMs, &banner);
	const std::string trimmed = trim(banner);
	if(result.ftpOk)
		result.ftpDetail = trimmed.empty() ? "connected, no banner" : trimmed;
	else
		result.ftpDetail = trimmed;

	// Installer: any HTTP reply on 12800 counts as available.
	RpiClient::Config rpiConfig;
	rpiConfig.host = address;
	rpiConfig.port = installerPort;
	rpiConfig.timeoutMs = timeoutMs + 1000;
	rpiConfig.maxAttempts = 1;
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
	}
	else
	{
		const ProbeResult probe =
			probeConsoleServices(cfg.consoleAddress, cfg.ftpPort, cfg.installerPort);

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
