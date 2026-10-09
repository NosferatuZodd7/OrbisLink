// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A save put back that the PS4 no longer lists needs its row in the
// console's list of saves. The row is added to a copy on this PC, which only
// goes back to the console whole — so this is worth testing here, against a
// database laid out like the console's.

#include "orbislink/qt/save_database.h"
#include "test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

using namespace orbislink;

namespace {

int counter = 0;

// An empty list of saves with the console's columns (as Apollo Save Tool
// writes them), plus `extra` columns.
QString makeDatabase(const QString &extra = QString(), const QString &seedColumns = QString(),
	const QString &seedValues = QString())
{
	const QString path = QDir::temp().filePath(
		QStringLiteral("orbislink-test-savedata-%1-%2.db").arg(QCoreApplication::applicationPid()).arg(++counter));
	QFile::remove(path);
	{
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("make"));
		db.setDatabaseName(path);
		db.open();
		QSqlQuery q(db);
		q.exec(QStringLiteral(
			"CREATE TABLE savedata(id INTEGER PRIMARY KEY, title_id TEXT NOT NULL, dir_name TEXT NOT NULL, "
			"main_title TEXT, sub_title TEXT, detail TEXT, tmp_dir_name TEXT, is_broken INTEGER, user_param "
			"INTEGER, blocks INTEGER, free_blocks INTEGER, size_kib INTEGER, mtime TEXT, fake_broken INTEGER, "
			"account_id INTEGER, user_id INTEGER, faked_owner INTEGER, cloud_icon_url TEXT, cloud_revision "
			"INTEGER, game_title_id TEXT%1)").arg(extra));
		q.exec(QStringLiteral(
			"INSERT INTO savedata(title_id, dir_name, main_title, is_broken, fake_broken, blocks%1) "
			"VALUES('CUSA00002', 'SAVE0', 'Neon Drift', 1, 0, 2%2)").arg(seedColumns, seedValues));
		db.close();
	}
	QSqlDatabase::removeDatabase(QStringLiteral("make"));
	return path;
}

struct Row
{
	int count = 0;
	QVariantMap values;
};

Row read(const QString &path, const QString &title, const QString &dir)
{
	Row row;
	{
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("read"));
		db.setDatabaseName(path);
		db.open();
		QSqlQuery q(db);
		q.prepare(QStringLiteral("SELECT title_id, dir_name, main_title, sub_title, detail, blocks, free_blocks, "
								 "size_kib, account_id, user_id, is_broken, game_title_id, mtime FROM savedata "
								 "WHERE title_id = ? AND dir_name = ?"));
		q.addBindValue(title);
		q.addBindValue(dir);
		q.exec();
		const char *names[] = { "title_id", "dir_name", "main_title", "sub_title", "detail", "blocks",
			"free_blocks", "size_kib", "account_id", "user_id", "is_broken", "game_title_id", "mtime" };
		while(q.next())
		{
			++row.count;
			for(int i = 0; i < 13; ++i)
				row.values[QString::fromLatin1(names[i])] = q.value(i);
		}
		db.close();
	}
	QSqlDatabase::removeDatabase(QStringLiteral("read"));
	return row;
}

SaveDbEntry entry(const std::string &title, const std::string &dir)
{
	SaveDbEntry e;
	e.titleId = title;
	e.dir = dir;
	e.mainTitle = "Orbis Racing";
	e.subTitle = "Career";
	e.detail = "Season 2 - 48%";
	e.blocks = 3;
	e.accountId = 0x0123456789ABCDEFLL;
	e.userId = 0x1a2b3c4d;
	return e;
}

} // namespace

ORBISLINK_TEST(a_missing_save_gets_its_row_once)
{
	const QString path = makeDatabase();
	SaveDatabase::Outcome outcome = SaveDatabase::registerSaves(path, { entry("CUSA00001", "SAVEDATA00") });
	CHECK(outcome.ok);
	CHECK_EQ(outcome.added, 1);
	CHECK(outcome.changed());

	const Row row = read(path, QStringLiteral("CUSA00001"), QStringLiteral("SAVEDATA00"));
	CHECK_EQ(row.count, 1);
	CHECK_EQ(row.values[QStringLiteral("main_title")].toString().toStdString(), std::string("Orbis Racing"));
	CHECK_EQ(row.values[QStringLiteral("sub_title")].toString().toStdString(), std::string("Career"));
	CHECK_EQ(row.values[QStringLiteral("blocks")].toLongLong(), 3LL);
	CHECK_EQ(row.values[QStringLiteral("free_blocks")].toLongLong(), 3LL);
	CHECK_EQ(row.values[QStringLiteral("size_kib")].toLongLong(), 96LL);
	CHECK_EQ(row.values[QStringLiteral("account_id")].toLongLong(), 0x0123456789ABCDEFLL);
	CHECK_EQ(row.values[QStringLiteral("user_id")].toLongLong(), 0x1a2b3c4dLL);
	CHECK_EQ(row.values[QStringLiteral("is_broken")].toInt(), 0);
	CHECK_EQ(row.values[QStringLiteral("game_title_id")].toString().toStdString(), std::string("CUSA00001"));
	CHECK(row.values[QStringLiteral("mtime")].toString().endsWith(QStringLiteral(".00Z")));

	// Again: nothing new, nothing duplicated.
	outcome = SaveDatabase::registerSaves(path, { entry("CUSA00001", "SAVEDATA00") });
	CHECK(outcome.ok);
	CHECK_EQ(outcome.added, 0);
	CHECK(!outcome.changed());
	CHECK_EQ(read(path, QStringLiteral("CUSA00001"), QStringLiteral("SAVEDATA00")).count, 1);
	QFile::remove(path);
}

// A row the console marked broken while its files were gone is only
// unmarked; its other values stay the console's.
ORBISLINK_TEST(a_listed_save_is_only_no_longer_marked_broken)
{
	const QString path = makeDatabase();
	const SaveDatabase::Outcome outcome = SaveDatabase::registerSaves(path, { entry("CUSA00002", "SAVE0") });
	CHECK(outcome.ok);
	CHECK_EQ(outcome.added, 0);
	CHECK_EQ(outcome.repaired, 1);
	const Row row = read(path, QStringLiteral("CUSA00002"), QStringLiteral("SAVE0"));
	CHECK_EQ(row.count, 1);
	CHECK_EQ(row.values[QStringLiteral("is_broken")].toInt(), 0);
	CHECK_EQ(row.values[QStringLiteral("main_title")].toString().toStdString(), std::string("Neon Drift"));
	CHECK_EQ(row.values[QStringLiteral("blocks")].toLongLong(), 2LL);
	QFile::remove(path);
}

// Columns this version does not know: with a default they are left to it;
// one that must be filled stops everything, and nothing is kept.
ORBISLINK_TEST(an_unknown_layout_changes_nothing)
{
	const QString lenient = makeDatabase(QStringLiteral(", newer_flag INTEGER NOT NULL DEFAULT 0"));
	SaveDatabase::Outcome outcome = SaveDatabase::registerSaves(lenient, { entry("CUSA00001", "SAVEDATA00") });
	CHECK(outcome.ok);
	CHECK_EQ(outcome.added, 1);
	QFile::remove(lenient);

	const QString strict = makeDatabase(QStringLiteral(", must_fill TEXT NOT NULL"), QStringLiteral(", must_fill"),
		QStringLiteral(", 'x'"));
	outcome = SaveDatabase::registerSaves(strict,
		{ entry("CUSA00002", "SAVE0"), entry("CUSA00001", "SAVEDATA00") });
	CHECK(!outcome.ok);
	CHECK(outcome.error.contains(QStringLiteral("must_fill")));
	// The broken mark of the first save was not changed either.
	CHECK_EQ(read(strict, QStringLiteral("CUSA00002"), QStringLiteral("SAVE0")).values[QStringLiteral("is_broken")].toInt(), 1);
	CHECK_EQ(read(strict, QStringLiteral("CUSA00001"), QStringLiteral("SAVEDATA00")).count, 0);
	QFile::remove(strict);

	// Not a list of saves at all.
	const QString other = QDir::temp().filePath(QStringLiteral("orbislink-test-other-%1.db").arg(QCoreApplication::applicationPid()));
	QFile::remove(other);
	outcome = SaveDatabase::registerSaves(other, { entry("CUSA00001", "SAVEDATA00") });
	CHECK(!outcome.ok);
	QFile::remove(other);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	return orbislink_test::runAll();
}
