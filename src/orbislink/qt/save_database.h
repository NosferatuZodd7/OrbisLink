// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/saves/save_vault.h"

#include <QString>

#include <vector>

namespace orbislink {

// The console's list of saves (savedata.db, SQLite), edited on a copy on
// this PC: a save put back that the console no longer lists gets its row,
// the same row Apollo Save Tool adds when it creates a save. Rows already
// there are left as they are, only no longer marked broken.
class SaveDatabase
{
public:
	struct Outcome
	{
		bool ok = false;
		int added = 0;    // rows added
		int repaired = 0; // rows that were marked broken
		QString error;
		bool changed() const { return added > 0 || repaired > 0; }
	};

	// Adds the rows to the database file at `path`, checks it is still whole
	// and closes it. On any failure nothing is kept (one transaction).
	static Outcome registerSaves(const QString &path, const std::vector<SaveDbEntry> &entries);

	// Whose saves the list holds: the PSN account (its PSID as a number) most
	// of its rows name, or 0 when none does or they do not agree. Only read.
	static int64_t owner(const QString &path);
};

} // namespace orbislink
