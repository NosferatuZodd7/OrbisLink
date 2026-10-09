// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/payloads/payload_layout.h"

#include <algorithm>
#include <cctype>

namespace orbislink {
namespace payloads {

namespace {

struct Lines
{
	std::vector<std::string> items;
	bool crlf = false;
};

Lines splitLines(const std::string &text)
{
	Lines lines;
	lines.crlf = text.find("\r\n") != std::string::npos;
	size_t start = 0;
	while(start < text.size())
	{
		size_t end = text.find('\n', start);
		if(end == std::string::npos)
			end = text.size();
		std::string line = text.substr(start, end - start);
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.items.push_back(line);
		start = end + 1;
	}
	return lines;
}

std::string joinLines(const Lines &lines)
{
	std::string out;
	const char *eol = lines.crlf ? "\r\n" : "\n";
	for(const std::string &line : lines.items)
	{
		out += line;
		out += eol;
	}
	return out;
}

std::string trim(const std::string &text)
{
	size_t begin = 0;
	size_t end = text.size();
	while(begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
		++begin;
	while(end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
		--end;
	return text.substr(begin, end - begin);
}

bool isDelay(const std::string &line)
{
	const std::string t = trim(line);
	if(t.size() < 2 || t[0] != '!')
		return false;
	return std::all_of(t.begin() + 1, t.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

bool isComment(const std::string &line)
{
	const std::string t = trim(line);
	return t.empty() || t[0] == '#' || t[0] == ';';
}

// A plugins.ini line: the path and its value ("" when the line has none,
// which older GoldHEN reads as on).
struct PluginLine
{
	std::string path;
	std::string value;
};

bool parsePluginLine(const std::string &line, PluginLine *out)
{
	const std::string t = trim(line);
	if(t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[')
		return false;
	const size_t eq = t.rfind('=');
	if(eq == std::string::npos)
	{
		out->path = t;
		out->value.clear();
	}
	else
	{
		out->path = trim(t.substr(0, eq));
		out->value = trim(t.substr(eq + 1));
		std::transform(out->value.begin(), out->value.end(), out->value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	}
	return !out->path.empty();
}

bool sectionHeader(const std::string &line, std::string *name)
{
	const std::string t = trim(line);
	if(t.size() < 2 || t.front() != '[' || t.back() != ']')
		return false;
	*name = trim(t.substr(1, t.size() - 2));
	return true;
}

} // namespace

std::string lowerExtension(const std::string &fileName)
{
	const size_t dot = fileName.rfind('.');
	if(dot == std::string::npos || dot == 0)
		return std::string();
	std::string ext = fileName.substr(dot);
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext;
}

std::vector<Folder> folders(Kind kind)
{
	if(kind == Kind::Ps4)
		return {
			{ "goldhen-payloads", "/data/GoldHEN/payloads", { ".bin" }, AutoStart::None, "", "goldhen.bin" },
			{ "payload-library", "/data/payloads", { ".bin", ".elf" }, AutoStart::None, "", "" },
			{ "goldhen-plugins", "/data/GoldHEN/plugins", { ".prx" }, AutoStart::Ini, "/data/GoldHEN/plugins.ini", "" },
		};
	return {
		{ "etahen-payloads", "/data/etaHEN/payloads", { ".elf", ".bin" }, AutoStart::Marker, "", "" },
		{ "etahen-plugins", "/data/etaHEN/plugins", { ".plugin", ".elf" }, AutoStart::Marker, "", "" },
		{ "autoloader", "/data/ps5_autoloader", { ".elf", ".bin", ".lua", ".js", ".jar" }, AutoStart::List,
			"/data/ps5_autoloader/autoload.txt", "" },
	};
}

std::vector<std::string> configFiles(Kind kind)
{
	if(kind == Kind::Ps4)
		return { "/data/GoldHEN/plugins.ini" };
	return { "/data/etaHEN/config.ini", "/data/ps5_autoloader/autoload.txt" };
}

uint16_t loaderPort(Kind kind, const std::string &fileName)
{
	if(kind == Kind::Ps4)
		return 9090;
	const std::string ext = lowerExtension(fileName);
	if(ext == ".bin")
		return 9020;
	if(ext == ".lua")
		return 9026;
	if(ext == ".jar")
		return 9025;
	if(ext == ".js")
		return 50000;
	return 9021;
}

bool sendable(Kind kind, const std::string &fileName)
{
	const std::string ext = lowerExtension(fileName);
	if(kind == Kind::Ps4)
		return ext == ".bin" || ext == ".elf";
	return ext == ".elf" || ext == ".bin" || ext == ".self" || ext == ".lua" || ext == ".js" || ext == ".jar";
}

std::vector<std::string> autoloadEntries(const std::string &text)
{
	std::vector<std::string> entries;
	for(const std::string &line : splitLines(text).items)
		if(!isComment(line) && !isDelay(line))
			entries.push_back(trim(line));
	return entries;
}

bool autoloadListed(const std::string &text, const std::string &name)
{
	const std::vector<std::string> entries = autoloadEntries(text);
	return std::find(entries.begin(), entries.end(), name) != entries.end();
}

std::string autoloadSet(const std::string &text, const std::string &name, bool listed)
{
	Lines lines = splitLines(text);
	if(listed)
	{
		if(autoloadListed(text, name))
			return text;
		// A blank last line stays last.
		while(!lines.items.empty() && trim(lines.items.back()).empty())
			lines.items.pop_back();
		lines.items.push_back(name);
		return joinLines(lines);
	}
	std::vector<std::string> kept;
	for(const std::string &line : lines.items)
	{
		if(!isComment(line) && !isDelay(line) && trim(line) == name)
		{
			// Its wait goes with it.
			if(!kept.empty() && isDelay(kept.back()))
				kept.pop_back();
			continue;
		}
		kept.push_back(line);
	}
	if(kept.size() == lines.items.size())
		return text;
	lines.items = kept;
	return joinLines(lines);
}

std::string autoloadRename(const std::string &text, const std::string &from, const std::string &to)
{
	Lines lines = splitLines(text);
	bool changed = false;
	for(std::string &line : lines.items)
		if(!isComment(line) && !isDelay(line) && trim(line) == from)
		{
			line = to;
			changed = true;
		}
	return changed ? joinLines(lines) : text;
}

bool pluginEnabled(const std::string &ini, const std::string &path)
{
	std::string section;
	for(const std::string &line : splitLines(ini).items)
	{
		std::string name;
		if(sectionHeader(line, &name))
		{
			section = name;
			continue;
		}
		PluginLine plugin;
		if(section == "default" && parsePluginLine(line, &plugin) && plugin.path == path)
			return plugin.value.empty() || plugin.value == "true" || plugin.value == "1";
	}
	return false;
}

std::string pluginSet(const std::string &ini, const std::string &path, bool enabled)
{
	Lines lines = splitLines(ini);
	const std::string wanted = path + (enabled ? "=true" : "=false");
	std::string section;
	int defaultStart = -1;
	int defaultEnd = -1; // one past its last non-blank line
	for(size_t i = 0; i < lines.items.size(); ++i)
	{
		std::string name;
		if(sectionHeader(lines.items[i], &name))
		{
			section = name;
			if(name == "default")
				defaultStart = defaultEnd = static_cast<int>(i) + 1;
			continue;
		}
		if(section != "default")
			continue;
		if(!trim(lines.items[i]).empty())
			defaultEnd = static_cast<int>(i) + 1;
		PluginLine plugin;
		if(parsePluginLine(lines.items[i], &plugin) && plugin.path == path)
		{
			if(trim(lines.items[i]) == wanted)
				return ini;
			lines.items[i] = wanted;
			return joinLines(lines);
		}
	}
	if(!enabled)
		return ini;
	if(defaultStart < 0)
	{
		// No "[default]" yet: it goes first, so it reads as the file's start.
		std::vector<std::string> head = { "[default]", wanted };
		if(!lines.items.empty())
			head.emplace_back();
		lines.items.insert(lines.items.begin(), head.begin(), head.end());
	}
	else
		lines.items.insert(lines.items.begin() + defaultEnd, wanted);
	return joinLines(lines);
}

std::string pluginRename(const std::string &ini, const std::string &from, const std::string &to)
{
	Lines lines = splitLines(ini);
	bool changed = false;
	for(std::string &line : lines.items)
	{
		PluginLine plugin;
		if(parsePluginLine(line, &plugin) && plugin.path == from)
		{
			line = to + (plugin.value.empty() ? std::string() : "=" + plugin.value);
			changed = true;
		}
	}
	return changed ? joinLines(lines) : ini;
}

std::string pluginRemove(const std::string &ini, const std::string &path)
{
	Lines lines = splitLines(ini);
	std::vector<std::string> kept;
	for(const std::string &line : lines.items)
	{
		PluginLine plugin;
		if(parsePluginLine(line, &plugin) && plugin.path == path)
			continue;
		kept.push_back(line);
	}
	if(kept.size() == lines.items.size())
		return ini;
	lines.items = kept;
	return joinLines(lines);
}

} // namespace payloads
} // namespace orbislink
