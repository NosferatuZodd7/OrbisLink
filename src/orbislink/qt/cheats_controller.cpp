// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/cheats_controller.h"

#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/translate_message.h"
#ifdef ORBISLINK_HAS_STORE
#include "orbislink/store/zip_reader.h"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>
#include <map>

namespace orbislink {

using cheats::CheatListing;
using cheats::Format;

namespace {

// The cheat repositories, read as raw files (their lists and the files).
constexpr char kGoldHenCheats[] = "https://raw.githubusercontent.com/GoldHEN/GoldHEN_Cheat_Repository/main/";
constexpr char kEtaHenCheats[] = "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/";
#ifdef ORBISLINK_HAS_STORE
// illusion0001's game patches, built into one archive at each release.
constexpr char kPatchArchive[] =
	"https://github.com/illusion0001/PS4-PS5-Game-Patch/releases/latest/download/patch1.zip";
#endif
// The lists and the archive are fetched again after this long.
constexpr qint64 kFreshForSeconds = 12 * 60 * 60;
// A game has a handful of cheat files; past this many only the first are read.
constexpr size_t kMaxFilesRead = 16;

// ORBISLINK_CHEATS_URL and ORBISLINK_PATCHES_URL stand in for the
// repositories (a copy on another server, or the tests').
std::vector<std::string> cheatRepositories(bool ps5)
{
	if(qEnvironmentVariableIsSet("ORBISLINK_CHEATS_URL"))
	{
		std::string base = qgetenv("ORBISLINK_CHEATS_URL").toStdString();
		if(!base.empty() && base.back() != '/')
			base += '/';
		return { base };
	}
	// GoldHEN's first: where the PS4 games' cheats are kept up to date;
	// etaHEN's has the PS5 games'.
	std::vector<std::string> bases { kGoldHenCheats };
	if(ps5)
		bases.push_back(kEtaHenCheats);
	return bases;
}

#ifdef ORBISLINK_HAS_STORE
std::string patchArchiveUrl()
{
	return qEnvironmentVariableIsSet("ORBISLINK_PATCHES_URL") ? qgetenv("ORBISLINK_PATCHES_URL").toStdString()
															 : std::string(kPatchArchive);
}
#endif

// The folder on this PC a repository's copy is kept in.
QString repositoryKey(const std::string &base)
{
	if(base == kGoldHenCheats)
		return QStringLiteral("goldhen");
	if(base == kEtaHenCheats)
		return QStringLiteral("etahen");
	return QStringLiteral("other");
}

QString toQ(const std::string &s)
{
	return QString::fromStdString(s);
}

bool writeLocal(const QString &path, const std::string &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly))
		return false;
	file.write(bytes.data(), static_cast<qint64>(bytes.size()));
	return file.commit();
}

bool readLocal(const QString &path, std::string *text)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		return false;
	const QByteArray bytes = file.readAll();
	text->assign(bytes.constData(), static_cast<size_t>(bytes.size()));
	return true;
}

bool fresh(const QString &path)
{
	const QFileInfo info(path);
	return info.exists() && info.lastModified().secsTo(QDateTime::currentDateTime()) < kFreshForSeconds;
}

// A file of the internet, kept at `file`: fetched again when it is not
// there, when `force`, or (with `maxAge`) when older than half a day. When
// the internet does not answer, the copy kept is used and `stale` says so.
bool fetchCached(const std::string &url, const QString &file, bool force, bool maxAge,
	const std::atomic<bool> &cancel, std::string *text, bool *stale, QString *error)
{
	const bool have = QFileInfo::exists(file);
	if(have && !force && (!maxAge || fresh(file)))
		return readLocal(file, text);
	HttpClient http(20000);
	HttpClient::FetchOptions options;
	options.cancel = &cancel;
	const HttpResponse reply = http.fetch(url, options);
	if(reply.transportOk && reply.status == 200)
	{
		*text = reply.body;
		writeLocal(file, reply.body);
		return true;
	}
	if(error)
		*error = reply.transportOk ? QCoreApplication::translate("CheatsController", "The server answered %1.")
										 .arg(reply.status)
								   : QString::fromStdString(reply.error);
	if(have && readLocal(file, text))
	{
		if(stale)
			*stale = true;
		return true;
	}
	return false;
}

std::string hex(uint64_t value)
{
	char text[24];
	std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
	return text;
}

// The console's HEN folders, made one level at a time.
FtpResult makeFolders(FtpClient &ftp, const std::string &path)
{
	FtpResult result = FtpResult::success();
	std::string at;
	for(const std::string &part : split(path, '/', false))
	{
		at += "/" + part;
		if(at == "/data")
			continue;
		result = ftp.makeDirectory(at);
	}
	return result;
}

// A small file of the console in memory ("" when it is not there).
bool readRemote(FtpClient &ftp, const std::string &path, std::string *text)
{
	std::vector<uint8_t> bytes;
	text->clear();
	if(!ftp.read(path, 0, 1 << 20, &bytes).ok)
		return false;
	text->assign(bytes.begin(), bytes.end());
	return true;
}

// Text onto the console, through a file of this PC.
FtpResult writeRemote(FtpClient &ftp, const std::string &path, const std::string &text)
{
	const QString local = QDir(QDir::tempPath())
							  .filePath(QStringLiteral("orbislink-cheats-%1-%2.tmp")
									  .arg(QCoreApplication::applicationPid())
									  .arg(QDateTime::currentMSecsSinceEpoch()));
	if(!writeLocal(local, text))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Could not write a file on this PC."));
	const FtpResult sent = ftp.upload(local.toStdString(), path);
	QFile::remove(local);
	return sent;
}

QString formatLabel(Format format)
{
	return toQ(cheats::formatName(format)).toUpper();
}

bool formatOf(const std::string &name, Format *format)
{
	const std::string lower = toLower(name);
	for(Format f : cheats::allFormats())
		if(endsWith(lower, "." + cheats::formatName(f)))
		{
			*format = f;
			return true;
		}
	return false;
}

} // namespace

CheatsController::CheatsController(AppController *app, QObject *parent) : QObject(parent), app_(app) {}

CheatsController::~CheatsController()
{
	cancel_->store(true);
	if(worker_.joinable())
		worker_.join();
}

QString CheatsController::cacheRoot()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("cheats"));
}

void CheatsController::open(const QVariantMap &card)
{
	Context context;
	context.titleId = card.value(QStringLiteral("titleId")).toString().trimmed().toUpper().toStdString();
	context.version = card.value(QStringLiteral("version")).toString().trimmed().toStdString();
	context.title = card.value(QStringLiteral("title")).toString().toStdString();
	context.ps5 = app_->installsFromConsole();
	context_ = context;
	game_ = QVariantMap { { QStringLiteral("titleId"), toQ(context.titleId) },
		{ QStringLiteral("title"), toQ(context.title) }, { QStringLiteral("version"), toQ(context.version) },
		{ QStringLiteral("platform"), card.value(QStringLiteral("platform")) } };
	++generation_;
	loaded_ = false;
	offline_ = false;
	error_.clear();
	consoleRead_ = false;
	cheats_.clear();
	exactCheats_ = 0;
	otherFiles_.clear();
	patches_.clear();
	patchVersions_.clear();
	patchPlugin_ = false;
	listings_.clear();
	patchList_.clear();
	patchXml_.clear();
	emit gameChanged();
	emit listsChanged();
	if(busy_)
	{
		pendingRead_ = true;
		return;
	}
	run(tr("Looking for cheats…"), Job(), false);
}

void CheatsController::reload()
{
	if(context_.titleId.empty())
		return;
	run(tr("Fetching the lists again…"), Job(), true);
}

void CheatsController::run(const QString &what, Job job, bool forceLists)
{
	if(busy_ || context_.titleId.empty())
		return;
	if(worker_.joinable())
		worker_.join();
	busy_ = true;
	status_ = what;
	emit busyChanged();

	const FtpClient::Config config = app_->ftpClientConfigFor(app_->settings().consoleAddress);
	const Context context = context_;
	const quint64 generation = generation_;
	const std::shared_ptr<std::atomic<bool>> cancel = cancel_;
	worker_ = std::thread([this, config, context, generation, cancel, job, forceLists]() {
		FtpClient ftp(config);
		bool error = false;
		const QString message = job ? job(ftp, &error) : QString();
		Snapshot snapshot;
		if(!cancel->load())
			snapshot = read(ftp, context, forceLists, *cancel);
		if(cancel->load())
			return;
		QMetaObject::invokeMethod(
			this,
			[this, snapshot, generation, message, error]() {
				busy_ = false;
				status_.clear();
				if(generation == generation_)
				{
					loaded_ = snapshot.loaded;
					offline_ = snapshot.offline;
					error_ = snapshot.error;
					consoleRead_ = snapshot.consoleRead;
					cheats_ = snapshot.cheats;
					exactCheats_ = snapshot.exactCheats;
					otherFiles_ = snapshot.otherFiles;
					patches_ = snapshot.patches;
					patchVersions_ = snapshot.patchVersions;
					patchPlugin_ = snapshot.patchPlugin;
					listings_ = snapshot.listings;
					patchList_ = snapshot.patchList;
					patchXml_ = snapshot.patchXml;
					emit listsChanged();
				}
				emit busyChanged();
				if(!message.isEmpty())
					emit finished(message, error);
				if(pendingRead_)
				{
					pendingRead_ = false;
					run(tr("Looking for cheats…"), Job(), false);
				}
			},
			Qt::QueuedConnection);
	});
}

CheatsController::Snapshot CheatsController::read(FtpClient &ftp, const Context &context, bool forceLists,
	const std::atomic<bool> &cancel)
{
	Snapshot snapshot;
	const QString root = cacheRoot();
	const std::string version = cheats::normalizeVersion(context.version);

	// ── the repositories' lists: the game's files, from each repository
	std::vector<Listing> listings;
	QSet<QString> seen;
	bool anyList = false;
	for(const std::string &base : cheatRepositories(context.ps5))
	{
		const QString folder = QDir(root).filePath(repositoryKey(base));
		for(Format format : cheats::allFormats())
		{
			if(cancel.load())
				return snapshot;
			const std::string name = cheats::formatName(format) + ".txt";
			std::string text;
			bool stale = false;
			QString why;
			if(!fetchCached(base + name, QDir(folder).filePath(toQ(name)), forceLists, true, cancel, &text, &stale,
				   &why))
			{
				snapshot.offline = true;
				if(snapshot.error.isEmpty())
					snapshot.error = why;
				continue;
			}
			anyList = true;
			snapshot.offline = snapshot.offline || stale;
			for(const CheatListing &item : cheats::parseCheatList(text, format))
			{
				if(!iequals(item.titleId, context.titleId))
					continue;
				// The same file in two repositories: the first one's.
				const QString key = toQ(item.consoleName()).toLower();
				if(seen.contains(key))
					continue;
				seen.insert(key);
				listings.push_back({ item, base });
			}
		}
	}
	snapshot.loaded = anyList;
	std::stable_sort(listings.begin(), listings.end(), [&version](const Listing &a, const Listing &b) {
		const bool aExact = !version.empty() && a.item.version == version;
		const bool bExact = !version.empty() && b.item.version == version;
		if(aExact != bExact)
			return aExact;
		if(a.item.version != b.item.version)
			return a.item.version > b.item.version;
		if(a.item.format != b.item.format)
			return static_cast<int>(a.item.format) < static_cast<int>(b.item.format);
		return a.item.variant < b.item.variant;
	});

	// ── what the console has: its cheat folders, for this game
	const cheats::Console console = context.ps5 ? cheats::Console::EtaHen : cheats::Console::GoldHen;
	std::vector<FtpEntry> top;
	snapshot.consoleRead = !cancel.load() && ftp.list("/data/", &top).ok;
	std::map<std::string, std::pair<std::string, Format>> onConsole; // lower-case name → name, format
	if(snapshot.consoleRead)
		for(Format format : cheats::allFormats())
		{
			std::vector<FtpEntry> entries;
			if(!ftp.list(cheats::cheatFolder(console, format), &entries).ok)
				continue;
			for(const FtpEntry &entry : entries)
				if(!entry.isDirectory && cheats::cheatFileIsFor(entry.name, context.titleId))
					onConsole[toLower(entry.name)] = { entry.name, format };
		}

	// ── each file's cheats, from its readable copy (kept on this PC)
	size_t read = 0;
	for(const Listing &listing : listings)
	{
		if(cancel.load())
			return snapshot;
		QVariantList names;
		std::string author;
		bool readable = false;
		if(read < kMaxFilesRead)
		{
			++read;
			const QString local = QDir(QDir(root).filePath(repositoryKey(listing.base)))
									  .filePath(toQ(listing.item.readablePath()));
			std::string text;
			if(fetchCached(listing.base + listing.item.readablePath(), local, forceLists, false, cancel, &text, nullptr,
				   nullptr))
			{
				const cheats::CheatFile file = cheats::parseCheatFile(text,
					listing.item.format == Format::Json ? Format::Json : Format::Shn);
				readable = file.ok;
				author = file.author;
				for(const cheats::Cheat &cheat : file.cheats)
					names << QVariantMap { { QStringLiteral("name"), toQ(cheat.name) },
						{ QStringLiteral("description"), toQ(cheat.description) } };
				// What was fetched was not a cheat file (a page of the
				// server): it is not kept.
				if(!file.ok)
					QFile::remove(local);
			}
		}
		const std::string consoleName = listing.item.consoleName();
		const bool installed = onConsole.erase(toLower(consoleName)) > 0;
		const bool exact = !version.empty() && listing.item.version == version;
		snapshot.exactCheats += exact ? 1 : 0;
		snapshot.cheats << QVariantMap { { QStringLiteral("key"), toQ(consoleName) },
			{ QStringLiteral("file"), toQ(listing.item.file) },
			{ QStringLiteral("consoleName"), toQ(consoleName) },
			{ QStringLiteral("format"), formatLabel(listing.item.format) },
			{ QStringLiteral("version"), toQ(listing.item.version) },
			{ QStringLiteral("variant"), toQ(listing.item.variant) },
			{ QStringLiteral("exact"), exact },
			{ QStringLiteral("installed"), installed },
			{ QStringLiteral("readable"), readable },
			{ QStringLiteral("cheats"), names },
			{ QStringLiteral("author"), toQ(author) },
			{ QStringLiteral("repository"), repositoryKey(listing.base) } };
	}
	for(const auto &left : onConsole)
		snapshot.otherFiles << QVariantMap { { QStringLiteral("name"), toQ(left.second.first) },
			{ QStringLiteral("format"), formatLabel(left.second.second) },
			{ QStringLiteral("path"), toQ(cheats::cheatFolder(console, left.second.second) + "/" + left.second.first) } };
	snapshot.listings = listings;

	// ── game patches (GoldHEN, PS4)
	if(context.ps5 || cancel.load())
		return snapshot;
#ifdef ORBISLINK_HAS_STORE
	const QString patchFolder = QDir(root).filePath(QStringLiteral("patches"));
	const QString archive = QDir(patchFolder).filePath(QStringLiteral("patch1.zip"));
	if(forceLists || !fresh(archive))
	{
		QDir().mkpath(patchFolder);
		const QString part = archive + QStringLiteral(".part");
		HttpClient http(60000);
		const HttpClient::DownloadResult got = http.download(patchArchiveUrl(), part.toStdString(),
			[&cancel](int64_t, int64_t) { return !cancel.load(); });
		std::vector<store::ZipEntry> check;
		std::string zipError;
		if(got.ok && store::readZipDirectory(part.toStdString(), &check, &zipError))
		{
			QFile::remove(archive);
			QFile::rename(part, archive);
		}
		else
		{
			QFile::remove(part);
			snapshot.offline = true;
			if(snapshot.error.isEmpty())
				snapshot.error = got.ok ? QString::fromStdString(zipError) : QString::fromStdString(got.error);
		}
	}
	std::vector<store::ZipEntry> entries;
	std::string zipError;
	if(QFileInfo::exists(archive) && store::readZipDirectory(archive.toStdString(), &entries, &zipError))
	{
		const std::string wanted = "patches/xml/" + context.titleId + ".xml";
		for(const store::ZipEntry &entry : entries)
		{
			if(entry.directory || !iequals(entry.name, wanted))
				continue;
			const QString local = QDir(patchFolder).filePath(toQ(context.titleId) + QStringLiteral(".xml"));
			std::string error;
			if(store::extractZipEntry(archive.toStdString(), entry, local.toStdString(), {}, &error))
				readLocal(local, &snapshot.patchXml);
			QFile::remove(local);
			break;
		}
	}
	const cheats::PatchFile file = cheats::parsePatchFile(snapshot.patchXml);
	if(!file.ok)
	{
		snapshot.patchXml.clear();
		return snapshot;
	}

	// Which switches the console has: one listing, then each of this
	// game's that is there.
	QSet<QString> switches;
	if(snapshot.consoleRead)
	{
		std::vector<FtpEntry> settings;
		if(ftp.list("/data/GoldHEN/patches/settings", &settings).ok)
			for(const FtpEntry &entry : settings)
				switches.insert(toQ(entry.name).toLower());
		std::string ini;
		if(readRemote(ftp, "/data/GoldHEN/plugins.ini", &ini))
			snapshot.patchPlugin = cheats::patchPluginLoads(ini, context.titleId);
	}
	QStringList others;
	for(const cheats::Patch &patch : file.patches)
	{
		if(!patch.appliesTo(context.version))
		{
			const QString other = toQ(patch.appVersion);
			if(!other.isEmpty() && !others.contains(other))
				others << other;
			continue;
		}
		const uint64_t hash = cheats::patchHash(patch, context.titleId);
		const std::string path = cheats::patchSettingsPath(hash);
		bool on = false;
		if(switches.contains(toQ(path.substr(path.rfind('/') + 1)).toLower()))
		{
			std::string content;
			on = readRemote(ftp, path, &content) && cheats::patchSwitchOn(content);
		}
		snapshot.patchList.push_back(patch);
		snapshot.patches << QVariantMap { { QStringLiteral("key"), toQ(hex(hash)) },
			{ QStringLiteral("name"), toQ(patch.name) }, { QStringLiteral("note"), toQ(patch.note) },
			{ QStringLiteral("author"), toQ(patch.author) }, { QStringLiteral("appVersion"), toQ(patch.appVersion) },
			{ QStringLiteral("lines"), patch.lines }, { QStringLiteral("on"), on } };
	}
	others.sort();
	snapshot.patchVersions = others;
#endif
	return snapshot;
}

void CheatsController::installCheat(const QString &key)
{
	const auto found = std::find_if(listings_.begin(), listings_.end(),
		[&key](const Listing &listing) { return toQ(listing.item.consoleName()) == key; });
	if(found == listings_.end())
		return;
	const Listing listing = *found;
	const Context context = context_;
	run(tr("Sending %1…").arg(key),
		[listing, context](FtpClient &ftp, bool *error) -> QString {
			const std::string name = listing.item.consoleName();
			HttpClient http(30000);
			const HttpResponse reply = http.fetch(listing.base + listing.item.loadablePath(), HttpClient::FetchOptions());
			// What came is the cheat file, not a page of the server.
			bool good = reply.transportOk && reply.status == 200 && !reply.body.empty();
			if(good && listing.item.format != Format::Mc4)
				good = cheats::parseCheatFile(reply.body, listing.item.format == Format::Json ? Format::Json
																						   : Format::Shn)
						   .ok;
			if(!good)
			{
				*error = true;
				return tr("%1 could not be downloaded: %2").arg(toQ(name),
					reply.transportOk ? tr("the server answered %1.").arg(reply.status) : toQ(reply.error));
			}
			const cheats::Console console = context.ps5 ? cheats::Console::EtaHen : cheats::Console::GoldHen;
			const std::string folder = cheats::cheatFolder(console, listing.item.format);
			makeFolders(ftp, folder);
			const FtpResult sent = writeRemote(ftp, folder + "/" + name, reply.body);
			if(!sent.ok)
			{
				*error = true;
				return tr("%1 could not be sent: %2").arg(toQ(name), translateMessage(sent.message));
			}
			if(context.ps5)
			{
				// etaHEN knows only the files its list names.
				const std::string list = cheats::etaHenList(listing.item.format);
				std::string text;
				readRemote(ftp, list, &text);
				const std::string changed = cheats::etaHenListAdd(text, name,
					listing.item.game.empty() ? context.title : listing.item.game);
				if(changed != text)
					writeRemote(ftp, list, changed);
				return tr("%1 is on the console. etaHEN loads it the next time it starts, or after “Reload "
						  "cheats” in its toolbox.")
					.arg(toQ(name));
			}
			return tr("%1 is on the console: in the game, hold Share for GoldHEN's cheat menu.")
				.arg(toQ(name));
		},
		false);
}

void CheatsController::removeCheat(const QString &name)
{
	Format format = Format::Json;
	const std::string file = name.toStdString();
	if(file.empty() || file.find('/') != std::string::npos || !formatOf(file, &format))
		return;
	const bool ps5 = context_.ps5;
	run(tr("Deleting %1…").arg(name),
		[file, format, ps5](FtpClient &ftp, bool *error) -> QString {
			const cheats::Console console = ps5 ? cheats::Console::EtaHen : cheats::Console::GoldHen;
			const FtpResult removed = ftp.removeFile(cheats::cheatFolder(console, format) + "/" + file);
			if(!removed.ok)
			{
				*error = true;
				return tr("%1 could not be deleted: %2").arg(toQ(file), translateMessage(removed.message));
			}
			if(ps5)
			{
				const std::string list = cheats::etaHenList(format);
				std::string text;
				if(readRemote(ftp, list, &text))
				{
					const std::string changed = cheats::etaHenListRemove(text, file);
					if(changed != text)
						writeRemote(ftp, list, changed);
				}
			}
			return tr("%1 is off the console.").arg(toQ(file));
		},
		false);
}

void CheatsController::setPatch(const QString &key, bool on)
{
	if(context_.ps5 || patchXml_.empty())
		return;
	const auto found = std::find_if(patchList_.begin(), patchList_.end(), [this, &key](const cheats::Patch &patch) {
		return toQ(hex(cheats::patchHash(patch, context_.titleId))) == key;
	});
	if(found == patchList_.end())
		return;
	const cheats::Patch patch = *found;
	const std::string xml = patchXml_;
	const std::string titleId = context_.titleId;
	run(on ? tr("Turning on %1…").arg(toQ(patch.name)) : tr("Turning off %1…").arg(toQ(patch.name)),
		[patch, xml, titleId, on](FtpClient &ftp, bool *error) -> QString {
			makeFolders(ftp, "/data/GoldHEN/patches/xml");
			makeFolders(ftp, "/data/GoldHEN/patches/settings");
			// The game's patch file as the archive has it: the switches
			// are worked out from what it says.
			FtpResult sent = writeRemote(ftp, cheats::patchXmlPath(titleId), xml);
			if(sent.ok)
				sent = writeRemote(ftp, cheats::patchSettingsPath(cheats::patchHash(patch, titleId)), on ? "1\n" : "0\n");
			if(!sent.ok)
			{
				*error = true;
				return tr("The patch could not be changed: %1").arg(translateMessage(sent.message));
			}
			return on ? tr("“%1” is on: it applies the next time the game starts.").arg(toQ(patch.name))
					  : tr("“%1” is off from the next time the game starts.").arg(toQ(patch.name));
		},
		false);
}

} // namespace orbislink
