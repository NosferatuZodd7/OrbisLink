// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/hb_store_db.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QUuid>
#include <QVariant>

#include <cctype>
#include <set>

namespace orbislink {

namespace {

std::string text(const QSqlQuery &query, const QSqlRecord &record, const char *column)
{
	const int index = record.indexOf(QString::fromLatin1(column));
	if(index < 0 || query.value(index).isNull())
		return std::string();
	return query.value(index).toString().trimmed().toStdString();
}

// A title ID as the stores use them: letters and digits, nine of them.
bool plainTitleId(const std::string &id)
{
	if(id.size() != 9)
		return false;
	for(char c : id)
		if(!std::isalnum(static_cast<unsigned char>(c)))
			return false;
	return true;
}

bool read(QSqlDatabase &db, std::vector<store::HbStoreApp> *apps, QString *error)
{
	QSqlQuery query(db);
	if(!query.exec(QStringLiteral("SELECT * FROM homebrews")))
	{
		*error = QStringLiteral("it is not a store's database: %1").arg(query.lastError().text());
		return false;
	}
	const QSqlRecord record = query.record();
	if(record.indexOf(QStringLiteral("id")) < 0 || record.indexOf(QStringLiteral("package")) < 0)
	{
		*error = QStringLiteral("its list of apps has no title IDs or packages");
		return false;
	}
	std::set<std::string> seen;
	while(query.next())
	{
		store::HbStoreApp app;
		app.titleId = QString::fromStdString(text(query, record, "id")).toUpper().toStdString();
		app.name = text(query, record, "name");
		app.package = text(query, record, "package");
		if(!plainTitleId(app.titleId) || app.package.empty() || !seen.insert(app.titleId).second)
			continue;
		if(app.name.empty())
			app.name = app.titleId;
		// What it is: the store's line, then its two notes.
		QStringList lines;
		for(const char *column : { "desc", "desc_1", "desc_2" })
		{
			const QString line = QString::fromStdString(text(query, record, column));
			if(!line.isEmpty())
				lines << line;
		}
		app.description = lines.join(QLatin1Char('\n')).toStdString();
		app.author = text(query, record, "Author");
		app.version = text(query, record, "version");
		app.sizeText = text(query, record, "Size");
		app.type = text(query, record, "apptype");
		app.firmware = text(query, record, "pv");
		app.released = text(query, record, "releaseddate");
		app.image = text(query, record, "image");
		if(app.image.empty())
			app.image = text(query, record, "main_icon_path");
		app.md5 = QString::fromStdString(text(query, record, "md5")).toLower().toStdString();
		app.ps5Version = text(query, record, "ps5_version");
		app.ps5Firmware = text(query, record, "ps5_pv");
		app.ps5SizeText = text(query, record, "ps5_Size");
		apps->push_back(app);
	}
	return true;
}

} // namespace

bool readHbStoreDatabase(const QString &path, std::vector<store::HbStoreApp> *apps, QString *error)
{
	apps->clear();
	QString why;
	if(!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
		why = QStringLiteral("SQLite is not available in this build");
	bool ok = false;
	if(why.isEmpty())
	{
		const QString connection =
			QStringLiteral("orbislink-hbstore-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
		{
			QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
			db.setDatabaseName(path);
			// Only read: the copy kept is the store's, as it came.
			db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
			if(!db.open())
				why = db.lastError().text();
			else
			{
				ok = read(db, apps, &why);
				db.close();
			}
		}
		QSqlDatabase::removeDatabase(connection);
	}
	if(!ok && error)
		*error = why;
	return ok;
}

} // namespace orbislink
