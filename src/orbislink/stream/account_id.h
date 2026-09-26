// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>

namespace orbislink {

// O Account ID da PSN, nas três formas em que se costuma encontrar.
//
// É sempre o mesmo número de 64 bits. O que muda é como está escrito:
//
//   decimal      o "user_id" tal como a conta PSN o tem
//   hexadecimal  o mesmo número em base 16, byte mais significativo à
//                esquerda — é assim que a maioria das ferramentas de uma
//                consola desbloqueada o mostra
//   base64       os 8 bytes do número em little-endian, codificados — é a
//                única forma que o Remote Play aceita
//
// A ordem dos bytes não é uma escolha deste projecto: está no
// scripts/psn-account-id.py do chiaki-ng, que faz
// base64.b64encode(user_id.to_bytes(8, "little")). Escrevê-la ao contrário
// dá um ID que a consola recusa sem dizer porquê.
struct AccountId
{
	bool valid = false;
	std::string base64;
	std::string hex;     // 16 dígitos, sem o "0x"
	std::string decimal;
	// Como o texto foi lido: "base64", "hex" ou "decimal".
	std::string format;
	// Porque falhou, quando valid é falso.
	std::string error;
};

// Lê o Account ID em qualquer das três formas e devolve as outras duas.
//
// Há uma ambiguidade que não se resolve sozinha: um texto só com algarismos
// e 16 caracteres tanto pode ser decimal como hexadecimal. Lê-se como
// decimal, porque é a forma em que a PSN o dá; para forçar hexadecimal,
// escreve-se com "0x" à frente. É por isso que a interface mostra as três
// formas ao mesmo tempo — quem o tem à frente vê logo se bate certo.
AccountId parseAccountId(const std::string &texto);

// Inverte a ordem dos bytes. Algumas ferramentas mostram os bytes em bruto
// em vez do número, e aí o hexadecimal sai ao contrário.
AccountId reverseAccountIdBytes(const AccountId &id);

} // namespace orbislink
