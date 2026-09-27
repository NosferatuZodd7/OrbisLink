// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>

namespace orbislink {

// An OrbisLink version: "0.1.8", "v0.1.8", "0.1.8-dev.42".
//
// Prereleases are ordered as in semver: 0.1.8-dev.2 comes BEFORE
// 0.1.8. That is what makes the testing channel work — someone on 0.1.8-dev.2
// must get 0.1.8-dev.3 and then the final 0.1.8, and someone on the final
// 0.1.8 must not be pushed back by a dev build.
struct Version
{
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::string pre;      // "dev.42", empty in the final release
	bool valid = false;

	std::string toString() const;
};

Version parseVersion(const std::string &text);

// <0 if a is older than b, 0 if equal, >0 if a is newer.
// An invalid version counts as the oldest possible.
int compareVersions(const Version &a, const Version &b);
int compareVersions(const std::string &a, const std::string &b);

} // namespace orbislink
