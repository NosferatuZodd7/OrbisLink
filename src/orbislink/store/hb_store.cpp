// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/store/hb_store.h"

#include "orbislink/common/json.h"
#include "orbislink/common/util.h"

#include <openssl/evp.h>

#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace orbislink::store {

const char *const kHbStoreAgent = "StoreHAX";

namespace {

bool hasWord(const std::string &text, const char *word)
{
	return toLower(text).find(word) != std::string::npos;
}

// The numbers of a version, in order: "01.08" → 1, 8; "2.3.2b" → 2, 3, 2.
std::vector<long> numbersOf(const std::string &version)
{
	std::vector<long> numbers;
	std::string digits;
	for(char c : version + ".")
	{
		if(std::isdigit(static_cast<unsigned char>(c)))
		{
			digits += c;
			continue;
		}
		if(!digits.empty())
			numbers.push_back(std::stol(digits.substr(0, 9)));
		digits.clear();
		if(c != '.')
			break;
	}
	return numbers;
}

} // namespace

bool HbStoreApp::forPs5() const
{
	return hasWord(firmware, "ps5") || !ps5Firmware.empty() || startsWith(titleId, "PPSA");
}

bool HbStoreApp::forPs4() const
{
	// "ps5_supported" alone, or a PS5 title: made for the PS5 only.
	return !startsWith(titleId, "PPSA") && toLower(trim(firmware)) != "ps5_supported";
}

bool HbStoreApp::ps5PackageIsThisOne() const
{
	if(!forPs5())
		return false;
	if(ps5Version.empty())
		return true;
	return compareLooseVersions(ps5Version, version) == 0 && (ps5SizeText.empty() || ps5SizeText == sizeText);
}

std::string HbStoreApp::kind() const
{
	const std::string t = toLower(trim(type));
	if(t.find("emulator") != std::string::npos)
		return "emulator";
	if(t.find("game") != std::string::npos)
		return "game";
	if(t == "utility" || t == "dev menu" || t == "tool")
		return "tool";
	if(t == "homebrew" || t.empty())
		return "app";
	return t;
}

std::string HbStoreApp::ps4Firmware() const
{
	std::vector<std::string> kept;
	for(const std::string &part : split(firmware, ','))
	{
		const std::string piece = trim(part);
		if(!piece.empty() && !hasWord(piece, "ps5"))
			kept.push_back(piece);
	}
	return join(kept, ", ");
}

std::string hbStoreBase(const std::string &address)
{
	std::string base = trim(address);
	if(base.empty())
		return std::string();
	if(base.find("://") == std::string::npos)
		base = "https://" + base;
	const std::string scheme = toLower(base.substr(0, base.find("://")));
	if(scheme != "http" && scheme != "https")
		return std::string();
	// No query, and not the database or the API itself.
	const size_t query = base.find_first_of("?#");
	if(query != std::string::npos)
		base.resize(query);
	for(const char *file : { "store.db", "api.php", "index.html", "index.php" })
		if(endsWith(toLower(base), std::string("/") + file))
			base.resize(base.size() - std::string(file).size());
	while(!base.empty() && base.back() == '/')
		base.pop_back();
	if(base.size() <= scheme.size() + 3)
		return std::string();
	return base;
}

std::string hbStoreDatabaseUrl(const std::string &base)
{
	return base + "/store.db";
}

std::string hbStoreHashUrl(const std::string &base)
{
	return base + "/api.php?db_check_hash=true";
}

std::string parseHbStoreHash(const std::string &body)
{
	const Json root = Json::parse(body);
	if(!root.isObject() || !root["hash"].isString())
		return std::string();
	const std::string hash = toLower(trim(root["hash"].toString()));
	if(hash.size() != 32)
		return std::string();
	for(char c : hash)
		if(!std::isxdigit(static_cast<unsigned char>(c)))
			return std::string();
	return hash;
}

std::string hbStoreDownloadsUrl(const std::string &base, const std::string &titleId)
{
	return base + "/download.php?tid=" + titleId + "&check=true";
}

long long parseHbStoreDownloads(const std::string &body)
{
	const Json root = Json::parse(body);
	if(!root.isObject())
		return -1;
	const Json &count = root["number_of_downloads"];
	// A string of digits, or a number.
	const std::string text = count.isString() ? trim(count.toString())
		: count.isNumber() ? std::to_string(count.toInt(-1)) : std::string();
	if(text.empty() || text.size() > 15)
		return -1;
	for(char c : text)
		if(!std::isdigit(static_cast<unsigned char>(c)))
			return -1;
	return std::stoll(text);
}

bool looksLikeSqlite(const std::string &bytes)
{
	return bytes.compare(0, 16, std::string("SQLite format 3\0", 16)) == 0;
}

namespace {

std::string hexOf(const unsigned char *digest, unsigned int size)
{
	static const char *hex = "0123456789abcdef";
	std::string out;
	for(unsigned int i = 0; i < size; ++i)
	{
		out += hex[digest[i] >> 4];
		out += hex[digest[i] & 0x0f];
	}
	return out;
}

} // namespace

std::string md5Hex(const std::string &bytes)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	if(EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_md5(), nullptr) != 1)
		return std::string();
	return hexOf(digest, size);
}

std::string md5File(const std::string &path)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	if(!in)
		return std::string();
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if(!context || EVP_DigestInit_ex(context, EVP_md5(), nullptr) != 1)
	{
		EVP_MD_CTX_free(context);
		return std::string();
	}
	std::vector<char> buffer(1 << 20);
	bool ok = true;
	while(in)
	{
		in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize got = in.gcount();
		if(got > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<size_t>(got)) != 1)
		{
			ok = false;
			break;
		}
	}
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	ok = ok && EVP_DigestFinal_ex(context, digest, &size) == 1;
	EVP_MD_CTX_free(context);
	return ok ? hexOf(digest, size) : std::string();
}

int compareLooseVersions(const std::string &a, const std::string &b)
{
	const std::vector<long> x = numbersOf(a);
	const std::vector<long> y = numbersOf(b);
	if(x.empty() || y.empty())
		return 0;
	for(size_t i = 0; i < std::max(x.size(), y.size()); ++i)
	{
		const long p = i < x.size() ? x[i] : 0;
		const long q = i < y.size() ? y[i] : 0;
		if(p != q)
			return p < q ? -1 : 1;
	}
	return 0;
}

std::string hbStorePackageName(const HbStoreApp &app)
{
	std::string name;
	for(char c : app.name)
		name += std::isalnum(static_cast<unsigned char>(c)) ? c : ' ';
	// Words joined: "Apollo Save Tool" → "Apollo_Save_Tool".
	std::vector<std::string> words;
	for(const std::string &word : split(name, ' ', false))
		words.push_back(word);
	std::string out = join(words, "_");
	if(out.size() > 60)
		out.resize(60);
	out += (out.empty() ? "" : "_") + app.titleId;
	if(!app.version.empty())
		out += "_v" + app.version;
	return sanitizeFileName(out + ".pkg");
}

} // namespace orbislink::store
