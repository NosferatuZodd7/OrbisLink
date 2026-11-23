// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/captures_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/library/shelf.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/translate_message.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <ctime>
#include <deque>
#include <set>
#include <vector>

namespace orbislink {

namespace {

constexpr const char *kRoot = "/user/av_contents";

struct Capture
{
	std::string path;
	std::string name;
	std::string kind; // "photo" or "video"
	std::string game; // its folder: the game's title ID (or name)
	int64_t size = 0;
	int64_t time = 0; // seconds since the epoch; 0 when not known
};

std::string kindOfCapture(const std::string &name)
{
	const std::string ext = fileExtensionLower(name);
	if(ext == ".jpg" || ext == ".jpeg" || ext == ".png")
		return "photo";
	if(ext == ".mp4" || ext == ".webm" || ext == ".mkv" || ext == ".m4v")
		return "video";
	return std::string();
}

// A game's title ID (CUSA12345, PPSA01234), not a system application's
// (NPXS40087, the PS5's capture service) nor a folder of the console's own.
bool looksLikeTitleId(const std::string &name)
{
	if(name.size() != 9 || name.compare(0, 4, "NPXS") == 0)
		return false;
	for(size_t i = 0; i < 9; ++i)
		if(i < 4 ? (name[i] < 'A' || name[i] > 'Z') : (name[i] < '0' || name[i] > '9'))
			return false;
	return true;
}

// A PS5 names a capture after when it was taken (YYYYMMDD_hhmmss_…), on the
// console's clock, read here as this PC's: the listing's own date may be
// to the minute or the day only. 0 when the name is not of that form.
int64_t timeFromName(const std::string &name)
{
	if(name.size() < 15 || name[8] != '_')
		return 0;
	const QDateTime taken = QDateTime::fromString(QString::fromStdString(name.substr(0, 15)),
		QStringLiteral("yyyyMMdd_HHmmss"));
	return taken.isValid() ? taken.toSecsSinceEpoch() : 0;
}

// The pictures and clips under `root`, down to `maxDepth` levels of folders,
// added to `found`. `error` (may be null) gets why `root` itself could not be
// read.
//
// A PS4 keeps them in photo/<game> and video/<game>; its "thumbnail" folders
// are the system's own small pictures, skipped. A PS5 (as read from a
// third-party tool's source, PS5-Upload-Suite, not yet seen here) keeps
// its photos encrypted, as .dat with a .meta beside, in
// photo/NPXS40087/NPXS40087/<bucket>; the only pictures that can be read
// are its previews, thumbnails/photo/NPXS40087/NPXS40087/<bucket>/<name>.jpg.jpeg.
// So on a PS5 the thumbnails folders are what is listed, the folders are
// not games (the game is left empty unless one is a title ID), and the date
// is the name's.
void walkCaptures(FtpClient &ftp, const std::string &root, int maxDepth, bool ps5, std::vector<Capture> *found,
	int *budget, std::string *error, const std::atomic<bool> &stop)
{
	std::deque<std::pair<std::string, int>> dirs { { root, 0 } };
	const int64_t now = static_cast<int64_t>(std::time(nullptr));
	bool first = true;
	while(!dirs.empty() && *budget > 0 && !stop.load())
	{
		const auto [dir, depth] = dirs.front();
		dirs.pop_front();
		std::vector<FtpEntry> entries;
		const FtpResult listed = ftp.list(dir, &entries);
		if(!listed.ok)
		{
			if(first && error)
				*error = listed.message;
			first = false;
			continue;
		}
		first = false;
		const std::string parent = dir.substr(dir.find_last_of('/') + 1);
		for(const FtpEntry &entry : entries)
		{
			if(--*budget <= 0)
				break;
			if(entry.name.empty() || entry.name[0] == '.')
				continue;
			const std::string path = dir + "/" + entry.name;
			if(entry.isDirectory)
			{
				if(depth < maxDepth && (ps5 || toLower(entry.name).find("thumbnail") == std::string::npos))
					dirs.emplace_back(path, depth + 1);
				continue;
			}
			// The PS5's encrypted originals (.dat, .meta) are not among these.
			const std::string kind = kindOfCapture(entry.name);
			if(kind.empty())
				continue;
			Capture capture;
			capture.path = path;
			capture.name = entry.name;
			capture.kind = kind;
			const std::string lowered = toLower(parent);
			if(ps5)
				capture.game = looksLikeTitleId(parent) ? parent : std::string();
			else
				capture.game = lowered == "photo" || lowered == "video" || lowered == "av_contents" ? std::string() : parent;
			capture.size = entry.size;
			capture.time = ps5 ? timeFromName(entry.name) : 0;
			if(capture.time == 0)
				capture.time = library::ftpListingTime(entry.modified, now);
			found->push_back(capture);
		}
	}
}

// Every picture and clip on the console, the newest first. On a PS5 with
// none under /user/av_contents, its users' screenshot folders too (where
// the same tool looks last).
std::vector<Capture> listCaptures(FtpClient &ftp, bool ps5, std::string *error, const std::atomic<bool> &stop)
{
	std::vector<Capture> found;
	int budget = 20000;
	// thumbnails/photo/NPXS40087/NPXS40087/<bucket> is five folders down.
	walkCaptures(ftp, kRoot, ps5 ? 6 : 4, ps5, &found, &budget, error, stop);
	if(ps5 && found.empty() && error->empty())
	{
		std::vector<FtpEntry> users;
		if(ftp.list("/user/home", &users).ok)
			for(const FtpEntry &user : users)
				if(user.isDirectory && !user.name.empty() && user.name[0] != '.' && !stop.load())
					walkCaptures(ftp, "/user/home/" + user.name + "/screenshot", 2, ps5, &found, &budget, nullptr,
						stop);
	}
	std::sort(found.begin(), found.end(), [](const Capture &a, const Capture &b) {
		return a.time != b.time ? a.time > b.time : a.path > b.path;
	});
	return found;
}

QString cacheRoot()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("captures"));
}

// The small pictures of one console's captures: a folder of its own, so
// those of another console in use are not pruned as no longer listed.
QString cacheFolder(const std::string &address)
{
	const QByteArray key = QCryptographicHash::hash(QByteArray::fromStdString(address), QCryptographicHash::Sha1);
	return QDir(cacheRoot()).filePath(QString::fromLatin1(key.toHex().left(12)));
}

// A name the PC's file system takes (Windows refuses : ? * " < > | \ /,
// trailing dots and spaces, and device names): the console's own may not be.
QString safeName(const std::string &name)
{
	QString out;
	for(const QChar c : QString::fromStdString(name))
		out += (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c)) ? QLatin1Char('_') : c;
	while(out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
		out.chop(1);
	if(out.isEmpty() || out == QLatin1String(".."))
		return QStringLiteral("_");
	static const QStringList reserved { QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
		QStringLiteral("NUL") };
	const QString stem = out.section(QLatin1Char('.'), 0, 0).toUpper();
	if(reserved.contains(stem) || ((stem.startsWith(QLatin1String("COM")) || stem.startsWith(QLatin1String("LPT")))
			&& stem.size() == 4 && stem.at(3).isDigit()))
		out.prepend(QLatin1Char('_'));
	return out;
}

// Its small picture on this PC, named after what it is (path and size, which
// do not move with the listing's way of writing dates): made once, kept.
QString thumbPath(const QString &cache, const Capture &capture)
{
	const QByteArray key = QByteArray::fromStdString(capture.path + "|" + std::to_string(capture.size));
	return QDir(cache).filePath(
		QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".jpg"));
}

QVariantList toList(const std::vector<Capture> &captures)
{
	QVariantList list;
	const QLocale locale;
	for(const Capture &capture : captures)
	{
		list << QVariantMap {
			{ QStringLiteral("path"), QString::fromStdString(capture.path) },
			{ QStringLiteral("name"), QString::fromStdString(capture.name) },
			{ QStringLiteral("kind"), QString::fromStdString(capture.kind) },
			{ QStringLiteral("game"), QString::fromStdString(capture.game) },
			{ QStringLiteral("size"), static_cast<double>(capture.size) },
			{ QStringLiteral("sizeText"), QString::fromStdString(humanBytes(capture.size)) },
			{ QStringLiteral("dateText"), capture.time > 0
				? locale.toString(QDateTime::fromSecsSinceEpoch(capture.time), QLocale::ShortFormat) : QString() },
		};
	}
	return list;
}

QVariantMap thumbsOf(const QString &cache, const std::vector<Capture> &captures)
{
	QVariantMap thumbs;
	for(const Capture &capture : captures)
	{
		const QString thumb = thumbPath(cache, capture);
		if(QFileInfo::exists(thumb))
			thumbs.insert(QString::fromStdString(capture.path), QUrl::fromLocalFile(thumb).toString());
	}
	return thumbs;
}

} // namespace

CapturesController::CapturesController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	// Another console in use, or the type of this one first known: the list
	// was of the one before, or read as of the other type. A listing still
	// running drops its result itself (run()); its small pictures stop.
	connect(app_, &AppController::settingsChanged, this, [this]() {
		emit consoleChanged();
		if(QString::fromStdString(app_->settings().consoleAddress) == listedAddress_ && app_->activeIsPs5() == listedPs5_)
			return;
		if(busy_)
			thumbStop_.store(true);
		if(!scanned_)
			return;
		captures_.clear();
		thumbs_.clear();
		scanned_ = false;
		emit capturesChanged();
		emit thumbsChanged();
	});
}

CapturesController::~CapturesController()
{
	lifeline_->cut();
	stopping_.store(true);
	if(worker_.joinable())
		worker_.join();
}

QString CapturesController::where() const { return QString::fromLatin1(kRoot); }

bool CapturesController::ps5() const { return app_->activeIsPs5(); }

QString CapturesController::folder() const
{
	QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	if(pictures.isEmpty())
		pictures = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	return QDir(pictures).filePath(QStringLiteral("OrbisLink captures"));
}

void CapturesController::openFolder() const
{
	QDir().mkpath(folder());
	QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
}

void CapturesController::setProgress(const QString &status, double fraction)
{
	lifeline_->post([this, status, fraction]() {
		status_ = status;
		progress_ = fraction;
		emit busyChanged();
	});
}

void CapturesController::refresh()
{
	run(tr("Reading the console's captures…"), nullptr, true);
}

void CapturesController::download(const QStringList &paths)
{
	// What to copy, from what was listed.
	std::vector<Capture> chosen;
	for(const QVariant &entry : captures_)
	{
		const QVariantMap m = entry.toMap();
		if(!paths.isEmpty() && !paths.contains(m.value(QStringLiteral("path")).toString()))
			continue;
		Capture capture;
		capture.path = m.value(QStringLiteral("path")).toString().toStdString();
		capture.name = m.value(QStringLiteral("name")).toString().toStdString();
		capture.game = m.value(QStringLiteral("game")).toString().toStdString();
		capture.size = static_cast<int64_t>(m.value(QStringLiteral("size")).toDouble());
		chosen.push_back(capture);
	}
	const QString into = folder();
	run(tr("Copying the captures to this PC…"), [this, chosen, into](FtpClient &ftp, bool *error) -> QString {
		int copied = 0, skipped = 0;
		QStringList failed;
		for(size_t i = 0; i < chosen.size() && !stopping_.load(); ++i)
		{
			const Capture &capture = chosen[i];
			setProgress(tr("Copying %1 (%2 of %3)…").arg(QString::fromStdString(capture.name)).arg(i + 1)
					.arg(chosen.size()),
				static_cast<double>(i) / chosen.size());
			const QString dir = capture.game.empty() ? into : QDir(into).filePath(safeName(capture.game));
			QDir().mkpath(dir);
			// A PS5 preview's "name.jpg.jpeg" is saved as "name.jpg".
			QString name = safeName(capture.name);
			if(name.endsWith(QLatin1String(".jpg.jpeg"), Qt::CaseInsensitive))
				name.chop(5);
			const QString target = QDir(dir).filePath(name);
			if(QFileInfo(target).size() == capture.size && capture.size > 0)
			{
				++skipped;
				continue;
			}
			const QString part = target + QStringLiteral(".part");
			// Stopped mid-file when the application closes.
			const FtpResult got = ftp.download(capture.path, part.toStdString(),
				[this](int64_t, int64_t) { return !stopping_.load(); });
			if(stopping_.load())
			{
				QFile::remove(part);
				break;
			}
			if(got.ok && (QFile::remove(target) || !QFileInfo::exists(target)) && QFile::rename(part, target))
				++copied;
			else
			{
				QFile::remove(part);
				failed << QString::fromStdString(capture.name) + QStringLiteral(": ")
						+ (got.ok ? tr("could not be written") : translateMessage(got.message));
			}
		}
		logInfo("Captures: " + std::to_string(copied) + " copied to " + into.toStdString() + ", "
			+ std::to_string(skipped) + " already there" + (failed.isEmpty() ? std::string()
				: "; not done: " + failed.join(QStringLiteral("; ")).toStdString()));
		*error = !failed.isEmpty();
		if(!failed.isEmpty())
			return tr("%1 of %2 captures copied. Not done: %3").arg(copied).arg(chosen.size())
				.arg(failed.join(QStringLiteral("; ")));
		if(copied == 0 && skipped > 0)
			return tr("They were already on this PC, in %1.").arg(QDir::toNativeSeparators(into));
		return tr("%n capture(s) copied to %1.", "", copied).arg(QDir::toNativeSeparators(into));
	}, false);
}

void CapturesController::remove(const QStringList &paths)
{
	// A PS5's preview deleted would leave its original (see walkCaptures):
	// the page offers no delete there.
	if(paths.isEmpty() || app_->activeIsPs5())
		return;
	run(tr("Deleting from the console…"), [this, paths](FtpClient &ftp, bool *error) -> QString {
		int deleted = 0;
		QStringList failed;
		for(int i = 0; i < paths.size() && !stopping_.load(); ++i)
		{
			setProgress(tr("Deleting %1 of %2…").arg(i + 1).arg(paths.size()), static_cast<double>(i) / paths.size());
			const FtpResult removed = ftp.removeFile(paths[i].toStdString());
			if(removed.ok)
				++deleted;
			else
				failed << QFileInfo(paths[i]).fileName() + QStringLiteral(": ") + translateMessage(removed.message);
		}
		logInfo("Captures: " + std::to_string(deleted) + " of " + std::to_string(paths.size())
			+ " deleted from the console.");
		*error = !failed.isEmpty();
		if(!failed.isEmpty())
			return tr("%1 of %2 deleted. Not done: %3").arg(deleted).arg(paths.size())
				.arg(failed.join(QStringLiteral("; ")));
		return tr("%n capture(s) deleted from the console.", "", deleted);
	}, true);
}

void CapturesController::run(const QString &what, Job job, bool list)
{
	if(busy_)
	{
		// Only the small pictures being made: they stop, and this goes on.
		if(thumbnailing_ && (job || list))
		{
			thumbStop_.store(true);
			QTimer::singleShot(300, this, [this, what, job, list]() { run(what, job, list); });
		}
		return;
	}
	const std::string address = app_->settings().consoleAddress;
	const bool ps5 = app_->activeIsPs5();
	thumbStop_.store(false);
	if(list)
	{
		listedAddress_ = QString::fromStdString(address);
		listedPs5_ = ps5;
	}
	if(!app_->ftpAnswers(address))
	{
		emit finished(tr("The console's FTP is not answering: its captures are read over it."), true);
		return;
	}
	busy_ = true;
	status_ = what;
	progress_ = 0.0;
	emit busyChanged();
	if(worker_.joinable())
		worker_.join();
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	worker_ = std::thread([this, life = lifeline_, job, list, config, address, ps5]() {
		FtpClient ftp(config);
		bool error = false;
		QString message = job ? job(ftp, &error) : QString();
		if(list && !stopping_.load())
		{
			std::string why;
			const std::vector<Capture> captures = listCaptures(ftp, ps5, &why, stopping_);
			if(!why.empty() && captures.empty())
			{
				logInfo("Captures: " + std::string(kRoot) + " could not be read: " + why);
				if(message.isEmpty())
				{
					message = tr("%1 could not be read on this console: %2").arg(QString::fromLatin1(kRoot),
						translateMessage(why));
					error = true;
				}
			}
			else
				logInfo("Captures: " + std::to_string(captures.size()) + " found on the console ("
					+ (ps5 ? "PS5" : "PS4") + " layout).");
			// What was read is of the console in use then: if another one (or
			// its type) is in use now, it is dropped, so that Copy and Delete
			// do not go to the new one with the old one's paths.
			const auto stillListed = [this, address, ps5]() {
				return app_->settings().consoleAddress == address && app_->activeIsPs5() == ps5;
			};
			const QString cache = cacheFolder(address);
			life->post([this, stillListed, list = toList(captures), thumbs = thumbsOf(cache, captures)]() {
				if(!stillListed())
					return;
				captures_ = list;
				thumbs_ = thumbs;
				scanned_ = true;
				thumbnailing_ = true;
				emit capturesChanged();
				emit thumbsChanged();
			});
			QDir().mkpath(cache);
			// The small pictures of captures no longer listed go (only after a
			// whole listing: an empty one may just be an unreadable console),
			// with those kept loose in the folder before there was one per
			// console.
			if(why.empty() && !captures.empty())
			{
				std::set<QString> wanted;
				for(const Capture &capture : captures)
				{
					wanted.insert(QFileInfo(thumbPath(cache, capture)).fileName());
					wanted.insert(QFileInfo(thumbPath(cache, capture)).fileName() + QStringLiteral(".bad"));
				}
				for(const QFileInfo &file : QDir(cache).entryInfoList(QDir::Files))
					if(!wanted.count(file.fileName()))
						QFile::remove(file.absoluteFilePath());
				for(const QFileInfo &file : QDir(cacheRoot()).entryInfoList(QDir::Files))
					QFile::remove(file.absoluteFilePath());
			}
			// Their small pictures, the newest first: a photo is fetched once,
			// made small and kept; one that does not decode is not fetched again.
			int made = 0, looked = 0;
			for(const Capture &capture : captures)
			{
				if(stopping_.load() || thumbStop_.load() || looked >= 300)
					break;
				if(capture.kind != "photo" || capture.size <= 0 || capture.size > 16 * 1024 * 1024)
					continue;
				++looked;
				const QString thumb = thumbPath(cache, capture);
				if(QFileInfo::exists(thumb) || QFileInfo::exists(thumb + QStringLiteral(".bad")))
					continue;
				setProgress(tr("Making the small pictures… %1").arg(QString::fromStdString(capture.name)),
					static_cast<double>(looked) / std::min<size_t>(captures.size(), 300));
				const QString part = thumb + QStringLiteral(".part");
				const bool got = ftp.download(capture.path, part.toStdString(),
					[this](int64_t, int64_t) { return !stopping_.load() && !thumbStop_.load(); }).ok;
				if(got)
				{
					QImage image(part);
					const QString partial = thumb + QStringLiteral(".tmp");
					if(!image.isNull()
						&& image.scaled(480, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(partial, "JPG", 82))
						QFile::rename(partial, thumb);
					else
					{
						QFile::remove(partial);
						QFile bad(thumb + QStringLiteral(".bad"));
						bad.open(QIODevice::WriteOnly);
					}
				}
				QFile::remove(part);
				if(++made % 6 == 0)
					life->post([this, stillListed, thumbs = thumbsOf(cache, captures)]() {
						if(!stillListed())
							return;
						thumbs_ = thumbs;
						emit thumbsChanged();
					});
			}
			life->post([this, stillListed, thumbs = thumbsOf(cache, captures)]() {
				thumbnailing_ = false;
				if(stillListed())
					thumbs_ = thumbs;
				emit thumbsChanged();
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
