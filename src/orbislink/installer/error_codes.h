// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace orbislink {

// Describes an error code returned by the console.
// Only codes whose value was confirmed in an official source are described
// (libkernel's 0x8002xxxx family, see docs/error_codes.md). Unknown codes are
// returned in hexadecimal, without inventing a meaning.
std::string describeConsoleError(uint32_t code);

// "0x8002001C" a partir de 0x8002001C.
std::string formatErrorCode(uint32_t code);

// true if the code means the console is out of space.
bool isOutOfSpaceError(uint32_t code);

} // namespace orbislink
