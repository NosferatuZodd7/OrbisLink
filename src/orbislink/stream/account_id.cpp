// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/account_id.h"

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

// Tira espaços, tabulações e quebras de linha de qualquer sítio do texto.
// Quem copia isto de um ecrã de consola traz espaços pelo meio a metade das
// vezes, e recusar por causa disso seria só mau feitio.
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

	// Os 8 bytes em little-endian: é esta a ordem que o Remote Play espera.
	unsigned char bytes[8];
	for(int i = 0; i < 8; ++i)
		bytes[i] = static_cast<unsigned char>((valor >> (i * 8)) & 0xFF);

	char b64[16] {};
	if(chiaki_base64_encode(bytes, sizeof(bytes), b64, sizeof(b64)) != CHIAKI_ERR_SUCCESS)
	{
		id.error = "Não foi possível converter o Account ID para base64.";
		return id;
	}
	id.base64 = b64;

	// Hexadecimal com o byte mais significativo à esquerda, que é como um
	// número se escreve e como as ferramentas o mostram.
	id.hex.reserve(16);
	for(int i = 15; i >= 0; --i)
		id.hex.push_back(kHexDigits[(valor >> (i * 4)) & 0xF]);

	// Decimal à mão: o std::to_string chega, mas assim fica claro que não há
	// sinal nenhum pelo meio.
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
		return erro("Falta o Account ID da PSN.");

	// 1. "0x…" é uma ordem explícita: lê-se em hexadecimal, e é assim que se
	//    desfaz a ambiguidade de um ID só com algarismos.
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
				return erro("Isto parece base64 mas não dá 8 bytes. Confirma que copiaste o "
							"Account ID todo.");
			}
			uint64_t valor = 0;
			for(int i = 7; i >= 0; --i)
				valor = (valor << 8) | bytes[i];
			return fromValue(valor, "base64");
		}
	}

	// 3. Só algarismos: é o user_id como a PSN o dá.
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
				// Um Account ID não passa dos 64 bits. Se passar, o texto
				// não é um Account ID e dizer isso é melhor do que dar a
				// volta ao contador em silêncio.
				if(valor > (UINT64_MAX - digito) / 10)
					return erro("Esse número é grande demais para ser um Account ID.");
				valor = valor * 10 + digito;
			}
			return fromValue(valor, "decimal");
		}
	}

	// 4. Hexadecimal, com ou sem "0x".
	if(hex.empty())
		hex = limpo;
	// Separadores que aparecem em ecrãs de consola.
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

	return erro("Não reconheço este Account ID. Aceito as três formas: os 16 dígitos "
				"hexadecimais, o número decimal, ou os 12 caracteres em base64.");
}

AccountId reverseAccountIdBytes(const AccountId &id)
{
	if(!id.valid)
		return id;
	// Reler o hexadecimal ao contrário é a maneira mais directa de trocar a
	// ordem dos bytes sem repetir a aritmética.
	uint64_t valor = 0;
	for(char c : id.hex)
		valor = (valor << 4) | static_cast<uint64_t>(hexValue(c));
	uint64_t trocado = 0;
	for(int i = 0; i < 8; ++i)
		trocado = (trocado << 8) | ((valor >> (i * 8)) & 0xFF);
	return fromValue(trocado, id.format);
}

} // namespace orbislink
