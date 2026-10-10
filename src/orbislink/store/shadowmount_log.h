// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace orbislink {
namespace store {

// What ShadowMountPlus — the payload that puts app folders on the PS5's
// home screen — says about one title in its log (/data/shadowmount/debug.log,
// read over FTP). It checks /data/homebrew every 15 seconds; an app missing
// from the home screen is one it skipped or failed to register, and the log
// says which.
struct ShadowMountReport
{
	enum class Verdict
	{
		NotSeen,     // the log never names it
		Registered,  // installed or restored on the home screen
		Settling,    // its files were still changing ("not stable yet")
		BadMetadata, // sce_sys/param.json missing or unreadable
		Failed,      // the console refused to register it (code in `code`)
		GaveUp,      // retry limit reached: no new try until reset or restart
		Duplicate,   // another copy of the same title ID is somewhere it scans
	};
	Verdict verdict = Verdict::NotSeen;
	// The error code of a failed registration ("0x80990001").
	std::string code;
	// The log lines about it, oldest first (a few after each mention too:
	// the failure line does not repeat the title ID).
	std::vector<std::string> lines;
};

ShadowMountReport readShadowMountLog(const std::string &log, const std::string &titleId);

} // namespace store
} // namespace orbislink
