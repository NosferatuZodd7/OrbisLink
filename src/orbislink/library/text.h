// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Small text helpers the library's files share; the others are in
// common/util.h.
#pragma once

#include "orbislink/common/util.h"

#include <cctype>
#include <string>

namespace orbislink {
namespace library {

// ASCII upper case: title IDs and add-on labels are compared in it.
inline std::string upper(std::string text)
{
	for(char &c : text)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return text;
}

// ".pkg" for "Game.PKG"; empty when there is no dot, or only a leading one.
inline std::string extensionOf(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos || dot == 0 ? std::string() : toLower(name.substr(dot));
}

} // namespace library
} // namespace orbislink
