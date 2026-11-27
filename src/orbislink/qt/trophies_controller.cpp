// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/trophies_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/translate_message.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace orbislink {

namespace {

// Where a PS4 keeps them (the paths Apollo Save Tool uses).
QString homeOf(const QString &user) { return QStringLiteral("/user/home/") + user; }
QString databaseOf(const QString &user) { return homeOf(user) + QStringLiteral("/trophy/db/trophy_local.db"); }
QString dataOf(const QString &user, const QString &npId) { return homeOf(user) + QStringLiteral("/trophy/data/") + npId; }
// Each game's trophy pictures, as the console installed them.
QString iconOf(const QString &npId) { return QStringLiteral("/user/trophy/conf/") + npId + QStringLiteral("/ICON0.PNG"); }
QString artOf(const QString &npId, int trophy)
{
	return QStringLiteral("/user/trophy/conf/") + npId + QStringLiteral("/TROP%1.PNG").arg(trophy, 3, 10, QLatin1Char('0'));
}

bool isUserFolder(const std::string &name)
{
	return name.size() == 8 && std::all_of(name.begin(), name.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

// A name Windows takes (the console's may not be).
QString safeName(QString name)
{
	for(QChar &c : name)
		if(c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c))
			c = QLatin1Char('_');
	while(name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
		name.chop(1);
	return name.isEmpty() ? QStringLiteral("_") : name;
}

QString cacheFor(const std::string &address)
{
	const QByteArray key = QCryptographicHash::hash(QByteArray::fromStdString(address), QCryptographicHash::Sha1);
	return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
		.filePath(QStringLiteral("trophies/") + QString::fromLatin1(key.toHex().left(12)));
}

// The name the console keeps for its user (username.dat), if it reads as one.
QString readUserName(const QString &path)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly) || file.size() > 64)
		return QString();
	QByteArray data = file.readAll();
	const int end = data.indexOf('\0');
	if(end >= 0)
		data.truncate(end);
	for(const char c : data)
		if(static_cast<unsigned char>(c) < 0x20)
			return QString();
	return QString::fromUtf8(data).trimmed();
}

QString gradeName(int grade)
{
	switch(grade)
	{
		case 1: return QStringLiteral("platinum");
		case 2: return QStringLiteral("gold");
		case 3: return QStringLiteral("silver");
		default: return QStringLiteral("bronze");
	}
}

struct Read
{
	QVariantList titles;
	std::map<int, QVariantList> trophies;
	QString error;
};

// The console's list of trophies, from a copy on this PC; only read.
Read readDatabase(const QString &path)
{
	Read read;
	static std::atomic<int> serial { 0 };
	const QString connection = QStringLiteral("trophies-") + QString::number(++serial);
	{
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
		db.setDatabaseName(path);
		db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
		if(!db.open())
		{
			read.error = db.lastError().text();
		}
		else
		{
			QSqlQuery titles(db);
			if(!titles.exec(QStringLiteral("SELECT * FROM tbl_trophy_title WHERE status = 0")))
				read.error = titles.lastError().text();
			while(titles.next())
			{
				const QSqlRecord row = titles.record();
				auto number = [&row](const char *field) {
					const int at = row.indexOf(QLatin1String(field));
					return at >= 0 ? row.value(at).toInt() : 0;
				};
				auto text = [&row](const char *field) {
					const int at = row.indexOf(QLatin1String(field));
					return at >= 0 ? row.value(at).toString() : QString();
				};
				QVariantMap title;
				title[QStringLiteral("id")] = number("id");
				title[QStringLiteral("npId")] = text("trophy_title_id");
				title[QStringLiteral("title")] = text("title");
				title[QStringLiteral("description")] = text("description");
				title[QStringLiteral("total")] = number("trophy_num");
				title[QStringLiteral("unlocked")] = number("unlocked_trophy_num");
				title[QStringLiteral("progress")] = number("progress");
				for(const char *grade : { "platinum", "gold", "silver", "bronze" })
				{
					title[QString::fromLatin1(grade)] = number((std::string("unlocked_") + grade + "_num").c_str());
					title[QString::fromLatin1(grade) + QStringLiteral("Total")] = number((std::string(grade) + "_num").c_str());
				}
				read.titles << title;
			}
			QSqlQuery flags(db);
			if(flags.exec(QStringLiteral("SELECT * FROM tbl_trophy_flag ORDER BY title_id, trophyid")))
			{
				while(flags.next())
				{
					const QSqlRecord row = flags.record();
					auto value = [&row](const char *field) {
						const int at = row.indexOf(QLatin1String(field));
						return at >= 0 ? row.value(at) : QVariant();
					};
					QVariantMap trophy;
					trophy[QStringLiteral("id")] = value("trophyid").toInt();
					trophy[QStringLiteral("group")] = value("groupid").toInt();
					trophy[QStringLiteral("title")] = value("title").toString();
					trophy[QStringLiteral("description")] = value("description").toString();
					trophy[QStringLiteral("grade")] = gradeName(value("grade").toInt());
					trophy[QStringLiteral("unlocked")] = value("unlocked").toInt() != 0;
					// Hidden until unlocked, where the list says so.
					const QVariant visible = value("visible");
					trophy[QStringLiteral("hidden")] = visible.isValid() && visible.toInt() == 0;
					const QDateTime when = QDateTime::fromString(value("time_unlocked").toString(), Qt::ISODateWithMs);
					trophy[QStringLiteral("date")] = value("unlocked").toInt() != 0 && when.isValid()
						? QLocale().toString(when.toLocalTime(), QLocale::ShortFormat) : QString();
					trophy[QStringLiteral("when")] = when.isValid() ? when.toMSecsSinceEpoch() : 0;
					// The day alone, for the list ("21 Nov 2026").
					trophy[QStringLiteral("day")] = value("unlocked").toInt() != 0 && when.isValid()
						? QLocale().toString(when.toLocalTime().date(), QStringLiteral("d MMM yyyy")) : QString();
					// The time without its seconds, as the locale writes it.
					QString shortTime = QLocale().timeFormat(QLocale::ShortFormat);
					shortTime.remove(QStringLiteral(":ss")).remove(QStringLiteral(".ss"));
					trophy[QStringLiteral("time")] = value("unlocked").toInt() != 0 && when.isValid()
						? QLocale().toString(when.toLocalTime().time(), shortTime) : QString();
					// How many players have it, where the list keeps it (not every
					// console's does): a percentage, or -1 when not known.
					double rate = -1;
					for(const char *field : { "rarity", "earned_rate", "earn_rate", "rare_rate" })
					{
						const QVariant v = value(field);
						if(v.isValid() && !v.isNull() && !v.toString().isEmpty())
						{
							rate = v.toDouble();
							break;
						}
					}
					trophy[QStringLiteral("rarity")] = rate;
					read.trophies[value("title_id").toInt()] << trophy;
				}
			}
		}
		db.close();
	}
	QSqlDatabase::removeDatabase(connection);
	// The ones played most recently first is not in the list; by name.
	std::sort(read.titles.begin(), read.titles.end(), [](const QVariant &a, const QVariant &b) {
		return a.toMap().value(QStringLiteral("title")).toString().localeAwareCompare(
				   b.toMap().value(QStringLiteral("title")).toString()) < 0;
	});
	return read;
}

// A folder on the console copied to this PC, every file in it.
bool downloadTree(FtpClient &ftp, const QString &remote, const QString &local, const std::atomic<bool> &stop,
	QString *error)
{
	std::vector<FtpEntry> entries;
	const FtpResult listed = ftp.list(remote.toStdString(), &entries);
	if(!listed.ok)
	{
		*error = translateMessage(listed.message);
		return false;
	}
	QDir().mkpath(local);
	for(const FtpEntry &entry : entries)
	{
		if(stop.load())
			return false;
		if(entry.name == "." || entry.name == "..")
			continue;
		const QString name = QString::fromStdString(entry.name);
		const QString from = remote + QLatin1Char('/') + name;
		const QString to = QDir(local).filePath(safeName(name));
		if(entry.isDirectory)
		{
			if(!downloadTree(ftp, from, to, stop, error))
				return false;
			continue;
		}
		const FtpResult got = ftp.download(from.toStdString(), to.toStdString(),
			[&stop](int64_t, int64_t) { return !stop.load(); });
		if(!got.ok)
		{
			*error = name + QStringLiteral(": ") + translateMessage(got.message);
			return false;
		}
	}
	return true;
}

} // namespace

TrophiesController::TrophiesController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	// Another console in use: what was read was of the one before.
	// Any other setting (the theme, the language…) keeps what was read.
	connect(app_, &AppController::settingsChanged, this, [this]() {
		if(!scanned_ || QString::fromStdString(app_->settings().consoleAddress) == address_)
			return;
		titles_.clear();
		users_.clear();
		trophies_.clear();
		user_.clear();
		scanned_ = false;
		emit titlesChanged();
	});
}

TrophiesController::~TrophiesController()
{
	lifeline_->cut();
	stopping_.store(true);
	if(worker_.joinable())
		worker_.join();
}

QString TrophiesController::folder() const
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
		.filePath(QStringLiteral("OrbisLink trophies"));
}

bool TrophiesController::ps5() const { return app_->activeIsPs5(); }

QString TrophiesController::userFolderOnPc(const QString &user) const
{
	QString name;
	for(const QVariant &entry : users_)
		if(entry.toMap().value(QStringLiteral("folder")).toString() == user)
			name = entry.toMap().value(QStringLiteral("name")).toString();
	return QDir(folder()).filePath(name.isEmpty() ? user : safeName(name) + QStringLiteral(" - ") + user);
}

void TrophiesController::openFolder() const
{
	QDir().mkpath(folder());
	QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
}

void TrophiesController::setProgress(const QString &status, double fraction)
{
	lifeline_->post([this, status, fraction]() {
		status_ = status;
		progress_ = fraction;
		emit busyChanged();
	});
}

QVariantList TrophiesController::compareOf(const QString &npId) const
{
	QVariantList list;
	for(const auto &[user, games] : everyone_)
	{
		const QVariantMap game = games.value(npId).toMap();
		if(game.isEmpty())
			continue;
		QString name = user;
		for(const QVariant &entry : users_)
			if(entry.toMap().value(QStringLiteral("folder")).toString() == user
				&& !entry.toMap().value(QStringLiteral("name")).toString().isEmpty())
				name = entry.toMap().value(QStringLiteral("name")).toString();
		QVariantMap row = game;
		row[QStringLiteral("user")] = user;
		row[QStringLiteral("name")] = name;
		QVariantList earned;
		for(const QVariant &t : game.value(QStringLiteral("trophies")).toList())
			if(t.toMap().value(QStringLiteral("unlocked")).toBool())
				earned << t.toMap().value(QStringLiteral("id"));
		row[QStringLiteral("earned")] = earned;
		row.remove(QStringLiteral("trophies"));
		list << row;
	}
	return list;
}

void TrophiesController::loadArt(int titleId)
{
	QString npId;
	for(const QVariant &entry : titles_)
		if(entry.toMap().value(QStringLiteral("id")).toInt() == titleId)
			npId = entry.toMap().value(QStringLiteral("npId")).toString();
	const QVariantList list = trophiesOf(titleId);
	if(npId.isEmpty() || list.isEmpty() || busy_)
		return;
	QList<int> wanted;
	for(const QVariant &t : list)
	{
		const int id = t.toMap().value(QStringLiteral("id")).toInt();
		if(!art_.contains(npId + QLatin1Char('/') + QString::number(id)))
			wanted << id;
	}
	if(wanted.isEmpty())
		return;
	const QString folder = QDir(cacheFor(address_.toStdString())).filePath(QStringLiteral("art/") + safeName(npId));
	run(tr("Getting the trophy pictures…"), [this, npId, wanted, folder](FtpClient &ftp, bool *) -> QString {
		QDir().mkpath(folder);
		QVariantMap found;
		for(int i = 0; i < wanted.size() && !stopping_.load(); ++i)
		{
			const QString file = QDir(folder).filePath(QStringLiteral("%1.png").arg(wanted[i]));
			const bool known = QFileInfo::exists(file);
			if(!known && !QFileInfo::exists(file + QStringLiteral(".bad")))
			{
				if(!ftp.download(artOf(npId, wanted[i]).toStdString(), file.toStdString()).ok)
				{
					QFile::remove(file);
					QFile bad(file + QStringLiteral(".bad"));
					bad.open(QIODevice::WriteOnly);
				}
			}
			found[npId + QLatin1Char('/') + QString::number(wanted[i])] =
				QFileInfo::exists(file) ? QUrl::fromLocalFile(file).toString() : QString();
		}
		lifeline_->post([this, found]() {
			for(auto it = found.begin(); it != found.end(); ++it)
				art_.insert(it.key(), it.value());
			emit artChanged();
		});
		return QString();
	}, false);
}

QVariantList TrophiesController::trophiesOf(int titleId) const
{
	const auto found = trophies_.find(titleId);
	return found != trophies_.end() ? found->second : QVariantList();
}

void TrophiesController::refresh()
{
	run(tr("Reading the console's trophies…"), nullptr, true);
}

void TrophiesController::setUser(const QString &folder)
{
	if(folder == user_)
		return;
	user_ = folder;
	run(tr("Reading the console's trophies…"), nullptr, true);
}

void TrophiesController::backup(const QList<int> &titleIds)
{
	QList<QVariantMap> chosen;
	for(const QVariant &entry : titles_)
	{
		const QVariantMap title = entry.toMap();
		if(titleIds.isEmpty() || titleIds.contains(title.value(QStringLiteral("id")).toInt()))
			chosen << title;
	}
	if(chosen.isEmpty() || user_.isEmpty())
		return;
	const QString user = user_;
	const QString into = userFolderOnPc(user);
	run(tr("Copying the trophies to this PC…"), [this, chosen, user, into](FtpClient &ftp, bool *error) -> QString {
		QDir().mkpath(into);
		// The user's whole list first: what each game shows comes from it.
		const QString list = QDir(into).filePath(QStringLiteral("trophy_local.db"));
		const FtpResult got = ftp.download(databaseOf(user).toStdString(), (list + QStringLiteral(".part")).toStdString());
		if(got.ok)
		{
			QFile::remove(list);
			QFile::rename(list + QStringLiteral(".part"), list);
		}
		int copied = 0;
		QStringList failed;
		for(int i = 0; i < chosen.size() && !stopping_.load(); ++i)
		{
			const QString npId = chosen[i].value(QStringLiteral("npId")).toString();
			const QString title = chosen[i].value(QStringLiteral("title")).toString();
			setProgress(tr("Copying %1 (%2 of %3)…").arg(title).arg(i + 1).arg(chosen.size()),
				static_cast<double>(i) / chosen.size());
			// Into a folder of its own first: an earlier copy stays until this one is whole.
			const QString target = QDir(into).filePath(safeName(npId));
			const QString partial = target + QStringLiteral(".part");
			QDir(partial).removeRecursively();
			QString why;
			if(downloadTree(ftp, dataOf(user, npId), partial, stopping_, &why))
			{
				QDir(target).removeRecursively();
				QDir().rename(partial, target);
				++copied;
			}
			else
			{
				QDir(partial).removeRecursively();
				if(!stopping_.load())
					failed << title + QStringLiteral(": ") + why;
			}
		}
		logInfo("Trophies: " + std::to_string(copied) + " game(s) copied to " + into.toStdString()
			+ (failed.isEmpty() ? std::string() : "; not done: " + failed.join(QStringLiteral("; ")).toStdString()));
		*error = !failed.isEmpty();
		if(!failed.isEmpty())
			return tr("%1 of %2 copied. Not done: %3").arg(copied).arg(chosen.size()).arg(failed.join(QStringLiteral("; ")));
		return tr("The trophies of %n game(s) copied to %1.", "", copied).arg(QDir::toNativeSeparators(into));
	}, true);
}

void TrophiesController::run(const QString &what, Job job, bool read)
{
	if(busy_)
		return;
	const std::string address = app_->settings().consoleAddress;
	if(app_->activeIsPs5())
	{
		emit finished(tr("A PS5 keeps its trophies another way, which this does not read yet."), true);
		return;
	}
	if(!app_->ftpAnswers(address))
	{
		emit finished(tr("The console's FTP is not answering: its trophies are read over it."), true);
		return;
	}
	busy_ = true;
	status_ = what;
	progress_ = 0.0;
	emit busyChanged();
	if(worker_.joinable())
		worker_.join();
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	const QString wanted = user_;
	worker_ = std::thread([this, life = lifeline_, job, read, config, address, wanted]() {
		FtpClient ftp(config);
		bool error = false;
		QString message = job ? job(ftp, &error) : QString();
		if(read && !stopping_.load())
		{
			const QString cache = cacheFor(address);
			QVariantList users;
			QString user;
			Read list;
			std::vector<FtpEntry> homes;
			const FtpResult listed = ftp.list("/user/home", &homes);
			if(!listed.ok)
				list.error = translateMessage(listed.message);
			QStringList withTrophies;
			for(const FtpEntry &home : homes)
			{
				if(!home.isDirectory || !isUserFolder(home.name))
					continue;
				const QString folder = QString::fromStdString(home.name);
				const QString local = QDir(cache).filePath(folder);
				QDir().mkpath(local);
				const QString nameFile = QDir(local).filePath(QStringLiteral("username.dat"));
				QString name;
				if(ftp.download(homeOf(folder).toStdString() + "/username.dat", nameFile.toStdString()).ok)
					name = readUserName(nameFile);
				const QString db = QDir(local).filePath(QStringLiteral("trophy_local.db"));
				QFile::remove(db);
				const bool has = ftp.download(databaseOf(folder).toStdString(), db.toStdString()).ok;
				if(has)
					withTrophies << folder;
				users << QVariantMap { { QStringLiteral("folder"), folder }, { QStringLiteral("name"), name },
					{ QStringLiteral("hasTrophies"), has } };
			}
			user = withTrophies.contains(wanted) ? wanted : withTrophies.value(0);
			// Every user's list too, for comparing the same game between them.
			std::map<QString, QVariantMap> everyone;
			for(const QString &other : withTrophies)
			{
				const Read theirs = readDatabase(QDir(QDir(cache).filePath(other)).filePath(QStringLiteral("trophy_local.db")));
				QVariantMap games;
				for(const QVariant &entry : theirs.titles)
				{
					QVariantMap game = entry.toMap();
					const auto found = theirs.trophies.find(game.value(QStringLiteral("id")).toInt());
					game[QStringLiteral("trophies")] = found != theirs.trophies.end() ? found->second : QVariantList();
					games[game.value(QStringLiteral("npId")).toString()] = game;
				}
				everyone[other] = games;
			}
			if(!user.isEmpty())
			{
				list = readDatabase(QDir(QDir(cache).filePath(user)).filePath(QStringLiteral("trophy_local.db")));
				// Each game's icon, fetched once and kept.
				const QString icons = QDir(cache).filePath(QStringLiteral("icons"));
				QDir().mkpath(icons);
				for(int i = 0; i < list.titles.size() && !stopping_.load(); ++i)
				{
					QVariantMap title = list.titles[i].toMap();
					const QString npId = title.value(QStringLiteral("npId")).toString();
					const QString icon = QDir(icons).filePath(safeName(npId) + QStringLiteral(".png"));
					if(!QFileInfo::exists(icon) && !QFileInfo::exists(icon + QStringLiteral(".bad")))
					{
						setProgress(tr("Getting the icons… %1").arg(title.value(QStringLiteral("title")).toString()),
							static_cast<double>(i) / list.titles.size());
						if(!ftp.download(iconOf(npId).toStdString(), icon.toStdString()).ok)
						{
							QFile::remove(icon);
							QFile bad(icon + QStringLiteral(".bad"));
							bad.open(QIODevice::WriteOnly);
						}
					}
					title[QStringLiteral("icon")] = QFileInfo::exists(icon) ? QUrl::fromLocalFile(icon).toString() : QString();
					list.titles[i] = title;
				}
			}
			else if(list.error.isEmpty())
				list.error = tr("No user of this console has a list of trophies.");
			if(!list.error.isEmpty())
				logWarning("Trophies: " + list.error.toStdString());
			else
				logInfo("Trophies: " + std::to_string(list.titles.size()) + " game(s) of user " + user.toStdString());
			life->post([this, users, user, list, address, everyone]() {
				// Another console in use since: what was read is not of it.
				if(app_->settings().consoleAddress != address)
					return;
				users_ = users;
				user_ = user;
				everyone_ = everyone;
				address_ = QString::fromStdString(address);
				trophies_ = list.trophies;
				// Whether this PC has a copy of each, and of when.
				const QString into = user.isEmpty() ? QString() : userFolderOnPc(user);
				titles_.clear();
				for(const QVariant &entry : list.titles)
				{
					QVariantMap title = entry.toMap();
					const QFileInfo copy(QDir(into).filePath(safeName(title.value(QStringLiteral("npId")).toString())));
					title[QStringLiteral("backedUp")] = !into.isEmpty() && copy.isDir();
					title[QStringLiteral("backupDate")] = copy.isDir()
						? QLocale().toString(copy.lastModified(), QLocale::ShortFormat) : QString();
					titles_ << title;
				}
				scanned_ = true;
				emit titlesChanged();
				if(!list.error.isEmpty())
					emit finished(list.error, true);
			});
		}
		life->post([this, message, error]() {
			busy_ = false;
			status_.clear();
			progress_ = 0.0;
			emit busyChanged();
			if(!message.isEmpty())
				emit finished(message, error);
		});
	});
}

} // namespace orbislink
