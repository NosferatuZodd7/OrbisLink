// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// 7-Zip run as a program of its own (7zz, 7z.exe), the way Packwright is:
// it opens and extracts the archives games are downloaded in (.zip, .7z,
// .rar and their volumes). A password goes to it through its standard
// input, never on its command line (where other programs could read it),
// and is never written anywhere by OrbisLink.

#include "orbislink/library/archive_set.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>

namespace orbislink {

class ArchiveTool
{
public:
	// The 7-Zip found: `configured` (a program or its folder) when it is
	// one, the one beside OrbisLink, the installed one, then the one on the
	// PATH. Empty when there is none.
	static QString find(const QString &configured = QString());

	// Its listing. `password` empty: none is given (7-Zip is told so and
	// never waits for one).
	static library::ArchiveListing list(const QString &program, const QString &archive, const QString &password,
		std::string *output = nullptr);

	// The first `maxBytes` of one file in it, without extracting the rest
	// (enough for a package's header, details and icon). Empty when it
	// cannot be read (a password, a volume missing…).
	static QByteArray peek(const QString &program, const QString &archive, const QString &entry,
		const QString &password, qint64 maxBytes);

	// The package a listing holds, or nullptr: the first part of a package
	// in parts (`wholeSize` gets all of their sizes), else the biggest one.
	static const library::ArchiveEntry *packageIn(const library::ArchiveListing &listing, int64_t *wholeSize = nullptr);

	// Everything in it, extracted into `folder`. `stop` is asked often
	// (true: 7-Zip is stopped); `progress` gets 0-100. Returns 7-Zip's exit
	// code, kNotStarted or kStopped; `output` gets what it said.
	static constexpr int kNotStarted = -1000;
	static constexpr int kStopped = -1001;
	static int extract(const QString &program, const QString &archive, const QString &folder,
		const QString &password, const std::function<bool()> &stop, const std::function<void(double)> &progress,
		std::string *output);
};

} // namespace orbislink
