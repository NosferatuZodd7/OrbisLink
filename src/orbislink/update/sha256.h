// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace orbislink {

// SHA-256 próprio, sem dependências.
//
// É de propósito que não usa o OpenSSL: o núcleo do OrbisLink compila sem
// Remote Play, e nesse caso não há OpenSSL nenhum no build. São cem linhas
// e o algoritmo tem vectores de teste públicos — está coberto em
// tests/test_update.cpp.
class Sha256
{
public:
	Sha256();
	void update(const void *data, size_t size);
	void update(const std::string &data);
	// Devolve os 64 caracteres em minúsculas. Depois disto o objecto não
	// deve ser reutilizado.
	std::string hex();

private:
	void compress(const uint8_t block[64]);

	uint32_t state_[8];
	uint8_t buffer_[64];
	size_t buffered_ = 0;
	uint64_t total_ = 0;
};

std::string sha256Hex(const std::string &data);
// Vazio se o ficheiro não se conseguir ler.
std::string sha256File(const std::string &path);

} // namespace orbislink
