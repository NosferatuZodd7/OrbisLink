// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/store/shadowmount_log.h"

#include <algorithm>
#include <cctype>

namespace orbislink {
namespace store {

namespace {

bool has(const std::string &line, const char *text) { return line.find(text) != std::string::npos; }

std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

// A detail line of the event before it ("  [REG] FAIL: 0x…"): indented,
// tagged, and naming no title of its own.
bool detailLine(const std::string &line)
{
	const size_t start = line.find_first_not_of(" \t");
	return start != std::string::npos && start > 0 && line[start] == '[' && !has(line, "PPSA")
		&& !has(line, "CUSA");
}

} // namespace

ShadowMountReport readShadowMountLog(const std::string &log, const std::string &titleId)
{
	ShadowMountReport report;
	if(titleId.empty())
		return report;

	std::vector<std::string> all;
	size_t start = 0;
	while(start < log.size())
	{
		size_t end = log.find('\n', start);
		if(end == std::string::npos)
			end = log.size();
		std::string line = log.substr(start, end - start);
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		all.push_back(line);
		start = end + 1;
	}

	for(size_t i = 0; i < all.size(); ++i)
	{
		if(!has(all[i], titleId.c_str()))
			continue;
		report.lines.push_back(all[i]);
		for(size_t j = i + 1; j < all.size() && j <= i + 3 && detailLine(all[j]); ++j)
			report.lines.push_back(all[j]);
	}
	// What it said last is where it stands now.
	for(const std::string &line : report.lines)
	{
		const std::string low = lower(line);
		if(has(line, "[REG] Installed") || has(line, "Installed NEW") || has(line, "[REG] Restored")
			|| has(line, "already present in app.db"))
		{
			report.verdict = ShadowMountReport::Verdict::Registered;
			report.code.clear();
		}
		else if(has(line, "FAIL: 0x"))
		{
			report.verdict = ShadowMountReport::Verdict::Failed;
			const size_t at = line.find("0x", line.find("FAIL: "));
			report.code = line.substr(at, line.find_first_of(" \t", at) - at);
		}
		else if(has(low, "retry limit") || has(low, "attempt limit"))
			report.verdict = ShadowMountReport::Verdict::GaveUp;
		else if(has(low, "not stable yet"))
			report.verdict = ShadowMountReport::Verdict::Settling;
		else if(has(low, "invalid param.json") || has(low, "game info unavailable"))
			report.verdict = ShadowMountReport::Verdict::BadMetadata;
		else if(has(low, "duplicate"))
			report.verdict = ShadowMountReport::Verdict::Duplicate;
		else if(has(line, "Install timeout") || (has(line, "[REG]") && has(low, "unavailable")))
		{
			report.verdict = ShadowMountReport::Verdict::Failed;
			report.code.clear();
		}
	}
	// The latest ones are enough to see what happened.
	if(report.lines.size() > 24)
		report.lines.erase(report.lines.begin(), report.lines.end() - 24);
	return report;
}

} // namespace store
} // namespace orbislink
