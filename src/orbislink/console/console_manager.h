// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/settings/settings_store.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace orbislink {

// 🟢 disponível, 🟡 a verificar, 🔴 indisponível (§5.1).
enum class ServiceState { Unknown, Checking, Available, Unavailable };

const char *serviceStateSymbol(ServiceState state);

struct ServiceStatus
{
	ServiceState state = ServiceState::Unknown;
	std::string detail; // resposta do serviço (ex.: "220 GoldHEN FTP")
	std::string hint;   // texto de ajuda para a UI quando está 🔴
	int64_t checkedAtMs = -1;
};

struct ConsoleStatus
{
	ServiceStatus remotePlay;
	ServiceStatus ftp;
	ServiceStatus installer;

	bool canInstallDirectly() const { return installer.state == ServiceState::Available; }
	bool canUseFtp() const { return ftp.state == ServiceState::Available; }
};

// Resultado de uma verificação pontual, sem tocar no estado do gestor: serve
// para confirmar um endereço que o utilizador ainda está a escrever nas
// definições, antes de o guardar.
struct ProbeResult
{
	bool ftpOk = false;
	bool installerOk = false;
	std::string ftpDetail;       // banner do FTP (ex.: "220 GoldHEN FTP")
	std::string installerDetail; // resposta (ou erro) do instalador remoto
	bool anyOk() const { return ftpOk || installerOk; }
	bool allOk() const { return ftpOk && installerOk; }
};

// Liga-se à consola e devolve o que respondeu. `timeoutMs` é o limite do FTP;
// o instalador leva mais um segundo por ser um pedido HTTP completo.
ProbeResult probeConsoleServices(const std::string &address, uint16_t ftpPort,
	uint16_t installerPort, int timeoutMs = 2000);

// Estado do Remote Play tal como o chiaki-ng o reporta.
enum class RemotePlayState { Unknown, Ready, Standby, Offline };

// Perfil da consola e verificação periódica dos serviços.
//
// O Remote Play em si é do chiaki-ng: o estado entra aqui por
// setRemotePlayState(), chamado pelo wrapper da sessão. As portas
// confirmadas do Remote Play (987/UDP descoberta PS4, 9295/TCP controlo e
// registo, 9296/UDP stream, 9297/UDP senkusha) estão em docs/validacao.md.
class ConsoleManager
{
public:
	explicit ConsoleManager(Settings settings);
	~ConsoleManager();

	void setSettings(const Settings &settings);
	Settings settings() const;
	void setAddress(const std::string &address);
	std::string address() const;

	// Verificação periódica (10 s enquanto a app está aberta).
	void start(int intervalSeconds = 10);
	void stop();
	bool running() const { return running_.load(); }

	// Verificação síncrona, usada antes de cada tarefa da fila.
	ConsoleStatus checkNow();
	ConsoleStatus status() const;

	void setRemotePlayState(RemotePlayState state, const std::string &detail = std::string());
	void setListener(std::function<void(const ConsoleStatus &)> listener);

private:
	void loop(int intervalSeconds);
	void publish(const ConsoleStatus &status);

	mutable std::mutex mutex_;
	Settings settings_;
	ConsoleStatus status_;
	std::function<void(const ConsoleStatus &)> listener_;

	std::atomic<bool> running_ { false };
	std::condition_variable wakeup_;
	std::mutex wakeupMutex_;
	std::thread thread_;
};

} // namespace orbislink
