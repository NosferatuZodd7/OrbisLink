// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/games_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/translate_message.h"

#ifdef ORBISLINK_HAS_FPKG
#include "orbislink/fpkg/classic_converter.h"
#include "orbislink/fpkg/classics_assets.h"
#include "orbislink/fpkg/disc_scanner.h"
#endif

#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <vector>

namespace orbislink {

namespace {

// Where converted games go on the console before they are installed.
constexpr char kConvertedGamesFolder[] = "/data/OrbisLinkFPKG/";

QString sizeText(quint64 bytes)
{
	if(bytes >= 1024ull * 1024 * 1024)
		return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 2);
	return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024), 0, 'f', 0);
}

#ifdef ORBISLINK_HAS_FPKG
// easy-ps2-fpkg's "official cover": the front cover by serial, from the
// xlenore/ps2-covers collection.
const char *const kCoverUrl = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/%1.jpg";

fpkg::Bytes png(const QImage &image)
{
	QByteArray data;
	QBuffer buffer(&data);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return fpkg::Bytes(data.begin(), data.end());
}

// `passes` box blurs of the given radius, across and down: close to a
// Gaussian blur.
QImage boxBlur(QImage image, int radius, int passes)
{
	image = image.convertToFormat(QImage::Format_RGB32);
	const int w = image.width(), h = image.height();
	std::vector<QRgb> line(static_cast<size_t>(std::max(w, h)));
	auto blurLine = [&](auto get, auto set, int n) {
		for(int i = 0; i < n; ++i)
			line[static_cast<size_t>(i)] = get(i);
		int r = 0, g = 0, b = 0;
		const int span = 2 * radius + 1;
		auto at = [&](int i) { return line[static_cast<size_t>(std::clamp(i, 0, n - 1))]; };
		for(int i = -radius; i <= radius; ++i)
		{
			r += qRed(at(i));
			g += qGreen(at(i));
			b += qBlue(at(i));
		}
		for(int i = 0; i < n; ++i)
		{
			set(i, qRgb(r / span, g / span, b / span));
			const QRgb out = at(i - radius), in = at(i + radius + 1);
			r += qRed(in) - qRed(out);
			g += qGreen(in) - qGreen(out);
			b += qBlue(in) - qBlue(out);
		}
	};
	for(int pass = 0; pass < passes; ++pass)
	{
		for(int y = 0; y < h; ++y)
		{
			auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
			blurLine([&](int x) { return row[x]; }, [&](int x, QRgb c) { row[x] = c; }, w);
		}
		for(int x = 0; x < w; ++x)
			blurLine([&](int y) { return reinterpret_cast<const QRgb *>(image.constScanLine(y))[x]; },
				[&](int y, QRgb c) { reinterpret_cast<QRgb *>(image.scanLine(y))[x] = c; }, h);
	}
	return image;
}

// The whole cover in the middle, over a blurred and darkened copy of itself
// that fills the rest — as easy-ps2-fpkg composes it.
QImage composeCover(const QImage &cover, int width, int height)
{
	QImage canvas(width, height, QImage::Format_RGB32);
	QImage fill = cover.scaled(width, height, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	fill = fill.copy((fill.width() - width) / 2, (fill.height() - height) / 2, width, height);
	// A strong, smooth blur (easy-ps2-fpkg's is Gaussian, radius 14): three
	// box blurs on a quarter-size copy, then back to full size.
	fill = boxBlur(fill.scaled(std::max(1, width / 4), std::max(1, height / 4), Qt::IgnoreAspectRatio,
					   Qt::SmoothTransformation),
		4, 3)
			   .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QPainter p(&canvas);
	p.drawImage(0, 0, fill);
	p.fillRect(canvas.rect(), QColor(0, 0, 0, 115)); // brightness 0.55
	const QImage front = cover.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	p.drawImage((width - front.width()) / 2, (height - front.height()) / 2, front);
	p.end();
	return canvas;
}

// The icon (512×512) and background (1920×1080) from the game's cover, or
// nothing when it has none: the emulator's own art stays then.
bool fetchCover(const std::string &serial, fpkg::Bytes *icon, fpkg::Bytes *background)
{
	HttpClient http(20000);
	HttpClient::FetchOptions options;
	options.headers = {"User-Agent: OrbisLink"};
	const HttpResponse r = http.fetch(QString::fromLatin1(kCoverUrl).arg(QString::fromStdString(serial)).toStdString(), options);
	if(!r.transportOk || r.status != 200 || r.body.empty())
		return false;
	const QImage cover = QImage::fromData(reinterpret_cast<const uchar *>(r.body.data()), static_cast<int>(r.body.size()));
	if(cover.isNull())
		return false;
	*icon = png(composeCover(cover, 512, 512));
	*background = png(composeCover(cover, 1920, 1080));
	return true;
}
#endif

} // namespace

struct GamesController::Job
{
	QString id;
	QString title;
	QString platform;
	QString path;
	QString listedPath;
	bool install = false;
	// The package already in the output folder, sent instead of making it again.
	QString reusePath;
	QString state = QStringLiteral("waiting"); // waiting, converting, done, error, cancelled
	QString stage;
	double percent = 0;
	QString message;
	QString pkgPath;
	// The queue task the package had before this job (an earlier upload or
	// install of the same file): its outcome is not this job's.
	QString staleTaskId;
	// A disc on the console: its path there, the folder its package goes
	// back to, and where it was fetched to on this PC (deleted after).
	QString remotePath;
	QString storageFolder;
	QString fetchedFolder;
	std::atomic<bool> cancel { false };
#ifdef ORBISLINK_HAS_FPKG
	fpkg::DiscInfo disc;
#endif
};

GamesController::GamesController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	connect(app_, &AppController::settingsChanged, this, &GamesController::foldersChanged);
	connect(app_, &AppController::transfersChanged, this, &GamesController::updateProgress);
#ifdef ORBISLINK_HAS_FPKG
	assetsState_ = fpkg::hasClassicsAssets(assetsFolder().toStdString()) ? QStringLiteral("ready")
																		  : QStringLiteral("missing");
#endif
	worker_ = std::thread([this]() { workerLoop(); });
	if(!gamesFolder().isEmpty())
		rescan();
}

GamesController::~GamesController()
{
	stopping_ = true;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(auto &job : jobs_)
			job->cancel = true;
	}
	wakeup_.notify_all();
	if(worker_.joinable())
		worker_.join();
	if(drivesThread_.joinable())
		drivesThread_.join();
}

bool GamesController::available() const
{
#ifdef ORBISLINK_HAS_FPKG
	return true;
#else
	return false;
#endif
}

QString GamesController::gamesFolder() const
{
	return QString::fromStdString(app_->settings().gamesFolder);
}

QString GamesController::assetsFolder() const
{
	const QByteArray given = qgetenv("ORBISLINK_CLASSICS_ASSETS");
	if(!given.isEmpty())
		return QString::fromLocal8Bit(given);
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
		.filePath(QStringLiteral("classics"));
}

QString GamesController::outputFolder() const
{
	return QString::fromStdString(app_->settings().convertOutputFolder);
}

bool GamesController::converting() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for(const auto &job : jobs_)
		if(job->state == QLatin1String("waiting") || job->state == QLatin1String("converting"))
			return true;
	return false;
}

QString GamesController::toLocalPath(const QString &folder) const
{
	const QUrl url(folder);
	if(url.isLocalFile())
		return QDir::cleanPath(url.toLocalFile());
	return folder.isEmpty() ? QString() : QDir::cleanPath(folder);
}

void GamesController::setGamesFolder(const QString &folder)
{
	const std::string path = toLocalPath(folder).toStdString();
	app_->updateSettings([&](Settings &s) { s.gamesFolder = path; });
	rescan();
}

void GamesController::setOutputFolder(const QString &folder)
{
	const std::string path = toLocalPath(folder).toStdString();
	app_->updateSettings([&](Settings &s) { s.convertOutputFolder = path; });
}

QString GamesController::folderOn(const QString &storage)
{
	if(storage == QLatin1String("usb"))
		return QStringLiteral("/mnt/usb0/OrbisLinkFPKG/");
	if(storage == QLatin1String("ext"))
		return QStringLiteral("/mnt/ext0/OrbisLinkFPKG/");
	return QString::fromLatin1(kConvertedGamesFolder);
}

QString GamesController::storage() const { return QString::fromStdString(app_->settings().installStorage); }

QString GamesController::storageFolder() const { return folderOn(storage()); }

void GamesController::setStorage(const QString &storage)
{
	const std::string chosen = storage == QLatin1String("usb") || storage == QLatin1String("ext")
		? storage.toStdString() : std::string("internal");
	app_->updateSettings([&](Settings &s) { s.installStorage = chosen; });
}

void GamesController::checkDrives()
{
	if(drivesThread_.joinable())
		drivesThread_.join();
	const FtpClient::Config config = app_->ftpClientConfigFor(app_->settings().consoleAddress);
	drivesThread_ = std::thread([this, config]() {
		// A drive is there when its mount point lists something; the
		// folder exists, empty, when nothing is plugged in.
		FtpClient ftp(config);
		const auto has = [&ftp](const std::string &path) {
			std::vector<FtpEntry> entries;
			if(!ftp.list(path, &entries).ok)
				return false;
			for(const FtpEntry &entry : entries)
				if(entry.name != "." && entry.name != "..")
					return true;
			return false;
		};
		const bool usb = has("/mnt/usb0");
		const bool ext = !stopping_ && has("/mnt/ext0");
		if(stopping_)
			return;
		QMetaObject::invokeMethod(this, [this, usb, ext]() {
			drives_ = { { QStringLiteral("checked"), true }, { QStringLiteral("usb"), usb },
				{ QStringLiteral("ext"), ext } };
			emit drivesChanged();
		}, Qt::QueuedConnection);
	});
}

void GamesController::setAssets(const QString &state, double percent, const QString &message)
{
	QMetaObject::invokeMethod(this, [this, state, percent, message]() {
		const bool becameReady = state == QLatin1String("ready") && assetsState_ != state;
		assetsState_ = state;
		assetsPercent_ = percent;
		assetsMessage_ = message;
		emit assetsChanged();
		// The emulator's title lists now give the games their names.
		if(becameReady)
			rescan();
	}, Qt::QueuedConnection);
}

bool GamesController::ensureAssets(const std::function<bool(const QString &, double)> &progress, QString *error)
{
#ifdef ORBISLINK_HAS_FPKG
	std::lock_guard<std::mutex> lock(assetsMutex_);
	const QString dir = assetsFolder();
	if(fpkg::hasClassicsAssets(dir.toStdString()))
	{
		setAssets(QStringLiteral("ready"), 100);
		return true;
	}
	QDir().mkpath(QFileInfo(dir).absolutePath());
	const QString archive = dir + QStringLiteral(".download");
	const QByteArray url = qEnvironmentVariableIsSet("ORBISLINK_CLASSICS_URL")
		? qgetenv("ORBISLINK_CLASSICS_URL") : QByteArray(fpkg::kClassicsAssetsUrl);
	logInfo("Games: downloading the emulator files from " + url.toStdString());
	setAssets(QStringLiteral("downloading"), 0);
	HttpClient http(30000);
	int lastPercent = -1;
	const HttpClient::DownloadResult got = http.download(url.toStdString(), archive.toStdString(),
		[&](int64_t done, int64_t total) {
			const double percent = total > 0 ? 100.0 * double(done) / double(total) : 0.0;
			if(int(percent) != lastPercent)
			{
				lastPercent = int(percent);
				setAssets(QStringLiteral("downloading"), percent);
			}
			return !progress || progress(QStringLiteral("download"), percent);
		});
	QString why;
	if(!got.ok)
		why = got.status >= 400 ? tr("the server answered %1").arg(got.status) : QString::fromStdString(got.error);
	else
	{
		setAssets(QStringLiteral("unpacking"), 0);
		std::string unpackError;
		lastPercent = -1;
		const bool unpacked = fpkg::installClassicsAssets(archive.toStdString(), dir.toStdString(),
			[&](uint64_t read, uint64_t total) {
				const double percent = total > 0 ? 100.0 * double(read) / double(total) : 0.0;
				if(int(percent) != lastPercent)
				{
					lastPercent = int(percent);
					setAssets(QStringLiteral("unpacking"), percent);
				}
				return !progress || progress(QStringLiteral("unpack"), percent);
			},
			&unpackError);
		if(!unpacked)
			why = QString::fromStdString(unpackError);
	}
	QFile::remove(archive);
	if(!why.isEmpty())
	{
		logWarning("Games: the emulator files could not be fetched — " + why.toStdString());
		setAssets(QStringLiteral("error"), 0, why);
		if(error)
			*error = tr("Could not get the emulator files: %1").arg(why);
		return false;
	}
	logInfo("Games: emulator files ready in " + dir.toStdString());
	setAssets(QStringLiteral("ready"), 100);
	return true;
#else
	Q_UNUSED(progress);
	Q_UNUSED(error);
	return false;
#endif
}

void GamesController::downloadAssets()
{
	if(assetsState_ == QLatin1String("downloading") || assetsState_ == QLatin1String("unpacking"))
		return;
	QPointer<GamesController> self(this);
	std::thread([self]() {
		if(self)
			self->ensureAssets({}, nullptr);
	}).detach();
}

void GamesController::rescan()
{
#ifdef ORBISLINK_HAS_FPKG
	const std::string folder = app_->settings().gamesFolder;
	const std::string emulatorFolder = assetsFolder().toStdString();
	const quint64 run = ++scanRun_;
	if(folder.empty())
	{
		games_.clear();
		scanning_ = false;
		scanStatus_.clear();
		emit scanChanged();
		return;
	}
	scanning_ = true;
	scanStatus_ = tr("Looking for games…");
	emit scanChanged();
	QPointer<GamesController> self(this);
	std::thread([self, run, folder, emulatorFolder]() {
		const fpkg::EmulatorInfo emus = fpkg::findEmulators(emulatorFolder);
		const std::vector<fpkg::DiscInfo> discs = fpkg::scanFolder(folder);
		QVariantList list;
		for(const fpkg::DiscInfo &d : discs)
		{
			QVariantMap m;
			std::string title = fpkg::lookupTitle(d.platform == "ps2" ? emus.titleDatabase : emus.ps1TitleDatabase,
				d.titleId);
			if(title.empty())
				title = d.title;
			m[QStringLiteral("path")] = QString::fromStdString(d.path);
			m[QStringLiteral("listedPath")] = QString::fromStdString(d.listedPath);
			m[QStringLiteral("fileName")] = QString::fromStdString(d.fileName);
			m[QStringLiteral("folder")] = QFileInfo(QString::fromStdString(d.listedPath)).absolutePath();
			m[QStringLiteral("platform")] = QString::fromStdString(d.platform);
			m[QStringLiteral("serial")] = QString::fromStdString(d.serial);
			m[QStringLiteral("titleId")] = QString::fromStdString(d.titleId);
			m[QStringLiteral("region")] = QString::fromStdString(d.region);
			m[QStringLiteral("title")] = QString::fromStdString(title);
			m[QStringLiteral("format")] = QString::fromStdString(d.format);
			m[QStringLiteral("size")] = static_cast<double>(d.size);
			m[QStringLiteral("sizeText")] = sizeText(d.size);
			m[QStringLiteral("disc")] = d.discNumber;
			m[QStringLiteral("convertible")] = d.titleId.size() == 9;
			list << m;
		}
		if(!self)
			return;
		QMetaObject::invokeMethod(self, [self, run, list]() {
			if(!self || run != self->scanRun_)
				return;
			self->games_ = list;
			self->scanning_ = false;
			self->scanStatus_ = list.isEmpty() ? tr("No PS1 or PS2 discs in this folder.")
				: tr("%n game(s)", "", list.size());
			logInfo("Games: " + std::to_string(list.size()) + " disc(s) found");
			emit self->scanChanged();
			self->updateProgress();
		}, Qt::QueuedConnection);
	}).detach();
#endif
}

QVariantMap GamesController::gameByPath(const QString &path) const
{
	for(const QVariant &v : games_)
	{
		const QVariantMap m = v.toMap();
		if(m.value(QStringLiteral("path")).toString() == path)
			return m;
	}
	return {};
}

QString GamesController::classicTitle(const QString &platform, const QString &titleId) const
{
#ifdef ORBISLINK_HAS_FPKG
	if(titleId.isEmpty())
		return {};
	const fpkg::EmulatorInfo emus = fpkg::findEmulators(assetsFolder().toStdString());
	return QString::fromStdString(fpkg::lookupTitle(
		platform == QLatin1String("ps2") ? emus.titleDatabase : emus.ps1TitleDatabase, titleId.toStdString()));
#else
	Q_UNUSED(platform);
	Q_UNUSED(titleId);
	return {};
#endif
}

QString GamesController::packageNameFor(const QString &path, const QString &title) const
{
#ifdef ORBISLINK_HAS_FPKG
	const QVariantMap game = gameByPath(path);
	return QString::fromStdString(fpkg::packageFileName(title.toStdString(),
		game.value(QStringLiteral("titleId")).toString().toStdString()));
#else
	Q_UNUSED(path);
	Q_UNUSED(title);
	return {};
#endif
}

QString GamesController::existingPackage(const QString &path, const QString &title) const
{
	const QString folder = outputFolder();
	if(folder.isEmpty() || title.trimmed().isEmpty())
		return {};
	const QString name = packageNameFor(path, title.trimmed());
	if(name.isEmpty())
		return {};
	const QString file = QDir(folder).filePath(name);
	const QFileInfo info(file);
	return info.isFile() && info.size() > 0 ? QDir::toNativeSeparators(file) : QString();
}

void GamesController::convert(const QStringList &paths, bool install, const QStringList &titles, bool reuse)
{
#ifdef ORBISLINK_HAS_FPKG
	if(outputFolder().isEmpty())
	{
		emit app_->notify(tr("Convert"), tr("Choose where the packages go first."), true);
		return;
	}
	int added = 0;
	for(int i = 0; i < paths.size(); ++i)
	{
		const QVariantMap game = gameByPath(paths[i]);
		if(game.isEmpty())
			continue;
		auto job = std::make_shared<Job>();
		job->path = paths[i];
		job->listedPath = game.value(QStringLiteral("listedPath")).toString();
		job->platform = game.value(QStringLiteral("platform")).toString();
		job->title = i < titles.size() && !titles[i].trimmed().isEmpty() ? titles[i].trimmed()
			: game.value(QStringLiteral("title")).toString();
		job->install = install;
		if(reuse)
			job->reusePath = existingPackage(job->path, job->title);
		job->disc = fpkg::inspectDisc(job->listedPath.toStdString());
		{
			std::lock_guard<std::mutex> lock(mutex_);
			job->id = QString::number(nextId_++);
			jobs_.push_back(job);
		}
		++added;
	}
	if(added == 0)
		return;
	wakeup_.notify_all();
	publish();
	emit app_->showPanel(QStringLiteral("queue"));
#else
	Q_UNUSED(paths);
	Q_UNUSED(install);
	Q_UNUSED(titles);
#endif
}

void GamesController::sendToConsole(const QStringList &paths)
{
	QStringList files;
	for(const QString &path : paths)
	{
		const QVariantMap game = gameByPath(path);
		if(game.isEmpty())
			continue;
		const QString listed = game.value(QStringLiteral("listedPath")).toString();
		// A .cue goes with its .bin: one is useless without the other.
		if(listed != path)
			files << listed;
		files << path;
	}
	if(!files.isEmpty())
		app_->addPaths(files, 1);
}

void GamesController::convertFromConsole(const QString &remotePath, const QString &title, const QString &platform)
{
#ifdef ORBISLINK_HAS_FPKG
	// Somewhere for the package: the user's documents unless chosen.
	if(outputFolder().isEmpty())
	{
		const QString fallback = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
			.filePath(QStringLiteral("OrbisLink/Packages"));
		QDir().mkpath(fallback);
		app_->updateSettings([&](Settings &s) { s.convertOutputFolder = QDir::toNativeSeparators(fallback).toStdString(); });
	}
	auto job = std::make_shared<Job>();
	job->remotePath = remotePath;
	job->path = remotePath;
	job->listedPath = remotePath;
	job->title = title;
	job->platform = platform;
	job->install = true;
	// The package goes back where the disc was.
	job->storageFolder = remotePath.left(remotePath.lastIndexOf(QLatin1Char('/')) + 1);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		job->id = QString::number(nextId_++);
		jobs_.push_back(job);
	}
	wakeup_.notify_all();
	publish();
	emit app_->showPanel(QStringLiteral("queue"));
#else
	Q_UNUSED(remotePath);
	Q_UNUSED(title);
	Q_UNUSED(platform);
#endif
}

bool GamesController::fetchFromConsole(const std::shared_ptr<Job> &job, const std::function<void()> &tick)
{
#ifdef ORBISLINK_HAS_FPKG
	const QString folder = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
		.filePath(QStringLiteral("from-console/%1").arg(job->id));
	QDir(folder).removeRecursively();
	QDir().mkpath(folder);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		job->fetchedFolder = folder;
		job->stage = QStringLiteral("fetch");
		job->percent = 0;
	}
	tick();
	const auto fail = [&](const QString &why) {
		std::lock_guard<std::mutex> lock(mutex_);
		job->state = job->cancel ? QStringLiteral("cancelled") : QStringLiteral("error");
		job->message = job->cancel ? QString() : why;
		return false;
	};

	FtpClient ftp(app_->ftpClientConfigFor(app_->settings().consoleAddress));
	const std::string remote = job->remotePath.toStdString();
	const std::string remoteFolder = remote.substr(0, remote.find_last_of('/') + 1);
	const auto localFor = [&folder](const std::string &path) {
		return QDir(folder).filePath(QString::fromStdString(path.substr(path.find_last_of('/') + 1)));
	};
	// A cue sheet brings the track it names.
	std::vector<std::string> files = { remote };
	if(endsWith(toLower(remote), ".cue"))
	{
		const QString cue = localFor(remote);
		if(!ftp.download(remote, cue.toStdString()).ok)
			return fail(tr("Could not read %1 on the console.").arg(QString::fromStdString(remote)));
		QFile file(cue);
		const std::string text = file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string();
		const std::string track = fpkg::cueImageName(text);
		if(track.empty())
			return fail(tr("The cue sheet names no file."));
		files = { remoteFolder + track };
	}
	int64_t total = 0;
	for(const std::string &file : files)
	{
		int64_t size = 0;
		if(ftp.remoteSize(file, &size).ok)
			total += size;
	}
	int64_t before = 0;
	for(const std::string &file : files)
	{
		const FtpResult got = ftp.download(file, localFor(file).toStdString(), [&](int64_t done, int64_t) {
			{
				std::lock_guard<std::mutex> lock(mutex_);
				job->percent = total > 0 ? 100.0 * static_cast<double>(before + done) / static_cast<double>(total) : 0;
			}
			tick();
			return !job->cancel.load() && !stopping_.load();
		});
		if(!got.ok)
			return fail(tr("Could not bring %1 from the console: %2")
					.arg(QString::fromStdString(file.substr(file.find_last_of('/') + 1)), translateMessage(got.message)));
		int64_t size = 0;
		if(ftp.remoteSize(file, &size).ok)
			before += size;
	}

	// Now it is read here, as a disc on this PC is.
	const QString listed = localFor(remote);
	const fpkg::DiscInfo disc = fpkg::inspectDisc(listed.toStdString());
	if(!disc.problem.empty() || disc.platform.empty())
		return fail(tr("%1 is not a PS1/PS2 disc this can convert (%2).")
				.arg(QString::fromStdString(disc.fileName), QString::fromStdString(disc.problem)));
	std::lock_guard<std::mutex> lock(mutex_);
	job->disc = disc;
	job->path = QString::fromStdString(disc.path);
	job->listedPath = listed;
	job->platform = QString::fromStdString(disc.platform);
	if(job->title.trimmed().isEmpty())
		job->title = QString::fromStdString(disc.title);
	return true;
#else
	Q_UNUSED(job);
	Q_UNUSED(tick);
	return false;
#endif
}

void GamesController::cancelConversion(const QString &id)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(auto &job : jobs_)
		{
			if(job->id != id)
				continue;
			job->cancel = true;
			if(job->state == QLatin1String("waiting"))
				job->state = QStringLiteral("cancelled");
		}
	}
	publish();
}

void GamesController::clearFinishedConversions()
{
	// A package still on its way to the console keeps its card.
	QSet<QString> travelling;
	for(const QVariant &v : conversions_)
	{
		const QVariantMap m = v.toMap();
		const QVariantMap t = m.value(QStringLiteral("transfer")).toMap();
		const QString stage = t.value(QStringLiteral("stage")).toString();
		if(stage == QLatin1String("sending") || stage == QLatin1String("installing"))
			travelling.insert(m.value(QStringLiteral("id")).toString());
	}
	QStringList packages;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
						[&](const std::shared_ptr<Job> &j) {
							const bool gone = j->state != QLatin1String("waiting")
								&& j->state != QLatin1String("converting") && !travelling.contains(j->id);
							if(gone)
								packages << j->pkgPath;
							return gone;
						}),
			jobs_.end());
	}
	for(const QString &pkg : packages)
		app_->removeFinishedTasksOf(pkg);
	publish();
}

QUrl GamesController::folderUrl(const QString &path) const
{
	if(!path.isEmpty() && QFileInfo(path).isDir())
		return QUrl::fromLocalFile(path);
	return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

void GamesController::openOutputFolder() const
{
	if(!outputFolder().isEmpty())
		QDesktopServices::openUrl(QUrl::fromLocalFile(outputFolder()));
}

void GamesController::publish()
{
	publishPending_ = false;
	QVariantList list;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(const auto &job : jobs_)
		{
			QVariantMap m;
			m[QStringLiteral("id")] = job->id;
			m[QStringLiteral("title")] = job->title;
			m[QStringLiteral("platform")] = job->platform;
			m[QStringLiteral("state")] = job->state;
			m[QStringLiteral("stage")] = job->stage;
			m[QStringLiteral("percent")] = job->percent;
			m[QStringLiteral("message")] = job->message;
			m[QStringLiteral("install")] = job->install;
			m[QStringLiteral("pkgPath")] = job->pkgPath;
			m[QStringLiteral("staleTaskId")] = job->staleTaskId;
			list << m;
		}
	}
	conversions_ = list;
	emit conversionsChanged();
	updateProgress();
}

void GamesController::updateProgress()
{
	QVariantMap progress;
	const QVariantMap transfers = app_->transfers();
	auto fromQueue = [&](const QString &file) -> QVariantMap {
		return transfers.value(AppController::transferKey(file)).toMap();
	};
	QMap<QString, QVariantMap> fromJobs;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(const auto &job : jobs_)
		{
			if(job->state == QLatin1String("done") && !job->pkgPath.isEmpty())
				packageOf_[job->path] = job->pkgPath;
			QVariantMap item;
			if(job->state == QLatin1String("waiting") || job->state == QLatin1String("converting"))
			{
				item[QStringLiteral("stage")] = job->state;
				item[QStringLiteral("step")] = job->stage;
				item[QStringLiteral("percent")] = job->percent;
			}
			else if(job->state == QLatin1String("error"))
			{
				item[QStringLiteral("stage")] = QStringLiteral("error");
				item[QStringLiteral("message")] = job->message;
			}
			else if(job->state == QLatin1String("done"))
			{
				item[QStringLiteral("stage")] = QStringLiteral("converted");
				item[QStringLiteral("percent")] = 100.0;
			}
			if(!item.isEmpty())
				fromJobs[job->path] = item; // the newest job of a disc wins
		}
	}
	for(const QVariant &v : games_)
	{
		const QVariantMap game = v.toMap();
		const QString path = game.value(QStringLiteral("path")).toString();
		QVariantMap item = fromJobs.value(path);
		const QString stage = item.value(QStringLiteral("stage")).toString();
		// After the conversion, the package's journey: sending, installing.
		if(stage.isEmpty() || stage == QLatin1String("converted"))
		{
			QVariantMap queued = packageOf_.contains(path) ? fromQueue(packageOf_.value(path)) : QVariantMap();
			// The disc file itself, sent as it is.
			if(queued.isEmpty())
				queued = fromQueue(path);
			if(!queued.isEmpty() && queued.value(QStringLiteral("stage")) != QLatin1String("cancelled"))
				item = queued;
		}
		if(!item.isEmpty())
			progress[path] = item;
	}
	if(progress != progress_)
	{
		progress_ = progress;
		emit progressChanged();
	}

	// Each conversion's card follows its package to the end: sending,
	// installing, and how that ended.
	QVariantList conversions = conversions_;
	for(QVariant &v : conversions)
	{
		QVariantMap m = v.toMap();
		const QString pkg = m.value(QStringLiteral("pkgPath")).toString();
		const QString key = pkg.isEmpty() ? QString() : AppController::transferKey(pkg);
		QVariantMap transfer = key.isEmpty() ? QVariantMap() : transfers.value(key).toMap();
		const QString stale = m.value(QStringLiteral("staleTaskId")).toString();
		if(!stale.isEmpty() && transfer.value(QStringLiteral("taskId")).toString() == stale)
			transfer.clear();
		m[QStringLiteral("pkgKey")] = key;
		m[QStringLiteral("transfer")] = transfer;
		v = m;
	}
	if(conversions != conversions_)
	{
		conversions_ = conversions;
		emit conversionsChanged();
	}
}

void GamesController::removeConversion(const QString &id)
{
	QStringList packages;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
						[&](const std::shared_ptr<Job> &j) {
							const bool gone = j->id == id && j->state != QLatin1String("waiting")
								&& j->state != QLatin1String("converting");
							if(gone)
								packages << j->pkgPath;
							return gone;
						}),
			jobs_.end());
	}
	for(const QString &pkg : packages)
		app_->removeFinishedTasksOf(pkg);
	publish();
}

void GamesController::workerLoop()
{
	while(!stopping_)
	{
		std::shared_ptr<Job> next;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wakeup_.wait(lock, [&]() {
				if(stopping_)
					return true;
				for(auto &job : jobs_)
					if(job->state == QLatin1String("waiting"))
						return true;
				return false;
			});
			if(stopping_)
				return;
			for(auto &job : jobs_)
			{
				if(job->state == QLatin1String("waiting"))
				{
					next = job;
					next->state = QStringLiteral("converting");
					break;
				}
			}
		}
		if(next)
			runJob(next);
	}
}

void GamesController::runJob(const std::shared_ptr<Job> &job)
{
#ifdef ORBISLINK_HAS_FPKG
	auto schedulePublish = [this]() {
		if(!publishPending_.exchange(true))
			QMetaObject::invokeMethod(this, [this]() { publish(); }, Qt::QueuedConnection);
	};
	schedulePublish();

	// A disc on the console comes to this PC first; the copy goes once the
	// package is made (or the job ends).
	struct Fetched
	{
		std::shared_ptr<Job> job;
		~Fetched()
		{
			if(!job->fetchedFolder.isEmpty())
				QDir(job->fetchedFolder).removeRecursively();
		}
	} fetched { job };
	if(!job->remotePath.isEmpty() && !fetchFromConsole(job, schedulePublish))
	{
		QMetaObject::invokeMethod(this, [this]() { publish(); }, Qt::QueuedConnection);
		return;
	}

	const Settings settings = app_->settings();
	fpkg::ClassicOptions options;
	options.title = job->title.toStdString();
	options.outputDir = settings.convertOutputFolder;
	options.now = QDateTime::currentSecsSinceEpoch();

	// First use: the emulator files, as easy-ps2-fpkg fetches them.
	QString assetsError;
	if(job->reusePath.isEmpty()
		&& !ensureAssets([&](const QString &stage, double percent) {
			   {
				   std::lock_guard<std::mutex> lock(mutex_);
				   job->stage = stage;
				   job->percent = percent;
			   }
			   schedulePublish();
			   return !job->cancel.load() && !stopping_.load();
		   },
			&assetsError))
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			job->state = job->cancel ? QStringLiteral("cancelled") : QStringLiteral("error");
			job->message = job->cancel ? QString() : assetsError;
		}
		QMetaObject::invokeMethod(this, [this]() { publish(); }, Qt::QueuedConnection);
		return;
	}
	const fpkg::EmulatorInfo emus = fpkg::findEmulators(assetsFolder().toStdString());
	// The game's own cover, when the collection has it (PS2 only).
	if(job->reusePath.isEmpty() && job->platform == QLatin1String("ps2") && !job->disc.serial.empty())
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			job->stage = QStringLiteral("cover");
			job->percent = 0;
		}
		schedulePublish();
		if(fetchCover(job->disc.serial, &options.icon, &options.background))
			logInfo("Games: cover art for " + job->disc.serial);
		else
			logInfo("Games: no cover for " + job->disc.serial + "; the emulator's art stays");
	}

	fpkg::ClassicResult result;
	std::string error;
	const QString existing = job->reusePath;
	if(!existing.isEmpty())
		logInfo("Games: using the package already made for \"" + options.title + "\"");
	else
		logInfo("Games: converting \"" + options.title + "\" (" + job->disc.serial + ")");
	if(!existing.isEmpty())
		result.pkgPath = QDir::fromNativeSeparators(existing).toStdString();
	const bool ok = !existing.isEmpty() || fpkg::convertClassic(job->disc, emus, options,
		[&](const std::string &stage, uint64_t done, uint64_t total) {
			{
				std::lock_guard<std::mutex> lock(mutex_);
				job->stage = QString::fromStdString(stage);
				// The image is nearly all the work; the rest gets a sliver.
				const double part = total ? double(done) / double(total) : 0.0;
				job->percent = stage == "prepare" ? 1.0 * part
					: stage == "image" ? 1.0 + 89.0 * part
					: stage == "digest" ? 90.0 + 9.0 * part
					: 99.0 + part;
			}
			schedulePublish();
			return !job->cancel.load() && !stopping_.load();
		},
		&result, &error);

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(ok)
		{
			job->state = QStringLiteral("done");
			job->percent = 100;
			job->pkgPath = QString::fromStdString(result.pkgPath);
		}
		else if(job->cancel)
		{
			job->state = QStringLiteral("cancelled");
		}
		else
		{
			job->state = QStringLiteral("error");
			job->message = QString::fromStdString(error);
		}
	}
	if(ok)
		logInfo("Games: package ready at " + result.pkgPath);
	else
		logWarning("Games: conversion failed — " + error);

	const QString pkg = QString::fromStdString(result.pkgPath);
	const QString sendTo = job->storageFolder.isEmpty() ? storageFolder() : job->storageFolder;
	const bool install = job->install;
	const QString title = job->title;
	const QString message = QString::fromStdString(error);
	const bool cancelled = job->cancel.load();
	QMetaObject::invokeMethod(this, [this, job, ok, install, pkg, sendTo, title, message, cancelled]() {
		// Over FTP into the app's own folder on the console, installed once
		// it lands, and deleted from there after the install.
		if(ok)
		{
			const QString before = app_->transfers().value(AppController::transferKey(pkg)).toMap()
				.value(QStringLiteral("taskId")).toString();
			std::lock_guard<std::mutex> lock(mutex_);
			job->staleTaskId = before;
		}
		if(ok && install)
			app_->sendAndInstall(pkg, sendTo);
		publish();
		if(ok && !install)
			emit app_->notify(tr("Convert"), tr("%1 is ready in the output folder.").arg(title), false);
		else if(!ok && !cancelled)
			emit app_->notify(tr("Convert"), tr("%1: %2").arg(title, message), true);
	}, Qt::QueuedConnection);
#else
	Q_UNUSED(job);
#endif
}

} // namespace orbislink
