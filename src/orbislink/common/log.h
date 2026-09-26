// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace orbislink {

enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3, Off = 4 };

const char *logLevelName(LogLevel level);

// Log rotativo (§8 da especificação: 5 ficheiros x 5 MB, debug desligado por
// omissão). Thread-safe; usado por todos os módulos do núcleo.
class Logger
{
public:
	static Logger &instance();

	void setLevel(LogLevel level);
	LogLevel level() const;

	// Ativa escrita em ficheiro com rotação. Devolve false se não conseguir abrir.
	bool setFile(const std::string &path, uint64_t maxBytes = 5ull * 1024 * 1024, int maxFiles = 5);
	void setConsoleOutput(bool enabled);
	// Recetor extra (a UI liga-se aqui para mostrar o log na janela de diagnóstico).
	void setSink(std::function<void(LogLevel, const std::string &)> sink);

	void log(LogLevel level, const std::string &message);

	// As últimas linhas, guardadas em memória. Servem para a janela de
	// diagnóstico e para o relatório de exportação sem ter de reler o
	// ficheiro — que pode já ter rodado, ou estar numa pasta que a pessoa
	// não sabe encontrar.
	std::vector<std::string> recent(size_t max = 0) const;
	void setRecentCapacity(size_t lines);

private:
	Logger() = default;
	void rotateIfNeeded();

	mutable std::mutex mutex_;
	LogLevel level_ = LogLevel::Info;
	bool console_ = true;
	std::string path_;
	std::ofstream file_;
	uint64_t written_ = 0;
	uint64_t maxBytes_ = 5ull * 1024 * 1024;
	int maxFiles_ = 5;
	std::function<void(LogLevel, const std::string &)> sink_;
	std::deque<std::string> recent_;
	size_t recentCapacity_ = 500;
};

void logDebug(const std::string &message);
void logInfo(const std::string &message);
void logWarning(const std::string &message);
void logError(const std::string &message);

// Remove dados sensíveis (Account ID, chaves de registo, tokens) antes de
// escrever no log ou de exportar diagnóstico (§8 e §9).
std::string redactSensitive(const std::string &text);

} // namespace orbislink
