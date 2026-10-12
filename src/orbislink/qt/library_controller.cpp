// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/library_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/library/console_library.h"
#include "orbislink/net/payload_sender.h"
#include "orbislink/payloads/payload_layout.h"
#include "orbislink/pkg/pkg_inspector.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/games_controller.h"
#include "orbislink/qt/shadowmount_control.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/qt/translate_message.h"
#include "orbislink/settings/settings_store.h"
#ifdef ORBISLINK_HAS_FPKG
#include "orbislink/fpkg/disc_scanner.h"
#endif

#include <QByteArray>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

namespace orbislink {

using library::Kind;

namespace {

QString dataUrl(const std::vector<uint8_t> &png)
{
	static const uint8_t magic[] = { 0x89, 'P', 'N', 'G' };
	if(png.size() < 8 || !std::equal(std::begin(magic), std::end(magic), png.begin()))
		return QString();
	const QByteArray bytes(reinterpret_cast<const char *>(png.data()), static_cast<int>(png.size()));
	return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

library::BlockReader::Fetch fetchFrom(FtpClient &ftp, const std::string &path)
{
	return [&ftp, path](int64_t offset, size_t length, std::vector<uint8_t> *bytes) {
		return ftp.read(path, offset, length, bytes).ok;
	};
}

std::string stem(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string upper(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return text;
}

} // namespace

LibraryController::LibraryController(AppController *app, GamesController *games, QObject *parent)
	: QObject(parent), app_(app), games_(games)
{
	// A package put in the library (a converted disc) or installed from it:
	// read it again, a moment later, to show it.
	connect(app_, &AppController::transfersChanged, this, [this]() {
		if(!scanned_ || scanning_)
			return;
		const QSet<QString> now = finishedTasks();
		for(const QString &id : now)
			if(!finishedSeen_.contains(id))
			{
				finishedSeen_ = now;
				QTimer::singleShot(1500, this, [this]() { refresh(); });
				return;
			}
	});
	loadUnfinishedCopies();
}

QSet<QString> LibraryController::finishedTasks() const
{
	QSet<QString> ids;
	const QVariantMap transfers = app_->transfers();
	for(auto it = transfers.cbegin(); it != transfers.cend(); ++it)
	{
		const QVariantMap transfer = it.value().toMap();
		const QString stage = transfer.value(QStringLiteral("stage")).toString();
		if(stage == QLatin1String("sent") || stage == QLatin1String("installed"))
			ids.insert(transfer.value(QStringLiteral("taskId")).toString());
	}
	return ids;
}

LibraryController::~LibraryController()
{
	saveUnfinishedCopies();
	cancel_ = true;
	{
		std::lock_guard<std::mutex> lock(copyMutex_);
		for(auto &pair : copyCancels_)
			pair.second->store(true);
		copies_.clear();
	}
	copyWake_.notify_all();
	if(scanner_.joinable())
		scanner_.join();
	if(worker_.joinable())
		worker_.join();
	if(copier_.joinable())
		copier_.join();
}

bool LibraryController::ps5() const { return app_->installsFromConsole(); }

QVariantMap LibraryController::itemAt(const QString &path) const
{
	for(const QVariant &entry : items_)
		if(entry.toMap().value(QStringLiteral("path")).toString() == path)
			return entry.toMap();
	return QVariantMap();
}

void LibraryController::refresh()
{
	if(scanning_)
		return;
	if(scanner_.joinable())
		scanner_.join();
	const std::string address = app_->settings().consoleAddress;
	if(address.empty() || !app_->canUseFtp())
	{
		status_ = tr("The console's FTP is not answering: the library is read over it.");
		emit itemsChanged();
		return;
	}
	scanning_ = true;
	status_ = tr("Reading the console's library…");
	emit itemsChanged();

	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	const bool onPs5 = ps5();
	const payloads::Kind consoleKind = onPs5 ? payloads::Kind::Ps5 : payloads::Kind::Ps4;
	GamesController *games = games_;
	scanner_ = std::thread([this, config, onPs5, consoleKind, games]() {
		FtpClient ftp(config);
		// The folder in the console's memory is made, so it is there to fill.
		ftp.makeDirectory(std::string("/data/") + library::kFolderName);
		// What the console has installed, by title ID: games and apps, and
		// the patches over them.
		std::set<std::string> installed;
		std::set<std::string> patched;
		std::vector<FtpEntry> apps;
		if(ftp.list("/user/app", &apps).ok)
			for(const FtpEntry &app : apps)
				installed.insert(upper(app.name));
		std::vector<FtpEntry> patches;
		if(ftp.list("/user/patch", &patches).ok)
			for(const FtpEntry &patch : patches)
				patched.insert(upper(patch.name));
		// A game's add-ons, by their label, read when one is in the library.
		std::map<std::string, std::set<std::string>> addOns;
		auto addOnInstalled = [&ftp, &addOns](const std::string &titleId, const std::string &contentId) {
			// "UP0000-CUSA00000_00-LABEL…": the label is what follows the "-".
			if(titleId.empty() || contentId.size() <= 20)
				return false;
			auto found = addOns.find(titleId);
			if(found == addOns.end())
			{
				std::set<std::string> labels;
				std::vector<FtpEntry> entries;
				if(ftp.list("/user/addcont/" + titleId, &entries).ok)
					for(const FtpEntry &entry : entries)
						labels.insert(upper(entry.name));
				found = addOns.emplace(titleId, labels).first;
			}
			return found->second.count(upper(contentId.substr(20))) > 0;
		};

		QVariantList items;
		QVariantList folders;
		for(const std::string &folder : library::libraryFolders())
		{
			if(cancel_)
				return;
			std::vector<FtpEntry> entries;
			if(!ftp.list(folder, &entries).ok)
				continue;
			folders << QVariantMap { { QStringLiteral("path"), QString::fromStdString(folder) },
				{ QStringLiteral("drive"), QString::fromStdString(library::driveOf(folder)) } };

			std::map<std::string, FtpEntry> byName;
			for(const FtpEntry &entry : entries)
				byName[toLower(entry.name)] = entry;
			// A cue sheet's track is shown as the cue, not apart.
			std::map<std::string, std::string> trackOf;
			std::set<std::string> tracks;
#ifdef ORBISLINK_HAS_FPKG
			for(const FtpEntry &entry : entries)
			{
				if(entry.isDirectory || !endsWith(toLower(entry.name), ".cue"))
					continue;
				std::vector<uint8_t> text;
				if(!ftp.read(entry.path, 0, 64 * 1024, &text).ok)
					continue;
				const std::string track = fpkg::cueImageName(std::string(text.begin(), text.end()));
				if(!track.empty())
				{
					tracks.insert(toLower(track));
					trackOf[entry.name] = track;
				}
			}
#endif

			for(const FtpEntry &entry : entries)
			{
				if(cancel_)
					return;
				if(entry.name == "." || entry.name == "..")
					continue;
				const Kind kind = library::kindOf(entry.name, entry.isDirectory, entry.size);
				if(kind == Kind::Other || (kind == Kind::Disc && tracks.count(toLower(entry.name))))
					continue;

				QVariantMap item;
				item[QStringLiteral("path")] = QString::fromStdString(entry.path);
				item[QStringLiteral("name")] = QString::fromStdString(entry.name);
				item[QStringLiteral("folder")] = entry.isDirectory;
				item[QStringLiteral("drive")] = QString::fromStdString(library::driveOf(entry.path));
				item[QStringLiteral("size")] = static_cast<double>(entry.size);
				item[QStringLiteral("sizeText")] = entry.isDirectory ? QString()
																	 : QString::fromStdString(humanBytes(entry.size));
				item[QStringLiteral("kind")] = QString::fromLatin1(library::kindName(kind));
				QString title = QString::fromStdString(stem(entry.name));
				std::string titleId;
				QString platform;
				QString action;
				QString note;
				// What cannot be used as it is goes with the rest.
				QString group = QStringLiteral("other");

				switch(kind)
				{
					case Kind::Package:
					{
						PkgInspector::Options options;
						options.maxIconBytes = 1024 * 1024;
						library::BlockReader reader(fetchFrom(ftp, entry.path), entry.size);
						const PkgInfo info = PkgInspector(options).inspect(entry.path, entry.size,
							[&reader](int64_t offset, void *buffer, size_t size) { return reader.read(offset, buffer, size); });
						if(!info.valid)
						{
							note = tr("Not a package this can read.");
							break;
						}
						title = QString::fromStdString(info.displayTitle());
						titleId = info.titleId;
						platform = QStringLiteral("ps4");
						item[QStringLiteral("version")] = QString::fromStdString(info.appVersion);
						item[QStringLiteral("category")] = QString::fromLatin1(pkgCategoryCode(info.kind));
						item[QStringLiteral("icon")] = dataUrl(info.iconPng);
						switch(info.kind)
						{
							case PkgCategory::Patch:
							case PkgCategory::DeltaPatch:
								group = QStringLiteral("extras");
								item[QStringLiteral("installed")] = patched.count(upper(titleId)) > 0;
								break;
							case PkgCategory::Dlc:
								group = QStringLiteral("extras");
								item[QStringLiteral("installed")] = addOnInstalled(upper(titleId), info.contentId);
								break;
							case PkgCategory::Theme:
								group = QStringLiteral("extras");
								break;
							default:
								group = QStringLiteral("games");
								item[QStringLiteral("installed")] = installed.count(upper(titleId)) > 0;
						}
						if(onPs5)
							action = QStringLiteral("install");
						else
							note = tr("On a PS4 a package on the console installs from Debug Settings → Package "
								"Installer (it lists /data/pkg and USB drives), or drop it on the window from this PC.");
						break;
					}
					case Kind::Disc:
					{
#ifdef ORBISLINK_HAS_FPKG
						std::string imagePath = entry.path;
						std::string imageName = entry.name;
						int64_t imageSize = entry.size;
						const auto cue = trackOf.find(entry.name);
						if(cue != trackOf.end())
						{
							const auto track = byName.find(toLower(cue->second));
							if(track == byName.end())
							{
								note = tr("The cue sheet names %1, which is not in the folder.")
									.arg(QString::fromStdString(cue->second));
								break;
							}
							imagePath = track->second.path;
							imageName = track->second.name;
							imageSize = track->second.size;
							item[QStringLiteral("size")] = static_cast<double>(imageSize);
							item[QStringLiteral("sizeText")] = QString::fromStdString(humanBytes(imageSize));
						}
						library::BlockReader reader(fetchFrom(ftp, imagePath), imageSize);
						const fpkg::DiscInfo disc = fpkg::inspectDisc(imageName, static_cast<uint64_t>(imageSize),
							[&reader](uint64_t offset, uint8_t *out, size_t size) {
								return reader.read(static_cast<int64_t>(offset), out, size);
							});
						title = QString::fromStdString(fpkg::titleFromFileName(entry.name));
						if(disc.platform.empty())
						{
							note = tr("Not a PS1/PS2 disc this can read.");
							break;
						}
						platform = QString::fromStdString(disc.platform);
						titleId = disc.titleId;
						const QString known = games ? games->classicTitle(platform, QString::fromStdString(titleId)) : QString();
						if(!known.isEmpty())
							title = known;
						item[QStringLiteral("serial")] = QString::fromStdString(disc.serial);
						item[QStringLiteral("installed")] = !titleId.empty() && installed.count(upper(titleId)) > 0;
						action = QStringLiteral("convert");
						group = QStringLiteral("games");
#else
						note = tr("This build cannot convert discs.");
#endif
						break;
					}
					case Kind::Image:
					{
						titleId = library::titleIdInName(entry.name);
						platform = QStringLiteral("ps5");
						group = QStringLiteral("games");
						item[QStringLiteral("installed")] = !titleId.empty() && installed.count(titleId) > 0;
						if(onPs5)
							action = QStringLiteral("mount");
						else
							note = tr("A PS5 game image: for a PS5 with ShadowMountPlus.");
						break;
					}
					case Kind::Folder:
					{
						// An app or game folder says what it is in sce_sys.
						std::vector<uint8_t> bytes;
						library::AppParams params;
						if(ftp.read(entry.path + "/sce_sys/param.json", 0, 256 * 1024, &bytes).ok)
						{
							params = library::readParamJson(std::string(bytes.begin(), bytes.end()));
							platform = QStringLiteral("ps5");
						}
						if(!params.ok && ftp.read(entry.path + "/sce_sys/param.sfo", 0, 256 * 1024, &bytes).ok)
						{
							params = library::readParamSfo(bytes);
							platform = QStringLiteral("ps4");
						}
						if(!params.ok)
							continue; // a folder of something else
						titleId = params.titleId;
						group = QStringLiteral("games");
						if(!params.title.empty())
							title = QString::fromStdString(params.title);
						item[QStringLiteral("version")] = QString::fromStdString(params.version);
						std::vector<uint8_t> icon;
						if(ftp.read(entry.path + "/sce_sys/icon0.png", 0, 1024 * 1024, &icon).ok)
							item[QStringLiteral("icon")] = dataUrl(icon);
						item[QStringLiteral("installed")] = installed.count(upper(titleId)) > 0;
						if(onPs5)
							action = QStringLiteral("mount");
						else
							note = tr("An app folder: for a PS5 with ShadowMountPlus.");
						break;
					}
					case Kind::Payload:
						action = QStringLiteral("run");
						group = QStringLiteral("payloads");
						item[QStringLiteral("port")] = payloads::loaderPort(consoleKind, entry.name);
						break;
					case Kind::Archive:
						note = tr("An archive: unpack it on the PC first; the console does not open them.");
						break;
					case Kind::Other:
						break;
				}
				item[QStringLiteral("title")] = title;
				item[QStringLiteral("titleId")] = QString::fromStdString(titleId);
				item[QStringLiteral("platform")] = platform;
				item[QStringLiteral("action")] = action;
				item[QStringLiteral("note")] = note;
				item[QStringLiteral("group")] = group;
				if(!item.contains(QStringLiteral("installed")))
					item[QStringLiteral("installed")] = false;
				items << item;
			}
		}
		std::sort(items.begin(), items.end(), [](const QVariant &a, const QVariant &b) {
			return a.toMap().value(QStringLiteral("title")).toString().compare(
					   b.toMap().value(QStringLiteral("title")).toString(), Qt::CaseInsensitive) < 0;
		});
		if(cancel_)
			return;
		QMetaObject::invokeMethod(this, [this, items, folders]() {
			items_ = items;
			folders_ = folders;
			scanning_ = false;
			scanned_ = true;
			finishedSeen_ = finishedTasks();
			status_.clear();
			emit itemsChanged();
		}, Qt::QueuedConnection);
	});
}

QString LibraryController::consoleName() const
{
	const std::string address = app_->settings().consoleAddress;
	for(const ConsoleEntry &console : app_->settings().consoles)
		if(console.address == address)
			return QString::fromStdString(console.name.empty() ? console.address : console.name);
	return QString::fromStdString(address);
}

void LibraryController::runAction(const QString &what, std::function<QString(FtpClient &ftp, bool *error)> job,
	bool rescan, const QVariantMap &item, const QString &kind)
{
	if(busy_)
		return;
	if(worker_.joinable())
		worker_.join();
	busy_ = true;
	status_ = what;
	emit busyChanged();
	emit itemsChanged();

	// Its card in the installs panel, beside the packages'.
	QString jobId;
	if(!kind.isEmpty())
	{
		jobId = QStringLiteral("library-%1").arg(nextJob_++);
		QVariantMap card;
		card[QStringLiteral("id")] = jobId;
		card[QStringLiteral("kind")] = kind;
		card[QStringLiteral("name")] = item.value(QStringLiteral("title"));
		card[QStringLiteral("file")] = item.value(QStringLiteral("name"));
		card[QStringLiteral("icon")] = item.value(QStringLiteral("icon"));
		card[QStringLiteral("itemKind")] = item.value(QStringLiteral("kind"));
		card[QStringLiteral("console")] = consoleName();
		card[QStringLiteral("state")] = QStringLiteral("working");
		card[QStringLiteral("stageText")] = what;
		jobs_.prepend(card);
		actionRecipes_[jobId] = { what, job, rescan, item, kind };
		emit jobsChanged();
		emit app_->showPanel(QStringLiteral("queue"));
	}

	const FtpClient::Config config = app_->ftpClientConfigFor(app_->settings().consoleAddress);
	worker_ = std::thread([this, config, job, rescan, jobId]() {
		FtpClient ftp(config);
		bool error = false;
		const QString message = job(ftp, &error);
		if(cancel_)
			return;
		QMetaObject::invokeMethod(this, [this, message, error, rescan, jobId]() {
			busy_ = false;
			status_.clear();
			emit busyChanged();
			emit itemsChanged();
			if(!jobId.isEmpty())
				updateJob(jobId, { { QStringLiteral("state"), error ? QStringLiteral("error") : QStringLiteral("done") },
					{ QStringLiteral("message"), message } });
			else if(!message.isEmpty())
				emit finished(message, error);
			if(rescan)
				refresh();
		}, Qt::QueuedConnection);
	});
}

void LibraryController::updateJob(const QString &id, const QVariantMap &fields)
{
	for(QVariant &entry : jobs_)
	{
		QVariantMap job = entry.toMap();
		if(job.value(QStringLiteral("id")).toString() != id)
			continue;
		for(auto it = fields.cbegin(); it != fields.cend(); ++it)
			job[it.key()] = it.value();
		entry = job;
		emit jobsChanged();
		return;
	}
}

void LibraryController::removeJob(const QString &id)
{
	for(int i = 0; i < jobs_.size(); ++i)
		if(jobs_[i].toMap().value(QStringLiteral("id")).toString() == id
			&& jobs_[i].toMap().value(QStringLiteral("state")).toString() != QLatin1String("working"))
		{
			jobs_.removeAt(i);
			actionRecipes_.remove(id);
			emit jobsChanged();
			return;
		}
}

void LibraryController::clearFinishedJobs()
{
	QVariantList kept;
	for(const QVariant &entry : jobs_)
	{
		if(entry.toMap().value(QStringLiteral("state")).toString() == QLatin1String("working"))
			kept << entry;
		else
			actionRecipes_.remove(entry.toMap().value(QStringLiteral("id")).toString());
	}
	if(kept.size() == jobs_.size())
		return;
	jobs_ = kept;
	emit jobsChanged();
}

void LibraryController::act(const QString &path)
{
	const QVariantMap item = itemAt(path);
	if(!item.isEmpty())
		perform(item, item.value(QStringLiteral("action")).toString());
}

void LibraryController::perform(const QVariantMap &item, const QString &action)
{
	const QString path = item.value(QStringLiteral("path")).toString();
	const QString name = item.value(QStringLiteral("name")).toString();
	const QString title = item.value(QStringLiteral("title")).toString();
	const std::string address = app_->settings().consoleAddress;
	if(path.isEmpty())
		return;

	if(action == QLatin1String("install"))
	{
		app_->installFromConsole(path);
		return;
	}
	if(action == QLatin1String("convert") || action == QLatin1String("convertOnly"))
	{
		games_->convertFromConsole(path, title, item.value(QStringLiteral("platform")).toString(),
			action == QLatin1String("convert"));
		return;
	}
	if(action == QLatin1String("delete"))
	{
		if(item.value(QStringLiteral("folder")).toBool())
			return;
		runAction(tr("Deleting %1…").arg(name), [path, name](FtpClient &ftp, bool *error) {
			const FtpResult removed = ftp.removeFile(path.toStdString());
			if(removed.ok)
				return tr("%1 deleted from the console.").arg(name);
			*error = true;
			return tr("Could not delete %1: %2").arg(name, translateMessage(removed.message));
		}, true);
		return;
	}
	if(action == QLatin1String("mount"))
	{
		const FtpClient::Config config = app_->ftpClientConfigFor(address);
		runAction(tr("Handing %1 to ShadowMountPlus…").arg(title), [this, path, name, title, config, address](
				FtpClient &ftp, bool *error) {
			const std::string from = path.toStdString();
			const std::string folder = library::mountFolderFor(from);
			if(folder.empty())
			{
				*error = true;
				return tr("ShadowMountPlus does not look on the drive %1 is on.").arg(name);
			}
			const std::string to = folder + "/" + name.toStdString();
			ftp.makeDirectory(folder);
			// Never over what is already there.
			std::vector<FtpEntry> there;
			if(ftp.list(folder, &there).ok)
				for(const FtpEntry &entry : there)
					if(entry.name == name.toStdString())
					{
						*error = true;
						return tr("%1 is already in %2.").arg(name, QString::fromStdString(folder));
					}
			const FtpResult moved = ftp.rename(from, to);
			if(!moved.ok)
			{
				*error = true;
				return tr("Could not move %1 to %2: %3").arg(name, QString::fromStdString(folder),
					translateMessage(moved.message));
			}
			logInfo("Library: " + from + " moved to " + to + " for ShadowMountPlus.");
			const std::string how = shadowmount::rescan(config, address, &cancel_);
			const QString where = QString::fromStdString(folder);
			if(how == "api")
				return tr("%1 is in %2: ShadowMountPlus puts it on the home screen now.").arg(title, where);
			if(how == "restarted")
				return tr("%1 is in %2, and ShadowMountPlus was started again: it is on the home screen in a few "
					"seconds.").arg(title, where);
			return tr("%1 is in %2: ShadowMountPlus puts it on the home screen at its next scan, within a minute.")
				.arg(title, where);
		}, true, item, QStringLiteral("mount"));
		return;
	}
	if(action == QLatin1String("run"))
	{
		const int port = item.value(QStringLiteral("port")).toInt();
		const int64_t size = static_cast<int64_t>(item.value(QStringLiteral("size")).toDouble());
		runAction(tr("Sending %1…").arg(name), [this, path, name, port, size, address](FtpClient &ftp, bool *error) {
			std::vector<uint8_t> bytes;
			const FtpResult got = ftp.read(path.toStdString(), 0, static_cast<size_t>(std::max<int64_t>(size, 1)), &bytes);
			if(!got.ok || bytes.empty())
			{
				*error = true;
				return tr("Could not read %1 on the console: %2").arg(name, translateMessage(got.message));
			}
			PayloadSender::Options options;
			options.listenMs = 2000;
			options.cancel = &cancel_;
			const PayloadSender::Result sent = PayloadSender::send(address, static_cast<uint16_t>(port), bytes, options);
			if(!sent.sent)
			{
				*error = true;
				return tr("%1 did not go: %2 (port %3)").arg(name, translateMessage(sent.error)).arg(port);
			}
			return tr("%1 is running on the console.").arg(name);
		}, false, item, QStringLiteral("run"));
	}
}

void LibraryController::uninstall(const QVariantMap &item, bool updateOnly)
{
	const QString titleId = item.value(QStringLiteral("titleId")).toString();
	const QString title = item.value(QStringLiteral("title")).toString();
	if(titleId.isEmpty())
		return;
	RpiClient::Config config;
	config.host = app_->settings().consoleAddress;
	config.port = app_->settings().installerPort;
	config.ps5 = app_->installsFromConsole();
	QStringList addons;
	bool hasUpdate = false;
	for(const QVariant &entry : item.value(QStringLiteral("parts")).toList())
	{
		const QVariantMap part = entry.toMap();
		if(part.value(QStringLiteral("what")).toString() == QLatin1String("addon"))
			addons << part.value(QStringLiteral("path")).toString();
		hasUpdate = hasUpdate || part.value(QStringLiteral("what")).toString() == QLatin1String("update");
	}
	QVariantMap card = item;
	card[QStringLiteral("icon")] = item.value(QStringLiteral("picture"));
	runAction(updateOnly ? tr("Uninstalling the update of %1…").arg(title) : tr("Uninstalling %1…").arg(title),
		[config, titleId, title, addons, hasUpdate, updateOnly](FtpClient &ftp, bool *error) {
			RpiClient installer(config);
			const std::string id = titleId.toStdString();
			if(updateOnly)
			{
				const InstallerResult removed = installer.uninstallPatch(id);
				if(!removed.ok)
				{
					*error = true;
					return tr("Could not uninstall the update of %1: %2").arg(title, translateMessage(removed.message));
				}
				logInfo("Library: update of " + id + " uninstalled");
				return tr("The update of %1 is off the console.").arg(title);
			}
			// The add-ons first, by the content ID in their package's header.
			for(const QString &path : addons)
			{
				std::vector<uint8_t> header;
				if(!ftp.read(path.toStdString(), 0x40, 36, &header).ok || header.size() < 36)
					continue;
				const std::string contentId(header.begin(), header.begin() + 36);
				const InstallerResult removed = installer.uninstallAdditionalContent(contentId);
				if(!removed.ok)
					logWarning("Library: add-on " + contentId + " not uninstalled — " + removed.message);
			}
			if(hasUpdate)
				installer.uninstallPatch(id);
			const InstallerResult removed = installer.uninstallGame(id);
			if(!removed.ok)
			{
				*error = true;
				return tr("Could not uninstall %1: %2").arg(title, translateMessage(removed.message));
			}
			logInfo("Library: " + id + " uninstalled");
			return tr("%1 is off the console.").arg(title);
		},
		true, card, QStringLiteral("uninstall"));
}

void LibraryController::convert(const QString &path, bool install)
{
	const QVariantMap item = itemAt(path);
	if(item.isEmpty() || item.value(QStringLiteral("action")).toString() != QLatin1String("convert"))
		return;
	perform(item, install ? QStringLiteral("convert") : QStringLiteral("convertOnly"));
}

void LibraryController::remove(const QString &path)
{
	const QVariantMap item = itemAt(path);
	if(!item.isEmpty())
		perform(item, QStringLiteral("delete"));
}

// ── copies to this PC ───────────────────────────────────────────────────

void LibraryController::copyToPc(const QString &title, const QString &icon, const QString &folder,
	const QVector<CopyItem> &files)
{
	if(files.isEmpty() || folder.isEmpty())
		return;
	CopyJob job;
	job.id = QStringLiteral("library-%1").arg(nextJob_++);
	job.title = title;
	job.icon = icon;
	job.folder = folder;
	job.files = files;

	qint64 total = 0;
	for(const CopyItem &file : files)
		total += file.size;
	QVariantMap card;
	card[QStringLiteral("id")] = job.id;
	card[QStringLiteral("kind")] = QStringLiteral("copy");
	card[QStringLiteral("name")] = title;
	card[QStringLiteral("icon")] = icon;
	card[QStringLiteral("itemKind")] = QStringLiteral("package");
	card[QStringLiteral("console")] = consoleName();
	card[QStringLiteral("state")] = QStringLiteral("working");
	card[QStringLiteral("stageText")] = tr("Waiting to copy");
	card[QStringLiteral("percent")] = 0.0;
	card[QStringLiteral("amountText")] = QString::fromStdString(humanBytes(total));
	card[QStringLiteral("folder")] = folder;
	jobs_.prepend(card);
	emit jobsChanged();
	emit app_->showPanel(QStringLiteral("queue"));
	enqueueCopy(job);
}

void LibraryController::enqueueCopy(CopyJob job)
{
	copyRecipes_[job.id] = job;
	job.config = app_->ftpClientConfigFor(app_->settings().consoleAddress);
	job.cancel = std::make_shared<std::atomic<bool>>(false);
	{
		std::lock_guard<std::mutex> lock(copyMutex_);
		copyCancels_[job.id] = job.cancel;
		copies_.push_back(job);
	}
	if(!copier_.joinable())
		copier_ = std::thread([this]() { copyLoop(); });
	copyWake_.notify_all();
}

void LibraryController::retryJob(const QString &id)
{
	const auto action = actionRecipes_.constFind(id);
	if(action != actionRecipes_.cend())
	{
		// One thing at a time: while another runs, the card stays as it is.
		if(busy_)
			return;
		const ActionRecipe again = action.value();
		removeJob(id);
		runAction(again.what, again.job, again.rescan, again.item, again.kind);
		return;
	}
	const auto recipe = copyRecipes_.constFind(id);
	if(recipe == copyRecipes_.cend())
		return;
	for(const QVariant &entry : jobs_)
	{
		const QVariantMap card = entry.toMap();
		if(card.value(QStringLiteral("id")).toString() == id
			&& card.value(QStringLiteral("state")).toString() == QLatin1String("working"))
			return;
	}
	updateJob(id, { { QStringLiteral("state"), QStringLiteral("working") },
		{ QStringLiteral("stageText"), tr("Waiting to copy") }, { QStringLiteral("message"), QString() },
		{ QStringLiteral("interrupted"), false }, { QStringLiteral("percent"), 0.0 } });
	emit app_->showPanel(QStringLiteral("queue"));
	enqueueCopy(recipe.value());
}

namespace {

QString copiesPath()
{
	return QString::fromStdString(joinPath(SettingsStore::defaultDirectory(), "copies.json"));
}

} // namespace

void LibraryController::saveUnfinishedCopies() const
{
	QJsonArray kept;
	for(const QVariant &entry : jobs_)
	{
		const QVariantMap card = entry.toMap();
		const QString id = card.value(QStringLiteral("id")).toString();
		const bool unfinished = card.value(QStringLiteral("state")).toString() == QLatin1String("working")
			|| card.value(QStringLiteral("interrupted")).toBool();
		if(!unfinished || !copyRecipes_.contains(id))
			continue;
		const CopyJob &job = copyRecipes_.value(id);
		QJsonArray files;
		for(const CopyItem &file : job.files)
			files.append(QJsonObject { { QStringLiteral("remote"), file.remote }, { QStringLiteral("local"), file.local },
				{ QStringLiteral("size"), static_cast<double>(file.size) }, { QStringLiteral("what"), file.what } });
		kept.append(QJsonObject { { QStringLiteral("title"), job.title }, { QStringLiteral("icon"), job.icon },
			{ QStringLiteral("folder"), job.folder }, { QStringLiteral("files"), files } });
	}
	if(kept.isEmpty())
	{
		QFile::remove(copiesPath());
		return;
	}
	QFile out(copiesPath());
	if(out.open(QIODevice::WriteOnly | QIODevice::Truncate))
		out.write(QJsonDocument(kept).toJson(QJsonDocument::Compact));
}

void LibraryController::loadUnfinishedCopies()
{
	QFile in(copiesPath());
	if(!in.open(QIODevice::ReadOnly))
		return;
	const QJsonArray kept = QJsonDocument::fromJson(in.readAll()).array();
	in.close();
	for(const QJsonValue &value : kept)
	{
		const QJsonObject saved = value.toObject();
		CopyJob job;
		job.id = QStringLiteral("library-%1").arg(nextJob_++);
		job.title = saved.value(QStringLiteral("title")).toString();
		job.icon = saved.value(QStringLiteral("icon")).toString();
		job.folder = saved.value(QStringLiteral("folder")).toString();
		qint64 total = 0;
		for(const QJsonValue &part : saved.value(QStringLiteral("files")).toArray())
		{
			const QJsonObject file = part.toObject();
			CopyItem item;
			item.remote = file.value(QStringLiteral("remote")).toString();
			item.local = file.value(QStringLiteral("local")).toString();
			item.size = static_cast<qint64>(file.value(QStringLiteral("size")).toDouble());
			item.what = file.value(QStringLiteral("what")).toString();
			total += item.size;
			job.files << item;
		}
		if(job.files.isEmpty() || job.folder.isEmpty())
			continue;
		// How far it got: whole files there, and the part of the one cut short.
		qint64 already = 0;
		for(const CopyItem &file : job.files)
		{
			const QFileInfo whole(file.local);
			const QFileInfo part(file.local + QStringLiteral(".part"));
			already += whole.exists() && whole.size() == file.size ? file.size
				: part.exists() ? std::min(file.size, part.size()) : 0;
		}
		copyRecipes_[job.id] = job;
		QVariantMap card;
		card[QStringLiteral("id")] = job.id;
		card[QStringLiteral("kind")] = QStringLiteral("copy");
		card[QStringLiteral("name")] = job.title;
		card[QStringLiteral("icon")] = job.icon;
		card[QStringLiteral("itemKind")] = QStringLiteral("package");
		card[QStringLiteral("console")] = consoleName();
		card[QStringLiteral("state")] = QStringLiteral("cancelled");
		card[QStringLiteral("interrupted")] = true;
		card[QStringLiteral("amountText")] = QString::fromStdString(humanBytes(total));
		card[QStringLiteral("percent")] = total > 0 ? 100.0 * double(already) / double(total) : 0.0;
		card[QStringLiteral("folder")] = job.folder;
		card[QStringLiteral("message")] = tr("Stopped when the app closed. ⟲ goes on from where it was.");
		jobs_.append(card);
	}
	QFile::remove(copiesPath());
	if(!jobs_.isEmpty())
		emit jobsChanged();
}

void LibraryController::cancelJob(const QString &id)
{
	std::lock_guard<std::mutex> lock(copyMutex_);
	const auto found = copyCancels_.find(id);
	if(found != copyCancels_.end())
		found->second->store(true);
}

void LibraryController::openJobFolder(const QString &id)
{
	for(const QVariant &entry : jobs_)
	{
		const QVariantMap job = entry.toMap();
		if(job.value(QStringLiteral("id")).toString() == id && !job.value(QStringLiteral("folder")).toString().isEmpty())
		{
			QDesktopServices::openUrl(QUrl::fromLocalFile(job.value(QStringLiteral("folder")).toString()));
			return;
		}
	}
}

void LibraryController::copyLoop()
{
	for(;;)
	{
		CopyJob job;
		{
			std::unique_lock<std::mutex> lock(copyMutex_);
			copyWake_.wait(lock, [this]() { return cancel_.load() || !copies_.empty(); });
			if(cancel_)
				return;
			job = copies_.front();
			copies_.pop_front();
		}
		runCopy(job);
		std::lock_guard<std::mutex> lock(copyMutex_);
		copyCancels_.erase(job.id);
	}
}

void LibraryController::runCopy(const CopyJob &job)
{
	const auto post = [this, &job](const QVariantMap &fields) {
		QMetaObject::invokeMethod(this, [this, id = job.id, fields]() { updateJob(id, fields); }, Qt::QueuedConnection);
	};
	const auto finish = [&](const QString &state, const QString &message) {
		post({ { QStringLiteral("state"), state }, { QStringLiteral("message"), message },
			{ QStringLiteral("speedText"), QString() }, { QStringLiteral("etaText"), QString() },
			{ QStringLiteral("bytesPerSecond"), 0.0 } });
		if(state == QLatin1String("error"))
			QMetaObject::invokeMethod(this, [this, title = job.title, message]() {
				emit app_->jobFinished(tr("Copy to this PC"), tr("%1: %2").arg(title, message), true);
			}, Qt::QueuedConnection);
	};
	if(job.cancel->load())
	{
		finish(QStringLiteral("cancelled"), tr("Cancelled before it started."));
		return;
	}

	qint64 total = 0;
	for(const CopyItem &file : job.files)
		total += file.size;
	// What is already here: whole files are not copied again, and a part
	// copied before goes on from where it stopped.
	qint64 already = 0;
	for(const CopyItem &file : job.files)
	{
		const QFileInfo whole(file.local);
		if(whole.exists() && whole.size() == file.size)
			already += file.size;
		else if(QFileInfo::exists(file.local + QStringLiteral(".part")))
			already += std::min(file.size, QFileInfo(file.local + QStringLiteral(".part")).size());
	}
	if(!QDir().mkpath(job.folder))
	{
		finish(QStringLiteral("error"), tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(job.folder)));
		return;
	}
	const QStorageInfo space(job.folder);
	if(space.isValid() && space.bytesAvailable() > 0 && space.bytesAvailable() < total - already)
	{
		finish(QStringLiteral("error"), tr("Not enough room in %1: it needs %2 and has %3.")
				.arg(QDir::toNativeSeparators(job.folder), QString::fromStdString(humanBytes(total - already)),
					QString::fromStdString(humanBytes(space.bytesAvailable()))));
		return;
	}
	logInfo("Library: copying \"" + job.title.toStdString() + "\" from the console to " + job.folder.toStdString());

	FtpClient ftp(job.config);
	qint64 copied = 0;
	// The speed over the last seconds, smoothed.
	double speed = 0;
	qint64 meterBytes = 0;
	auto meterTime = std::chrono::steady_clock::now();
	QElapsedTimer shown;
	shown.start();
	for(int i = 0; i < job.files.size(); ++i)
	{
		const CopyItem &file = job.files[i];
		const QString what = file.what == QLatin1String("update") ? tr("the update")
			: file.what == QLatin1String("addon") ? tr("an add-on") : tr("the game");
		const QString step = job.files.size() > 1
			? tr("Copying %1 (%2 of %3)").arg(what).arg(i + 1).arg(job.files.size()) : tr("Copying %1").arg(what);
		const QFileInfo whole(file.local);
		if(whole.exists() && whole.size() == file.size)
		{
			copied += file.size;
			continue;
		}
		const QString part = file.local + QStringLiteral(".part");
		const bool resume = QFileInfo::exists(part);
		const qint64 before = resume ? std::min(file.size, QFileInfo(part).size()) : 0;
		post({ { QStringLiteral("stageText"), step } });
		meterBytes = copied + before;
		meterTime = std::chrono::steady_clock::now();
		const FtpResult got = ftp.download(file.remote.toStdString(), QDir::toNativeSeparators(part).toStdString(),
			[&](int64_t done, int64_t) {
				const qint64 now = copied + before + done;
				const auto clock = std::chrono::steady_clock::now();
				const double elapsed = std::chrono::duration<double>(clock - meterTime).count();
				if(elapsed >= 0.5)
				{
					const double rate = static_cast<double>(now - meterBytes) / elapsed;
					speed = speed > 0 ? 0.7 * speed + 0.3 * rate : rate;
					meterBytes = now;
					meterTime = clock;
				}
				if(shown.elapsed() >= 250)
				{
					shown.restart();
					const qint64 left = std::max<qint64>(0, total - now);
					post({ { QStringLiteral("percent"), total > 0 ? 100.0 * double(now) / double(total) : 0.0 },
						{ QStringLiteral("speedText"), speed > 1
							? QString::fromStdString(humanBytes(static_cast<int64_t>(speed))) + QStringLiteral("/s")
							: QString() },
						{ QStringLiteral("etaText"), speed > 1
							? QString::fromStdString(humanDuration(static_cast<int64_t>(double(left) / speed + 0.5)))
							: QString() },
						{ QStringLiteral("amountText"), tr("%1 of %2").arg(QString::fromStdString(humanBytes(now)),
							QString::fromStdString(humanBytes(total))) },
						{ QStringLiteral("bytesPerSecond"), speed },
						{ QStringLiteral("bytesLeft"), static_cast<double>(left) },
						{ QStringLiteral("bytesTotal"), static_cast<double>(total) } });
				}
				return !job.cancel->load() && !cancel_.load();
			},
			resume);
		if(got.cancelled || job.cancel->load())
		{
			logInfo("Library: copy of \"" + job.title.toStdString() + "\" cancelled");
			finish(QStringLiteral("cancelled"), tr("Stopped. What was copied is kept: copying it again goes on from there."));
			return;
		}
		if(!got.ok)
		{
			logWarning("Library: copy of " + file.remote.toStdString() + " failed — " + got.message);
			finish(QStringLiteral("error"), tr("Could not copy %1: %2").arg(QFileInfo(file.remote).fileName(),
				translateMessage(got.message)));
			return;
		}
		QFile::remove(file.local);
		if(!QFile::rename(part, file.local))
		{
			finish(QStringLiteral("error"), tr("Could not name the copy %1.").arg(QDir::toNativeSeparators(file.local)));
			return;
		}
		copied += file.size;
	}
	logInfo("Library: \"" + job.title.toStdString() + "\" copied to " + job.folder.toStdString());
	post({ { QStringLiteral("state"), QStringLiteral("done") }, { QStringLiteral("percent"), 100.0 },
		{ QStringLiteral("speedText"), QString() }, { QStringLiteral("etaText"), QString() },
		{ QStringLiteral("amountText"), QString::fromStdString(humanBytes(total)) },
		{ QStringLiteral("message"), tr("In the folder %1").arg(QDir(job.folder).dirName()) },
		{ QStringLiteral("bytesPerSecond"), 0.0 } });
	QMetaObject::invokeMethod(this, [this, title = job.title]() {
		emit app_->jobFinished(tr("Copy to this PC"), tr("%1 is on this PC.").arg(title), false);
	}, Qt::QueuedConnection);
}

} // namespace orbislink
