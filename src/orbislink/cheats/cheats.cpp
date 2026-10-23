// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/cheats/cheats.h"

#include "orbislink/common/json.h"
#include "orbislink/common/util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace orbislink {
namespace cheats {

namespace {

bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

bool isLetter(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool allDigits(const std::string &s, size_t from, size_t count)
{
	if(from + count > s.size())
		return false;
	for(size_t i = from; i < from + count; ++i)
		if(!isDigit(s[i]))
			return false;
	return true;
}

// A version at `at`: "01.000.000" (PS5), "01.09", "1.09", "01_09".
// Its length in `s`, 0 when there is none; `out` written the way the
// consoles write it.
size_t readVersion(const std::string &s, size_t at, std::string *out)
{
	size_t major = 0;
	while(major < 2 && at + major < s.size() && isDigit(s[at + major]))
		++major;
	if(major == 0 || at + major >= s.size())
		return 0;
	const std::string lead = major == 1 ? "0" : "";
	const size_t ps5 = major + 8;
	if(at + ps5 <= s.size() && s[at + major] == '.' && allDigits(s, at + major + 1, 3) && s[at + major + 4] == '.'
		&& allDigits(s, at + major + 5, 3) && (at + ps5 == s.size() || !isDigit(s[at + ps5])))
	{
		*out = lead + s.substr(at, ps5);
		return ps5;
	}
	const char separator = s[at + major];
	if((separator != '.' && separator != '_') || !allDigits(s, at + major + 1, 2))
		return 0;
	const size_t length = major + 3;
	if(at + length < s.size() && isDigit(s[at + length]))
		return 0;
	*out = lead + s.substr(at, major) + "." + s.substr(at + major + 1, 2);
	return length;
}

bool isTitleId(const std::string &s)
{
	if(s.size() != 9)
		return false;
	for(size_t i = 0; i < 4; ++i)
		if(!isLetter(s[i]))
			return false;
	return allDigits(s, 4, 5);
}

const char *extensionOf(Format format)
{
	switch(format)
	{
	case Format::Shn:
		return ".shn";
	case Format::Mc4:
		return ".mc4";
	case Format::Json:
		break;
	}
	return ".json";
}

// "<TITLE ID>_<version>[_<variant>]" without the extension.
bool parseBaseName(const std::string &base, std::string *titleId, std::string *version, std::string *variant)
{
	size_t cut = base.find_first_of("_- ");
	if(cut == std::string::npos || !isTitleId(base.substr(0, cut)))
		return false;
	std::string v;
	const size_t length = readVersion(base, cut + 1, &v);
	if(length == 0)
		return false;
	std::string rest = base.substr(cut + 1 + length);
	while(!rest.empty() && (rest.front() == '_' || rest.front() == '-' || rest.front() == ' '))
		rest.erase(rest.begin());
	*titleId = base.substr(0, cut);
	std::transform(titleId->begin(), titleId->end(), titleId->begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	*version = v;
	*variant = trim(rest);
	return true;
}

// The name without the format's extension (".mc4.xml" too), or "" when
// it is not of that format.
std::string stripExtension(const std::string &file, Format format)
{
	const std::string lower = toLower(file);
	if(format == Format::Mc4 && endsWith(lower, ".mc4.xml"))
		return file.substr(0, file.size() - 8);
	const std::string ext = extensionOf(format);
	if(endsWith(lower, ext))
		return file.substr(0, file.size() - ext.size());
	return std::string();
}

std::vector<std::string> lines(const std::string &text)
{
	std::vector<std::string> out;
	for(std::string line : split(text, '\n'))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		out.push_back(line);
	}
	return out;
}

// Runs of white space made one space, the ends trimmed.
std::string tidy(const std::string &text)
{
	std::string out;
	bool space = false;
	for(char c : text)
	{
		if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			space = !out.empty();
			continue;
		}
		if(space)
			out += ' ';
		space = false;
		out += c;
	}
	return out;
}

void appendUtf8(std::string *out, uint32_t cp)
{
	if(cp < 0x80)
		*out += static_cast<char>(cp);
	else if(cp < 0x800)
	{
		*out += static_cast<char>(0xC0 | (cp >> 6));
		*out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else if(cp < 0x10000)
	{
		*out += static_cast<char>(0xE0 | (cp >> 12));
		*out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		*out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else if(cp < 0x110000)
	{
		*out += static_cast<char>(0xF0 | (cp >> 18));
		*out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		*out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		*out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

} // namespace

std::string formatName(Format format)
{
	return std::string(extensionOf(format)).substr(1);
}

const std::vector<Format> &allFormats()
{
	static const std::vector<Format> formats { Format::Json, Format::Shn, Format::Mc4 };
	return formats;
}

std::string normalizeVersion(const std::string &version)
{
	const std::string text = trim(version);
	std::string out;
	const size_t length = readVersion(text, 0, &out);
	return length == text.size() && length > 0 ? out : std::string();
}

std::string CheatListing::readablePath() const
{
	const std::string folder = formatName(format) + "/";
	if(format == Format::Mc4 && !endsWith(toLower(file), ".xml"))
		return folder + file + ".xml";
	return folder + file;
}

std::string CheatListing::loadablePath() const
{
	const std::string folder = formatName(format) + "/";
	if(format == Format::Mc4 && endsWith(toLower(file), ".xml"))
		return folder + file.substr(0, file.size() - 4);
	return folder + file;
}

std::string CheatListing::consoleName() const
{
	return titleId + "_" + version + (variant.empty() ? std::string() : "_" + variant) + extensionOf(format);
}

std::vector<CheatListing> parseCheatList(const std::string &text, Format format)
{
	std::vector<CheatListing> out;
	for(const std::string &line : lines(text))
	{
		const size_t equals = line.find('=');
		if(equals == std::string::npos)
			continue;
		CheatListing listing;
		listing.format = format;
		listing.file = trim(line.substr(0, equals));
		listing.game = trim(line.substr(equals + 1));
		// A file of the repository's folder, never a path out of it.
		if(listing.file.empty() || listing.file.find_first_of("/\\") != std::string::npos
			|| listing.file.find("..") != std::string::npos)
			continue;
		const std::string base = stripExtension(listing.file, format);
		if(base.empty() || !parseBaseName(base, &listing.titleId, &listing.version, &listing.variant))
			continue;
		if(listing.variant.find_first_of("/\\") != std::string::npos)
			continue;
		out.push_back(listing);
	}
	return out;
}

std::vector<CheatListing> cheatsFor(const std::vector<CheatListing> &all, const std::string &titleId,
	const std::string &version, size_t *exact)
{
	const std::string wanted = normalizeVersion(version);
	std::vector<CheatListing> out;
	for(const CheatListing &listing : all)
		if(iequals(listing.titleId, titleId))
			out.push_back(listing);
	std::stable_sort(out.begin(), out.end(), [&wanted](const CheatListing &a, const CheatListing &b) {
		const bool aExact = !wanted.empty() && a.version == wanted;
		const bool bExact = !wanted.empty() && b.version == wanted;
		if(aExact != bExact)
			return aExact;
		if(a.version != b.version)
			return a.version > b.version;
		if(a.format != b.format)
			return static_cast<int>(a.format) < static_cast<int>(b.format);
		return a.variant < b.variant;
	});
	if(exact)
	{
		*exact = 0;
		for(const CheatListing &listing : out)
			*exact += !wanted.empty() && listing.version == wanted ? 1 : 0;
	}
	return out;
}

CheatFile parseCheatFile(const std::string &text, Format format)
{
	CheatFile file;
	if(format == Format::Json)
	{
		const Json json = Json::parse(text);
		if(!json.isObject() || !json["mods"].isArray())
			return file;
		file.game = tidy(json["name"].toString());
		file.version = tidy(json["version"].toString());
		file.process = tidy(json["process"].toString());
		const Json &credits = json["credits"];
		if(credits.isArray())
		{
			std::vector<std::string> names;
			for(const Json &name : credits.items())
				if(!tidy(name.toString()).empty())
					names.push_back(tidy(name.toString()));
			file.author = join(names, ", ");
		}
		else
			file.author = tidy(credits.toString());
		for(const Json &mod : json["mods"].items())
		{
			Cheat cheat;
			cheat.name = tidy(mod["name"].toString());
			cheat.description = tidy(mod["description"].toString());
			if(!cheat.name.empty())
				file.cheats.push_back(cheat);
		}
		file.ok = true;
		return file;
	}
	for(const XmlElement &element : xmlElements(text))
	{
		if(element.name == "Trainer")
		{
			file.ok = true;
			file.game = tidy(element.attribute("Game"));
			file.version = tidy(element.attribute("Version"));
			file.process = tidy(element.attribute("Process"));
			file.author = tidy(element.attribute("Moder"));
		}
		else if(element.name == "Cheat")
		{
			Cheat cheat;
			cheat.name = tidy(element.attribute("Text"));
			cheat.description = tidy(element.attribute("Description"));
			if(!cheat.name.empty())
				file.cheats.push_back(cheat);
		}
	}
	return file;
}

// ── the console's folders

std::string cheatFolder(Console console, Format format)
{
	return std::string(console == Console::GoldHen ? "/data/GoldHEN/cheats/" : "/data/etaHEN/cheats/")
		+ formatName(format);
}

std::string etaHenList(Format format)
{
	return "/data/etaHEN/cheats/" + formatName(format) + ".txt";
}

std::string etaHenListAdd(const std::string &list, const std::string &file, const std::string &game)
{
	for(const std::string &line : lines(list))
		if(trim(line.substr(0, line.find('='))) == file)
			return list;
	std::string name = tidy(game);
	if(name.empty())
		name = file.substr(0, file.find('.'));
	std::string out = list;
	if(!out.empty() && out.back() != '\n')
		out += '\n';
	return out + file + "=" + name + "\n";
}

std::string etaHenListRemove(const std::string &list, const std::string &file)
{
	std::string out;
	bool changed = false;
	for(const std::string &line : lines(list))
	{
		if(!line.empty() && trim(line.substr(0, line.find('='))) == file)
		{
			changed = true;
			continue;
		}
		out += line + "\n";
	}
	if(!changed)
		return list;
	// The last line of the list had no line end of its own.
	while(out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n')
		out.pop_back();
	return out == "\n" ? std::string() : out;
}

bool cheatFileIsFor(const std::string &fileName, const std::string &titleId, const std::string &version)
{
	for(Format format : allFormats())
	{
		const std::string base = stripExtension(fileName, format);
		std::string id;
		std::string v;
		std::string variant;
		if(!base.empty() && parseBaseName(base, &id, &v, &variant))
			return iequals(id, titleId) && (version.empty() || v == normalizeVersion(version));
	}
	return false;
}

// ── game patches

bool Patch::appliesTo(const std::string &version) const
{
	return appVersion == version || startsWith(appVersion, "mask") || startsWith(appVersion, "all");
}

PatchFile parsePatchFile(const std::string &xml)
{
	PatchFile file;
	for(const XmlElement &element : xmlElements(xml))
	{
		if(element.name == "Patch")
			file.ok = true;
		else if(element.name == "ID")
		{
			const std::string id = trim(element.text);
			if(!id.empty())
				file.titleIds.push_back(id);
		}
		else if(element.name == "Metadata")
		{
			file.ok = true;
			Patch patch;
			patch.title = element.attribute("Title");
			patch.name = element.attribute("Name");
			// Line breaks are written into the notes as "\n".
			patch.note = element.attribute("Note");
			for(size_t at = patch.note.find("\\n"); at != std::string::npos; at = patch.note.find("\\n", at + 1))
				patch.note.replace(at, 2, "\n");
			patch.author = element.attribute("Author");
			patch.patchVersion = element.attribute("PatchVer");
			patch.appVersion = element.attribute("AppVer");
			patch.appElf = element.attribute("AppElf");
			file.patches.push_back(patch);
		}
		else if(element.name == "Line" && !file.patches.empty())
			++file.patches.back().lines;
	}
	return file;
}

std::string patchXmlPath(const std::string &titleId)
{
	return "/data/GoldHEN/patches/xml/" + titleId + ".xml";
}

uint64_t patchHash(const Patch &patch, const std::string &titleId)
{
	return patchHash(patch.title, patch.name, patch.appVersion, patchXmlPath(titleId), patch.appElf);
}

uint64_t patchHash(const std::string &title, const std::string &name, const std::string &appVersion,
	const std::string &xmlPath, const std::string &appElf)
{
	// snprintf into char[256]: at most 255 bytes, cut wherever they end.
	std::string input = title + name + appVersion + xmlPath + appElf;
	if(input.size() > 255)
		input.resize(255);
	uint64_t hash = 5381;
	for(char byte : input)
	{
		if(byte == '\0')
			break;
		// "u32 c = *str++": a char, signed on the console, widened.
		const uint32_t c = static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(byte)));
		hash = (hash * 33) ^ c;
	}
	return hash;
}

std::string patchSettingsPath(uint64_t hash)
{
	char name[32];
	std::snprintf(name, sizeof(name), "0x%016llx.txt", static_cast<unsigned long long>(hash));
	return std::string("/data/GoldHEN/patches/settings/") + name;
}

bool patchSwitchOn(const std::string &content)
{
	return !content.empty() && content[0] == '1';
}

bool patchPluginLoads(const std::string &pluginsIni, const std::string &titleId)
{
	std::string section;
	for(const std::string &raw : lines(pluginsIni))
	{
		const std::string line = trim(raw);
		if(line.empty() || line[0] == ';' || line[0] == '#')
			continue;
		if(line.front() == '[' && line.back() == ']')
		{
			section = trim(line.substr(1, line.size() - 2));
			continue;
		}
		if(!iequals(section, "default") && !iequals(section, titleId))
			continue;
		const size_t equals = line.find('=');
		const std::string path = toLower(trim(line.substr(0, equals)));
		const std::string value = equals == std::string::npos ? std::string() : toLower(trim(line.substr(equals + 1)));
		if(endsWith(path, "game_patch.prx") && (value.empty() || value == "true" || value == "1"))
			return true;
	}
	return false;
}

// ── a little XML

std::string XmlElement::attribute(const std::string &key) const
{
	const auto found = attributes.find(key);
	return found == attributes.end() ? std::string() : found->second;
}

std::string xmlDecode(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	for(size_t i = 0; i < text.size(); ++i)
	{
		if(text[i] != '&')
		{
			out += text[i];
			continue;
		}
		const size_t end = text.find(';', i);
		if(end == std::string::npos || end - i > 10)
		{
			out += text[i];
			continue;
		}
		const std::string entity = text.substr(i + 1, end - i - 1);
		if(entity == "amp")
			out += '&';
		else if(entity == "lt")
			out += '<';
		else if(entity == "gt")
			out += '>';
		else if(entity == "quot")
			out += '"';
		else if(entity == "apos")
			out += '\'';
		else if(entity.size() > 1 && entity[0] == '#')
		{
			const bool hex = entity[1] == 'x' || entity[1] == 'X';
			const std::string digits = entity.substr(hex ? 2 : 1);
			char *stop = nullptr;
			const unsigned long cp = std::strtoul(digits.c_str(), &stop, hex ? 16 : 10);
			if(digits.empty() || *stop != '\0' || cp == 0)
			{
				out += text[i];
				continue;
			}
			appendUtf8(&out, static_cast<uint32_t>(cp));
		}
		else
		{
			out += text[i];
			continue;
		}
		i = end;
	}
	return out;
}

std::vector<XmlElement> xmlElements(const std::string &text)
{
	std::vector<XmlElement> out;
	int depth = 0;
	size_t i = 0;
	auto skipPast = [&text, &i](const char *marker) {
		const size_t at = text.find(marker, i);
		i = at == std::string::npos ? text.size() : at + std::char_traits<char>::length(marker);
	};
	while(i < text.size())
	{
		const size_t open = text.find('<', i);
		if(open == std::string::npos)
			break;
		i = open;
		if(text.compare(i, 4, "<!--") == 0)
		{
			skipPast("-->");
			continue;
		}
		if(text.compare(i, 9, "<![CDATA[") == 0)
		{
			skipPast("]]>");
			continue;
		}
		if(text.compare(i, 2, "<?") == 0)
		{
			skipPast("?>");
			continue;
		}
		if(text.compare(i, 2, "<!") == 0)
		{
			skipPast(">");
			continue;
		}
		if(text.compare(i, 2, "</") == 0)
		{
			depth = std::max(0, depth - 1);
			skipPast(">");
			continue;
		}
		++i;
		XmlElement element;
		element.depth = depth;
		while(i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '>'
			&& text[i] != '/')
			element.name += text[i++];
		bool closed = false;
		while(i < text.size())
		{
			while(i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
				++i;
			if(i >= text.size())
				break;
			if(text[i] == '>')
			{
				++i;
				break;
			}
			if(text[i] == '/')
			{
				closed = true;
				++i;
				continue;
			}
			std::string key;
			while(i < text.size() && text[i] != '=' && text[i] != '>' && text[i] != '/'
				&& !std::isspace(static_cast<unsigned char>(text[i])))
				key += text[i++];
			while(i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
				++i;
			if(i >= text.size() || text[i] != '=')
			{
				if(!key.empty())
					element.attributes[key] = std::string();
				continue;
			}
			++i;
			while(i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
				++i;
			if(i >= text.size())
				break;
			const char quote = text[i];
			if(quote != '"' && quote != '\'')
			{
				std::string value;
				while(i < text.size() && text[i] != '>' && !std::isspace(static_cast<unsigned char>(text[i])))
					value += text[i++];
				element.attributes[key] = xmlDecode(value);
				continue;
			}
			const size_t end = text.find(quote, i + 1);
			if(end == std::string::npos)
			{
				i = text.size();
				break;
			}
			element.attributes[key] = xmlDecode(text.substr(i + 1, end - i - 1));
			i = end + 1;
		}
		if(element.name.empty())
			continue;
		const size_t next = text.find('<', i);
		element.text = xmlDecode(text.substr(i, (next == std::string::npos ? text.size() : next) - i));
		if(!closed)
			++depth;
		out.push_back(std::move(element));
	}
	return out;
}

} // namespace cheats
} // namespace orbislink
