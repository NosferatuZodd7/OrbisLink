// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/store_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/net/http_client.h"
#include "orbislink/net/payload_sender.h"
#include "orbislink/payloads/payload_layout.h"
#include "orbislink/pkg/sfo_parser.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/hb_store_db.h"
#include "orbislink/qt/payloads_controller.h"
#include "orbislink/qt/shadowmount_control.h"
#include "orbislink/store/shadowmount_log.h"
#include "orbislink/store/store_installer.h"
#include "orbislink/update/sha256.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace orbislink {

namespace {

// The PS5's FTP as the installer sees it.
class FtpStoreRemote : public store::StoreRemote
{
public:
	explicit FtpStoreRemote(FtpClient &ftp) : ftp_(ftp) {}

	bool list(const std::string &dir, std::vector<FtpEntry> *entries, std::string *error) override
	{
		return answer(ftp_.list(dir, entries), error);
	}
	bool download(const std::string &remote, const std::string &local, std::string *error) override
	{
		return answer(ftp_.download(remote, local), error);
	}
	bool upload(const std::string &local, const std::string &remote, std::string *error,
		const store::Transfer &progress) override
	{
		const FtpResult result = ftp_.upload(local, remote, progress);
		if(result.cancelled)
		{
			if(error)
				*error = "cancelled";
			return false;
		}
		return answer(result, error);
	}
	bool makeDirectory(const std::string &dir) override { return ftp_.makeDirectory(dir).ok; }
	bool rename(const std::string &from, const std::string &to, std::string *error) override
	{
		return answer(ftp_.rename(from, to), error);
	}
	bool removeFile(const std::string &path, std::string *error) override
	{
		return answer(ftp_.removeFile(path), error);
	}
	bool removeDirectory(const std::string &path, std::string *error) override
	{
		return answer(ftp_.removeDirectory(path), error);
	}
	bool chmod(const std::string &path, const std::string &mode, std::string *error) override
	{
		return answer(ftp_.setPermissions(path, mode), error);
	}

private:
	static bool answer(const FtpResult &result, std::string *error)
	{
		if(!result.ok && error)
			*error = result.message;
		return result.ok;
	}
	FtpClient &ftp_;
};

QString sizeText(int64_t bytes)
{
	return bytes > 0 ? QString::fromStdString(humanBytes(bytes)) : QString();
}

QStringList toStrings(const std::vector<std::string> &values)
{
	QStringList list;
	for(const std::string &value : values)
		list << QString::fromStdString(value);
	return list;
}

// The fields of a catalog app the page shows.
QVariantMap itemFor(const store::StoreApp &app)
{
	QVariantMap item;
	item[QStringLiteral("titleId")] = QString::fromStdString(app.titleId);
	item[QStringLiteral("name")] = QString::fromStdString(app.name.empty() ? app.titleId : app.name);
	item[QStringLiteral("kind")] = QString::fromStdString(app.kind);
	item[QStringLiteral("author")] = QString::fromStdString(app.author);
	item[QStringLiteral("version")] = QString::fromStdString(app.version);
	item[QStringLiteral("contentVersion")] = QString::fromStdString(app.contentVersion);
	item[QStringLiteral("size")] = sizeText(app.size);
	item[QStringLiteral("updated")] = QString::fromStdString(app.updated.substr(0, 10));
	item[QStringLiteral("released")] = QString::fromStdString(app.released.substr(0, 10));
	item[QStringLiteral("sandbox")] = QString::fromStdString(app.sandbox);
	item[QStringLiteral("format")] = QString::fromStdString(app.format);
	item[QStringLiteral("available")] = app.available();
	item[QStringLiteral("installable")] = app.installable();
	return item;
}

// What is left of a store's address to show: its host and path.
QString shortAddress(const std::string &url)
{
	const QUrl parsed(QString::fromStdString(url));
	QString text = parsed.host() + parsed.path();
	while(text.endsWith(QLatin1Char('/')))
		text.chop(1);
	return text.isEmpty() ? QString::fromStdString(url) : text;
}

bool readWhole(const std::string &path, std::string *out)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	if(!in)
		return false;
	out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

bool writeWhole(const std::string &path, const std::string &bytes)
{
	std::error_code ignored;
	fs::create_directories(fs::u8path(path).parent_path(), ignored);
	const std::string part = path + ".part";
	{
		std::ofstream out(fs::u8path(part), std::ios::binary | std::ios::trunc);
		out << bytes;
		if(!out)
			return false;
	}
	fs::rename(fs::u8path(part), fs::u8path(path), ignored);
	return !ignored;
}

// A picture's first bytes: PNG or JPEG.
bool looksLikePicture(const std::string &bytes)
{
	return bytes.compare(0, 4, "\x89PNG") == 0 || bytes.compare(0, 3, "\xFF\xD8\xFF") == 0;
}

} // namespace

StoreController::StoreController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	// The catalog asks every client to say what it is.
	const std::string agent = "OrbisLink/" + QCoreApplication::applicationVersion().toStdString();
	catalog_ = std::make_unique<store::StoreCatalog>(cacheDir(), agent);
	catalog_->setCustomBase(app_->settings().storeCatalogAddress);
	// Which consoles there are, the one in use, and whose FTP answers.
	connect(app_, &AppController::settingsChanged, this, &StoreController::consolesChanged);
	connect(app_, &AppController::settingsChanged, this, &StoreController::storesChanged);
	connect(app_, &AppController::statusChanged, this, &StoreController::consolesChanged);
	connect(app_, &AppController::ftpReachableChanged, this, &StoreController::consolesChanged);
	// The card of the job that ended says how it went.
	connect(this, &StoreController::finished, this, [this](const QString &message, bool error) {
		QVariantMap fields = { { QStringLiteral("state"), !error ? QStringLiteral("done")
														  : cancel_ ? QStringLiteral("cancelled") : QStringLiteral("error") },
			{ QStringLiteral("message"), message }, { QStringLiteral("speedText"), QString() },
			{ QStringLiteral("etaText"), QString() } };
		if(!error)
			fields[QStringLiteral("percent")] = 100.0;
		if(!cancel_)
			emit app_->jobFinished(tr("Homebrew store"), message, error);
		updateJob(fields);
		jobId_.clear();
	});
}

StoreController::~StoreController()
{
	cancel_ = true;
	for(std::thread *thread : { &loader_, &detailer_, &worker_, &checker_, &localScanner_, &adder_ })
		if(thread->joinable())
			thread->join();
}

std::string StoreController::cacheDir() const
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
		.filePath(QStringLiteral("store")).toStdString();
}

QVariantList StoreController::consoles() const
{
	QVariantList items;
	const QString chosen = target();
	for(const ConsoleEntry &console : app_->settings().consoles)
	{
		QVariantMap item;
		item[QStringLiteral("address")] = QString::fromStdString(console.address);
		item[QStringLiteral("name")] = QString::fromStdString(console.name.empty() ? console.address : console.name);
		item[QStringLiteral("type")] = QString::fromStdString(console.type);
		item[QStringLiteral("ftp")] = app_->ftpAnswers(console.address);
		item[QStringLiteral("target")] = QString::fromStdString(console.address) == chosen;
		items << item;
	}
	return items;
}

QString StoreController::target() const
{
	// The console in use: the queue installs packages on it.
	const Settings &settings = app_->settings();
	for(const ConsoleEntry &console : settings.consoles)
		if(console.address == settings.consoleAddress)
			return QString::fromStdString(console.address);
	return settings.consoles.empty() ? QString() : QString::fromStdString(settings.consoles.front().address);
}

QString StoreController::targetName() const
{
	const QString address = target();
	for(const ConsoleEntry &console : app_->settings().consoles)
		if(QString::fromStdString(console.address) == address)
			return QString::fromStdString(console.name.empty() ? console.address : console.name);
	return QString();
}

QString StoreController::targetType() const
{
	const QString address = target();
	for(const ConsoleEntry &console : app_->settings().consoles)
		if(QString::fromStdString(console.address) == address && !console.type.empty())
			return QString::fromStdString(console.type);
	// Never answered Remote Play: the jailbreak it runs says which it is.
	const QString jailbreak = app_->jailbreaks().value(address).toString();
	if(jailbreak.isEmpty())
		return QString();
	return jailbreak.contains(QLatin1String("etaHEN"), Qt::CaseInsensitive) ? QStringLiteral("ps5")
																		   : QStringLiteral("ps4");
}

bool StoreController::canInstall() const
{
	const QString address = target();
	return !address.isEmpty() && targetType() == QLatin1String("ps5") && app_->ftpAnswers(address.toStdString());
}

bool StoreController::canInstallPackages() const
{
	return !target().isEmpty();
}

void StoreController::setTarget(const QString &address)
{
	if(address.isEmpty() || address == target())
		return;
	app_->selectConsole(address);
	emit consolesChanged();
	// What that console has installed, and what of the stores goes on it.
	publish();
	refresh();
}

void StoreController::setProgress(const QString &stage, int64_t done, int64_t total)
{
	using namespace std::chrono;
	const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	const bool fresh = stage != meterStage_;
	if(fresh)
	{
		meterStage_ = stage;
		samples_.clear();
	}
	// The speed over the last three seconds: steady enough to read, quick
	// enough to follow a change. One sample every 100 ms is plenty.
	if(samples_.empty() || now - samples_.back().first >= 100)
		samples_.emplace_back(now, done);
	while(samples_.size() > 2 && now - samples_.front().first > 3000)
		samples_.pop_front();
	const int64_t span = now - samples_.front().first;
	const int64_t moved = done - samples_.front().second;
	const double speed = span >= 500 && moved > 0
		? static_cast<double>(moved) * 1000.0 / static_cast<double>(span)
		: 0.0;

	// The window is told a few times a second, not on every block.
	const bool last = total > 0 && done >= total;
	if(!fresh && !last && now - lastPostMs_ < 200)
		return;
	lastPostMs_ = now;

	const double fraction = total > 0 ? std::min(1.0, static_cast<double>(done) / static_cast<double>(total)) : 0.0;
	const QString speedText = speed > 0
		? QString::fromStdString(humanBytes(static_cast<int64_t>(speed))) + QStringLiteral("/s")
		: QString();
	const QString amountText = total > 0
		? QStringLiteral("%1 / %2").arg(QString::fromStdString(humanBytes(done)),
			QString::fromStdString(humanBytes(total)))
		: QString();
	const QString etaText = speed > 0 && total > done
		? QString::fromStdString(humanDuration(static_cast<int64_t>(static_cast<double>(total - done) / speed)))
		: QString();
	const double bytesPerSecond = speed;
	const double bytesLeft = total > done ? static_cast<double>(total - done) : 0.0;
	const double bytesTotal = total > 0 ? static_cast<double>(total) : 0.0;
	QMetaObject::invokeMethod(this, [this, stage, fraction, speedText, amountText, etaText, bytesPerSecond, bytesLeft,
										bytesTotal]() {
		stage_ = stage;
		progress_ = fraction;
		speedText_ = speedText;
		amountText_ = amountText;
		emit progressChanged();
		const QString stageText = stage == QLatin1String("download") ? tr("Downloading…")
			: stage == QLatin1String("verify") ? tr("Checking the download…")
			: stage == QLatin1String("unpack") ? tr("Copying to the PS5…")
			: stage == QLatin1String("finish") ? tr("Putting it in place…")
			: stage == QLatin1String("shadowmount") ? tr("Starting ShadowMountPlus again…")
			: stage;
		updateJob({ { QStringLiteral("stageText"), stageText }, { QStringLiteral("percent"), fraction * 100.0 },
			{ QStringLiteral("speedText"), speedText }, { QStringLiteral("amountText"), amountText },
			{ QStringLiteral("etaText"), etaText }, { QStringLiteral("bytesPerSecond"), bytesPerSecond },
			{ QStringLiteral("bytesLeft"), bytesLeft }, { QStringLiteral("bytesTotal"), bytesTotal } });
	}, Qt::QueuedConnection);
}

void StoreController::resetProgress()
{
	progress_ = 0.0;
	speedText_.clear();
	amountText_.clear();
}

QVariantMap StoreController::sourceFor(const StoreSource &source, const store::StoreApp *folder,
	const store::HbStoreApp *package) const
{
	const QString type = targetType();
	QVariantMap item;
	item[QStringLiteral("store")] = QString::fromStdString(source.url);
	item[QStringLiteral("storeName")] = QString::fromStdString(source.name);
	// Why it cannot go, in full and in a word or two for a badge.
	QString note;
	QString brief;
	bool installable = false;
	if(folder)
	{
		item[QStringLiteral("format")] = QStringLiteral("folder");
		item[QStringLiteral("version")] = QString::fromStdString(folder->version);
		item[QStringLiteral("size")] = sizeText(folder->size);
		item[QStringLiteral("available")] = folder->available();
		if(!folder->available())
			note = brief = tr("Coming soon");
		else if(!folder->installable())
			note = brief = tr("Not an app folder");
		else if(type == QLatin1String("ps4"))
		{
			note = tr("For the PS5: an app folder ShadowMountPlus puts on its home screen");
			brief = tr("For the PS5");
		}
		else
			installable = true;
	}
	else if(package)
	{
		item[QStringLiteral("format")] = QStringLiteral("pkg");
		item[QStringLiteral("version")] = QString::fromStdString(package->version);
		item[QStringLiteral("size")] = QString::fromStdString(package->sizeText);
		item[QStringLiteral("firmware")] = QString::fromStdString(package->ps4Firmware());
		item[QStringLiteral("available")] = true;
		if(type == QLatin1String("ps5"))
		{
			if(!package->forPs5())
			{
				note = tr("For the PS4: the store does not say it runs on the PS5");
				brief = tr("For the PS4");
			}
			else if(!package->ps5PackageIsThisOne())
			{
				note = tr("Its PS5 build (%1) is not the package the store hands out")
						   .arg(QString::fromStdString(package->ps5Version));
				brief = tr("For the PS4");
			}
			else
				installable = true;
		}
		else if(type == QLatin1String("ps4") && !package->forPs4())
			note = brief = tr("For the PS5");
		else
			installable = true;
	}
	item[QStringLiteral("installable")] = installable;
	item[QStringLiteral("note")] = note;
	item[QStringLiteral("brief")] = brief;
	return item;
}

void StoreController::publish()
{
	// One card per title ID, in the order of the stores: the first store
	// that has it gives what the card shows, each one its tag.
	std::map<std::string, QVariantMap> cards;
	std::vector<std::string> order;
	auto add = [&](const std::string &titleId, const QVariantMap &fields, const QVariantMap &source, bool update,
				   const QString &description) {
		auto found = cards.find(titleId);
		if(found == cards.end())
		{
			QVariantMap card = fields;
			card[QStringLiteral("stores")] = QStringList();
			card[QStringLiteral("sources")] = QVariantList();
			card[QStringLiteral("installable")] = false;
			card[QStringLiteral("available")] = false;
			card[QStringLiteral("update")] = false;
			card[QStringLiteral("description")] = description;
			found = cards.emplace(titleId, card).first;
			order.push_back(titleId);
		}
		QVariantMap &card = found->second;
		QStringList names = card.value(QStringLiteral("stores")).toStringList();
		names << source.value(QStringLiteral("storeName")).toString();
		card[QStringLiteral("stores")] = names;
		QVariantList sources = card.value(QStringLiteral("sources")).toList();
		sources << source;
		card[QStringLiteral("sources")] = sources;
		card[QStringLiteral("installable")] = card.value(QStringLiteral("installable")).toBool()
			|| source.value(QStringLiteral("installable")).toBool();
		card[QStringLiteral("available")] = card.value(QStringLiteral("available")).toBool()
			|| source.value(QStringLiteral("available")).toBool();
		card[QStringLiteral("update")] = card.value(QStringLiteral("update")).toBool() || update;
		if(card.value(QStringLiteral("description")).toString().isEmpty())
			card[QStringLiteral("description")] = description;
	};

	for(const StoreSource &source : app_->settings().stores)
	{
		if(!source.enabled)
			continue;
		if(source.kind == "catalog")
		{
			for(const store::StoreApp &app : list_)
			{
				const auto installed = installed_.find(app.titleId);
				// Newer in the catalog than on the PS5: an update.
				const bool update = installed != installed_.end() && app.installable()
					&& store::compareContentVersions(app.contentVersion, installed->second.contentVersion) > 0;
				add(app.titleId, itemFor(app), sourceFor(source, &app, nullptr), update, QString());
			}
			continue;
		}
		for(const HbStore &hb : hbStores_)
		{
			if(hb.source.url != source.url)
				continue;
			for(const store::HbStoreApp &app : hb.apps)
			{
				QVariantMap fields;
				fields[QStringLiteral("titleId")] = QString::fromStdString(app.titleId);
				fields[QStringLiteral("name")] = QString::fromStdString(app.name);
				fields[QStringLiteral("kind")] = QString::fromStdString(app.kind());
				fields[QStringLiteral("author")] = QString::fromStdString(app.author);
				fields[QStringLiteral("version")] = QString::fromStdString(app.version);
				fields[QStringLiteral("contentVersion")] = QString();
				fields[QStringLiteral("size")] = QString::fromStdString(app.sizeText);
				fields[QStringLiteral("updated")] = QString::fromStdString(app.released.substr(0, 10));
				fields[QStringLiteral("released")] = QString::fromStdString(app.released.substr(0, 10));
				fields[QStringLiteral("sandbox")] = QString();
				fields[QStringLiteral("format")] = QStringLiteral("pkg");
				const QVariantMap from = sourceFor(source, nullptr, &app);
				const auto installed = installedPackages_.find(app.titleId);
				const bool update = installed != installedPackages_.end() && from.value(QStringLiteral("installable")).toBool()
					&& store::compareLooseVersions(app.version, installed->second) > 0;
				add(app.titleId, fields, from, update, QString::fromStdString(app.description));
			}
		}
	}

	QVariantList items;
	for(const std::string &titleId : order)
	{
		QVariantMap item = cards[titleId];
		const auto icon = icons_.find(titleId);
		item[QStringLiteral("icon")] = icon == icons_.end() ? QUrl()
			: QUrl::fromLocalFile(QString::fromStdString(icon->second));
		const auto folder = installed_.find(titleId);
		const auto package = installedPackages_.find(titleId);
		const bool there = folder != installed_.end() || package != installedPackages_.end();
		item[QStringLiteral("installed")] = there;
		// As a folder of the catalog's (which the store can take away again),
		// or as a package.
		item[QStringLiteral("installedAs")] = folder != installed_.end() ? QStringLiteral("folder")
			: there ? QStringLiteral("pkg") : QString();
		item[QStringLiteral("installedVersion")] = folder != installed_.end()
			? QString::fromStdString(folder->second.contentVersion)
			: package != installedPackages_.end() ? QString::fromStdString(package->second) : QString();
		items << item;
	}
	apps_ = items;
	emit appsChanged();
	emit storesChanged();
}

std::string StoreController::hbStoreDir(const std::string &cacheDir, const std::string &base)
{
	// "https://api.pkg-zone.com" → "api.pkg-zone.com"; a path keeps its parts.
	std::string name = base.substr(base.find("://") == std::string::npos ? 0 : base.find("://") + 3);
	for(char &c : name)
		if(!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-')
			c = '_';
	return joinPath(joinPath(cacheDir, "hb-stores"), name);
}

StoreController::HbStore StoreController::loadHbStore(const StoreSource &source, const std::string &cacheDir,
	const std::atomic<bool> *cancel)
{
	HbStore out;
	out.source = source;
	const std::string base = store::hbStoreBase(source.url);
	if(base.empty())
	{
		out.error = tr("Its address is not an http(s) one.");
		return out;
	}
	const std::string kept = joinPath(hbStoreDir(cacheDir, base), "store.db");
	HttpClient http(20000);
	HttpClient::FetchOptions options;
	options.headers.push_back(std::string("User-Agent: ") + store::kHbStoreAgent);
	options.cancel = cancel;
	// Its MD5 first: when the copy kept has it, nothing else is fetched.
	std::string hash;
	const HttpResponse answer = http.fetch(store::hbStoreHashUrl(base), options);
	if(answer.transportOk && answer.status == 200)
		hash = store::parseHbStoreHash(answer.body);
	const bool current = !hash.empty() && store::md5File(kept) == hash;
	if(!current)
	{
		http.setTimeoutMs(60000);
		const HttpResponse database = http.fetch(store::hbStoreDatabaseUrl(base), options);
		QString why;
		if(!database.transportOk)
			why = QString::fromStdString(database.error);
		else if(database.status != 200)
			why = tr("it answered HTTP %1").arg(database.status);
		else if(!store::looksLikeSqlite(database.body))
			why = tr("what it sent is not a store's database");
		else if(!hash.empty() && store::md5Hex(database.body) != hash)
			why = tr("its database does not match the MD5 it gives");
		else if(!writeWhole(kept, database.body))
			why = tr("its database could not be kept on this PC");
		if(!why.isEmpty())
		{
			logWarning("Store: " + base + " — " + why.toStdString());
			out.error = why;
			out.offline = fileSize(kept) > 0;
		}
	}
	QString why;
	if(fileSize(kept) > 0 && !readHbStoreDatabase(QString::fromStdString(kept), &out.apps, &why))
	{
		out.apps.clear();
		out.offline = false;
		out.error = tr("its database cannot be read: %1").arg(why);
	}
	return out;
}

void StoreController::refresh()
{
	if(loading_)
		return;
	loading_ = true;
	error_.clear();
	emit stateChanged();
	// The icons of the last reading stop at the one under way.
	stopIcons_ = true;
	if(loader_.joinable())
		loader_.join();
	stopIcons_ = false;
	const std::string address = target().toStdString();
	const bool folders = canInstall();
	const bool reachable = !address.empty() && app_->ftpAnswers(address);
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	const std::string scratch = joinPath(cacheDir(), "work");
	const std::string cache = cacheDir();
	std::vector<StoreSource> sources;
	bool catalogOn = false;
	for(const StoreSource &source : app_->settings().stores)
	{
		if(!source.enabled)
			continue;
		if(source.kind == "catalog")
			catalogOn = true;
		else
			sources.push_back(source);
	}
	loader_ = std::thread([this, folders, reachable, config, scratch, cache, sources, catalogOn]() {
		std::string why;
		bool ok = !catalogOn;
		bool offline = false;
		std::vector<store::StoreApp> list;
		std::string base;
		if(catalogOn)
		{
			std::lock_guard<std::mutex> lock(catalogMutex_);
			ok = catalog_->refresh(&why);
			list = catalog_->apps();
			offline = catalog_->offline();
			base = catalog_->base();
		}
		if(!ok)
			logWarning("Store: the catalog could not be read: " + why);
		// Each store shows as soon as it is read: a slow one keeps no other
		// waiting.
		const QString catalogHost = QUrl(QString::fromStdString(base)).host();
		QMetaObject::invokeMethod(this, [this, ok, why, offline, list, catalogHost]() {
			catalogSource_ = catalogHost;
			error_ = ok ? QString() : QString::fromStdString(why);
			offline_ = offline;
			list_ = list;
			publish();
		}, Qt::QueuedConnection);
		// What the console has: the PS5's app folders, and what is under
		// /user/app (the versions of those once the stores say which matter).
		std::map<std::string, Installed> installed;
		std::set<std::string> titles;
		bool known = false;
		std::unique_ptr<FtpClient> ftp;
		if(reachable)
		{
			FtpClient::Config readOnly = config;
			readOnly.maxRetries = 1;
			ftp = std::make_unique<FtpClient>(readOnly);
			if(folders)
			{
				FtpStoreRemote remote(*ftp);
				store::StoreInstaller installer;
				std::string listError;
				for(const store::InstalledApp &app : installer.installed(remote, scratch, &listError))
					installed[app.titleId] = { app.contentVersion };
				known = listError.empty();
			}
			std::vector<FtpEntry> apps;
			if(ftp->list("/user/app", &apps).ok)
				for(const FtpEntry &entry : apps)
				{
					std::string id = entry.name;
					std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return std::toupper(c); });
					if(entry.isDirectory)
						titles.insert(id);
				}
		}
		std::map<std::string, std::string> packages;
		for(const std::string &id : titles)
			packages[id] = std::string();
		QMetaObject::invokeMethod(this, [this, installed, packages, known]() {
			installed_ = installed;
			installedPackages_ = packages;
			installedKnown_ = known;
			publish();
		}, Qt::QueuedConnection);

		// The icons, a few at a time as they come (each is downloaded once).
		std::map<std::string, std::string> batch;
		auto flush = [this, &batch]() {
			if(batch.empty())
				return;
			QMetaObject::invokeMethod(this, [this, batch]() {
				for(const auto &pair : batch)
					// The catalog's first; a store's only where it has none.
					if(!icons_.count(pair.first) || pair.second.find("hb-stores") == std::string::npos)
						icons_[pair.first] = pair.second;
				publish();
			}, Qt::QueuedConnection);
			batch.clear();
		};
		std::set<std::string> pictured;
		for(const store::StoreApp &app : list)
		{
			if(cancel_ || stopIcons_)
				break;
			std::string icon;
			{
				std::lock_guard<std::mutex> lock(catalogMutex_);
				icon = catalog_->iconFile(app);
			}
			if(!icon.empty())
			{
				batch[app.titleId] = icon;
				pictured.insert(app.titleId);
			}
			if(batch.size() >= 12)
				flush();
		}
		flush();

		std::vector<HbStore> hbStores;
		std::set<std::string> offered;
		for(const StoreSource &source : sources)
		{
			if(cancel_)
				break;
			hbStores.push_back(loadHbStore(source, cache, &cancel_));
			for(const store::HbStoreApp &app : hbStores.back().apps)
				offered.insert(app.titleId);
			QMetaObject::invokeMethod(this, [this, loaded = hbStores.back()]() {
				auto same = std::find_if(hbStores_.begin(), hbStores_.end(),
					[&loaded](const HbStore &hb) { return hb.source.url == loaded.source.url; });
				if(same != hbStores_.end())
					*same = loaded;
				else
					hbStores_.push_back(loaded);
				publish();
			}, Qt::QueuedConnection);
		}
		// The version of what a store has and the console installed, from
		// the system's copy of its param.sfo.
		if(ftp)
		{
			std::error_code ignored;
			fs::create_directories(fs::u8path(scratch), ignored);
			for(const std::string &id : titles)
			{
				if(cancel_ || !offered.count(id))
					continue;
				const std::string local = joinPath(scratch, id + "-param.sfo");
				if(ftp->download("/system_data/priv/appmeta/" + id + "/param.sfo", local).ok)
				{
					std::string bytes;
					Sfo sfo;
					if(readWhole(local, &bytes) && sfo.parse(std::vector<uint8_t>(bytes.begin(), bytes.end())))
						packages[id] = trim(sfo.stringValue("APP_VER", sfo.stringValue("VERSION")));
				}
				fs::remove(fs::u8path(local), ignored);
			}
		}
		QMetaObject::invokeMethod(this, [this, packages, hbStores]() {
			loading_ = false;
			hbStores_ = hbStores;
			installedPackages_ = packages;
			publish();
			emit stateChanged();
			// "On this PC" says what this PS5 has.
			if(!localApps_.isEmpty())
				scanLocal();
		}, Qt::QueuedConnection);

		// A store's pictures: kept for good once had, and asked for one every
		// second and a half at most (the stores count the requests).
		for(const HbStore &hb : hbStores)
		{
			const std::string dir = joinPath(hbStoreDir(cache, store::hbStoreBase(hb.source.url)), "icons");
			std::vector<const store::HbStoreApp *> sorted;
			for(const store::HbStoreApp &app : hb.apps)
				sorted.push_back(&app);
			// In the order the page shows them, by name.
			std::sort(sorted.begin(), sorted.end(), [](const store::HbStoreApp *a, const store::HbStoreApp *b) {
				return QString::fromStdString(a->name).compare(QString::fromStdString(b->name), Qt::CaseInsensitive) < 0;
			});
			for(const store::HbStoreApp *app : sorted)
			{
				if(cancel_ || stopIcons_)
					break;
				if(pictured.count(app->titleId) || app->image.empty())
					continue;
				const std::string stem = joinPath(dir, sanitizeFileName(app->titleId + "-" + app->version));
				std::string path = fileSize(stem + ".png") > 0 ? stem + ".png" : stem + ".jpg";
				if(fileSize(path) <= 0)
				{
					HttpClient http(15000);
					HttpClient::FetchOptions options;
					options.headers.push_back(std::string("User-Agent: ") + store::kHbStoreAgent);
					options.cancel = &stopIcons_;
					const HttpResponse picture = http.fetch(app->image, options);
					const bool good = picture.transportOk && picture.status == 200 && looksLikePicture(picture.body);
					if(good)
					{
						path = stem + (picture.body.compare(0, 4, "\x89PNG") == 0 ? ".png" : ".jpg");
						writeWhole(path, picture.body);
					}
					for(int i = 0; i < 15 && !cancel_ && !stopIcons_; ++i)
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
					if(!good)
						continue;
				}
				batch[app->titleId] = path;
				pictured.insert(app->titleId);
				if(batch.size() >= 12)
					flush();
			}
			flush();
		}
	});
}

void StoreController::showDetails(const QString &titleId)
{
	QVariantMap pending;
	pending[QStringLiteral("titleId")] = titleId;
	pending[QStringLiteral("ready")] = false;
	for(const QVariant &entry : apps_)
		if(entry.toMap().value(QStringLiteral("titleId")).toString() == titleId)
		{
			pending = entry.toMap();
			pending[QStringLiteral("ready")] = false;
		}
	detail_ = pending;
	// Only the catalog has more to read; a store's database said it all.
	bool inCatalog = false;
	for(const QVariant &source : pending.value(QStringLiteral("sources")).toList())
		if(source.toMap().value(QStringLiteral("format")).toString() == QLatin1String("folder"))
			inCatalog = true;
	if(!inCatalog)
	{
		detail_[QStringLiteral("ready")] = true;
		emit detailChanged();
		return;
	}
	emit detailChanged();
	if(detailer_.joinable())
		detailer_.join();
	const std::string id = titleId.toStdString();
	detailer_ = std::thread([this, id, pending]() {
		store::StoreAppDetail detail;
		std::string why;
		bool ok = false;
		{
			std::lock_guard<std::mutex> lock(catalogMutex_);
			ok = catalog_->details(id, &detail, &why);
		}
		QVariantMap result = pending;
		result[QStringLiteral("ready")] = true;
		result[QStringLiteral("error")] = ok ? QString() : QString::fromStdString(why);
		if(ok)
		{
			result[QStringLiteral("description")] = QString::fromStdString(detail.description);
			result[QStringLiteral("license")] = QString::fromStdString(detail.license);
			result[QStringLiteral("sourceRepo")] = QString::fromStdString(detail.sourceRepo);
			result[QStringLiteral("page")] = QString::fromStdString(detail.page);
			result[QStringLiteral("releaseUrl")] = QString::fromStdString(detail.releaseUrl);
			result[QStringLiteral("releaseNotes")] = QString::fromStdString(detail.releaseNotes);
			result[QStringLiteral("releaseNotesTruncated")] = detail.releaseNotesTruncated;
			result[QStringLiteral("prerelease")] = detail.prerelease;
			result[QStringLiteral("artifactName")] = QString::fromStdString(detail.artifactName);
			result[QStringLiteral("safetyKnown")] = detail.safetyKnown;
			result[QStringLiteral("safetyRoutes")] = toStrings(detail.safetyRoutes);
			result[QStringLiteral("safetyHelpers")] = detail.safetyHelpers;
			result[QStringLiteral("safetyHelpersUnapproved")] = detail.safetyHelpersUnapproved;
			result[QStringLiteral("safetyNetwork")] = detail.safetyNetwork;
			result[QStringLiteral("safetyBuild")] = QString::fromStdString(detail.safetyBuild);
		}
		QMetaObject::invokeMethod(this, [this, result]() {
			// Only the one still asked for.
			if(detail_.value(QStringLiteral("titleId")) != result.value(QStringLiteral("titleId")))
				return;
			detail_ = result;
			emit detailChanged();
		}, Qt::QueuedConnection);
	});
}

void StoreController::startWork(const QString &titleId, const QString &what, const QString &kind,
	std::function<void()> work)
{
	if(!working_.isEmpty())
		return;
	working_ = titleId;
	stage_ = what;
	resetProgress();
	meterStage_.clear();
	samples_.clear();
	lastPostMs_ = 0;
	cancel_ = false;
	emit stateChanged();
	emit progressChanged();

	// Its card in the queue panel, beside the packages'.
	QVariantMap job;
	jobId_ = QStringLiteral("%1-%2").arg(titleId).arg(QDateTime::currentMSecsSinceEpoch());
	job[QStringLiteral("id")] = jobId_;
	job[QStringLiteral("titleId")] = titleId;
	job[QStringLiteral("name")] = titleId;
	for(const QVariant &entry : apps_)
	{
		const QVariantMap app = entry.toMap();
		if(app.value(QStringLiteral("titleId")).toString() != titleId)
			continue;
		job[QStringLiteral("name")] = app.value(QStringLiteral("name"));
		job[QStringLiteral("icon")] = app.value(QStringLiteral("icon"));
		job[QStringLiteral("version")] = app.value(QStringLiteral("version"));
	}
	job[QStringLiteral("kind")] = kind;
	job[QStringLiteral("state")] = QStringLiteral("working");
	job[QStringLiteral("stageText")] = what;
	job[QStringLiteral("percent")] = 0.0;
	job[QStringLiteral("console")] = targetName();
	jobs_.prepend(job);
	emit jobsChanged();
	emit app_->showPanel(QStringLiteral("queue"));

	if(worker_.joinable())
		worker_.join();
	worker_ = std::thread([work]() { work(); });
}

void StoreController::cancel() { cancel_ = true; }

void StoreController::updateJob(const QVariantMap &fields)
{
	for(QVariant &entry : jobs_)
	{
		QVariantMap job = entry.toMap();
		if(job.value(QStringLiteral("id")).toString() != jobId_)
			continue;
		for(auto it = fields.cbegin(); it != fields.cend(); ++it)
			job[it.key()] = it.value();
		entry = job;
		emit jobsChanged();
		return;
	}
}

void StoreController::removeJob(const QString &id)
{
	for(int i = 0; i < jobs_.size(); ++i)
		if(jobs_[i].toMap().value(QStringLiteral("id")).toString() == id
			&& jobs_[i].toMap().value(QStringLiteral("state")).toString() != QLatin1String("working"))
		{
			jobs_.removeAt(i);
			emit jobsChanged();
			return;
		}
}

void StoreController::clearFinishedJobs()
{
	QVariantList kept;
	for(const QVariant &entry : jobs_)
		if(entry.toMap().value(QStringLiteral("state")).toString() == QLatin1String("working"))
			kept << entry;
	if(kept.size() == jobs_.size())
		return;
	jobs_ = kept;
	emit jobsChanged();
}



void StoreController::checkHomeScreen(const QString &titleId)
{
	if(homeCheck_.value(QStringLiteral("busy")).toBool())
		return;
	const std::string address = target().toStdString();
	const std::string id = titleId.toStdString();
	homeCheck_ = { { QStringLiteral("titleId"), titleId }, { QStringLiteral("busy"), true } };
	emit homeCheckChanged();
	if(checker_.joinable())
		checker_.join();
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	const bool ftp = app_->ftpAnswers(address);
	const std::string scratch = joinPath(cacheDir(), "work");
	checker_ = std::thread([this, address, id, titleId, config, ftp, scratch]() {
		QVariantMap result { { QStringLiteral("titleId"), titleId }, { QStringLiteral("busy"), false } };
		// Asked to look again, its failed tries forgotten: worth it whatever
		// the log says, when its API lets the network in.
		result[QStringLiteral("rescanned")] =
			shadowmount::ask(address, "/api/v1/scan", "{\"reset_attempts\":true}");
		if(!ftp)
			result[QStringLiteral("verdict")] = QStringLiteral("no-ftp");
		else
		{
			FtpClient client(config);
			std::error_code ignored;
			fs::create_directories(fs::u8path(scratch), ignored);
			std::string log;
			bool found = false;
			// The current log, after the one it rotated away.
			for(const char *name : { "/data/shadowmount/debug.log.1", "/data/shadowmount/debug.log" })
			{
				const std::string local = joinPath(scratch, "shadowmount.log");
				if(client.download(name, local).ok)
				{
					found = true;
					QFile file(QString::fromStdString(local));
					if(file.open(QIODevice::ReadOnly))
						log += file.readAll().toStdString();
				}
				fs::remove(fs::u8path(local), ignored);
			}
			if(!found)
				result[QStringLiteral("verdict")] = QStringLiteral("no-log");
			else
			{
				const store::ShadowMountReport report = store::readShadowMountLog(log, id);
				static const char *names[] = { "not-seen", "registered", "settling", "bad-metadata", "failed",
					"gave-up", "duplicate" };
				result[QStringLiteral("verdict")] = QString::fromLatin1(names[static_cast<int>(report.verdict)]);
				result[QStringLiteral("code")] = QString::fromStdString(report.code);
				QStringList lines;
				for(const std::string &line : report.lines)
					lines << QString::fromStdString(line).trimmed();
				result[QStringLiteral("lines")] = lines;
			}
		}
		QMetaObject::invokeMethod(this, [this, result]() {
			homeCheck_ = result;
			emit homeCheckChanged();
		}, Qt::QueuedConnection);
	});
}

void StoreController::install(const QString &titleId, const QString &storeUrl)
{
	if(!working_.isEmpty())
		return;
	// The store it comes from: the one asked for, else the one that can.
	QVariantMap chosen;
	QString name = titleId;
	for(const QVariant &entry : apps_)
	{
		const QVariantMap app = entry.toMap();
		if(app.value(QStringLiteral("titleId")).toString() != titleId)
			continue;
		name = app.value(QStringLiteral("name")).toString();
		for(const QVariant &value : app.value(QStringLiteral("sources")).toList())
		{
			const QVariantMap source = value.toMap();
			if(!source.value(QStringLiteral("installable")).toBool())
				continue;
			if(storeUrl.isEmpty() ? chosen.isEmpty() : source.value(QStringLiteral("store")).toString() == storeUrl)
				chosen = source;
		}
	}
	if(chosen.isEmpty())
		return;
	if(chosen.value(QStringLiteral("format")).toString() == QLatin1String("pkg"))
	{
		const std::string url = chosen.value(QStringLiteral("store")).toString().toStdString();
		for(const HbStore &hb : hbStores_)
		{
			if(hb.source.url != url)
				continue;
			for(const store::HbStoreApp &app : hb.apps)
				if(QString::fromStdString(app.titleId) == titleId)
					return installPackage(titleId, hb.source, app);
		}
		return;
	}
	if(!canInstall())
		return;
	const std::string id = titleId.toStdString();
	const std::string address = target().toStdString();
	const QString console = targetName();
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	const std::string cache = cacheDir();
	startWork(titleId, tr("Getting %1…").arg(name), QStringLiteral("install"), [this, id, address, name, console, config, cache]() {
		auto finish = [this, id](const QString &message, bool error, const std::string &version) {
			finishInstall(id, message, error, version);
		};
		store::StoreAppDetail detail;
		std::string why;
		bool known = false;
		{
			std::lock_guard<std::mutex> lock(catalogMutex_);
			known = catalog_->details(id, &detail, &why);
		}
		if(!known)
			return finish(tr("%1: its details could not be read: %2").arg(name, QString::fromStdString(why)), true, {});
		if(!detail.installable() || detail.artifactUrl.empty() || detail.sha256.size() != 64)
			return finish(tr("%1 is not offered as an app folder, which is what the app installs.").arg(name), true, {});

		// The archive, downloaded once and checked against the catalog.
		const std::string expected = toLower(detail.sha256);
		const std::string zip = joinPath(joinPath(cache, "downloads"), id + "-" + expected.substr(0, 16) + ".zip");
		std::error_code ignored;
		fs::create_directories(fs::u8path(joinPath(cache, "downloads")), ignored);
		if(fileSize(zip) <= 0 || toLower(sha256File(zip)) != expected)
		{
			const std::string part = zip + ".part";
			HttpClient http(30000);
			const HttpClient::DownloadResult result = http.download(detail.artifactUrl, part,
				[this](int64_t done, int64_t total) {
					if(cancel_)
						return false;
					setProgress(QStringLiteral("download"), done, total);
					return true;
				});
			if(!result.ok)
			{
				fs::remove(fs::u8path(part), ignored);
				return finish(cancel_ ? tr("Installing %1 was cancelled.").arg(name)
									  : tr("%1 could not be downloaded: %2").arg(name, QString::fromStdString(result.error)),
					!cancel_, {});
			}
			fs::rename(fs::u8path(part), fs::u8path(zip), ignored);
		}
		setProgress(QStringLiteral("verify"), 0, 0);
		if(toLower(sha256File(zip)) != expected)
		{
			fs::remove(fs::u8path(zip), ignored);
			logWarning("Store: " + id + " does not match the catalog's SHA-256; not installed.");
			return finish(tr("The download of %1 is not the one the catalog vouches for (its checksum differs): "
							 "it was not installed.").arg(name), true, {});
		}

		putOnConsole(id, name, zip, false, address, console, config, cache, detail.contentVersion);
	});
}

void StoreController::finishInstall(const std::string &id, const QString &message, bool error,
	const std::string &version)
{
	QMetaObject::invokeMethod(this, [this, id, message, error, version]() {
		if(!error)
			installed_[id] = { version };
		working_.clear();
		stage_.clear();
		resetProgress();
		publish();
		emit stateChanged();
		emit progressChanged();
		emit finished(message, error);
		// "On this PC" says what is installed now.
		if(!error && !localApps_.isEmpty())
			scanLocal();
	}, Qt::QueuedConnection);
}

void StoreController::putOnConsole(const std::string &id, const QString &name, const std::string &zip, bool keepZip,
	const std::string &address, const QString &console, const FtpClient::Config &config, const std::string &cache,
	const std::string &version)
{
	std::string why;
	std::error_code ignored;
	// Its folder on the PS5.
	FtpClient ftp(config);
	FtpStoreRemote remote(ftp);
	store::StoreInstaller installer;
	store::InstallReport report;
	const bool ok = installer.install(remote, zip, id, joinPath(cache, "work"),
		[this](const std::string &stage, uint64_t done, uint64_t total) {
			if(cancel_)
				return false;
			setProgress(QString::fromStdString(stage), static_cast<int64_t>(done), static_cast<int64_t>(total));
			return true;
		},
		&why, &report);
	if(!ok)
		return finishInstall(id, why == "cancelled" ? tr("Installing %1 was cancelled.").arg(name)
													: tr("%1 could not be installed: %2").arg(name, QString::fromStdString(why)),
			why != "cancelled", {});
	if(!keepZip)
		fs::remove(fs::u8path(zip), ignored);
	// ShadowMountPlus finds it at its next scan; sooner when its API
	// lets this ask.
	// Look now, and try again titles it gave up on (an earlier failed
	// try leaves them out until it is reset or restarted).
	const bool scanned = shadowmount::ask(address, "/api/v1/scan", "{\"reset_attempts\":true}");
	// Its API only listens on the PS5 itself unless told otherwise: then
	// ShadowMountPlus is started again, which scans at once.
	bool restarted = false;
	if(!scanned)
	{
		setProgress(QStringLiteral("shadowmount"), 0, 0);
		restarted = shadowmount::restart(config, address, &cancel_);
	}
	QString message = tr("%1 is on %2 (%3).")
		.arg(name, console, QString::fromStdString(installer.installRoot() + "/" + id))
		+ QStringLiteral(" ") + (scanned ? tr("ShadowMountPlus puts it on the home screen now.")
			: restarted ? tr("ShadowMountPlus was started again: it puts it on the home screen in a few seconds.")
						: tr("ShadowMountPlus puts it on the home screen at its next scan, within a minute; if it does "
							 "not, \"Not on the home screen?\" in its window says why."));
	if(!report.carried.empty())
		message += QStringLiteral(" ") + tr("Your files in its folder (%n) went on into the new version.", "",
			static_cast<int>(report.carried.size()));
	if(!report.left.empty())
	{
		QStringList left;
		for(const std::string &name : report.left)
			left << QString::fromStdString(name);
		message += QStringLiteral(" ") + tr("Some of your files could not be moved to the new version; they are "
			"in %1: %2").arg(QString::fromStdString(report.earlierCopy), left.join(QStringLiteral(", ")));
	}
	if(!report.permissionsSet)
		message += QStringLiteral(" ") + tr("The console's FTP cannot set permissions: if the app does not "
			"start (CE-107750-0), set its folder and everything in it to 777.");
	finishInstall(id, message, false, version);
}

// ── a store's package ───────────────────────────────────────────────────

void StoreController::installPackage(const QString &titleId, const StoreSource &source, const store::HbStoreApp &app)
{
	if(!canInstallPackages())
		return;
	const QString name = QString::fromStdString(app.name);
	const QString console = targetName();
	const QString storeName = QString::fromStdString(source.name);
	const std::string folder = QDir::toNativeSeparators(localFolder()).toStdString();
	const std::string file = joinPath(folder, store::hbStorePackageName(app));
	logInfo("Store: downloading " + app.titleId + " from " + source.url);
	startWork(titleId, tr("Getting %1 from %2…").arg(name, storeName), QStringLiteral("download"),
		[this, app, name, console, storeName, file]() {
			auto finish = [this](const QString &message, bool error, const QString &queued) {
				QMetaObject::invokeMethod(this, [this, message, error, queued]() {
					// The console installs it from the queue, beside the rest.
					if(!queued.isEmpty())
						app_->addPaths({ queued }, 0);
					working_.clear();
					stage_.clear();
					resetProgress();
					publish();
					emit stateChanged();
					emit progressChanged();
					emit finished(message, error);
				}, Qt::QueuedConnection);
			};
			std::error_code ignored;
			fs::create_directories(fs::u8path(file).parent_path(), ignored);
			const std::string expected = toLower(app.md5);
			// Downloaded before, and whole: used again.
			const bool have = !expected.empty() && fileSize(file) > 0 && store::md5File(file) == expected;
			if(!have)
			{
				const std::string part = file + ".part";
				HttpClient http(30000);
				HttpClient::DownloadOptions options;
				options.userAgent = store::kHbStoreAgent;
				options.resume = true;
				HttpClient::DownloadResult result;
				// A connection that breaks goes on from where it stopped.
				for(int attempt = 0; attempt < 4 && !cancel_; ++attempt)
				{
					result = http.download(app.package, part,
						[this](int64_t done, int64_t total) {
							if(cancel_)
								return false;
							setProgress(QStringLiteral("download"), done, total);
							return true;
						},
						options);
					if(result.ok || cancel_ || result.status >= 400)
						break;
					std::this_thread::sleep_for(std::chrono::seconds(1 + attempt));
				}
				if(!result.ok)
				{
					fs::remove(fs::u8path(part), ignored);
					return finish(cancel_ ? tr("Getting %1 was cancelled.").arg(name)
										  : tr("%1 could not be downloaded from %2: %3")
												.arg(name, storeName, QString::fromStdString(result.error)),
						!cancel_, QString());
				}
				setProgress(QStringLiteral("verify"), 0, 0);
				if(!expected.empty() && store::md5File(part) != expected)
				{
					fs::remove(fs::u8path(part), ignored);
					logWarning("Store: " + app.titleId + " does not match the store's MD5; not installed.");
					return finish(tr("The download of %1 is not the one %2 lists (its MD5 differs): it was not "
									 "installed.").arg(name, storeName), true, QString());
				}
				// A package begins so; anything else is a page the store sent instead.
				std::string head;
				{
					std::ifstream in(fs::u8path(part), std::ios::binary);
					head.resize(4);
					in.read(&head[0], 4);
					head.resize(static_cast<size_t>(std::max<std::streamsize>(0, in.gcount())));
				}
				if(head != std::string("\x7F" "CNT", 4))
				{
					fs::remove(fs::u8path(part), ignored);
					return finish(tr("What %1 sent for %2 is not a package: it was not installed.").arg(storeName, name),
						true, QString());
				}
				fs::remove(fs::u8path(file), ignored);
				fs::rename(fs::u8path(part), fs::u8path(file), ignored);
				if(ignored)
					return finish(tr("%1 could not be put in %2.").arg(name, QString::fromStdString(file)), true, QString());
			}
			const QString path = QString::fromStdString(file);
			finish(tr("%1 was downloaded to %2 and is in the queue, to install on %3.")
					   .arg(name, QFileInfo(path).dir().dirName(), console),
				false, path);
		});
}

// ── the stores ───────────────────────────────────────────────────────────

QVariantList StoreController::stores() const
{
	QVariantList items;
	for(const StoreSource &source : app_->settings().stores)
	{
		QVariantMap item;
		item[QStringLiteral("url")] = QString::fromStdString(source.url);
		item[QStringLiteral("name")] = QString::fromStdString(source.name);
		item[QStringLiteral("kind")] = QString::fromStdString(source.kind);
		item[QStringLiteral("enabled")] = source.enabled;
		item[QStringLiteral("builtIn")] = Settings::builtInStore(source);
		int count = 0;
		QString error;
		bool offline = false;
		QString address = shortAddress(source.url);
		if(source.kind == "catalog")
		{
			count = static_cast<int>(list_.size());
			error = error_;
			offline = offline_;
			if(!catalogSource_.isEmpty())
				address = catalogSource_;
		}
		else
			for(const HbStore &hb : hbStores_)
				if(hb.source.url == source.url)
				{
					count = static_cast<int>(hb.apps.size());
					error = hb.error;
					offline = hb.offline;
				}
		item[QStringLiteral("address")] = address;
		item[QStringLiteral("count")] = count;
		item[QStringLiteral("error")] = error;
		item[QStringLiteral("offline")] = offline;
		items << item;
	}
	return items;
}

bool StoreController::addStore(const QString &address, const QString &name)
{
	const std::string base = store::hbStoreBase(address.toStdString());
	if(base.empty())
		return false;
	if(addingStore_)
		return true;
	for(const StoreSource &source : app_->settings().stores)
		if(source.kind == "hb-store" && store::hbStoreBase(source.url) == base)
		{
			emit storeAdded(false, tr("%1 is in the list already.").arg(QString::fromStdString(source.name)));
			return true;
		}
	addingStore_ = true;
	emit storesChanged();
	if(adder_.joinable())
		adder_.join();
	const QString label = name.trimmed().isEmpty() ? QUrl(QString::fromStdString(base)).host() : name.trimmed();
	const std::string cache = cacheDir();
	adder_ = std::thread([this, base, label, cache]() {
		StoreSource source { label.toStdString(), base, "hb-store", true };
		HbStore loaded = loadHbStore(source, cache, &cancel_);
		const bool isStore = loaded.error.isEmpty() || !loaded.apps.empty();
		bool mirror = false;
		if(!isStore)
		{
			// A copy of homebrew.page's catalog is no store of its own: its mirror.
			HttpClient http(15000);
			HttpClient::FetchOptions options;
			options.headers.push_back("User-Agent: OrbisLink/" + QCoreApplication::applicationVersion().toStdString());
			for(const std::string &at : { base + "/", base + "/api/v1/" })
			{
				const HttpResponse answer = http.fetch(at + "manifest.json", options);
				store::CatalogManifest manifest;
				if(answer.transportOk && answer.status == 200 && store::parseManifest(answer.body, &manifest, nullptr))
				{
					mirror = true;
					source.url = at;
					break;
				}
			}
		}
		QMetaObject::invokeMethod(this, [this, source, loaded, isStore, mirror]() {
			addingStore_ = false;
			if(mirror)
			{
				setCatalogAddress(QString::fromStdString(source.url));
				emit storeAdded(true, tr("That is a copy of homebrew.page's catalog: it is now the catalog's "
										 "mirror, checked against its signature as always."));
			}
			else if(!isStore)
				emit storeAdded(false, tr("No store answers there: %1").arg(loaded.error));
			else
			{
				app_->updateSettings([&source](Settings &s) { s.stores.push_back(source); });
				hbStores_.push_back(loaded);
				publish();
				emit storeAdded(true, tr("%1 was added, with %n app(s).", "", static_cast<int>(loaded.apps.size()))
										  .arg(QString::fromStdString(source.name)));
				// Its pictures, and what of it the console has.
				refresh();
			}
			emit storesChanged();
		}, Qt::QueuedConnection);
	});
	return true;
}

void StoreController::removeStore(const QString &url)
{
	const std::string target = url.toStdString();
	bool removed = false;
	app_->updateSettings([&target, &removed](Settings &s) {
		for(auto it = s.stores.begin(); it != s.stores.end(); ++it)
			if(it->url == target && !Settings::builtInStore(*it))
			{
				s.stores.erase(it);
				removed = true;
				return;
			}
	});
	if(!removed)
		return;
	hbStores_.erase(std::remove_if(hbStores_.begin(), hbStores_.end(),
						[&target](const HbStore &hb) { return hb.source.url == target; }),
		hbStores_.end());
	// Its copy on this PC goes with it.
	std::error_code ignored;
	fs::remove_all(fs::u8path(hbStoreDir(cacheDir(), store::hbStoreBase(target))), ignored);
	publish();
}

void StoreController::setStoreEnabled(const QString &url, bool enabled)
{
	const std::string target = url.toStdString();
	bool changed = false;
	app_->updateSettings([&](Settings &s) {
		for(StoreSource &source : s.stores)
			if(source.url == target && source.enabled != enabled)
			{
				source.enabled = enabled;
				changed = true;
			}
	});
	if(!changed)
		return;
	publish();
	// Turned on: read it.
	if(enabled)
		refresh();
}

// ── the catalog's address ────────────────────────────────────────────────

QString StoreController::catalogAddress() const
{
	return QString::fromStdString(app_->settings().storeCatalogAddress);
}

QString StoreController::defaultCatalogAddress() const
{
	return QString::fromStdString(store::StoreCatalog::defaultBase());
}

bool StoreController::setCatalogAddress(const QString &address)
{
	const QString own = address.trimmed();
	if(!own.isEmpty())
	{
		const QUrl url(own);
		if(!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
			|| url.host().isEmpty())
			return false;
	}
	app_->updateSettings([&own](Settings &s) { s.storeCatalogAddress = own.toStdString(); });
	{
		std::lock_guard<std::mutex> lock(catalogMutex_);
		catalog_->setCustomBase(own.toStdString());
	}
	emit catalogAddressChanged();
	refresh();
	return true;
}

// ── "On this PC" ─────────────────────────────────────────────────────────

QString StoreController::localFolder() const
{
	const std::string chosen = app_->settings().storeLocalFolder;
	if(!chosen.empty())
		return QString::fromStdString(chosen);
	return QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
}

QUrl StoreController::localFolderUrl() const
{
	return QUrl::fromLocalFile(localFolder());
}

void StoreController::setLocalFolder(const QUrl &folder)
{
	const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
	if(path.isEmpty())
		return;
	app_->updateSettings([&path](Settings &s) { s.storeLocalFolder = QDir::toNativeSeparators(path).toStdString(); });
	emit localChanged();
	scanLocal();
}

void StoreController::scanLocal()
{
	if(localScanning_)
		return;
	localScanning_ = true;
	emit localChanged();
	if(localScanner_.joinable())
		localScanner_.join();
	const QString folder = localFolder();
	const std::string scratch = joinPath(cacheDir(), "work");
	localScanner_ = std::thread([this, folder, scratch]() {
		QVariantList found;
		const QFileInfoList files = QDir(folder).entryInfoList({ QStringLiteral("*.zip"), QStringLiteral("*.ZIP") },
			QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
		for(const QFileInfo &file : files)
		{
			if(cancel_)
				break;
			store::ArchiveApp app;
			std::string why;
			QVariantMap item;
			item[QStringLiteral("path")] = file.absoluteFilePath();
			item[QStringLiteral("file")] = file.fileName();
			item[QStringLiteral("sizeText")] = QString::fromStdString(humanBytes(file.size()));
			if(store::readArchiveApp(QDir::toNativeSeparators(file.absoluteFilePath()).toStdString(), scratch, &app, &why))
			{
				item[QStringLiteral("titleId")] = QString::fromStdString(app.titleId);
				item[QStringLiteral("name")] = app.name.empty() ? QString::fromStdString(app.titleId)
																: QString::fromStdString(app.name);
				item[QStringLiteral("version")] = QString::fromStdString(app.contentVersion);
			}
			else
			{
				// Shown, with why it cannot be installed: a ZIP that is not an app.
				item[QStringLiteral("name")] = file.completeBaseName();
				item[QStringLiteral("problem")] = QString::fromStdString(why);
			}
			found << item;
		}
		QMetaObject::invokeMethod(this, [this, found]() {
			QVariantList items = found;
			// What the PS5 has, by title ID, as the catalog's apps say it.
			for(QVariant &entry : items)
			{
				QVariantMap item = entry.toMap();
				const auto there = installed_.find(item.value(QStringLiteral("titleId")).toString().toStdString());
				const bool installed = there != installed_.end();
				item[QStringLiteral("installed")] = installed;
				item[QStringLiteral("installedVersion")] = installed ? QString::fromStdString(there->second.contentVersion)
																	 : QString();
				item[QStringLiteral("update")] = installed
					&& store::compareContentVersions(item.value(QStringLiteral("version")).toString().toStdString(),
						   there->second.contentVersion) > 0;
				entry = item;
			}
			localApps_ = items;
			localScanning_ = false;
			emit localChanged();
		}, Qt::QueuedConnection);
	});
}

void StoreController::installLocal(const QString &path)
{
	// One at a time, as the catalog's.
	if(!canInstall() || !working_.isEmpty())
		return;
	// A path, or the file: URL a file dialog gives.
	const QUrl url(path);
	const QString local = url.isLocalFile() ? url.toLocalFile() : path;
	store::ArchiveApp app;
	std::string why;
	const std::string zip = QDir::toNativeSeparators(local).toStdString();
	if(!store::readArchiveApp(zip, joinPath(cacheDir(), "work"), &app, &why))
	{
		emit app_->notify(tr("Homebrew store"), tr("%1 cannot be installed: %2")
				.arg(QFileInfo(local).fileName(), QString::fromStdString(why)), true);
		return;
	}
	const QString titleId = QString::fromStdString(app.titleId);
	const QString name = app.name.empty() ? titleId : QString::fromStdString(app.name);
	const std::string address = target().toStdString();
	const QString console = targetName();
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	const std::string cache = cacheDir();
	const std::string id = app.titleId;
	const std::string version = app.contentVersion;
	logInfo("Store: installing " + id + " from " + zip);
	startWork(titleId, tr("Installing %1…").arg(name), QStringLiteral("install"),
		[this, id, name, zip, address, console, config, cache, version]() {
			putOnConsole(id, name, zip, true, address, console, config, cache, version);
		});
	updateJob({ { QStringLiteral("name"), name }, { QStringLiteral("version"), QString::fromStdString(version) } });
}

void StoreController::uninstall(const QString &titleId)
{
	if(!canInstall())
		return;
	QString name = titleId;
	for(const QVariant &entry : apps_)
		if(entry.toMap().value(QStringLiteral("titleId")).toString() == titleId)
			name = entry.toMap().value(QStringLiteral("name")).toString();
	const std::string id = titleId.toStdString();
	const std::string address = target().toStdString();
	const QString console = targetName();
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	startWork(titleId, tr("Removing %1…").arg(name), QStringLiteral("remove"), [this, id, address, name, console, config]() {
		// ShadowMountPlus first, when it lets this ask: it takes the app off
		// the home screen.
		const bool unregistered = shadowmount::ask(address, "/api/v1/games/uninstall", "{\"title_id\":\"" + id + "\"}");
		FtpClient ftp(config);
		FtpStoreRemote remote(ftp);
		store::StoreInstaller installer;
		std::string why;
		const bool ok = installer.uninstall(remote, id, &why);
		QString message;
		if(!ok)
			message = tr("%1 could not be removed: %2").arg(name, QString::fromStdString(why));
		else if(unregistered)
			message = tr("%1 was removed from %2.").arg(name, console);
		else
			message = tr("%1's folder was removed from %2. Its icon goes when ShadowMountPlus removes missing "
				"games; or delete it on the console.").arg(name, console);
		QMetaObject::invokeMethod(this, [this, id, ok, message]() {
			if(ok)
				installed_.erase(id);
			working_.clear();
			stage_.clear();
			resetProgress();
			publish();
			emit stateChanged();
			emit progressChanged();
			emit finished(message, !ok);
		}, Qt::QueuedConnection);
	});
}

} // namespace orbislink
