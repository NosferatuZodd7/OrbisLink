// SPDX-License-Identifier: AGPL-3.0-or-later
//
// orbislink-cli — acesso ao núcleo do OrbisLink sem interface gráfica.
// Serve para desenvolvimento, diagnóstico e para os testes de integração
// contra a consola falsa (tools/mock-console).

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

Uso: orbislink-cli <comando> [opções]

Comandos:
  inspect <ficheiro.pkg...>            Mostra os metadados dos pkg.
  services --host <ip>                 Verifica FTP (2121) e instalador (12800).
  serve --host <ip> <ficheiro.pkg>     Serve um pkg e imprime o URL (não instala).
  install --host <ip> <ficheiro...>    Instalação direta: serve e instala pela fila.
                                       Com --ftp envia por FTP em vez de instalar;
                                       junta --install-after-upload para fazer as duas.
  ftp-ls --host <ip> [caminho]         Lista uma pasta da consola.
  ftp-put --host <ip> <local> [remoto] Envia um ficheiro por FTP.
  ftp-get --host <ip> <remoto> <local> Descarrega um ficheiro por FTP.
  ftp-rm --host <ip> <remoto>          Apaga um ficheiro na consola.
  ftp-mkdir --host <ip> <remoto>       Cria uma pasta na consola.

Opções comuns:
  --ftp-port <n>        (por omissão 2121)
  --installer-port <n>  (por omissão 12800)
  --http-port <n>       (por omissão 8765)
  --bind <ip>           Interface do servidor HTTP local (por omissão, a da consola)
  --remote-dir <path>   Pasta de destino no FTP (por omissão /data/pkg/)
  --timeout <segundos>  Tempo máximo à espera da instalação (por omissão 600)
  --debug               Log detalhado
  --advanced            Permite escrever nas zonas protegidas do sistema
  --ftp                 No "install": envia por FTP em vez de instalar
  --install-after-upload  Depois do envio por FTP, instala a partir do PC
  --delete-after-install  E apaga da consola a cópia enviada
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
		std::cerr << "Indica pelo menos um ficheiro .pkg.\n";
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
			std::cout << "  ERRO: " << info.error << "\n";
			++failures;
			continue;
		}
		std::cout << "  Título      : " << info.displayTitle() << "\n"
				  << "  TITLE_ID    : " << (info.titleId.empty() ? "(desconhecido)" : info.titleId) << "\n"
				  << "  CONTENT_ID  : " << info.contentId << "\n"
				  << "  Tipo        : " << pkgCategoryLabelPt(info.kind) << " ("
				  << (info.category.empty() ? pkgCategoryCode(info.kind) : info.category) << ")\n"
				  << "  Versão      : " << (info.appVersion.empty() ? "-" : info.appVersion) << "\n"
				  << "  Tamanho     : " << humanBytes(info.fileSize) << "\n"
				  << "  Ícone       : " << (info.iconPng.empty() ? "não" : humanBytes(static_cast<int64_t>(info.iconPng.size())))
				  << "\n";
	}
	return failures == 0 ? 0 : 1;
}

int commandServices(const Options &options)
{
	const Settings settings = settingsFromOptions(options);
	if(settings.consoleAddress.empty())
	{
		std::cerr << "Falta --host.\n";
		return 2;
	}
	ConsoleManager manager(settings);
	const ConsoleStatus status = manager.checkNow();
	std::cout << serviceStateSymbol(status.ftp.state) << " FTP " << settings.ftpPort << " — "
			  << (status.ftp.detail.empty() ? status.ftp.hint : status.ftp.detail) << "\n"
			  << serviceStateSymbol(status.installer.state) << " Instalador "
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
		std::cerr << "Indica o ficheiro a servir.\n";
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
		std::cerr << "Não foi possível arrancar o servidor HTTP: " << error << "\n";
		return 1;
	}
	for(const std::string &path : options.positional)
	{
		const std::string token = server.registerFile(path, baseName(path));
		if(token.empty())
		{
			std::cerr << "Ficheiro inacessível: " << path << "\n";
			continue;
		}
		std::cout << server.urlForToken(token) << "\n";
	}
	std::cout << "Ctrl+C para parar.\n";
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
		std::cerr << "Uso: orbislink-cli install --host <ip> <ficheiro.pkg...>\n";
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
		std::cerr << "Não foi possível arrancar o servidor HTTP local: " << error << "\n";
		return 1;
	}

	RpiClient::Config rpiConfig;
	rpiConfig.host = settings.consoleAddress;
	rpiConfig.port = settings.installerPort;
	RpiClient installer(rpiConfig);

	FtpClient ftp(ftpConfigFrom(settings));
	const bool porFtp = options.has("ftp");

	// Um envio só por FTP não precisa do instalador; com
	// --install-after-upload precisa, porque instala a seguir.
	if(!porFtp || settings.installAfterUpload)
	{
		std::string detail;
		if(!installer.probe(&detail))
		{
			std::cerr << "Instalador remoto indisponível. Abre o Remote Package Installer na consola. ("
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
		std::cerr << "Recusado — " << reason << "\n";
	if(ids.empty())
	{
		server.stop();
		return 1;
	}

	queue.setListener([](const QueueTask &task) {
		std::cout << "\r" << std::left << std::setw(28)
				  << (task.title.size() > 26 ? task.title.substr(0, 26) : task.title) << " "
				  << std::setw(12) << taskStateLabelPt(task.state) << " " << progressBar(task.percent())
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
		std::cerr << "Tempo esgotado à espera da instalação.\n";
		return 1;
	}
	return failures == 0 ? 0 : 1;
}

int commandFtp(const std::string &command, const Options &options)
{
	const Settings settings = settingsFromOptions(options);
	if(settings.consoleAddress.empty())
	{
		std::cerr << "Falta --host.\n";
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
			std::cerr << "Uso: ftp-put --host <ip> <local> [remoto]\n";
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
		std::cout << "Enviado para " << remote << "\n";
		return 0;
	}

	if(command == "ftp-get")
	{
		if(options.positional.size() < 2)
		{
			std::cerr << "Uso: ftp-get --host <ip> <remoto> <local>\n";
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
			std::cerr << "Indica o caminho remoto.\n";
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

	std::cerr << "Comando desconhecido: " << command << "\n";
	printUsage();
	return 2;
}
