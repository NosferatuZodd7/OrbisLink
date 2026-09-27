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

// Rotating log (§8 of the specification: 5 files x 5 MB, debug off by
// default). Thread-safe; used by every core module.
class Logger
{
public:
	static Logger &instance();

	void setLevel(LogLevel level);
	LogLevel level() const;

	// Turns on writing to a file with rotation. Returns false if it cannot open it.
	bool setFile(const std::string &path, uint64_t maxBytes = 5ull * 1024 * 1024, int maxFiles = 5);
	void setConsoleOutput(bool enabled);
	// Extra receiver (the UI hooks in here to show the log in the diagnostics window).
	void setSink(std::function<void(LogLevel, const std::string &)> sink);

	void log(LogLevel level, const std::string &message);

	// The last lines, kept in memory. They feed the diagnostics window and
	// the exported report without re-reading the file — which may already
	// have rotated, or be in a folder the person cannot find.
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

// Removes sensitive data (Account ID, registration keys, tokens) before
// writing to the log or exporting diagnostics (§8 and §9).
std::string redactSensitive(const std::string &text);

} // namespace orbislink
