// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace orbislink {

// Describes an error code returned by the console.
// Only codes whose value and name come from a citable source are described
// (libkernel's 0x8002xxxx and BGFT's 0x8099xxxx families, see
// docs/error_codes.md). Unknown codes are returned in hexadecimal, without
// inventing a meaning.
std::string describeConsoleError(uint32_t code);

// SCE_BGFT_ERROR_TASK_DUPLICATED: the console's download list already has a
// task for that package, left by an earlier try.
constexpr uint32_t kBgftTaskDuplicated = 0x80990015u;

// "0x8002001C" from 0x8002001C.
std::string formatErrorCode(uint32_t code);

// true if the code means the console is out of space.
bool isOutOfSpaceError(uint32_t code);

} // namespace orbislink
