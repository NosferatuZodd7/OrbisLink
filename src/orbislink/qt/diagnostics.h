// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace orbislink {

class AppController;

// Gathers into a single file everything needed to understand a fault
// without being in front of the machine: versions, environment, settings
// (without the secrets), service state, the trace of the last Remote Play
// attempt and the tail of the log.
//
// The goal is that attaching this file to a message is enough.
class Diagnostics
{
public:
	// The report as text. It never includes registration keys, rp_key or
	// the Account ID — the Logger already masks them, and what is read from
	// the settings is filtered here again.
	static QString report(AppController *app);

	// Writes the report to a file. Returns the path, or empty on
	// failure. Empty `directory` = desktop.
	static QString write(AppController *app, const QString &directory, QString *error);

	// Suggested name, with date and time so it does not overwrite the previous one.
	static QString suggestedFileName();
};

} // namespace orbislink
