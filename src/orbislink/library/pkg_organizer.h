// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Putting the packages of a folder of this PC in order, as PS4 PKG Tool
// does: each renamed from what it says about itself (a pattern of
// placeholders), the base game, its update and its add-ons numbered in the
// order they install, a folder per game, and copies of the same package
// told apart. Nothing is renamed until the plan is shown and applied.

#include "orbislink/pkg/pkg_inspector.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {
namespace library {

struct OrganizeOptions
{
	// {TITLE} {TITLE_ID} {CONTENT_ID} {VERSION} {CATEGORY} {REGION}
	// {FIRMWARE} {TYPE}; the extension is added.
	std::string pattern = "{TITLE} [{TITLE_ID}] [{CATEGORY}] [v{VERSION}]";
	// "00 - " before the base game, "01 - " before its update, "02 - "
	// before its add-ons: they sort in the order they install.
	bool installOrder = false;
	// Into "<Title> (<TITLE_ID>)", inside the folder organised.
	bool folderPerGame = false;
};

// The presets offered, the first being the default.
const std::vector<std::string> &organizePatterns();

struct OrganizeStep
{
	std::string from;     // the package now
	std::string to;       // where it goes (equal to `from` when it stays)
	std::string title;    // what it says it is
	// Another file is the same package (content ID, kind and version):
	// `sameAs` is that one, and this one stays where it is.
	bool duplicate = false;
	std::string sameAs;
	bool changes() const { return !duplicate && to != from; }
};

// A placeholder's value for a package; the region from its content ID
// ("UP" USA, "EP" Europe, "JP" Japan, "HP" Asia, "KP" Korea).
std::string expandPattern(const std::string &pattern, const PkgInfo &info);
std::string regionOf(const std::string &contentId);
// A name every system takes: no / \ : * ? " < > | or control characters,
// no trailing dots or spaces, runs of spaces made one, at most 200 bytes.
std::string safeFileName(const std::string &name);

// The packages of `folder` (and its folders, a few levels down) and where
// each would go. Unreadable files are left out.
std::vector<OrganizeStep> planOrganize(const std::string &folder, const OrganizeOptions &options,
	const std::atomic<bool> *cancel = nullptr);
// The same from packages already read (tests, and a plan made again).
std::vector<OrganizeStep> planOrganize(const std::string &folder, const std::vector<PkgInfo> &packages,
	const OrganizeOptions &options);

struct OrganizeResult
{
	int moved = 0;
	std::vector<std::string> failed; // "<file>: <why>"
};

// Renames (and moves) each step that changes; a name already taken gets
// " (2)", " (3)"… Folders left empty by a move are removed.
OrganizeResult applyOrganize(const std::string &folder, const std::vector<OrganizeStep> &plan,
	const std::function<void(int done, int total)> &progress = {});

} // namespace library
} // namespace orbislink
