// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace orbislink {

// Traduz um código de erro devolvido pela consola.
// Só estão traduzidos códigos cujo valor foi confirmado em fonte oficial
// (família 0x8002xxxx do libkernel, ver docs/error_codes.md). Códigos
// desconhecidos são devolvidos em hexadecimal, sem inventar significado.
std::string describeConsoleError(uint32_t code);

// "0x8002001C" a partir de 0x8002001C.
std::string formatErrorCode(uint32_t code);

// true se o código corresponder a falta de espaço na consola.
bool isOutOfSpaceError(uint32_t code);

} // namespace orbislink
