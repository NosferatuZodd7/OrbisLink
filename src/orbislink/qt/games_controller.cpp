// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/games_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/qt/app_controller.h"

#ifdef ORBISLINK_HAS_FPKG
#include "orbislink/fpkg/classic_converter.h"
#include "orbislink/fpkg/disc_scanner.h"
#endif

#include <QBuffer>
#include <QGuiApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace orbislink {

namespace {

QString sizeText(quint64 bytes)
{
	if(bytes >= 1024ull * 1024 * 1024)
		return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 2);
	return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024), 0, 'f', 0);
}

#ifdef ORBISLINK_HAS_FPKG
// A plain cover for the PS4 menu, so a converted game does not show the
// art of the game the emulator came from: the platform, large, and the name.
fpkg::Bytes drawArt(const QString &title, const QString &platform, int width, int height)
{
	QImage image(width, height, QImage::Format_RGB888);
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	QLinearGradient g(0, 0, width, height);
	const bool ps2 = platform == QLatin1String("ps2");
	g.setColorAt(0, ps2 ? QColor(0x14, 0x23, 0x39) : QColor(0x2a, 0x2d, 0x34));
	g.setColorAt(1, ps2 ? QColor(0x0b, 0x11, 0x1a) : QColor(0x14, 0x16, 0x1b));
	p.fillRect(image.rect(), g);
	const int unit = std::min(width, height);
	QFont big = QGuiApplication::font();
	big.setPixelSize(unit / 4);
	big.setWeight(QFont::Black);
	p.setFont(big);
	p.setPen(ps2 ? QColor(0x38, 0x8a, 0xff) : QColor(0xc8, 0xcc, 0xd4));
	const QRect top(0, height / 8, width, height / 2);
	p.drawText(top, Qt::AlignHCenter | Qt::AlignVCenter, ps2 ? QStringLiteral("PS2") : QStringLiteral("PS1"));
	QFont name = big;
	name.setPixelSize(unit / 14);
	name.setWeight(QFont::DemiBold);
	p.setFont(name);
	p.setPen(QColor(0xeb, 0xed, 0xf3));
	const QRect bottom(width / 12, height / 2 + height / 12, width - width / 6, height / 3);
	p.drawText(bottom, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title);
	p.end();
	QByteArray png;
	QBuffer buffer(&png);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return fpkg::Bytes(png.begin(), png.end());
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
	std::atomic<bool> cancel { false };
#ifdef ORBISLINK_HAS_FPKG
	fpkg::DiscInfo disc;
	fpkg::Bytes icon;
	fpkg::Bytes background;
#endif
};

GamesController::GamesController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	connect(app_, &AppController::settingsChanged, this, &GamesController::foldersChanged);
	connect(app_, &AppController::transfersChanged, this, &GamesController::updateProgress);
	refreshEmulators();
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

QString GamesController::emulatorFolder() const
{
	return QString::fromStdString(app_->settings().emulatorFolder);
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

void GamesController::setEmulatorFolder(const QString &folder)
{
	const std::string path = toLocalPath(folder).toStdString();
	app_->updateSettings([&](Settings &s) { s.emulatorFolder = path; });
	refreshEmulators();
	rescan();
}

void GamesController::setOutputFolder(const QString &folder)
{
	const std::string path = toLocalPath(folder).toStdString();
	app_->updateSettings([&](Settings &s) { s.convertOutputFolder = path; });
}

void GamesController::refreshEmulators()
{
#ifdef ORBISLINK_HAS_FPKG
	const fpkg::EmulatorInfo emus = fpkg::findEmulators(app_->settings().emulatorFolder);
	ps1Emulator_ = emus.hasPs1();
	ps2Emulator_ = emus.hasPs2();
	ps2EmulatorName_ = QString::fromStdString(emus.ps2Name);
	logInfo("Games: emulator folder \"" + app_->settings().emulatorFolder + "\" — PS2 "
		+ (emus.hasPs2() ? "\"" + emus.ps2Name + "\"" : std::string("missing")) + ", PS1 "
		+ (emus.hasPs1() ? "found" : "missing"));
#endif
	emit foldersChanged();
}

void GamesController::rescan()
{
#ifdef ORBISLINK_HAS_FPKG
	const std::string folder = app_->settings().gamesFolder;
	const std::string emulatorFolder = app_->settings().emulatorFolder;
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
			std::string title = d.platform == "ps2" ? fpkg::lookupTitle(emus.titleDatabase, d.titleId) : std::string();
			if(title.empty())
				title = d.title;
			const bool hasEmulator = d.platform == "ps2" ? emus.hasPs2() : emus.hasPs1();
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
			m[QStringLiteral("hasEmulator")] = hasEmulator;
			m[QStringLiteral("convertible")] = hasEmulator && d.titleId.size() == 9;
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
		// The art is drawn here, on the interface's thread.
		job->icon = drawArt(job->title, job->platform, 512, 512);
		job->background = drawArt(job->title, job->platform, 1920, 1080);
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
		if(stage == QLatin1String("sending") || stage == QLatin1String("installing")
			|| t.value(QStringLiteral("installNext")).toBool())
			travelling.insert(m.value(QStringLiteral("id")).toString());
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
						[&](const std::shared_ptr<Job> &j) {
							return j->state != QLatin1String("waiting")
								&& j->state != QLatin1String("converting") && !travelling.contains(j->id);
						}),
			jobs_.end());
	}
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
	// Packages that just landed on the console: now they are installed.
	// (Only once this upload was seen under way: an old "sent" of the same
	// file does not count.)
	for(auto it = installAfterSend_.begin(); it != installAfterSend_.end();)
	{
		const QString stage = transfers.value(it.key()).toMap().value(QStringLiteral("stage")).toString();
		if(stage == QLatin1String("sending"))
		{
			it.value() = true;
			++it;
		}
		else if(stage == QLatin1String("sent") && it.value())
		{
			const QString pkg = it.key();
			it = installAfterSend_.erase(it);
			QMetaObject::invokeMethod(this, [this, pkg]() { app_->addPaths(QStringList { pkg }, 0); },
				Qt::QueuedConnection);
		}
		else if(it.value() && (stage == QLatin1String("error") || stage == QLatin1String("cancelled")))
			it = installAfterSend_.erase(it);
		else
			++it;
	}
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
		if(transfer.value(QStringLiteral("stage")) == QLatin1String("sent") && installAfterSend_.contains(key))
			transfer[QStringLiteral("installNext")] = true;
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
	{
		std::lock_guard<std::mutex> lock(mutex_);
		jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
						[&](const std::shared_ptr<Job> &j) {
							return j->id == id && j->state != QLatin1String("waiting")
								&& j->state != QLatin1String("converting");
						}),
			jobs_.end());
	}
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

	const Settings settings = app_->settings();
	fpkg::ClassicOptions options;
	options.title = job->title.toStdString();
	options.outputDir = settings.convertOutputFolder;
	options.icon = job->icon;
	options.background = job->background;
	options.now = QDateTime::currentSecsSinceEpoch();
	const fpkg::EmulatorInfo emus = fpkg::findEmulators(settings.emulatorFolder);

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
	const bool install = job->install;
	const QString title = job->title;
	const QString message = QString::fromStdString(error);
	const bool cancelled = job->cancel.load();
	QMetaObject::invokeMethod(this, [this, job, ok, install, pkg, title, message, cancelled]() {
		// Over FTP, like any file dropped on the FTP zone (same-name checks,
		// the upload folder), and installed once it lands — unless the
		// setting already installs every upload.
		if(ok)
		{
			const QString before = app_->transfers().value(AppController::transferKey(pkg)).toMap()
				.value(QStringLiteral("taskId")).toString();
			std::lock_guard<std::mutex> lock(mutex_);
			job->staleTaskId = before;
		}
		if(ok && install)
		{
			if(!app_->settings().installAfterUpload)
				installAfterSend_[AppController::transferKey(pkg)] = false;
			app_->addPaths(QStringList { pkg }, 1);
		}
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
