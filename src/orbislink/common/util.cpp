// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/util.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <random>
#include <sstream>
#include <sys/stat.h>

namespace orbislink {

std::string trim(const std::string &s)
{
	size_t begin = 0;
	size_t end = s.size();
	while(begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
		++begin;
	while(end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
		--end;
	return s.substr(begin, end - begin);
}

std::string toLower(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for(char c : s)
		out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	return out;
}

bool iequals(const std::string &a, const std::string &b)
{
	if(a.size() != b.size())
		return false;
	for(size_t i = 0; i < a.size(); ++i)
	{
		if(std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return true;
}

bool startsWith(const std::string &s, const std::string &prefix)
{
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string &s, const std::string &suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string &s, char sep, bool keepEmpty)
{
	std::vector<std::string> parts;
	std::string current;
	for(char c : s)
	{
		if(c == sep)
		{
			if(keepEmpty || !current.empty())
				parts.push_back(current);
			current.clear();
		}
		else
			current.push_back(c);
	}
	if(keepEmpty || !current.empty())
		parts.push_back(current);
	return parts;
}

std::string join(const std::vector<std::string> &parts, const std::string &sep)
{
	std::string out;
	for(size_t i = 0; i < parts.size(); ++i)
	{
		if(i)
			out += sep;
		out += parts[i];
	}
	return out;
}

std::string randomToken(size_t bytes)
{
	static const char *hex = "0123456789abcdef";
	std::random_device rd;
	std::string out;
	out.reserve(bytes * 2);
	for(size_t i = 0; i < bytes; ++i)
	{
		unsigned value = rd() & 0xFFu;
		out.push_back(hex[(value >> 4) & 0xF]);
		out.push_back(hex[value & 0xF]);
	}
	return out;
}

std::string sanitizeFileName(const std::string &name)
{
	std::string base = baseName(name);
	std::string out;
	out.reserve(base.size());
	for(char c : base)
	{
		const bool safe = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
		out.push_back(safe ? c : '_');
	}
	while(!out.empty() && out.front() == '.')
		out.erase(out.begin());
	if(out.empty())
		out = "ficheiro";
	if(out.size() > 120)
		out = out.substr(out.size() - 120);
	return out;
}

std::string urlEncodePath(const std::string &path)
{
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for(unsigned char c : path)
	{
		const bool unreserved = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
		if(unreserved)
			out.push_back(static_cast<char>(c));
		else
		{
			out.push_back('%');
			out.push_back(hex[(c >> 4) & 0xF]);
			out.push_back(hex[c & 0xF]);
		}
	}
	return out;
}

std::string urlDecode(const std::string &text)
{
	std::string out;
	for(size_t i = 0; i < text.size(); ++i)
	{
		if(text[i] == '%' && i + 2 < text.size()
			&& std::isxdigit(static_cast<unsigned char>(text[i + 1]))
			&& std::isxdigit(static_cast<unsigned char>(text[i + 2])))
		{
			auto digit = [](char c) -> int {
				if(c >= '0' && c <= '9') return c - '0';
				if(c >= 'a' && c <= 'f') return c - 'a' + 10;
				return c - 'A' + 10;
			};
			out.push_back(static_cast<char>((digit(text[i + 1]) << 4) | digit(text[i + 2])));
			i += 2;
		}
		else
			out.push_back(text[i]);
	}
	return out;
}

std::string baseName(const std::string &path)
{
	size_t pos = path.find_last_of("/\\");
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string fileExtensionLower(const std::string &path)
{
	const std::string base = baseName(path);
	size_t dot = base.find_last_of('.');
	if(dot == std::string::npos || dot + 1 >= base.size())
		return std::string();
	return toLower(base.substr(dot));
}

std::string joinPath(const std::string &a, const std::string &b)
{
	if(a.empty())
		return b;
	if(b.empty())
		return a;
	const bool aSlash = a.back() == '/' || a.back() == '\\';
	const bool bSlash = b.front() == '/' || b.front() == '\\';
	if(aSlash && bSlash)
		return a + b.substr(1);
	if(!aSlash && !bSlash)
		return a + "/" + b;
	return a + b;
}

std::string normalizeRemotePath(const std::string &path)
{
	std::vector<std::string> stack;
	for(const std::string &part : split(path, '/', false))
	{
		if(part == ".")
			continue;
		if(part == "..")
		{
			if(!stack.empty())
				stack.pop_back();
			continue;
		}
		stack.push_back(part);
	}
	return "/" + join(stack, "/");
}

int64_t fileSize(const std::string &path)
{
#ifdef _WIN32
	struct _stat64 st {};
	if(_stat64(path.c_str(), &st) != 0)
		return -1;
#else
	struct stat st {};
	if(stat(path.c_str(), &st) != 0)
		return -1;
#endif
	return static_cast<int64_t>(st.st_size);
}

bool fileExists(const std::string &path)
{
#ifdef _WIN32
	struct _stat64 st {};
	return _stat64(path.c_str(), &st) == 0 && (st.st_mode & _S_IFREG) != 0;
#else
	struct stat st {};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool directoryExists(const std::string &path)
{
#ifdef _WIN32
	struct _stat64 st {};
	return _stat64(path.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
	struct stat st {};
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::string humanBytes(int64_t bytes)
{
	if(bytes < 0)
		return "?";
	static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
	double value = static_cast<double>(bytes);
	int unit = 0;
	while(value >= 1024.0 && unit < 4)
	{
		value /= 1024.0;
		++unit;
	}
	char buf[48];
	std::snprintf(buf, sizeof(buf), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
	return buf;
}

std::string humanDuration(int64_t seconds)
{
	if(seconds < 0)
		return "?";
	const int64_t h = seconds / 3600;
	const int64_t m = (seconds % 3600) / 60;
	const int64_t s = seconds % 60;
	char buf[48];
	if(h > 0)
		std::snprintf(buf, sizeof(buf), "%lldh%02lldm", static_cast<long long>(h), static_cast<long long>(m));
	else if(m > 0)
		std::snprintf(buf, sizeof(buf), "%lldm%02llds", static_cast<long long>(m), static_cast<long long>(s));
	else
		std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(s));
	return buf;
}

int64_t nowUnixSeconds()
{
	return std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch())
		.count();
}

int64_t monotonicMillis()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

} // namespace orbislink
