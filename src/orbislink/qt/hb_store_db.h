// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/store/hb_store.h"

#include <QString>

#include <vector>

namespace orbislink {

// Reads the apps of a Homebrew Store database (store.db: SQLite, the
// "homebrews" table), read-only; the columns a store leaves out are left
// empty. False, with why, when the file is not such a database.
bool readHbStoreDatabase(const QString &path, std::vector<store::HbStoreApp> *apps, QString *error);

} // namespace orbislink
