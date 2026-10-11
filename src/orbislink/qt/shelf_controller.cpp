// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/shelf_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"
#include "orbislink/payloads/payload_layout.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/games_controller.h"
#include "orbislink/qt/library_controller.h"
#include "orbislink/qt/payloads_controller.h"
#include "orbislink/qt/translate_message.h"
#ifdef ORBISLINK_HAS_FPKG
#include "orbislink/fpkg/disc_scanner.h"
#endif

#include <QCollator>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>

#include <algorithm>
#include <deque>
#include <map>
#include <set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace orbislink {

using library::Kind;
using library::ShelfItem;

namespace {

const char *const kPs2Covers = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/%1.jpg";
const char *const kPs1Covers = "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/default/%1.jpg";
const QString kConsoleLibrary = QStringLiteral("console-library");

QString fileUrl(const std::string &file)
{
	return file.empty() ? QString() : QUrl::fromLocalFile(QString::fromStdString(file)).toString();
}

int groupRank(const QString &group)
{
	static const QStringList order { QStringLiteral("folders"), QStringLiteral("games"), QStringLiteral("extras"),
		QStringLiteral("payloads"), QStringLiteral("other") };
	const int rank = static_cast<int>(order.indexOf(group));
	return rank < 0 ? 9 : rank;
}

int sectionRank(const QString &section)
{
	static const QStringList order { QStringLiteral("favorites"), QStringLiteral("console"), QStringLiteral("usb"),
		QStringLiteral("pc"), QStringLiteral("custom") };
	const int rank = static_cast<int>(order.indexOf(section));
	return rank < 0 ? 9 : rank;
}

// The disc drawn where there is no picture: a CD for a PS1 game, a DVD for
// a PS2 one, a Blu-ray for the rest; folders and files have their own.
QString placeholderFor(const QString &kind, const QString &platform, bool appFolder)
{
	if(platform == QLatin1String("ps1"))
		return QStringLiteral("cd");
	if(platform == QLatin1String("ps2"))
		return QStringLiteral("dvd");
	if(kind == QLatin1String("folder") && !appFolder)
		return QStringLiteral("folder");
	if(kind == QLatin1String("payload"))
		return QStringLiteral("payload");
	if(kind == QLatin1String("archive"))
		return QStringLiteral("archive");
	return QStringLiteral("bd");
}

QString samePathKey(QString path)
{
	path = QDir::fromNativeSeparators(path);
	while(path.size() > 1 && path.endsWith(QLatin1Char('/')) && !path.endsWith(QLatin1String(":/")))
		path.chop(1);
#ifdef _WIN32
	path = path.toLower();
#endif
	return path;
}

bool removableVolume(const QStorageInfo &volume)
{
	if(!volume.isValid() || !volume.isReady() || volume.isRoot())
		return false;
#ifdef _WIN32
	const std::wstring root = QDir::toNativeSeparators(volume.rootPath()).toStdWString();
	return GetDriveTypeW(root.c_str()) == DRIVE_REMOVABLE;
#else
	// Where the desktop mounts a drive plugged in; /mnt is for mounts made
	// by hand (those are added as a drive, from the list of drives).
	const QString root = volume.rootPath();
	return root.startsWith(QLatin1String("/media/")) || root.startsWith(QLatin1String("/run/media/"))
		|| root.startsWith(QLatin1String("/Volumes/"));
#endif
}

// A drive of this PC worth offering: not the system's own mounts.
bool usefulVolume(const QStorageInfo &volume)
{
	if(!volume.isValid() || !volume.isReady() || volume.bytesTotal() < 64ll * 1024 * 1024)
		return false;
#ifndef _WIN32
	const QString root = volume.rootPath();
	for(const char *system : { "/proc", "/sys", "/dev", "/snap", "/boot", "/var", "/tmp", "/etc", "/usr", "/opt" })
		if(root.startsWith(QLatin1String(system)))
			return false;
	if(root.startsWith(QLatin1String("/run/")) && !root.startsWith(QLatin1String("/run/media/")))
		return false;
#endif
	return true;
}

QString volumeName(const QStorageInfo &volume)
{
	const QString root = QDir::toNativeSeparators(volume.rootPath());
	const QString label = volume.displayName();
	return label.isEmpty() || label == volume.rootPath() || label == root ? root
		: QStringLiteral("%1 (%2)").arg(label, root);
}

bool isDriveRoot(const QString &path)
{
	const QString clean = QDir::fromNativeSeparators(path);
	static const QRegularExpression letter(QStringLiteral("^[A-Za-z]:/?$"));
	return clean == QLatin1String("/") || letter.match(clean).hasMatch();
}

} // namespace

// ── the rows ─────────────────────────────────────────────────────────────

int ShelfRows::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ShelfRows::data(const QModelIndex &index, int role) const
{
	if(!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size()))
		return QVariant();
	const Row &row = rows_[static_cast<size_t>(index.row())];
	switch(role)
	{
		case HeaderRole: return row.header;
		case CountRole: return row.count;
		case FirstRole: return row.first;
		case CardsRole:
		{
			QVariantList cards;
			for(int i = row.first; i < row.first + row.length; ++i)
			{
				QVariantMap card = cards_[i].toMap();
				card[QStringLiteral("index")] = i;
				cards << card;
			}
			return cards;
		}
	}
	return QVariant();
}

QHash<int, QByteArray> ShelfRows::roleNames() const
{
	return { { HeaderRole, "header" }, { CountRole, "count" }, { CardsRole, "cards" }, { FirstRole, "first" } };
}

void ShelfRows::layout(bool sectioned, int columns, std::vector<Row> *rows, std::vector<int> *rowOf) const
{
	rows->clear();
	rowOf->assign(static_cast<size_t>(cards_.size()), 0);
	const int n = static_cast<int>(cards_.size());
	int i = 0;
	while(i < n)
	{
		const QString section = cards_[i].toMap().value(QStringLiteral("section")).toString();
		int end = i;
		while(end < n && (!sectioned || cards_[end].toMap().value(QStringLiteral("section")).toString() == section))
			++end;
		if(sectioned)
		{
			Row header;
			header.header = section;
			header.count = end - i;
			header.first = i;
			rows->push_back(header);
		}
		for(int start = i; start < end; start += columns)
		{
			Row row;
			row.first = start;
			row.length = std::min(columns, end - start);
			for(int k = start; k < start + row.length; ++k)
				(*rowOf)[static_cast<size_t>(k)] = static_cast<int>(rows->size());
			rows->push_back(row);
		}
		i = end;
	}
}

void ShelfRows::setCards(const QVariantList &cards, bool sectioned, int columns)
{
	columns = std::max(1, columns);
	// The same cards in the same order and shape: only what changed is told.
	bool sameShape = cards.size() == cards_.size() && sectioned == sectioned_ && columns == columns_;
	if(sameShape)
		for(int i = 0; i < cards.size(); ++i)
		{
			const QVariantMap a = cards[i].toMap();
			const QVariantMap b = cards_[i].toMap();
			if(a.value(QStringLiteral("key")) != b.value(QStringLiteral("key"))
				|| a.value(QStringLiteral("section")) != b.value(QStringLiteral("section")))
			{
				sameShape = false;
				break;
			}
		}
	if(sameShape)
	{
		for(int i = 0; i < cards.size(); ++i)
			if(cards[i] != cards_[i])
				updateCard(i, cards[i].toMap());
		return;
	}
	beginResetModel();
	cards_ = cards;
	sectioned_ = sectioned;
	columns_ = columns;
	layout(sectioned_, columns_, &rows_, &rowOf_);
	endResetModel();
}

void ShelfRows::updateCard(int index, const QVariantMap &card)
{
	if(index < 0 || index >= cards_.size())
		return;
	cards_[index] = card;
	const int row = rowOf_[static_cast<size_t>(index)];
	emit dataChanged(this->index(row), this->index(row), { CardsRole });
}

QVariantMap ShelfRows::card(int index) const
{
	return index >= 0 && index < cards_.size() ? cards_[index].toMap() : QVariantMap();
}

int ShelfRows::rowOfCard(int index) const
{
	return index >= 0 && index < static_cast<int>(rowOf_.size()) ? rowOf_[static_cast<size_t>(index)] : -1;
}

int ShelfRows::neighbour(int index, int step) const
{
	if(cards_.isEmpty())
		return -1;
	index = std::clamp(index, 0, static_cast<int>(cards_.size()) - 1);
	int row = rowOf_[static_cast<size_t>(index)];
	const int column = index - rows_[static_cast<size_t>(row)].first;
	const int direction = step < 0 ? -1 : 1;
	int remaining = std::abs(step);
	while(remaining > 0)
	{
		int next = row + direction;
		while(next >= 0 && next < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(next)].length == 0)
			next += direction;
		if(next < 0 || next >= static_cast<int>(rows_.size()))
			break;
		row = next;
		--remaining;
	}
	const Row &target = rows_[static_cast<size_t>(row)];
	return target.first + std::min(column, target.length - 1);
}

// ── the controller ───────────────────────────────────────────────────────

ShelfController::ShelfController(AppController *app, GamesController *games, LibraryController *consoleLibrary,
	PayloadsController *payloads, QObject *parent)
	: QObject(parent), app_(app), games_(games), consoleLibrary_(consoleLibrary), payloads_(payloads),
	  placeCancel_(std::make_shared<std::atomic<bool>>(false)),
	  sourcesCancel_(std::make_shared<std::atomic<bool>>(false))
{
	// Owned here, never by the interface that reads it.
	rows_.setParent(this);
	const QString cacheFolder = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
		.filePath(QStringLiteral("shelf"));
	cache_ = std::make_unique<library::ShelfCache>(QDir::toNativeSeparators(cacheFolder).toStdString());

	reloadTimer_.setSingleShot(true);
	reloadTimer_.setInterval(700);
	connect(&reloadTimer_, &QTimer::timeout, this, [this]() {
		if(!place_.sourceId.isEmpty() && !loading_)
			loadPlace();
	});
	volumesTimer_.setInterval(3000);
	connect(&volumesTimer_, &QTimer::timeout, this, &ShelfController::checkVolumes);
	sourcesTimer_.setSingleShot(true);
	sourcesTimer_.setInterval(800);
	connect(&sourcesTimer_, &QTimer::timeout, this, [this]() {
		const QSet<QString> stale = staleSources_;
		staleSources_.clear();
		for(const QString &id : stale)
			if(const Source *found = source(id))
				refreshSourcePreview(*found);
	});
	connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString &dir) {
		const QString changed = samePathKey(dir);
		const Source *current = source(place_.sourceId);
		if(current && current->where == QLatin1String("pc") && samePathKey(place_.path) == changed)
			reloadTimer_.start();
		for(const Source &item : sources_)
			if(item.where == QLatin1String("pc") && samePathKey(item.path) == changed)
			{
				staleSources_.insert(item.id);
				sourcesTimer_.start();
			}
	});

	// The console coming and going changes what can be opened.
	connect(app_, &AppController::statusChanged, this, [this]() {
		const Source *library = source(kConsoleLibrary);
		if(library && library->available != consoleUsable())
		{
			rebuildSources();
			if(consoleUsable())
				scanConsoleDrives();
		}
	});
	connect(app_, &AppController::settingsChanged, this, [this]() { rebuildSources(); });
	connect(consoleLibrary_, &LibraryController::itemsChanged, this, [this]() {
		if(const Source *library = source(kConsoleLibrary))
			refreshSourcePreview(*library);
		if(place_.sourceId == kConsoleLibrary)
			loadConsoleLibrary();
	});
	// After a move or a delete on the console, the folder open is read again.
	connect(consoleLibrary_, &LibraryController::busyChanged, this, [this]() {
		const Source *current = source(place_.sourceId);
		if(!consoleLibrary_->busy() && current && current->where == QLatin1String("console")
			&& current->id != kConsoleLibrary)
			loadPlace();
	});
	connect(consoleLibrary_, &LibraryController::finished, this, &ShelfController::notice);

	worker_ = std::thread([this]() { workLoop(); });
	seedLocations();
	checkVolumes();
	rebuildSources();
}

ShelfController::~ShelfController()
{
	placeCancel_->store(true);
	sourcesCancel_->store(true);
	{
		std::lock_guard<std::mutex> lock(jobsMutex_);
		stopping_ = true;
		urgent_.clear();
		later_.clear();
	}
	jobsWake_.notify_all();
	if(worker_.joinable())
		worker_.join();
	cache_->save();
}

void ShelfController::post(bool urgent, std::function<void()> job)
{
	{
		std::lock_guard<std::mutex> lock(jobsMutex_);
		(urgent ? urgent_ : later_).push_back(std::move(job));
	}
	jobsWake_.notify_one();
}

void ShelfController::workLoop()
{
	for(;;)
	{
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(jobsMutex_);
			jobsWake_.wait(lock, [this]() { return stopping_ || !urgent_.empty() || !later_.empty(); });
			if(stopping_)
				return;
			std::deque<std::function<void()>> &queue = urgent_.empty() ? later_ : urgent_;
			job = std::move(queue.front());
			queue.pop_front();
		}
		job();
	}
}

void ShelfController::onUi(std::function<void()> fn)
{
	QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
}

FtpClient::Config ShelfController::ftpConfig() const
{
	return app_->ftpClientConfigFor(app_->settings().consoleAddress);
}

bool ShelfController::consoleUsable() const
{
	return !app_->settings().consoleAddress.empty() && app_->canUseFtp();
}

library::DiscProbe ShelfController::probe() const
{
	library::DiscProbe probe;
#ifdef ORBISLINK_HAS_FPKG
	probe.cueTrack = [](const std::string &text) { return fpkg::cueImageName(text); };
	probe.inspect = [](const std::string &name, int64_t size,
						const std::function<bool(uint64_t, uint8_t *, size_t)> &read) {
		const fpkg::DiscInfo disc = fpkg::inspectDisc(name, static_cast<uint64_t>(std::max<int64_t>(size, 0)), read);
		library::DiscId id;
		id.platform = disc.platform;
		id.serial = disc.serial;
		id.titleId = disc.titleId;
		id.title = disc.title;
		return id;
	};
	GamesController *games = games_;
	probe.knownTitle = [games](const std::string &platform, const std::string &titleId) {
		return games->classicTitle(QString::fromStdString(platform), QString::fromStdString(titleId)).toStdString();
	};
#endif
	return probe;
}

// ── places ───────────────────────────────────────────────────────────────

void ShelfController::seedLocations()
{
	if(app_->settings().libraryLocationsSeeded)
		return;
	app_->updateSettings([](Settings &s) {
		auto add = [&s](const std::string &id, const QString &path, const std::string &where) {
			if(path.isEmpty())
				return;
			LibraryLocation place;
			place.id = id;
			place.path = where == "pc" ? QDir::toNativeSeparators(path).toStdString() : path.toStdString();
			place.where = where;
			s.libraryLocations.push_back(place);
		};
		add("preset-desktop", QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), "pc");
		add("preset-downloads", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), "pc");
		add("preset-documents", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "pc");
		add("preset-console-pkg", QString::fromStdString(s.ftpUploadDirectory.empty() ? "/data/pkg/" : s.ftpUploadDirectory),
			"console");
		s.libraryLocationsSeeded = true;
	});
}

void ShelfController::rebuildSources()
{
	const Settings &settings = app_->settings();
	const bool consoleSet = !settings.consoleAddress.empty();
	const bool usable = consoleUsable();
	std::vector<Source> sources;
	if(consoleSet)
	{
		Source library;
		library.id = kConsoleLibrary;
		library.name = tr("Console library");
		library.path = QString::fromLatin1(library::kFolderName);
		library.kind = QStringLiteral("console");
		library.where = QStringLiteral("console");
		library.available = usable;
		sources.push_back(library);
		for(const QString &drive : consoleDrives_)
		{
			Source item;
			const QString name = drive.mid(drive.lastIndexOf(QLatin1Char('/')) + 1);
			item.id = QStringLiteral("console-") + name;
			item.name = name.startsWith(QLatin1String("usb")) ? tr("Console USB (%1)").arg(name)
															   : tr("Console extended storage (%1)").arg(name);
			item.path = drive;
			item.kind = QStringLiteral("console");
			item.where = QStringLiteral("console");
			item.available = usable;
			sources.push_back(item);
		}
	}
	for(const QString &root : pcDrives_)
	{
		bool kept = false;
		for(const LibraryLocation &place : settings.libraryLocations)
			kept = kept || samePathKey(QString::fromStdString(place.path)) == samePathKey(root);
		if(kept)
			continue;
		Source item;
		item.id = QStringLiteral("usb:") + root;
		const QStorageInfo volume(root);
		item.name = QStringLiteral("USB — ") + volumeName(volume);
		item.path = QDir::toNativeSeparators(root);
		item.kind = QStringLiteral("usb");
		item.where = QStringLiteral("pc");
		sources.push_back(item);
	}
	for(const LibraryLocation &place : settings.libraryLocations)
	{
		Source item;
		item.id = QString::fromStdString(place.id);
		item.path = QString::fromStdString(place.path);
		item.where = QString::fromStdString(place.where);
		item.favorite = place.favorite;
		item.inSearch = place.inSearch;
		item.location = true;
		const bool console = item.where == QLatin1String("console");
		if(console && !consoleSet)
			continue;
		item.available = console ? usable : QFileInfo(item.path).isDir();
		if(console)
			item.kind = QStringLiteral("console");
		else if(item.id.startsWith(QLatin1String("preset-")))
			item.kind = QStringLiteral("pc");
		else if(isDriveRoot(item.path))
			item.kind = QStringLiteral("drive");
		else if(pcDrives_.contains(QDir::fromNativeSeparators(QStorageInfo(item.path).rootPath())))
			item.kind = QStringLiteral("usb");
		else
			item.kind = QStringLiteral("custom");
		item.name = QString::fromStdString(place.name);
		if(item.name.isEmpty())
		{
			if(item.id == QLatin1String("preset-desktop"))
				item.name = tr("Desktop");
			else if(item.id == QLatin1String("preset-downloads"))
				item.name = tr("Downloads");
			else if(item.id == QLatin1String("preset-documents"))
				item.name = tr("Documents");
			else if(item.id == QLatin1String("preset-console-pkg"))
				item.name = QStringLiteral("data/pkg");
			else if(isDriveRoot(item.path))
				item.name = tr("This PC — %1").arg(QDir::toNativeSeparators(item.path));
			else
			{
				QString clean = QDir::fromNativeSeparators(item.path);
				while(clean.endsWith(QLatin1Char('/')) && clean.size() > 1)
					clean.chop(1);
				item.name = clean.mid(clean.lastIndexOf(QLatin1Char('/')) + 1);
			}
		}
		sources.push_back(item);
	}

	// What was there and is no longer.
	const Source *before = source(place_.sourceId);
	const bool wasAvailable = before && before->available;
	const QString beforeName = before ? before->name : QString();
	sources_ = sources;
	emit locationsChanged();
	for(const Source &item : sources_)
		if(item.available && !sourceLooks_.contains(item.id) && active_)
			refreshSourcePreview(item);
	if(!place_.sourceId.isEmpty())
	{
		const Source *now = source(place_.sourceId);
		if(!now || !now->available)
			setGone(true, now ? tr("%1 is not there any more: a drive taken out, or the console gone.").arg(now->name)
				: place_.sourceId.startsWith(QLatin1String("usb:"))
				? tr("%1 was taken out. Plug it in again and it opens here.").arg(beforeName)
				: tr("This place was taken off the library."));
		else if(!wasAvailable && gone_)
			loadPlace();
		emit placeChanged();
	}
	else
		showCards();
}

const ShelfController::Source *ShelfController::source(const QString &id) const
{
	for(const Source &item : sources_)
		if(item.id == id)
			return &item;
	return nullptr;
}

QVariantMap ShelfController::sourceCard(const Source &item) const
{
	const QVariantMap look = sourceLooks_.value(item.id);
	QVariantMap card;
	card[QStringLiteral("card")] = QStringLiteral("source");
	card[QStringLiteral("key")] = QStringLiteral("src:") + item.id;
	card[QStringLiteral("id")] = item.id;
	card[QStringLiteral("title")] = item.name;
	card[QStringLiteral("name")] = item.name;
	card[QStringLiteral("path")] = item.where == QLatin1String("pc") ? QDir::toNativeSeparators(item.path) : item.path;
	card[QStringLiteral("kind")] = item.kind;
	card[QStringLiteral("where")] = item.where;
	card[QStringLiteral("section")] = item.favorite ? QStringLiteral("favorites")
		: item.kind == QLatin1String("drive") ? QStringLiteral("pc") : item.kind;
	card[QStringLiteral("available")] = item.available;
	card[QStringLiteral("favorite")] = item.favorite;
	card[QStringLiteral("inSearch")] = item.inSearch;
	card[QStringLiteral("location")] = item.location;
	card[QStringLiteral("count")] = look.value(QStringLiteral("count"), -1);
	card[QStringLiteral("previews")] = look.value(QStringLiteral("previews"));
	return card;
}

QVariantList ShelfController::locations() const
{
	QVariantList out;
	for(const Source &item : sources_)
	{
		if(!item.location)
			continue;
		QVariantMap entry;
		entry[QStringLiteral("id")] = item.id;
		entry[QStringLiteral("shownName")] = item.name;
		entry[QStringLiteral("path")] = item.where == QLatin1String("pc") ? QDir::toNativeSeparators(item.path) : item.path;
		entry[QStringLiteral("where")] = item.where;
		entry[QStringLiteral("kind")] = item.kind;
		entry[QStringLiteral("favorite")] = item.favorite;
		entry[QStringLiteral("inSearch")] = item.inSearch;
		entry[QStringLiteral("available")] = item.available;
		for(const LibraryLocation &place : app_->settings().libraryLocations)
			if(QString::fromStdString(place.id) == item.id)
				entry[QStringLiteral("name")] = QString::fromStdString(place.name);
		out << entry;
	}
	return out;
}

QVariantList ShelfController::volumes() const
{
	QVariantList out;
	for(const QStorageInfo &volume : QStorageInfo::mountedVolumes())
	{
		if(!usefulVolume(volume))
			continue;
		out << QVariantMap { { QStringLiteral("path"), QDir::toNativeSeparators(volume.rootPath()) },
			{ QStringLiteral("name"), volumeName(volume) },
			{ QStringLiteral("removable"), removableVolume(volume) } };
	}
	return out;
}

void ShelfController::checkVolumes()
{
	QStringList roots;
	for(const QStorageInfo &volume : QStorageInfo::mountedVolumes())
		if(removableVolume(volume))
			roots << QDir::fromNativeSeparators(volume.rootPath());
	roots.sort();
	if(roots == pcDrives_)
		return;
	pcDrives_ = roots;
	rebuildSources();
}

void ShelfController::scanConsoleDrives()
{
	const FtpClient::Config config = ftpConfig();
	post(false, [this, config]() {
		FtpClient ftp(config);
		std::vector<FtpEntry> mounts;
		QStringList drives;
		if(ftp.list("/mnt", &mounts).ok)
			for(const FtpEntry &mount : mounts)
			{
				static const QRegularExpression drive(QStringLiteral("^(usb[0-7]|ext[01])$"));
				const QString name = QString::fromStdString(mount.name);
				if(!mount.isDirectory || !drive.match(name).hasMatch())
					continue;
				// A drive point with nothing in it has no drive behind it.
				std::vector<FtpEntry> inside;
				if(ftp.list("/mnt/" + mount.name, &inside).ok && !inside.empty())
					drives << QStringLiteral("/mnt/") + name;
			}
		drives.sort();
		onUi([this, drives]() {
			if(drives == consoleDrives_)
				return;
			consoleDrives_ = drives;
			rebuildSources();
		});
	});
}

void ShelfController::refreshSourcePreview(const Source &item)
{
	if(item.id == kConsoleLibrary)
	{
		if(!consoleLibrary_->scanned() && !consoleLibrary_->scanning() && consoleUsable() && active_)
			consoleLibrary_->refresh();
		QVariantList previews;
		const QVariantList items = consoleLibrary_->items();
		for(const QVariant &entry : items)
		{
			const QVariantMap thing = entry.toMap();
			QString picture = thing.value(QStringLiteral("icon")).toString();
			if(picture.isEmpty())
				picture = fileUrl(cache_->pictureFor(thing.value(QStringLiteral("titleId")).toString().toStdString()));
			if(picture.isEmpty() || previews.size() >= 4)
				continue;
			previews << QVariantMap { { QStringLiteral("picture"), picture },
				{ QStringLiteral("title"), thing.value(QStringLiteral("title")) } };
		}
		sourceLooks_[item.id] = QVariantMap { { QStringLiteral("count"),
			consoleLibrary_->scanned() ? items.size() : -1 }, { QStringLiteral("previews"), previews } };
		if(place_.sourceId.isEmpty())
			showCards();
		return;
	}
	if(!item.available)
		return;
	const Source copy = item;
	const FtpClient::Config config = ftpConfig();
	const library::DiscProbe discProbe = probe();
	const std::string place = copy.where == QLatin1String("console")
		? "console:" + app_->settings().consoleAddress : std::string("pc");
	const bool ps5 = app_->installsFromConsole();
	std::shared_ptr<std::atomic<bool>> cancel = sourcesCancel_;
	post(false, [this, copy, config, discProbe, place, ps5, cancel]() {
		std::unique_ptr<FtpClient> ftp;
		std::unique_ptr<library::FileSource> files;
		if(copy.where == QLatin1String("console"))
		{
			ftp = std::make_unique<FtpClient>(config);
			files = std::make_unique<library::FtpSource>(*ftp);
		}
		else
			files = std::make_unique<library::LocalSource>();
		std::vector<library::ShelfEntry> entries;
		if(!files->list(copy.path.toStdString(), &entries, nullptr))
		{
			onUi([this, copy]() {
				sourceLooks_[copy.id] = QVariantMap { { QStringLiteral("count"), 0 } };
				if(place_.sourceId.isEmpty())
					showCards();
			});
			return;
		}
		std::map<std::string, std::string> trackOf;
		const std::set<std::string> tracks = library::cueTracks(*files, entries, discProbe, &trackOf);
		const library::FolderIndex folder(entries);
		auto look = [](const ShelfItem &thing) {
			return QVariantMap { { QStringLiteral("picture"), fileUrl(thing.pictureFile) },
				{ QStringLiteral("title"), QString::fromStdString(thing.title) },
				{ QStringLiteral("placeholder"), placeholderFor(QString::fromLatin1(library::kindName(thing.kind)),
					QString::fromStdString(thing.platform), thing.appFolder) } };
		};
		auto isGame = [](const ShelfItem &thing) {
			return !thing.pictureFile.empty() || (thing.group == "games" && !thing.titleId.empty());
		};
		QVariantList cards;
		QVariantList previews;
		const library::ShelfEntry *firstFolder = nullptr;
		ShelfItem folderTile;
		int count = 0;
		for(const library::ShelfEntry &entry : entries)
		{
			if(cancel->load())
				return;
			if(!library::shownOnShelf(entry) || (!entry.directory && tracks.count(toLower(entry.name))))
				continue;
			++count;
			ShelfItem thing = library::quickItem(entry);
			const std::string key = library::ShelfCache::keyOf(place, thing);
			// What was not read before is read now, but only so much of it:
			// the rest waits until the place is opened.
			if(!cache_->find(key, &thing) && count <= 48)
			{
				const auto track = trackOf.find(entry.name);
				thing = library::identify(*files, entry, folder, discProbe,
					track == trackOf.end() ? std::string() : track->second);
				findCover(&thing, cancel.get());
				cache_->store(key, &thing);
			}
			else if(thing.pictureFile.empty())
				findCover(&thing, nullptr);
			cards << cardFor(thing, copy, QSet<QString>(), ps5);
			if(thing.directory && !thing.appFolder)
			{
				if(!firstFolder)
				{
					firstFolder = &entry;
					folderTile = thing;
				}
			}
			else if(isGame(thing) && previews.size() < 3)
				previews << look(thing);
		}
		// Its folders show as a tile of their own, the last of the three; too
		// few games at the top (a drive with its games in folders) and the
		// folders' own games fill in, as on the console's folders.
		const int room = firstFolder ? 2 : 3;
		while(previews.size() > room)
			previews.removeLast();
		for(const library::ShelfEntry &entry : entries)
		{
			if(previews.size() >= room || cancel->load())
				break;
			if(!entry.directory || !library::shownOnShelf(entry))
				continue;
			const std::string key = "preview|" + place + "|" + entry.path + "|" + std::to_string(entry.modified);
			library::FolderPreview preview;
			if(!cache_->findPreview(key, &preview))
			{
				preview = library::previewFolder(*files, entry.path, discProbe, 3, 12, cancel.get());
				if(cancel->load())
					return;
				for(ShelfItem &picture : preview.pictures)
					findCover(&picture, cancel.get());
				cache_->storePreview(key, &preview);
			}
			for(ShelfItem &picture : preview.pictures)
			{
				findCover(&picture, nullptr);
				if(previews.size() < room && !(picture.directory && !picture.appFolder) && isGame(picture))
					previews << look(picture);
			}
		}
		if(firstFolder)
			previews << look(folderTile);

		// For the search: what is in the folders below, by name (and in full
		// where it was read before) — two levels down, one on the console,
		// none under a whole drive, and only so much of it.
		const int depth = copy.kind == QLatin1String("drive") ? 0 : copy.where == QLatin1String("console") ? 1 : 2;
		std::deque<std::pair<std::string, int>> below;
		for(const library::ShelfEntry &entry : entries)
			if(entry.directory && library::shownOnShelf(entry) && depth > 0)
				below.emplace_back(entry.path, 1);
		int budget = 1500;
		while(!below.empty() && budget > 0 && !cancel->load())
		{
			const auto [dir, level] = below.front();
			below.pop_front();
			std::vector<library::ShelfEntry> inside;
			if(!files->list(dir, &inside, nullptr))
				continue;
			for(const library::ShelfEntry &entry : inside)
			{
				if(!library::shownOnShelf(entry) || --budget <= 0)
					continue;
				ShelfItem thing = library::quickItem(entry);
				cache_->find(library::ShelfCache::keyOf(place, thing), &thing);
				findCover(&thing, nullptr);
				cards << cardFor(thing, copy, QSet<QString>(), ps5);
				if(entry.directory && level < depth)
					below.emplace_back(entry.path, level + 1);
			}
		}
		cache_->save();
		onUi([this, copy, cards, previews, count]() {
			sourceLooks_[copy.id] = QVariantMap { { QStringLiteral("count"), count },
				{ QStringLiteral("previews"), previews } };
			searchIndex_[copy.id] = cards;
			if(place_.sourceId.isEmpty())
				showCards();
		});
	});
}

// ── reading a folder ─────────────────────────────────────────────────────

QSet<QString> ShelfController::installedOnConsole(FtpClient *ftp)
{
	{
		std::lock_guard<std::mutex> lock(consoleMutex_);
		if(installedAge_.isValid() && installedAge_.elapsed() < 60000)
			return installed_;
	}
	QSet<QString> ids;
	std::vector<FtpEntry> apps;
	if(ftp->list("/user/app", &apps).ok)
		for(const FtpEntry &app : apps)
			ids.insert(QString::fromStdString(app.name).toUpper());
	std::lock_guard<std::mutex> lock(consoleMutex_);
	installed_ = ids;
	installedAge_.start();
	return ids;
}

void ShelfController::findCover(ShelfItem *item, const std::atomic<bool> *cancel)
{
	if(!item->pictureFile.empty() || !item->picture.empty())
		return;
	// 1. One the app already has for this game.
	for(const std::string &id : { item->titleId, item->serial })
	{
		const std::string file = cache_->pictureFor(id);
		if(!file.empty())
		{
			item->pictureFile = file;
			item->pictureFrom = "app";
			return;
		}
	}
	// 4. The cover collection, by the disc's serial (the same one the
	// converter takes a PS2 game's cover from).
	if(!cancel || item->serial.empty() || (item->platform != "ps1" && item->platform != "ps2"))
		return;
	const QString serial = QString::fromStdString(item->serial);
	{
		std::lock_guard<std::mutex> lock(consoleMutex_);
		if(noCover_.contains(serial))
			return;
	}
	HttpClient http(15000);
	HttpClient::FetchOptions options;
	options.headers = { "User-Agent: OrbisLink" };
	options.cancel = cancel;
	const QString url = QString::fromLatin1(item->platform == "ps2" ? kPs2Covers : kPs1Covers).arg(serial);
	const HttpResponse reply = http.fetch(url.toStdString(), options);
	const std::string &body = reply.body;
	const bool picture = body.size() > 4
		&& ((static_cast<uint8_t>(body[0]) == 0xFF && static_cast<uint8_t>(body[1]) == 0xD8)
			|| (static_cast<uint8_t>(body[0]) == 0x89 && body[1] == 'P'));
	if(!reply.transportOk || reply.status != 200 || !picture)
	{
		if(reply.transportOk && !(cancel && cancel->load()))
		{
			std::lock_guard<std::mutex> lock(consoleMutex_);
			noCover_.insert(serial);
		}
		return;
	}
	const std::string file = cache_->keep(std::vector<uint8_t>(body.begin(), body.end()),
		static_cast<uint8_t>(body[0]) == 0x89 ? ".png" : ".jpg");
	if(file.empty())
		return;
	cache_->remember(item->serial, file);
	cache_->remember(item->titleId, file);
	item->pictureFile = file;
	item->pictureFrom = "collection";
}

QVariantMap ShelfController::cardFor(const ShelfItem &item, const Source &from, const QSet<QString> &installed,
	bool ps5) const
{
	const bool console = from.where == QLatin1String("console");
	const QString kind = QString::fromLatin1(library::kindName(item.kind));
	const QString group = QString::fromStdString(item.group);
	const QString platform = QString::fromStdString(item.platform);
	const QString titleId = QString::fromStdString(item.titleId);
	const bool plainFolder = item.directory && !item.appFolder;
	QVariantMap card;
	card[QStringLiteral("card")] = plainFolder ? QStringLiteral("folder") : QStringLiteral("title");
	card[QStringLiteral("key")] = QString::fromStdString(item.path);
	card[QStringLiteral("path")] = QString::fromStdString(item.path);
	card[QStringLiteral("name")] = QString::fromStdString(item.name);
	card[QStringLiteral("title")] = QString::fromStdString(item.title);
	card[QStringLiteral("ext")] = QString::fromStdString(item.ext);
	card[QStringLiteral("kind")] = kind;
	card[QStringLiteral("group")] = group;
	card[QStringLiteral("section")] = group;
	card[QStringLiteral("app")] = item.appFolder;
	card[QStringLiteral("picture")] = fileUrl(item.pictureFile);
	card[QStringLiteral("pictureFrom")] = QString::fromStdString(item.pictureFrom);
	card[QStringLiteral("placeholder")] = placeholderFor(kind, platform, item.appFolder);
	card[QStringLiteral("platform")] = platform;
	card[QStringLiteral("titleId")] = titleId;
	card[QStringLiteral("serial")] = QString::fromStdString(item.serial);
	card[QStringLiteral("version")] = QString::fromStdString(item.version);
	card[QStringLiteral("category")] = QString::fromStdString(item.category);
	card[QStringLiteral("size")] = static_cast<double>(item.size);
	card[QStringLiteral("sizeText")] = item.directory ? QString() : QString::fromStdString(humanBytes(item.size));
	card[QStringLiteral("modified")] = static_cast<double>(item.modified);
	card[QStringLiteral("dateText")] = item.modified > 0
		? QLocale().toString(QDateTime::fromSecsSinceEpoch(item.modified), QLocale::ShortFormat) : QString();
	card[QStringLiteral("installed")] = group == QLatin1String("games") && !titleId.isEmpty()
		&& installed.contains(titleId.toUpper());
	card[QStringLiteral("readable")] = item.readable;
	card[QStringLiteral("sourceId")] = from.id;
	card[QStringLiteral("sourceName")] = from.name;
	card[QStringLiteral("where")] = from.where;
	card[QStringLiteral("track")] = QString::fromStdString(item.trackPath);
	card[QStringLiteral("count")] = -1;

	QStringList actions;
	QString note;
	const bool canConvert =
#ifdef ORBISLINK_HAS_FPKG
		true;
#else
		false;
#endif
	if(plainFolder)
		actions << QStringLiteral("open");
	else if(console)
	{
		switch(item.kind)
		{
			case Kind::Package:
				if(!item.readable)
					note = tr("Not a package this can read.");
				else if(ps5)
					actions << QStringLiteral("install");
				else
					note = tr("On a PS4 a package on the console installs from Debug Settings → Package Installer.");
				break;
			case Kind::Disc:
				if(item.readable && canConvert && !platform.isEmpty())
					actions << QStringLiteral("convert") << QStringLiteral("convertOnly");
				else
					note = tr("Not a PS1/PS2 disc this can read.");
				break;
			case Kind::Image:
			case Kind::Folder:
				if(ps5)
					actions << QStringLiteral("mount");
				else
					note = tr("A PS5 game: for a PS5 with ShadowMountPlus.");
				break;
			case Kind::Payload:
				actions << QStringLiteral("run");
				card[QStringLiteral("port")] = payloads::loaderPort(ps5 ? payloads::Kind::Ps5 : payloads::Kind::Ps4, item.name);
				break;
			case Kind::Archive:
				note = tr("An archive: unpack it on the PC first; the console does not open them.");
				break;
			case Kind::Other:
				break;
		}
		if(!item.directory)
			actions << QStringLiteral("delete");
	}
	else
	{
		switch(item.kind)
		{
			case Kind::Package:
				if(item.readable)
					actions << QStringLiteral("install") << QStringLiteral("send");
				else
					note = tr("Not a package this can read.");
				break;
			case Kind::Disc:
				if(item.readable && canConvert && !platform.isEmpty())
					actions << QStringLiteral("convert") << QStringLiteral("convertOnly");
				else if(!item.readable)
					note = tr("Not a PS1/PS2 disc this can read.");
				actions << QStringLiteral("sendFile");
				break;
			case Kind::Image:
			case Kind::Folder:
				note = tr("A PS5 game: copy it to /data/homebrew on the PS5 (Files tab), where ShadowMountPlus finds it.");
				break;
			case Kind::Payload:
				actions << QStringLiteral("runPayload");
				break;
			case Kind::Archive:
				note = tr("An archive: unpack it on the PC first; the console does not open them.");
				break;
			case Kind::Other:
				break;
		}
		actions << QStringLiteral("reveal");
	}
	card[QStringLiteral("actions")] = actions;
	card[QStringLiteral("note")] = note;
	return card;
}

void ShelfController::openPlace(const QString &sourceId, const QString &path)
{
	const Source *item = source(sourceId);
	if(!item)
		return;
	if(!item->available)
	{
		emit notice(item->where == QLatin1String("console")
				? tr("%1 is on the console, whose FTP is not answering.").arg(item->name)
				: tr("%1 is not there now.").arg(item->name), true);
		return;
	}
	place_.sourceId = sourceId;
	place_.root = item->path;
	place_.path = path.isEmpty() ? item->path : path;
	all_.clear();
	if(!query_.isEmpty())
		query_.clear();
	emit placeChanged();
	watchPlace();
	loadPlace();
}

void ShelfController::watchPlace()
{
	const QStringList watched = watcher_.directories();
	if(!watched.isEmpty())
		watcher_.removePaths(watched);
	QStringList wanted;
	const Source *current = source(place_.sourceId);
	if(current && current->where == QLatin1String("pc") && QFileInfo(place_.path).isDir())
		wanted << place_.path;
	for(const Source &item : sources_)
		if(item.where == QLatin1String("pc") && item.available && QFileInfo(item.path).isDir())
			wanted << item.path;
	wanted.removeDuplicates();
	if(!wanted.isEmpty())
		watcher_.addPaths(wanted);
}

void ShelfController::setGone(bool gone, const QString &message)
{
	if(gone == gone_ && message == message_)
		return;
	gone_ = gone;
	message_ = message;
	if(gone)
	{
		all_.clear();
		showCards();
		setLoading(false);
	}
	emit stateChanged();
}

void ShelfController::setLoading(bool loading)
{
	if(loading_ == loading)
		return;
	loading_ = loading;
	emit stateChanged();
	// Done: what moved while it filled in takes its place.
	if(!loading)
		showCards();
}

void ShelfController::loadPlace()
{
	const Source *item = source(place_.sourceId);
	if(!item)
	{
		home();
		return;
	}
	placeCancel_->store(true);
	placeCancel_ = std::make_shared<std::atomic<bool>>(false);
	const uint64_t generation = ++placeGeneration_;
	gone_ = false;
	message_.clear();
	emit stateChanged();
	if(item->id == kConsoleLibrary)
	{
		loadConsoleLibrary();
		return;
	}
	if(!item->available)
	{
		setGone(true, tr("%1 is not there any more: a drive taken out, or the console gone.").arg(item->name));
		return;
	}
	setLoading(true);
	const Source copy = *item;
	const std::string path = place_.path.toStdString();
	const FtpClient::Config config = ftpConfig();
	const bool usable = consoleUsable();
	const bool ps5 = app_->installsFromConsole();
	const library::DiscProbe discProbe = probe();
	const std::string place = copy.where == QLatin1String("console")
		? "console:" + app_->settings().consoleAddress : std::string("pc");
	std::shared_ptr<std::atomic<bool>> cancel = placeCancel_;
	post(true, [this, copy, path, config, usable, ps5, discProbe, place, cancel, generation]() {
		auto current = [this, generation]() { return generation == placeGeneration_.load(); };
		std::unique_ptr<FtpClient> ftp;
		std::unique_ptr<library::FileSource> files;
		if(copy.where == QLatin1String("console"))
		{
			ftp = std::make_unique<FtpClient>(config);
			files = std::make_unique<library::FtpSource>(*ftp);
		}
		else
			files = std::make_unique<library::LocalSource>();
		std::vector<library::ShelfEntry> entries;
		std::string error;
		if(!files->list(path, &entries, &error))
		{
			const QString why = translateMessage(error);
			onUi([this, why, current]() {
				if(!current())
					return;
				all_.clear();
				message_ = tr("This folder cannot be read: %1").arg(why);
				showCards();
				setLoading(false);
				emit stateChanged();
			});
			return;
		}
		std::map<std::string, std::string> trackOf;
		const std::set<std::string> tracks = library::cueTracks(*files, entries, discProbe, &trackOf);
		const library::FolderIndex folder(entries);
		std::vector<const library::ShelfEntry *> shown;
		for(const library::ShelfEntry &entry : entries)
			if(library::shownOnShelf(entry) && !(!entry.directory && tracks.count(toLower(entry.name))))
				shown.push_back(&entry);

		// What it is from its name (or from what was found before), at once.
		std::vector<ShelfItem> items;
		std::vector<bool> known;
		QVariantList quick;
		for(const library::ShelfEntry *entry : shown)
		{
			ShelfItem thing = library::quickItem(*entry);
			const bool found = cache_->find(library::ShelfCache::keyOf(place, thing), &thing);
			known.push_back(found);
			quick << cardFor(thing, copy, QSet<QString>(), ps5);
			items.push_back(std::move(thing));
		}
		onUi([this, quick, current]() {
			if(!current())
				return;
			all_ = quick;
			showCards();
		});

		// Then the rest, a few at a time as it is read.
		QSet<QString> installed;
		if(usable)
		{
			if(ftp)
				installed = installedOnConsole(ftp.get());
			else
			{
				FtpClient other(config);
				installed = installedOnConsole(&other);
			}
		}
		QVariantList batch;
		QElapsedTimer flushed;
		flushed.start();
		auto flush = [&]() {
			if(batch.isEmpty())
				return;
			onUi([this, batch, current]() {
				if(current())
					updateCards(batch);
			});
			batch.clear();
			flushed.restart();
		};
		for(size_t i = 0; i < shown.size(); ++i)
		{
			if(cancel->load())
				return;
			ShelfItem &thing = items[i];
			if(!known[i])
			{
				const auto track = trackOf.find(shown[i]->name);
				thing = library::identify(*files, *shown[i], folder, discProbe,
					track == trackOf.end() ? std::string() : track->second);
				findCover(&thing, cancel.get());
				cache_->store(library::ShelfCache::keyOf(place, thing), &thing);
			}
			else if(thing.pictureFile.empty() && thing.group == "games")
			{
				findCover(&thing, cancel.get());
				if(!thing.pictureFile.empty())
					cache_->store(library::ShelfCache::keyOf(place, thing), &thing);
			}
			batch << cardFor(thing, copy, installed, ps5);
			if(flushed.elapsed() > 200)
				flush();
		}
		flush();

		// Each folder's look: a few covers of what is in it.
		for(size_t i = 0; i < shown.size(); ++i)
		{
			if(cancel->load())
				return;
			const ShelfItem &thing = items[i];
			if(!thing.directory || thing.appFolder)
				continue;
			const std::string key = "preview|" + place + "|" + thing.path + "|" + std::to_string(thing.modified);
			library::FolderPreview preview;
			if(!cache_->findPreview(key, &preview))
			{
				preview = library::previewFolder(*files, thing.path, discProbe, 3, 12, cancel.get());
				if(cancel->load())
					return;
				for(ShelfItem &picture : preview.pictures)
					findCover(&picture, cancel.get());
				cache_->storePreview(key, &preview);
			}
			// Covers found since (a disc's, from the collection) take their place.
			for(ShelfItem &picture : preview.pictures)
				findCover(&picture, nullptr);
			QVariantMap card = cardFor(thing, copy, installed, ps5);
			QVariantList previews;
			for(const ShelfItem &picture : preview.pictures)
				previews << QVariantMap { { QStringLiteral("picture"), fileUrl(picture.pictureFile) },
					{ QStringLiteral("title"), QString::fromStdString(picture.title) },
					{ QStringLiteral("placeholder"), placeholderFor(QString::fromLatin1(library::kindName(picture.kind)),
						QString::fromStdString(picture.platform), picture.appFolder) } };
			card[QStringLiteral("previews")] = previews;
			card[QStringLiteral("count")] = preview.count;
			batch << card;
			if(flushed.elapsed() > 200)
				flush();
		}
		flush();
		cache_->save();
		onUi([this, current, copy]() {
			if(!current())
				return;
			// What was seen here can be found from the library's search.
			QVariantList &seen = searchIndex_[copy.id];
			QSet<QString> keys;
			for(const QVariant &card : seen)
				keys.insert(card.toMap().value(QStringLiteral("key")).toString());
			for(const QVariant &card : all_)
				if(!keys.contains(card.toMap().value(QStringLiteral("key")).toString()) && seen.size() < 5000)
					seen << card;
			setLoading(false);
		});
	});
}

void ShelfController::loadConsoleLibrary()
{
	if(!consoleUsable())
	{
		setGone(true, tr("The console's FTP is not answering: the library is read over it."));
		return;
	}
	if(!consoleLibrary_->scanned() && !consoleLibrary_->scanning())
		consoleLibrary_->refresh();
	const Source *library = source(kConsoleLibrary);
	if(!library)
		return;
	const bool ps5 = app_->installsFromConsole();
	QVariantList cards;
	std::vector<ShelfItem> discs;
	for(const QVariant &entry : consoleLibrary_->items())
	{
		const QVariantMap thing = entry.toMap();
		ShelfItem item;
		item.path = thing.value(QStringLiteral("path")).toString().toStdString();
		item.name = thing.value(QStringLiteral("name")).toString().toStdString();
		item.directory = thing.value(QStringLiteral("folder")).toBool();
		item.kind = library::kindOf(item.name, item.directory, static_cast<int64_t>(thing.value(QStringLiteral("size")).toDouble()));
		item.appFolder = item.directory;
		item.size = static_cast<int64_t>(thing.value(QStringLiteral("size")).toDouble());
		item.ext = item.directory ? std::string() : fileExtensionLower(item.name);
		item.title = thing.value(QStringLiteral("title")).toString().toStdString();
		item.titleId = thing.value(QStringLiteral("titleId")).toString().toStdString();
		item.version = thing.value(QStringLiteral("version")).toString().toStdString();
		item.platform = thing.value(QStringLiteral("platform")).toString().toStdString();
		item.serial = thing.value(QStringLiteral("serial")).toString().toStdString();
		item.category = thing.value(QStringLiteral("category")).toString().toStdString();
		item.group = thing.value(QStringLiteral("group")).toString().toStdString();
		item.readable = thing.value(QStringLiteral("note")).toString().isEmpty()
			|| !thing.value(QStringLiteral("action")).toString().isEmpty();
		findCover(&item, nullptr);
		QVariantMap card = cardFor(item, *library, QSet<QString>(), ps5);
		const QString icon = thing.value(QStringLiteral("icon")).toString();
		if(!icon.isEmpty())
			card[QStringLiteral("picture")] = icon;
		card[QStringLiteral("installed")] = thing.value(QStringLiteral("installed"));
		if(!thing.value(QStringLiteral("note")).toString().isEmpty())
			card[QStringLiteral("note")] = thing.value(QStringLiteral("note"));
		if(thing.contains(QStringLiteral("port")))
			card[QStringLiteral("port")] = thing.value(QStringLiteral("port"));
		cards << card;
		if(icon.isEmpty() && item.pictureFile.empty() && !item.serial.empty())
			discs.push_back(item);
	}
	all_ = cards;
	searchIndex_[kConsoleLibrary] = cards;
	showCards();
	setLoading(consoleLibrary_->scanning());
	// The discs' covers come from the collection, as they arrive.
	if(discs.empty())
		return;
	const Source copy = *library;
	const uint64_t generation = placeGeneration_.load();
	std::shared_ptr<std::atomic<bool>> cancel = placeCancel_;
	post(true, [this, discs, copy, ps5, cancel, generation]() {
		QVariantList found;
		for(ShelfItem item : discs)
		{
			if(cancel->load())
				return;
			findCover(&item, cancel.get());
			if(item.pictureFile.empty())
				continue;
			QVariantMap card;
			card[QStringLiteral("key")] = QString::fromStdString(item.path);
			card[QStringLiteral("picture")] = fileUrl(item.pictureFile);
			found << card;
		}
		cache_->save();
		onUi([this, found, generation]() {
			if(generation != placeGeneration_.load())
				return;
			QVariantList merged;
			for(const QVariant &entry : found)
			{
				const QVariantMap patch = entry.toMap();
				for(const QVariant &card : all_)
					if(card.toMap().value(QStringLiteral("key")) == patch.value(QStringLiteral("key")))
					{
						QVariantMap full = card.toMap();
						full[QStringLiteral("picture")] = patch.value(QStringLiteral("picture"));
						merged << full;
					}
			}
			updateCards(merged);
		});
	});
}

// ── what is shown ────────────────────────────────────────────────────────

bool ShelfController::matches(const QVariantMap &card) const
{
	if(card.value(QStringLiteral("card")).toString() == QLatin1String("source"))
		return query_.isEmpty() || card.value(QStringLiteral("name")).toString().contains(query_, Qt::CaseInsensitive)
			|| card.value(QStringLiteral("path")).toString().contains(query_, Qt::CaseInsensitive);
	const QString group = card.value(QStringLiteral("group")).toString();
	if(filter_ == QLatin1String("games") && group != QLatin1String("games"))
		return false;
	if(filter_ == QLatin1String("pkg") && card.value(QStringLiteral("ext")).toString() != QLatin1String(".pkg"))
		return false;
	if(filter_ == QLatin1String("folders") && card.value(QStringLiteral("card")).toString() != QLatin1String("folder"))
		return false;
	if(filter_ == QLatin1String("other")
		&& (group == QLatin1String("games") || card.value(QStringLiteral("card")).toString() == QLatin1String("folder")))
		return false;
	if(query_.isEmpty())
		return true;
	for(const char *field : { "title", "name", "ext", "titleId", "serial", "sourceName" })
		if(card.value(QLatin1String(field)).toString().contains(query_, Qt::CaseInsensitive))
			return true;
	return false;
}

QVariantList ShelfController::viewCards() const
{
	QVariantList cards;
	const QString where = level();
	if(where == QLatin1String("home"))
	{
		for(const Source &item : sources_)
			cards << sourceCard(item);
		std::stable_sort(cards.begin(), cards.end(), [](const QVariant &a, const QVariant &b) {
			return sectionRank(a.toMap().value(QStringLiteral("section")).toString())
				< sectionRank(b.toMap().value(QStringLiteral("section")).toString());
		});
		return cards;
	}
	if(where == QLatin1String("search"))
	{
		QSet<QString> seen;
		for(const Source &item : sources_)
		{
			if(!item.inSearch || !item.available)
				continue;
			QVariantMap card = sourceCard(item);
			if(matches(card) && filter_ == QLatin1String("all"))
			{
				card[QStringLiteral("section")] = QStringLiteral("places");
				cards << card;
			}
			for(const QVariant &entry : searchIndex_.value(item.id))
			{
				const QVariantMap found = entry.toMap();
				const QString key = found.value(QStringLiteral("key")).toString();
				if(!seen.contains(key) && matches(found))
				{
					seen.insert(key);
					cards << found;
				}
			}
		}
	}
	else
	{
		for(const QVariant &entry : all_)
			if(matches(entry.toMap()))
				cards << entry;
	}
	QCollator collator;
	collator.setNumericMode(true);
	collator.setCaseSensitivity(Qt::CaseInsensitive);
	const QString order = sort_;
	std::stable_sort(cards.begin(), cards.end(), [&collator, order](const QVariant &left, const QVariant &right) {
		const QVariantMap a = left.toMap();
		const QVariantMap b = right.toMap();
		const bool aPlace = a.value(QStringLiteral("card")).toString() == QLatin1String("source");
		const bool bPlace = b.value(QStringLiteral("card")).toString() == QLatin1String("source");
		if(aPlace != bPlace)
			return aPlace;
		if(order == QLatin1String("type"))
		{
			const int ra = groupRank(a.value(QStringLiteral("group")).toString());
			const int rb = groupRank(b.value(QStringLiteral("group")).toString());
			if(ra != rb)
				return ra < rb;
		}
		if(order == QLatin1String("date"))
		{
			const double da = a.value(QStringLiteral("modified")).toDouble();
			const double db = b.value(QStringLiteral("modified")).toDouble();
			if(da != db)
				return da > db;
		}
		return collator.compare(a.value(QStringLiteral("title")).toString(), b.value(QStringLiteral("title")).toString()) < 0;
	});
	return cards;
}

void ShelfController::showCards()
{
	const QVariantList cards = viewCards();
	const QString where = level();
	rows_.setCards(cards, where == QLatin1String("home") || sort_ == QLatin1String("type"), columns_);
	emit viewChanged();
}

void ShelfController::updateCards(const QVariantList &cards)
{
	bool moved = false;
	for(const QVariant &entry : cards)
	{
		const QVariantMap card = entry.toMap();
		const QString key = card.value(QStringLiteral("key")).toString();
		for(QVariant &kept : all_)
		{
			QVariantMap old = kept.toMap();
			if(old.value(QStringLiteral("key")).toString() != key)
				continue;
			moved = moved || old.value(QStringLiteral("group")) != card.value(QStringLiteral("group"))
				|| old.value(QStringLiteral("title")) != card.value(QStringLiteral("title"))
				|| old.value(QStringLiteral("card")) != card.value(QStringLiteral("card"));
			kept = card;
			break;
		}
	}
	// In place while it fills in; laid out again once it is done.
	if(moved && !loading_)
	{
		showCards();
		return;
	}
	for(int i = 0; i < rows_.count(); ++i)
	{
		const QString key = rows_.card(i).value(QStringLiteral("key")).toString();
		for(const QVariant &entry : cards)
			if(entry.toMap().value(QStringLiteral("key")).toString() == key)
			{
				QVariantMap card = entry.toMap();
				if(level() == QLatin1String("search"))
					card[QStringLiteral("section")] = rows_.card(i).value(QStringLiteral("section"));
				rows_.updateCard(i, card);
			}
	}
}

int ShelfController::total() const
{
	const QString where = level();
	if(where == QLatin1String("home"))
		return static_cast<int>(sources_.size());
	if(where == QLatin1String("search"))
	{
		int n = 0;
		for(const QVariantList &cards : searchIndex_)
			n += static_cast<int>(cards.size());
		return n;
	}
	return static_cast<int>(all_.size());
}

QString ShelfController::level() const
{
	if(!place_.sourceId.isEmpty())
		return QStringLiteral("folder");
	return query_.trimmed().isEmpty() ? QStringLiteral("home") : QStringLiteral("search");
}

QString ShelfController::title() const
{
	const QString where = level();
	if(where == QLatin1String("home"))
		return tr("Library");
	if(where == QLatin1String("search"))
		return tr("Search");
	const Source *item = source(place_.sourceId);
	if(!item)
		return tr("Library");
	if(samePathKey(place_.path) == samePathKey(place_.root))
		return item->name;
	QString clean = QDir::fromNativeSeparators(place_.path);
	while(clean.endsWith(QLatin1Char('/')) && clean.size() > 1)
		clean.chop(1);
	return clean.mid(clean.lastIndexOf(QLatin1Char('/')) + 1);
}

QVariantList ShelfController::crumbs() const
{
	QVariantList out { QVariantMap { { QStringLiteral("title"), tr("Library") } } };
	const QString where = level();
	if(where == QLatin1String("search"))
		out << QVariantMap { { QStringLiteral("title"), tr("Search") } };
	const Source *item = source(place_.sourceId);
	if(where != QLatin1String("folder") || !item)
		return out;
	out << QVariantMap { { QStringLiteral("title"), item->name } };
	const QString root = samePathKey(place_.root);
	const QString path = samePathKey(place_.path);
	if(path.startsWith(root) && path.size() > root.size())
	{
		QString rest = path.mid(root.size());
		if(rest.startsWith(QLatin1Char('/')))
			rest.remove(0, 1);
		for(const QString &part : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts))
			out << QVariantMap { { QStringLiteral("title"), part } };
	}
	return out;
}

QString ShelfController::placeKind() const
{
	const Source *item = source(place_.sourceId);
	return item ? item->kind : QString();
}

void ShelfController::setQuery(const QString &query)
{
	if(query == query_)
		return;
	const QString before = level();
	query_ = query;
	if(level() != before)
		emit placeChanged();
	showCards();
}

void ShelfController::setFilter(const QString &filter)
{
	if(filter == filter_)
		return;
	filter_ = filter;
	showCards();
}

void ShelfController::setSort(const QString &sort)
{
	if(sort == sort_)
		return;
	sort_ = sort;
	showCards();
}

void ShelfController::setColumns(int columns)
{
	columns = std::max(1, columns);
	if(columns == columns_)
		return;
	columns_ = columns;
	showCards();
}

void ShelfController::setActive(bool active)
{
	if(active == active_)
		return;
	active_ = active;
	emit activeChanged();
	if(!active_)
	{
		volumesTimer_.stop();
		return;
	}
	volumesTimer_.start();
	checkVolumes();
	if(consoleUsable())
		scanConsoleDrives();
	for(const Source &item : sources_)
		if(item.available && !sourceLooks_.contains(item.id))
			refreshSourcePreview(item);
	watchPlace();
	showCards();
}

// ── moving around ────────────────────────────────────────────────────────

void ShelfController::refresh()
{
	if(!place_.sourceId.isEmpty())
	{
		if(place_.sourceId == kConsoleLibrary)
			consoleLibrary_->refresh();
		else
			loadPlace();
		return;
	}
	sourcesCancel_->store(true);
	sourcesCancel_ = std::make_shared<std::atomic<bool>>(false);
	{
		std::lock_guard<std::mutex> lock(consoleMutex_);
		installedAge_.invalidate();
		noCover_.clear();
	}
	checkVolumes();
	if(consoleUsable())
	{
		scanConsoleDrives();
		consoleLibrary_->refresh();
	}
	for(const Source &item : sources_)
		refreshSourcePreview(item);
}

QVariantMap ShelfController::card(int index) const
{
	return rows_.card(index);
}

void ShelfController::openCard(int index)
{
	const QVariantMap card = rows_.card(index);
	const QString kind = card.value(QStringLiteral("card")).toString();
	if(kind == QLatin1String("source"))
		openPlace(card.value(QStringLiteral("id")).toString(), QString());
	else if(kind == QLatin1String("folder"))
		openPlace(card.value(QStringLiteral("sourceId")).toString(), card.value(QStringLiteral("path")).toString());
}

void ShelfController::back()
{
	if(level() == QLatin1String("search"))
	{
		setQuery(QString());
		return;
	}
	if(place_.sourceId.isEmpty())
		return;
	if(samePathKey(place_.path) == samePathKey(place_.root) || gone_)
	{
		home();
		return;
	}
	QString clean = QDir::fromNativeSeparators(place_.path);
	while(clean.endsWith(QLatin1Char('/')) && clean.size() > 1)
		clean.chop(1);
	QString parent = clean.left(clean.lastIndexOf(QLatin1Char('/')));
	if(parent.isEmpty() || parent.endsWith(QLatin1Char(':')))
		parent += QLatin1Char('/');
	const Source *item = source(place_.sourceId);
	if(item && item->where == QLatin1String("pc"))
		parent = QDir::toNativeSeparators(parent);
	if(!samePathKey(parent).startsWith(samePathKey(place_.root)))
	{
		home();
		return;
	}
	openPlace(place_.sourceId, parent);
}

void ShelfController::home()
{
	placeCancel_->store(true);
	++placeGeneration_;
	place_ = Place();
	all_.clear();
	gone_ = false;
	message_.clear();
	loading_ = false;
	emit stateChanged();
	emit placeChanged();
	watchPlace();
	showCards();
	for(const Source &item : sources_)
		if(item.available && !sourceLooks_.contains(item.id))
			refreshSourcePreview(item);
}

void ShelfController::goTo(int crumb)
{
	if(crumb <= 0)
	{
		if(!query_.isEmpty())
			setQuery(QString());
		home();
		return;
	}
	if(place_.sourceId.isEmpty())
		return;
	if(crumb == 1)
	{
		openPlace(place_.sourceId, QString());
		return;
	}
	const QVariantList parts = crumbs();
	QString path = QDir::fromNativeSeparators(place_.root);
	for(int i = 2; i <= crumb && i < parts.size(); ++i)
	{
		if(!path.endsWith(QLatin1Char('/')))
			path += QLatin1Char('/');
		path += parts[i].toMap().value(QStringLiteral("title")).toString();
	}
	const Source *item = source(place_.sourceId);
	openPlace(place_.sourceId, item && item->where == QLatin1String("pc") ? QDir::toNativeSeparators(path) : path);
}

void ShelfController::act(int index, const QString &action)
{
	const QVariantMap card = rows_.card(index);
	if(card.isEmpty())
		return;
	const QString path = card.value(QStringLiteral("path")).toString();
	const QString name = card.value(QStringLiteral("name")).toString();
	if(action == QLatin1String("open"))
	{
		openCard(index);
		return;
	}
	if(card.value(QStringLiteral("where")).toString() == QLatin1String("console"))
	{
		QVariantMap item = card;
		item[QStringLiteral("folder")] = card.value(QStringLiteral("card")).toString() == QLatin1String("folder")
			|| card.value(QStringLiteral("app")).toBool();
		if(!item.contains(QStringLiteral("port")))
			item[QStringLiteral("port")] = payloads::loaderPort(
				app_->installsFromConsole() ? payloads::Kind::Ps5 : payloads::Kind::Ps4, name.toStdString());
		// The library's own picture travels to its card in the installs panel.
		const QString picture = card.value(QStringLiteral("picture")).toString();
		item[QStringLiteral("icon")] = picture;
		consoleLibrary_->perform(item, action);
		return;
	}
	if(action == QLatin1String("install"))
		app_->addPaths({ path }, 0);
	else if(action == QLatin1String("send"))
		app_->addPaths({ path }, 1);
	else if(action == QLatin1String("sendFile"))
	{
		QStringList files { path };
		const QString track = card.value(QStringLiteral("track")).toString();
		if(!track.isEmpty())
			files << track;
		app_->addPaths(files, 1);
	}
	else if(action == QLatin1String("convert") || action == QLatin1String("convertOnly"))
		games_->convertFile(path, action == QLatin1String("convert"));
	else if(action == QLatin1String("runPayload"))
		payloads_->sendFromPc(path, payloads_->portFor(name));
	else if(action == QLatin1String("reveal"))
	{
		const QFileInfo info(path);
		QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() && card.value(QStringLiteral("card")) == QLatin1String("folder")
				? info.absoluteFilePath() : info.absolutePath()));
	}
}

// ── managing the places ──────────────────────────────────────────────────

void ShelfController::addFolder(const QUrl &folder)
{
	const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
	addPlace(path, QStringLiteral("pc"));
}

void ShelfController::addPlace(const QString &path, const QString &where)
{
	const bool console = where == QLatin1String("console");
	QString clean = path.trimmed();
	if(clean.isEmpty())
		return;
	if(console)
	{
		if(!clean.startsWith(QLatin1Char('/')))
			clean.prepend(QLatin1Char('/'));
	}
	else
	{
		if(!QFileInfo(clean).isDir())
		{
			emit notice(tr("%1 is not a folder of this PC.").arg(clean), true);
			return;
		}
		clean = QDir::toNativeSeparators(QDir::cleanPath(clean));
		if(isDriveRoot(clean) && !clean.endsWith(QDir::separator()))
			clean += QDir::separator();
	}
	for(const LibraryLocation &place : app_->settings().libraryLocations)
		if(QString::fromStdString(place.where) == (console ? QStringLiteral("console") : QStringLiteral("pc"))
			&& samePathKey(QString::fromStdString(place.path)) == samePathKey(clean))
		{
			emit notice(tr("%1 is already in the library.").arg(clean), false);
			return;
		}
	const std::string id = "loc-" + randomToken(4);
	app_->updateSettings([&](Settings &s) {
		LibraryLocation place;
		place.id = id;
		place.path = clean.toStdString();
		place.where = console ? "console" : "pc";
		s.libraryLocations.push_back(place);
	});
	rebuildSources();
	watchPlace();
	if(const Source *added = source(QString::fromStdString(id)))
	{
		refreshSourcePreview(*added);
		emit notice(tr("%1 is in the library.").arg(added->name), false);
	}
}

void ShelfController::addPreset(const QString &which)
{
	QString path;
	std::string where = "pc";
	if(which == QLatin1String("desktop"))
		path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	else if(which == QLatin1String("downloads"))
		path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
	else if(which == QLatin1String("documents"))
		path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	else if(which == QLatin1String("console-pkg"))
	{
		path = QString::fromStdString(app_->settings().ftpUploadDirectory.empty() ? "/data/pkg/"
																				   : app_->settings().ftpUploadDirectory);
		where = "console";
	}
	if(path.isEmpty())
		return;
	const std::string id = "preset-" + which.toStdString();
	for(const LibraryLocation &place : app_->settings().libraryLocations)
		if(place.id == id)
			return;
	app_->updateSettings([&](Settings &s) {
		LibraryLocation place;
		place.id = id;
		place.path = where == "pc" ? QDir::toNativeSeparators(path).toStdString() : path.toStdString();
		place.where = where;
		s.libraryLocations.push_back(place);
	});
	rebuildSources();
	watchPlace();
	if(const Source *added = source(QString::fromStdString(id)))
		refreshSourcePreview(*added);
}

void ShelfController::removeLocation(const QString &id)
{
	app_->updateSettings([&](Settings &s) {
		s.libraryLocations.erase(std::remove_if(s.libraryLocations.begin(), s.libraryLocations.end(),
									 [&](const LibraryLocation &place) { return QString::fromStdString(place.id) == id; }),
			s.libraryLocations.end());
	});
	sourceLooks_.remove(id);
	searchIndex_.remove(id);
	if(place_.sourceId == id)
		home();
	rebuildSources();
	watchPlace();
}

void ShelfController::renameLocation(const QString &id, const QString &name)
{
	app_->updateSettings([&](Settings &s) {
		for(LibraryLocation &place : s.libraryLocations)
			if(QString::fromStdString(place.id) == id)
				place.name = name.trimmed().toStdString();
	});
	rebuildSources();
}

void ShelfController::setLocationPath(const QString &id, const QString &path)
{
	QString clean = path.trimmed();
	if(clean.isEmpty())
		return;
	app_->updateSettings([&](Settings &s) {
		for(LibraryLocation &place : s.libraryLocations)
			if(QString::fromStdString(place.id) == id)
			{
				if(place.where == "console" && !clean.startsWith(QLatin1Char('/')))
					clean.prepend(QLatin1Char('/'));
				place.path = clean.toStdString();
			}
	});
	sourceLooks_.remove(id);
	searchIndex_.remove(id);
	if(place_.sourceId == id)
		home();
	rebuildSources();
	watchPlace();
}

void ShelfController::setFavorite(const QString &id, bool on)
{
	app_->updateSettings([&](Settings &s) {
		for(LibraryLocation &place : s.libraryLocations)
			if(QString::fromStdString(place.id) == id)
				place.favorite = on;
	});
	rebuildSources();
}

void ShelfController::setInSearch(const QString &id, bool on)
{
	app_->updateSettings([&](Settings &s) {
		for(LibraryLocation &place : s.libraryLocations)
			if(QString::fromStdString(place.id) == id)
				place.inSearch = on;
	});
	rebuildSources();
}

} // namespace orbislink
