// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/account_id.h"

#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"

#include <chiaki/base64.h>

#include <cctype>
#include <cstdint>
#include <cstring>

namespace orbislink {

namespace {

const char kHexDigits[] = "0123456789ABCDEF";

bool isHexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hexValue(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool isBase64Char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+'
		|| c == '/' || c == '-' || c == '_';
}

// Strips spaces, tabs and line breaks from anywhere in the text.
// Anyone copying this from a console screen brings spaces along half the
// time, and refusing because of that would just be petty.
std::string withoutSpaces(const std::string &message)
{
	std::string out;
	out.reserve(message.size());
	for(char c : message)
	{
		if(!std::isspace(static_cast<unsigned char>(c)))
			out.push_back(c);
	}
	return out;
}

AccountId fromValue(uint64_t value, const std::string &format)
{
	AccountId id;
	id.format = format;

	// The 8 bytes in little-endian: this is the order Remote Play expects.
	unsigned char bytes[8];
	for(int i = 0; i < 8; ++i)
		bytes[i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);

	char b64[16] {};
	if(chiaki_base64_encode(bytes, sizeof(bytes), b64, sizeof(b64)) != CHIAKI_ERR_SUCCESS)
	{
		id.error = QT_TRANSLATE_NOOP("Messages", "Could not convert the Account ID to base64.");
		return id;
	}
	id.base64 = b64;

	// Hexadecimal with the most significant byte on the left, which is how a
	// number is written and how tools show it.
	id.hex.reserve(16);
	for(int i = 15; i >= 0; --i)
		id.hex.push_back(kHexDigits[(value >> (i * 4)) & 0xF]);

	// Decimal by hand: std::to_string would do, but this makes it clear
	// there is no sign involved.
	if(value == 0)
	{
		id.decimal = "0";
	}
	else
	{
		std::string reversed;
		for(uint64_t v = value; v > 0; v /= 10)
			reversed.push_back(static_cast<char>('0' + (v % 10)));
		id.decimal.assign(reversed.rbegin(), reversed.rend());
	}

	id.valid = true;
	return id;
}

AccountId err(const std::string &message)
{
	AccountId id;
	id.error = message;
	return id;
}

} // namespace

AccountId parseAccountId(const std::string &message)
{
	const std::string cleaned = withoutSpaces(trim(message));
	if(cleaned.empty())
		return err(QT_TRANSLATE_NOOP("Messages", "The PSN Account ID is missing."));

	// 1. "0x…" is an explicit request: read as hexadecimal, which is how the
	//    ambiguity of an all-digit ID is resolved.
	std::string hex;
	if(cleaned.size() > 2 && cleaned[0] == '0' && (cleaned[1] == 'x' || cleaned[1] == 'X'))
		hex = cleaned.substr(2);

	// 2. Base64 of the 8 bytes has exactly 12 characters and ends in "=".
	//    None of the other forms looks like this.
	if(hex.empty() && cleaned.size() == 12 && cleaned.back() == '=')
	{
		bool looksLike = true;
		for(size_t i = 0; i + 1 < cleaned.size(); ++i)
		{
			if(!isBase64Char(cleaned[i]))
			{
				looksLike = false;
				break;
			}
		}
		if(looksLike)
		{
			unsigned char bytes[8] {};
			size_t size = sizeof(bytes);
			if(chiaki_base64_decode(cleaned.c_str(), cleaned.size(), bytes, &size)
					!= CHIAKI_ERR_SUCCESS
				|| size != sizeof(bytes))
			{
				return err(QT_TRANSLATE_NOOP("Messages", "This looks like base64 but does not decode to 8 "
					"bytes. Check that you copied the whole Account "
					"ID."));
			}
			uint64_t value = 0;
			for(int i = 7; i >= 0; --i)
				value = (value << 8) | bytes[i];
			return fromValue(value, "base64");
		}
	}

	// 3. Digits only: the user_id as PSN gives it.
	if(hex.empty())
	{
		bool digitsOnly = true;
		for(char c : cleaned)
		{
			if(c < '0' || c > '9')
			{
				digitsOnly = false;
				break;
			}
		}
		if(digitsOnly)
		{
			uint64_t value = 0;
			for(char c : cleaned)
			{
				const uint64_t digit = static_cast<uint64_t>(c - '0');
				// An Account ID never exceeds 64 bits. If it does, the text
				// is not an Account ID, and saying so is better than silently
				// wrapping the counter around.
				if(value > (UINT64_MAX - digit) / 10)
					return err(QT_TRANSLATE_NOOP("Messages", "That number is too large to be an Account ID."));
				value = value * 10 + digit;
			}
			return fromValue(value, "decimal");
		}
	}

	// 4. Hexadecimal, with or without "0x".
	if(hex.empty())
		hex = cleaned;
	// Separators that show up on console screens.
	std::string hexOnly;
	for(char c : hex)
	{
		if(c == ':' || c == '-')
			continue;
		hexOnly.push_back(c);
	}
	if(hexOnly.size() == 16)
	{
		uint64_t value = 0;
		bool good = true;
		for(char c : hexOnly)
		{
			if(!isHexDigit(c))
			{
				good = false;
				break;
			}
			value = (value << 4) | static_cast<uint64_t>(hexValue(c));
		}
		if(good)
			return fromValue(value, "hex");
	}

	return err(QT_TRANSLATE_NOOP("Messages", "I do not recognise this Account ID. All three forms "
		"are accepted: the 16 hexadecimal digits, the "
		"decimal number, or the 12 base64 characters."));
}

AccountId reverseAccountIdBytes(const AccountId &id)
{
	if(!id.valid)
		return id;
	// Re-reading the hexadecimal backwards is the most direct way to swap
	// the byte order without repeating the arithmetic.
	uint64_t value = 0;
	for(char c : id.hex)
		value = (value << 4) | static_cast<uint64_t>(hexValue(c));
	uint64_t swapped = 0;
	for(int i = 0; i < 8; ++i)
		swapped = (swapped << 8) | ((value >> (i * 8)) & 0xFF);
	return fromValue(swapped, id.format);
}

} // namespace orbislink
