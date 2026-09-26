// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/log.h"

#include <chrono>
#include <cstdio>
#include <vector>
#include <ctime>
#include <iostream>
#include <regex>

namespace orbislink {

const char *logLevelName(LogLevel level)
{
	switch(level)
	{
		case LogLevel::Debug: return "DEBUG";
		case LogLevel::Info: return "INFO";
		case LogLevel::Warning: return "AVISO";
		case LogLevel::Error: return "ERRO";
		case LogLevel::Off: return "OFF";
	}
	return "?";
}

Logger &Logger::instance()
{
	static Logger logger;
	return logger;
}

void Logger::setLevel(LogLevel level)
{
	std::lock_guard<std::mutex> lock(mutex_);
	level_ = level;
}

LogLevel Logger::level() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return level_;
}

void Logger::setConsoleOutput(bool enabled)
{
	std::lock_guard<std::mutex> lock(mutex_);
	console_ = enabled;
}

void Logger::setSink(std::function<void(LogLevel, const std::string &)> sink)
{
	std::lock_guard<std::mutex> lock(mutex_);
	sink_ = std::move(sink);
}

bool Logger::setFile(const std::string &path, uint64_t maxBytes, int maxFiles)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if(file_.is_open())
		file_.close();
	path_ = path;
	maxBytes_ = maxBytes > 0 ? maxBytes : 1;
	maxFiles_ = maxFiles > 1 ? maxFiles : 1;
	file_.open(path_, std::ios::app | std::ios::binary);
	if(!file_.is_open())
		return false;
	file_.seekp(0, std::ios::end);
	written_ = static_cast<uint64_t>(file_.tellp());
	return true;
}

void Logger::rotateIfNeeded()
{
	if(path_.empty() || written_ < maxBytes_)
		return;
	file_.close();
	// orbislink.log -> orbislink.log.1 -> ... -> orbislink.log.(maxFiles_-1)
	std::string oldest = path_ + "." + std::to_string(maxFiles_ - 1);
	std::remove(oldest.c_str());
	for(int i = maxFiles_ - 2; i >= 1; --i)
	{
		std::string from = path_ + "." + std::to_string(i);
		std::string to = path_ + "." + std::to_string(i + 1);
		std::rename(from.c_str(), to.c_str());
	}
	std::string first = path_ + ".1";
	std::rename(path_.c_str(), first.c_str());
	file_.open(path_, std::ios::trunc | std::ios::binary);
	written_ = 0;
}

void Logger::log(LogLevel level, const std::string &message)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if(level < level_ || level_ == LogLevel::Off)
		return;

	auto now = std::chrono::system_clock::now();
	auto seconds = std::chrono::system_clock::to_time_t(now);
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
	std::tm tm {};
#ifdef _WIN32
	gmtime_s(&tm, &seconds);
#else
	gmtime_r(&seconds, &tm);
#endif
	char stamp[80];
	std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900,
		tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(millis));

	const std::string safe = redactSensitive(message);
	const std::string line = std::string(stamp) + " [" + logLevelName(level) + "] " + safe + "\n";

	if(console_)
	{
		std::ostream &out = level >= LogLevel::Warning ? std::cerr : std::cout;
		out << line;
		out.flush();
	}
	if(file_.is_open())
	{
		file_ << line;
		file_.flush();
		written_ += line.size();
		rotateIfNeeded();
	}
	recent_.push_back(line.substr(0, line.size() - 1));
	while(recent_.size() > recentCapacity_)
		recent_.pop_front();

	if(sink_)
		sink_(level, safe);
}

std::vector<std::string> Logger::recent(size_t max) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if(max == 0 || max >= recent_.size())
		return std::vector<std::string>(recent_.begin(), recent_.end());
	return std::vector<std::string>(recent_.end() - static_cast<long>(max), recent_.end());
}

void Logger::setRecentCapacity(size_t lines)
{
	std::lock_guard<std::mutex> lock(mutex_);
	recentCapacity_ = lines > 0 ? lines : 1;
	while(recent_.size() > recentCapacity_)
		recent_.pop_front();
}

void logDebug(const std::string &message) { Logger::instance().log(LogLevel::Debug, message); }
void logInfo(const std::string &message) { Logger::instance().log(LogLevel::Info, message); }
void logWarning(const std::string &message) { Logger::instance().log(LogLevel::Warning, message); }
void logError(const std::string &message) { Logger::instance().log(LogLevel::Error, message); }

std::string redactSensitive(const std::string &text)
{
	// Construído uma vez e dentro de um try: se a biblioteca de expressões
	// regulares desta plataforma recusar o padrão, perde-se a redação mas não
	// se perde a aplicação. (Sem isto, um regex_error aqui mataria o processo
	// em silêncio na primeira mensagem escrita.)
	static bool available = true;
	if(!available)
		return text;

	static const std::vector<std::regex> patterns = []() {
		std::vector<std::regex> compiled;
		try
		{
			// Chaves/segredos em pares chave=valor ou JSON.
			compiled.emplace_back(
				R"((?:"?)(account_?id|psn_?account_?id|rp_?key|rp_?regist_?key|regist_?key|morning|apssid|ap_?bssid|ap_?key|ap_?name|user_?credential|password|token)("?\s*[:=]\s*)("?)([^",}\s]+))",
				std::regex::icase);
		}
		catch(const std::exception &)
		{
			compiled.clear();
		}
		return compiled;
	}();
	if(patterns.empty())
		return text;
	try
	{
		std::string out = text;
		for(const auto &re : patterns)
			out = std::regex_replace(out, re, "$1$2$3[REDIGIDO]");
		return out;
	}
	catch(const std::exception &)
	{
		available = false;
		return text;
	}
}

} // namespace orbislink
