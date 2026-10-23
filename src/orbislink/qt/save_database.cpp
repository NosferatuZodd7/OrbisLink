// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/save_database.h"

#include <QDateTime>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QVariant>

namespace orbislink {

namespace {

struct Column
{
	QString name;
	bool required = false; // NOT NULL with no default, and not the key
};

QString quoted(const QString &name)
{
	return QLatin1Char('"') + QString(name).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
}

// What a new row holds, by column: Apollo Save Tool's row for a save it
// creates, with the save's own names.
QMap<QString, QVariant> rowFor(const SaveDbEntry &entry)
{
	const QString now = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'.00Z'"));
	QMap<QString, QVariant> row;
	row[QStringLiteral("title_id")] = QString::fromStdString(entry.titleId);
	row[QStringLiteral("dir_name")] = QString::fromStdString(entry.dir);
	row[QStringLiteral("main_title")] = QString::fromStdString(entry.mainTitle);
	row[QStringLiteral("sub_title")] = QString::fromStdString(entry.subTitle);
	row[QStringLiteral("detail")] = QString::fromStdString(entry.detail);
	row[QStringLiteral("tmp_dir_name")] = QString();
	row[QStringLiteral("is_broken")] = 0;
	row[QStringLiteral("user_param")] = 0;
	row[QStringLiteral("blocks")] = static_cast<qint64>(entry.blocks);
	row[QStringLiteral("free_blocks")] = static_cast<qint64>(entry.blocks);
	row[QStringLiteral("size_kib")] = static_cast<qint64>(entry.blocks * 32);
	row[QStringLiteral("mtime")] = now;
	row[QStringLiteral("fake_broken")] = 0;
	row[QStringLiteral("account_id")] = static_cast<qint64>(entry.accountId);
	row[QStringLiteral("user_id")] = static_cast<qint64>(entry.userId);
	row[QStringLiteral("faked_owner")] = 0;
	row[QStringLiteral("cloud_icon_url")] = QString();
	row[QStringLiteral("cloud_revision")] = 0;
	row[QStringLiteral("game_title_id")] = QString::fromStdString(entry.titleId);
	// A PS5's list of PS4 saves has this one too.
	row[QStringLiteral("system_blocks")] = 0;
	// Text columns take '' rather than NULL, as in the console's own rows.
	for(auto it = row.begin(); it != row.end(); ++it)
		if(it.value().typeId() == QMetaType::QString && it.value().toString().isNull())
			it.value() = QStringLiteral("");
	return row;
}

SaveDatabase::Outcome apply(QSqlDatabase &db, const std::vector<SaveDbEntry> &entries)
{
	SaveDatabase::Outcome outcome;
	auto failed = [&outcome](const QString &why) {
		outcome.ok = false;
		outcome.error = why;
		return outcome;
	};

	QSqlQuery info(db);
	if(!info.exec(QStringLiteral("PRAGMA table_info(savedata)")))
		return failed(info.lastError().text());
	QList<Column> columns;
	while(info.next())
	{
		Column column;
		column.name = info.value(1).toString();
		column.required = info.value(3).toInt() != 0 && info.value(4).isNull() && info.value(5).toInt() == 0;
		columns.push_back(column);
	}
	info.finish();
	auto has = [&columns](const QString &name) {
		for(const Column &column : columns)
			if(column.name == name)
				return true;
		return false;
	};
	if(!has(QStringLiteral("title_id")) || !has(QStringLiteral("dir_name")))
		return failed(QStringLiteral("This is not the list of saves this version knows (no savedata table)."));

	if(!db.transaction())
		return failed(db.lastError().text());
	auto rollback = [&db, &failed](const QString &why) {
		if(!db.rollback())
			return failed(why + QStringLiteral(" (and undoing it failed: ") + db.lastError().text()
				+ QStringLiteral(")"));
		return failed(why);
	};

	for(const SaveDbEntry &entry : entries)
	{
		const QString title = QString::fromStdString(entry.titleId);
		const QString dir = QString::fromStdString(entry.dir);
		QSqlQuery find(db);
		find.prepare(QStringLiteral("SELECT COUNT(*) FROM savedata WHERE title_id = ? AND dir_name = ?"));
		find.addBindValue(title);
		find.addBindValue(dir);
		if(!find.exec() || !find.next())
			return rollback(find.lastError().text());
		const bool listed = find.value(0).toInt() > 0;
		// A statement left open keeps SQLite from rolling back.
		find.finish();

		if(listed)
		{
			// Listed already: only no longer marked broken, now that its
			// files are back.
			QStringList flags;
			for(const QString &flag : { QStringLiteral("is_broken"), QStringLiteral("fake_broken") })
				if(has(flag))
					flags << flag;
			if(flags.isEmpty())
				continue;
			QStringList sets;
			QStringList broken;
			for(const QString &flag : flags)
			{
				sets << quoted(flag) + QStringLiteral(" = 0");
				broken << quoted(flag) + QStringLiteral(" != 0");
			}
			QSqlQuery repair(db);
			repair.prepare(QStringLiteral("UPDATE savedata SET %1 WHERE title_id = ? AND dir_name = ? AND (%2)")
							   .arg(sets.join(QStringLiteral(", ")), broken.join(QStringLiteral(" OR "))));
			repair.addBindValue(title);
			repair.addBindValue(dir);
			if(!repair.exec())
				return rollback(repair.lastError().text());
			if(repair.numRowsAffected() > 0)
				++outcome.repaired;
			repair.finish();
			continue;
		}

		const QMap<QString, QVariant> row = rowFor(entry);
		QStringList names;
		QList<QVariant> values;
		for(const Column &column : columns)
		{
			const auto value = row.find(column.name);
			if(value != row.end())
			{
				names << quoted(column.name);
				values << value.value();
			}
			else if(column.required)
			{
				return rollback(QStringLiteral("The console's list of saves has a column this version does not "
											   "know how to fill (%1).").arg(column.name));
			}
		}
		QStringList marks;
		for(int i = 0; i < names.size(); ++i)
			marks << QStringLiteral("?");
		QSqlQuery insert(db);
		insert.prepare(QStringLiteral("INSERT INTO savedata(%1) VALUES(%2)")
						   .arg(names.join(QStringLiteral(", ")), marks.join(QStringLiteral(", "))));
		for(const QVariant &value : values)
			insert.addBindValue(value);
		if(!insert.exec())
			return rollback(insert.lastError().text());
		insert.finish();
		++outcome.added;
	}

	if(!db.commit())
		return rollback(db.lastError().text());

	// Still a whole database: what goes back to the console must be.
	QSqlQuery check(db);
	if(!check.exec(QStringLiteral("PRAGMA integrity_check")) || !check.next()
		|| check.value(0).toString() != QLatin1String("ok"))
		return failed(QStringLiteral("The edited list of saves did not pass SQLite's check."));
	outcome.ok = true;
	return outcome;
}

} // namespace

SaveDatabase::Outcome SaveDatabase::registerSaves(const QString &path, const std::vector<SaveDbEntry> &entries)
{
	Outcome outcome;
	if(!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
	{
		outcome.error = QStringLiteral("SQLite is not available in this build.");
		return outcome;
	}
	const QString connection =
		QStringLiteral("orbislink-savedata-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
	{
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
		db.setDatabaseName(path);
		if(!db.open())
			outcome.error = db.lastError().text();
		else
		{
			outcome = apply(db, entries);
			db.close();
		}
	}
	QSqlDatabase::removeDatabase(connection);
	return outcome;
}

int64_t SaveDatabase::owner(const QString &path)
{
	if(!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
		return 0;
	int64_t found = 0;
	const QString connection =
		QStringLiteral("orbislink-savedata-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
	{
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
		db.setDatabaseName(path);
		db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
		if(db.open())
		{
			QSqlQuery query(db);
			if(query.exec(QStringLiteral("SELECT account_id, COUNT(*) FROM savedata WHERE account_id IS NOT NULL "
										 "AND account_id != 0 GROUP BY account_id ORDER BY COUNT(*) DESC")))
			{
				int64_t best = 0;
				int bestCount = 0;
				int total = 0;
				while(query.next())
				{
					const int count = query.value(1).toInt();
					if(total == 0)
					{
						best = query.value(0).toLongLong();
						bestCount = count;
					}
					total += count;
				}
				// Most of them, not merely the most.
				if(bestCount * 2 > total)
					found = best;
			}
			query.finish();
			db.close();
		}
	}
	QSqlDatabase::removeDatabase(connection);
	return found;
}

} // namespace orbislink
