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
std::string semEspacos(const std::string &texto)
{
	std::string out;
	out.reserve(texto.size());
	for(char c : texto)
	{
		if(!std::isspace(static_cast<unsigned char>(c)))
			out.push_back(c);
	}
	return out;
}

AccountId fromValue(uint64_t valor, const std::string &formato)
{
	AccountId id;
	id.format = formato;

	// The 8 bytes in little-endian: this is the order Remote Play expects.
	unsigned char bytes[8];
	for(int i = 0; i < 8; ++i)
		bytes[i] = static_cast<unsigned char>((valor >> (i * 8)) & 0xFF);

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
		id.hex.push_back(kHexDigits[(valor >> (i * 4)) & 0xF]);

	// Decimal by hand: std::to_string would do, but this makes it clear
	// there is no sign involved.
	if(valor == 0)
	{
		id.decimal = "0";
	}
	else
	{
		std::string invertido;
		for(uint64_t v = valor; v > 0; v /= 10)
			invertido.push_back(static_cast<char>('0' + (v % 10)));
		id.decimal.assign(invertido.rbegin(), invertido.rend());
	}

	id.valid = true;
	return id;
}

AccountId erro(const std::string &mensagem)
{
	AccountId id;
	id.error = mensagem;
	return id;
}

} // namespace

AccountId parseAccountId(const std::string &texto)
{
	const std::string limpo = semEspacos(trim(texto));
	if(limpo.empty())
		return erro(QT_TRANSLATE_NOOP("Messages", "The PSN Account ID is missing."));

	// 1. "0x…" is an explicit request: read as hexadecimal, which is how the
	//    ambiguity of an all-digit ID is resolved.
	std::string hex;
	if(limpo.size() > 2 && limpo[0] == '0' && (limpo[1] == 'x' || limpo[1] == 'X'))
		hex = limpo.substr(2);

	// 2. Base64 dos 8 bytes tem exactamente 12 caracteres e acaba em "=".
	//    Nenhuma das outras formas se parece com isto.
	if(hex.empty() && limpo.size() == 12 && limpo.back() == '=')
	{
		bool parece = true;
		for(size_t i = 0; i + 1 < limpo.size(); ++i)
		{
			if(!isBase64Char(limpo[i]))
			{
				parece = false;
				break;
			}
		}
		if(parece)
		{
			unsigned char bytes[8] {};
			size_t tamanho = sizeof(bytes);
			if(chiaki_base64_decode(limpo.c_str(), limpo.size(), bytes, &tamanho)
					!= CHIAKI_ERR_SUCCESS
				|| tamanho != sizeof(bytes))
			{
				return erro(QT_TRANSLATE_NOOP("Messages", "This looks like base64 but does not decode to 8 "
					"bytes. Check that you copied the whole Account "
					"ID."));
			}
			uint64_t valor = 0;
			for(int i = 7; i >= 0; --i)
				valor = (valor << 8) | bytes[i];
			return fromValue(valor, "base64");
		}
	}

	// 3. Digits only: the user_id as PSN gives it.
	if(hex.empty())
	{
		bool soDigitos = true;
		for(char c : limpo)
		{
			if(c < '0' || c > '9')
			{
				soDigitos = false;
				break;
			}
		}
		if(soDigitos)
		{
			uint64_t valor = 0;
			for(char c : limpo)
			{
				const uint64_t digito = static_cast<uint64_t>(c - '0');
				// An Account ID never exceeds 64 bits. If it does, the text
				// is not an Account ID, and saying so is better than silently
				// wrapping the counter around.
				if(valor > (UINT64_MAX - digito) / 10)
					return erro(QT_TRANSLATE_NOOP("Messages", "That number is too large to be an Account ID."));
				valor = valor * 10 + digito;
			}
			return fromValue(valor, "decimal");
		}
	}

	// 4. Hexadecimal, com ou sem "0x".
	if(hex.empty())
		hex = limpo;
	// Separators that show up on console screens.
	std::string apenasHex;
	for(char c : hex)
	{
		if(c == ':' || c == '-')
			continue;
		apenasHex.push_back(c);
	}
	if(apenasHex.size() == 16)
	{
		uint64_t valor = 0;
		bool bom = true;
		for(char c : apenasHex)
		{
			if(!isHexDigit(c))
			{
				bom = false;
				break;
			}
			valor = (valor << 4) | static_cast<uint64_t>(hexValue(c));
		}
		if(bom)
			return fromValue(valor, "hex");
	}

	return erro(QT_TRANSLATE_NOOP("Messages", "I do not recognise this Account ID. All three forms "
		"are accepted: the 16 hexadecimal digits, the "
		"decimal number, or the 12 base64 characters."));
}

AccountId reverseAccountIdBytes(const AccountId &id)
{
	if(!id.valid)
		return id;
	// Re-reading the hexadecimal backwards is the most direct way to swap
	// the byte order without repeating the arithmetic.
	uint64_t valor = 0;
	for(char c : id.hex)
		valor = (valor << 4) | static_cast<uint64_t>(hexValue(c));
	uint64_t trocado = 0;
	for(int i = 0; i < 8; ++i)
		trocado = (trocado << 8) | ((valor >> (i * 8)) & 0xFF);
	return fromValue(trocado, id.format);
}

} // namespace orbislink
