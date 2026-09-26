// SPDX-License-Identifier: AGPL-3.0-or-later
//
// orbislink-cli — access to the OrbisLink core without a graphical interface.
// Used for development, diagnostics and the integration tests against the
// mock console (tools/mock-console).

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/console/console_manager.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/http/local_http_server.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/net/net_utils.h"
#include "orbislink/pkg/pkg_inspector.h"
#include "orbislink/queue/install_queue.h"
#include "orbislink/settings/settings_store.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

using namespace orbislink;

namespace {

struct Options
{
	std::map<std::string, std::string> values;
	std::vector<std::string> positional;

	std::string get(const std::string &key, const std::string &fallback = std::string()) const
	{
		auto it = values.find(key);
		return it == values.end() ? fallback : it->second;
	}
	bool has(const std::string &key) const { return values.find(key) != values.end(); }
	int getInt(const std::string &key, int fallback) const
	{
		auto it = values.find(key);
		if(it == values.end())
			return fallback;
		try { return std::stoi(it->second); }
		catch(...) { return fallback; }
	}
};

Options parseOptions(int argc, char **argv, int from)
{
	Options options;
	for(int i = from; i < argc; ++i)
	{
		std::string argument = argv[i];
		if(startsWith(argument, "--"))
		{
			const size_t equals = argument.find('=');
			std::string key = argument.substr(2, equals == std::string::npos ? std::string::npos : equals - 2);
			if(equals != std::string::npos)
				options.values[key] = argument.substr(equals + 1);
			else if(i + 1 < argc && !startsWith(argv[i + 1], "--"))
				options.values[key] = argv[++i];
			else
				options.values[key] = "1";
		}
		else
			options.positional.push_back(argument);
	}
	return options;
}

void printUsage()
{
	std::cout << R"(OrbisLink CLI

Usage: orbislink-cli <command> [options]

Commands:
  inspect <file.pkg...>                Shows the pkg metadata.
  services --host <ip>                 Checks FTP (2121) and the installer (12800).
  serve --host <ip> <file.pkg>         Serves a pkg and prints the URL (does not install).
  install --host <ip> <file...>        Direct install: serves and installs through the queue.
                                       With --ftp it uploads over FTP instead of installing;
                                       add --install-after-upload to do both.
  ftp-ls --host <ip> [path]            Lists a folder on the console.
  ftp-put --host <ip> <local> [remote] Uploads a file over FTP.
  ftp-get --host <ip> <remote> <local> Downloads a file over FTP.
  ftp-rm --host <ip> <remote>          Deletes a file on the console.
  ftp-mkdir --host <ip> <remote>       Creates a folder on the console.

Common options:
  --ftp-port <n>        (default 2121)
  --installer-port <n>  (default 12800)
  --http-port <n>       (default 8765)
  --bind <ip>           Local HTTP server interface (default: the one facing the console)
  --remote-dir <path>   Destination folder on FTP (default /data/pkg/)
  --timeout <seconds>   Maximum wait for the installation (default 600)
  --debug               Detailed log
  --advanced            Allows writing to the protected system areas
  --ftp                 For "install": upload over FTP instead of installing
  --install-after-upload  After the FTP upload, install from the PC
  --delete-after-install  And delete the uploaded copy from the console
)";
}

std::string progressBar(double percent)
{
	const int width = 24;
	const int filled = static_cast<int>(percent / 100.0 * width);
	std::string bar(static_cast<size_t>(width), '.');
	for(int i = 0; i < filled && i < width; ++i)
		bar[static_cast<size_t>(i)] = '#';
	std::ostringstream os;
	os << "[" << bar << "] " << std::fixed << std::setprecision(1) << percent << "%";
	return os.str();
}

Settings settingsFromOptions(const Options &options)
{
	Settings settings;
	settings.consoleAddress = options.get("host");
	settings.ftpPort = static_cast<uint16_t>(options.getInt("ftp-port", 2121));
	settings.installerPort = static_cast<uint16_t>(options.getInt("installer-port", 12800));
	settings.httpPort = static_cast<uint16_t>(options.getInt("http-port", 8765));
	settings.ftpUploadDirectory = options.get("remote-dir", "/data/pkg/");
	settings.ftpAdvancedMode = options.has("advanced");
	settings.checkAlreadyInstalled = !options.has("no-exists-check");
	settings.installAfterUpload = options.has("install-after-upload");
	settings.deleteFromConsoleAfterInstall = options.has("delete-after-install");
	return settings;
}

FtpClient::Config ftpConfigFrom(const Settings &settings)
{
	FtpClient::Config config;
	config.host = settings.consoleAddress;
	config.port = settings.ftpPort;
	config.advancedMode = settings.ftpAdvancedMode;
	return config;
}

int commandInspect(const Options &options)
{
	if(options.positional.empty())
	{
		std::cerr << "Give at least one .pkg file.\n";
		return 2;
	}
	PkgInspector inspector;
	int failures = 0;
	for(const std::string &path : options.positional)
	{
		const PkgInfo info = inspector.inspect(path);
		std::cout << baseName(path) << "\n";
		if(!info.valid)
		{
			std::cout << "  ERROR: " << info.error << "\n";
			++failures;
			continue;
		}
		std::cout << "  Title       : " << info.displayTitle() << "\n"
				  << "  TITLE_ID    : " << (info.titleId.empty() ? "(unknown)" : info.titleId) << "\n"
				  << "  CONTENT_ID  : " << info.contentId << "\n"
				  << "  Type        : " << pkgCategoryLabel(info.kind) << " ("
				  << (info.category.empty() ? pkgCategoryCode(info.kind) : info.category) << ")\n"
				  << "  Version     : " << (info.appVersion.empty() ? "-" : info.appVersion) << "\n"
				  << "  Size        : " << humanBytes(info.fileSize) << "\n"
				  << "  Icon        : " << (info.iconPng.empty() ? "no" : humanBytes(static_cast<int64_t>(info.iconPng.size())))
				  << "\n";
	}
	return failures == 0 ? 0 : 1;
}

int commandServices(const Options &options)
{
	const Settings settings = settingsFromOptions(options);
	if(settings.consoleAddress.empty())
	{
		std::cerr << "Missing --host.\n";
		return 2;
	}
	ConsoleManager manager(settings);
	const ConsoleStatus status = manager.checkNow();
	std::cout << serviceStateSymbol(status.ftp.state) << " FTP " << settings.ftpPort << " — "
			  << (status.ftp.detail.empty() ? status.ftp.hint : status.ftp.detail) << "\n"
			  << serviceStateSymbol(status.installer.state) << " Installer "
			  << settings.installerPort << " — "
			  << (status.installer.detail.empty() ? status.installer.hint : status.installer.detail)
			  << "\n";
	return (status.canUseFtp() || status.canInstallDirectly()) ? 0 : 1;
}

std::string bindAddressFor(const Options &options, const std::string &consoleAddress)
{
	if(options.has("bind"))
		return options.get("bind");
	if(consoleAddress == "127.0.0.1" || consoleAddress == "localhost")
		return "127.0.0.1";
	const std::string local = localAddressForConsole(consoleAddress);
	return local.empty() ? "127.0.0.1" : local;
}

int commandServe(const Options &options)
{
	const Settings settings = settingsFromOptions(options);
	if(options.positional.empty())
	{
		std::cerr << "Give the file to serve.\n";
		return 2;
	}
	LocalHttpServer server;
	LocalHttpServer::Config config;
	config.bindAddress = bindAddressFor(options, settings.consoleAddress);
	config.port = settings.httpPort;
	config.allowedClient = settings.restrictToConsoleIp ? settings.consoleAddress : std::string();
	std::string error;
	if(!server.start(config, &error))
	{
		std::cerr << "Could not start the HTTP server: " << error << "\n";
		return 1;
	}
	for(const std::string &path : options.positional)
	{
		const std::string token = server.registerFile(path, baseName(path));
		if(token.empty())
		{
			std::cerr << "File not accessible: " << path << "\n";
			continue;
		}
		std::cout << server.urlForToken(token) << "\n";
	}
	std::cout << "Ctrl+C to stop.\n";
	const int seconds = options.getInt("timeout", 3600);
	std::this_thread::sleep_for(std::chrono::seconds(seconds));
	server.stop();
	return 0;
}

int commandInstall(const Options &options)
{
	const Settings settings = settingsFromOptions(options);
	if(settings.consoleAddress.empty() || options.positional.empty())
	{
		std::cerr << "Usage: orbislink-cli install --host <ip> <file.pkg...>\n";
		return 2;
	}

	LocalHttpServer server;
	LocalHttpServer::Config httpConfig;
	httpConfig.bindAddress = bindAddressFor(options, settings.consoleAddress);
	httpConfig.port = settings.httpPort;
	httpConfig.allowedClient = settings.restrictToConsoleIp ? settings.consoleAddress : std::string();
	std::string error;
	if(!server.start(httpConfig, &error))
	{
		std::cerr << "Could not start the local HTTP server: " << error << "\n";
		return 1;
	}

	RpiClient::Config rpiConfig;
	rpiConfig.host = settings.consoleAddress;
	rpiConfig.port = settings.installerPort;
	RpiClient installer(rpiConfig);

	FtpClient ftp(ftpConfigFrom(settings));
	const bool porFtp = options.has("ftp");

	// An FTP-only upload does not need the installer; with
	// --install-after-upload it does, because it installs afterwards.
	if(!porFtp || settings.installAfterUpload)
	{
		std::string detail;
		if(!installer.probe(&detail))
		{
			std::cerr << "Remote installer unavailable. Open Remote Package Installer on the console. ("
					  << detail << ")\n";
			server.stop();
			return 1;
		}
	}

	InstallQueue::Dependencies deps;
	deps.httpServer = &server;
	deps.installer = &installer;
	if(porFtp)
		deps.ftp = &ftp;
	InstallQueue queue(deps, settings);

	std::vector<std::string> rejected;
	const auto ids = queue.enqueue(options.positional,
		porFtp ? TransferMode::FtpUpload : TransferMode::DirectInstall, &rejected);
	for(const std::string &reason : rejected)
		std::cerr << "Rejected — " << reason << "\n";
	if(ids.empty())
	{
		server.stop();
		return 1;
	}

	queue.setListener([](const QueueTask &task) {
		std::cout << "\r" << std::left << std::setw(28)
				  << (task.title.size() > 26 ? task.title.substr(0, 26) : task.title) << " "
				  << std::setw(12) << taskStateLabel(task.state) << " " << progressBar(task.percent())
				  << "      " << std::flush;
		if(task.isTerminal())
			std::cout << "\n" << (task.message.empty() ? "" : "  " + task.message + "\n") << std::flush;
	});
	queue.start();

	const auto deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(options.getInt("timeout", 600));
	bool timedOut = false;
	for(;;)
	{
		if(queue.tasks().empty())
			break;
		if(std::chrono::steady_clock::now() > deadline)
		{
			timedOut = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	queue.stop();
	server.stop();

	int failures = 0;
	for(const QueueTask &task : queue.history())
	{
		if(task.state != TaskState::Completed)
			++failures;
	}
	if(timedOut)
	{
		std::cerr << "Timed out waiting for the installation.\n";
		return 1;
	}
	return failures == 0 ? 0 : 1;
}

int commandFtp(const std::string &command, const Options &options)
{
	const Settings settings = settingsFromOptions(options);
	if(settings.consoleAddress.empty())
	{
		std::cerr << "Missing --host.\n";
		return 2;
	}
	FtpClient client(ftpConfigFrom(settings));

	if(command == "ftp-ls")
	{
		const std::string path = options.positional.empty() ? "/" : options.positional[0];
		std::vector<FtpEntry> entries;
		const FtpResult result = client.list(path, &entries);
		if(!result.ok)
		{
			std::cerr << "FTP: " << result.message << "\n";
			return 1;
		}
		for(const FtpEntry &entry : entries)
		{
			std::cout << (entry.isDirectory ? "d " : "- ") << std::setw(12) << std::right
					  << (entry.isDirectory ? std::string("-") : humanBytes(entry.size)) << "  "
					  << entry.name << "\n";
		}
		return 0;
	}

	if(command == "ftp-put")
	{
		if(options.positional.empty())
		{
			std::cerr << "Usage: ftp-put --host <ip> <local> [remote]\n";
			return 2;
		}
		const std::string local = options.positional[0];
		const std::string remote = options.positional.size() > 1
			? options.positional[1]
			: normalizeRemotePath(settings.ftpUploadDirectory + "/" + baseName(local));
		const FtpResult result = client.upload(local, remote, [](int64_t done, int64_t total) {
			if(total > 0)
			{
				std::cout << "\r" << progressBar(100.0 * static_cast<double>(done) / static_cast<double>(total))
						  << " " << humanBytes(done) << std::flush;
			}
			return true;
		});
		std::cout << "\n";
		if(!result.ok)
		{
			std::cerr << "FTP: " << result.message << "\n";
			return 1;
		}
		std::cout << "Uploaded to " << remote << "\n";
		return 0;
	}

	if(command == "ftp-get")
	{
		if(options.positional.size() < 2)
		{
			std::cerr << "Usage: ftp-get --host <ip> <remote> <local>\n";
			return 2;
		}
		const FtpResult result = client.download(options.positional[0], options.positional[1],
			[](int64_t done, int64_t total) {
				if(total > 0)
					std::cout << "\r" << progressBar(100.0 * static_cast<double>(done) / static_cast<double>(total)) << std::flush;
				return true;
			});
		std::cout << "\n";
		if(!result.ok)
		{
			std::cerr << "FTP: " << result.message << "\n";
			return 1;
		}
		return 0;
	}

	if(command == "ftp-rm" || command == "ftp-mkdir")
	{
		if(options.positional.empty())
		{
			std::cerr << "Give the remote path.\n";
			return 2;
		}
		const FtpResult result = command == "ftp-rm" ? client.removeFile(options.positional[0])
													 : client.makeDirectory(options.positional[0]);
		if(!result.ok)
		{
			std::cerr << "FTP: " << result.message << "\n";
			return 1;
		}
		return 0;
	}

	printUsage();
	return 2;
}

} // namespace

int main(int argc, char **argv)
{
	if(argc < 2)
	{
		printUsage();
		return 2;
	}

	const std::string command = argv[1];
	const Options options = parseOptions(argc, argv, 2);
	Logger::instance().setLevel(options.has("debug") ? LogLevel::Debug : LogLevel::Warning);

	if(command == "inspect")
		return commandInspect(options);
	if(command == "services")
		return commandServices(options);
	if(command == "serve")
		return commandServe(options);
	if(command == "install")
		return commandInstall(options);
	if(startsWith(command, "ftp-"))
		return commandFtp(command, options);
	if(command == "--help" || command == "-h" || command == "help")
	{
		printUsage();
		return 0;
	}

	std::cerr << "Unknown command: " << command << "\n";
	printUsage();
	return 2;
}
