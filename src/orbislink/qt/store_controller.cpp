// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/store_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/net/http_client.h"
#include "orbislink/net/payload_sender.h"
#include "orbislink/payloads/payload_layout.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/payloads_controller.h"
#include "orbislink/store/shadowmount_log.h"
#include "orbislink/store/store_installer.h"
#include "orbislink/update/sha256.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <filesystem>

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

} // namespace

StoreController::StoreController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	// The catalog asks every client to say what it is.
	const std::string agent = "OrbisLink/" + QCoreApplication::applicationVersion().toStdString();
	catalog_ = std::make_unique<store::StoreCatalog>(cacheDir(), agent);
	// Which PS5s there are, and whose FTP answers.
	connect(app_, &AppController::settingsChanged, this, &StoreController::consolesChanged);
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
		updateJob(fields);
		jobId_.clear();
	});
}

StoreController::~StoreController()
{
	cancel_ = true;
	for(std::thread *thread : { &loader_, &detailer_, &worker_, &checker_ })
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
		if(console.type != "ps5")
			continue;
		QVariantMap item;
		item[QStringLiteral("address")] = QString::fromStdString(console.address);
		item[QStringLiteral("name")] = QString::fromStdString(console.name.empty() ? console.address : console.name);
		item[QStringLiteral("ftp")] = app_->ftpAnswers(console.address);
		item[QStringLiteral("target")] = QString::fromStdString(console.address) == chosen;
		items << item;
	}
	return items;
}

QString StoreController::target() const
{
	const Settings &settings = app_->settings();
	const ConsoleEntry *firstAnswering = nullptr;
	const ConsoleEntry *first = nullptr;
	for(const ConsoleEntry &console : settings.consoles)
	{
		if(console.type != "ps5")
			continue;
		// The one picked; else the one in use; else one whose FTP answers.
		if(!target_.isEmpty() && QString::fromStdString(console.address) == target_)
			return target_;
		if(!first)
			first = &console;
		if(!firstAnswering && app_->ftpAnswers(console.address))
			firstAnswering = &console;
	}
	for(const ConsoleEntry &console : settings.consoles)
		if(console.type == "ps5" && console.address == settings.consoleAddress)
			return QString::fromStdString(console.address);
	const ConsoleEntry *pick = firstAnswering ? firstAnswering : first;
	return pick ? QString::fromStdString(pick->address) : QString();
}

QString StoreController::targetName() const
{
	const QString address = target();
	for(const ConsoleEntry &console : app_->settings().consoles)
		if(QString::fromStdString(console.address) == address)
			return QString::fromStdString(console.name.empty() ? console.address : console.name);
	return QString();
}

bool StoreController::canInstall() const
{
	const QString address = target();
	return !address.isEmpty() && app_->ftpAnswers(address.toStdString());
}

void StoreController::setTarget(const QString &address)
{
	if(address == target_)
		return;
	target_ = address;
	emit consolesChanged();
	// What that PS5 has installed.
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
	QMetaObject::invokeMethod(this, [this, stage, fraction, speedText, amountText, etaText]() {
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
			{ QStringLiteral("etaText"), etaText } });
	}, Qt::QueuedConnection);
}

void StoreController::resetProgress()
{
	progress_ = 0.0;
	speedText_.clear();
	amountText_.clear();
}

void StoreController::publish()
{
	QVariantList items;
	for(const store::StoreApp &app : list_)
	{
		QVariantMap item = itemFor(app);
		const auto icon = icons_.find(app.titleId);
		item[QStringLiteral("icon")] = icon == icons_.end() ? QUrl()
			: QUrl::fromLocalFile(QString::fromStdString(icon->second));
		const auto installed = installed_.find(app.titleId);
		const bool there = installed != installed_.end();
		item[QStringLiteral("installed")] = there;
		item[QStringLiteral("installedVersion")] = there ? QString::fromStdString(installed->second.contentVersion)
														 : QString();
		// Newer in the catalog than on the PS5: an update.
		item[QStringLiteral("update")] = there && app.installable()
			&& store::compareContentVersions(app.contentVersion, installed->second.contentVersion) > 0;
		items << item;
	}
	apps_ = items;
	emit appsChanged();
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
	const bool reachable = canInstall();
	const FtpClient::Config config = app_->ftpClientConfigFor(address);
	const std::string scratch = joinPath(cacheDir(), "work");
	loader_ = std::thread([this, reachable, config, scratch]() {
		std::string why;
		bool ok = false;
		bool offline = false;
		std::vector<store::StoreApp> list;
		{
			std::lock_guard<std::mutex> lock(catalogMutex_);
			ok = catalog_->refresh(&why);
			list = catalog_->apps();
			offline = catalog_->offline();
		}
		if(!ok)
			logWarning("Store: the catalog could not be read: " + why);
		// What the PS5 has in its install folder, with the version of each.
		std::map<std::string, Installed> installed;
		bool known = false;
		if(reachable)
		{
			FtpClient::Config readOnly = config;
			readOnly.maxRetries = 1;
			FtpClient ftp(readOnly);
			FtpStoreRemote remote(ftp);
			store::StoreInstaller installer;
			std::string listError;
			for(const store::InstalledApp &app : installer.installed(remote, scratch, &listError))
				installed[app.titleId] = { app.contentVersion };
			known = listError.empty();
		}
		QMetaObject::invokeMethod(this, [this, ok, why, offline, list, installed, known]() {
			loading_ = false;
			error_ = ok ? QString() : QString::fromStdString(why);
			offline_ = offline;
			list_ = list;
			installed_ = installed;
			installedKnown_ = known;
			publish();
			emit stateChanged();
		}, Qt::QueuedConnection);

		// The icons, a few at a time as they come (each is downloaded once).
		std::map<std::string, std::string> batch;
		auto flush = [this, &batch]() {
			if(batch.empty())
				return;
			QMetaObject::invokeMethod(this, [this, batch]() {
				for(const auto &pair : batch)
					icons_[pair.first] = pair.second;
				publish();
			}, Qt::QueuedConnection);
			batch.clear();
		};
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
				batch[app.titleId] = icon;
			if(batch.size() >= 12)
				flush();
		}
		flush();
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

bool StoreController::restartShadowMount(const FtpClient::Config &config, const std::string &address) const
{
	// Its file on the console, where the payload managers keep it.
	FtpClient ftp(config);
	std::vector<std::string> folders = { "/data/etaHEN/payloads", "/data/etaHEN/plugins", "/data/ps5_autoloader",
		"/data/shadowmount" };
	std::vector<FtpEntry> pldmgr;
	if(ftp.list("/data/pldmgr/payloads", &pldmgr).ok)
		for(const FtpEntry &entry : pldmgr)
			if(entry.isDirectory && entry.name != "." && entry.name != "..")
				folders.push_back(entry.path);
	std::string found;
	for(const std::string &folder : folders)
	{
		std::vector<FtpEntry> entries;
		if(!ftp.list(folder, &entries).ok)
			continue;
		for(const FtpEntry &entry : entries)
			if(!entry.isDirectory && startsWith(toLower(entry.name), "shadowmount") && endsWith(toLower(entry.name), ".elf"))
				found = entry.path;
		if(!found.empty())
			break;
	}
	std::vector<uint8_t> bytes;
	if(!found.empty())
	{
		const QString local = QDir(QDir::tempPath()).filePath(
			QStringLiteral("orbislink-smp-%1.elf").arg(QCoreApplication::applicationPid()));
		if(ftp.download(found, local.toStdString()).ok)
		{
			QFile file(local);
			if(file.open(QIODevice::ReadOnly))
			{
				const QByteArray data = file.readAll();
				bytes.assign(data.constData(), data.constData() + data.size());
			}
		}
		QFile::remove(local);
	}
	QString why;
	if(bytes.empty() && !PayloadsController::fetchFromLibrary(QStringLiteral("ShadowMountPlus"), &cancel_, &bytes, nullptr, &why))
	{
		logWarning("Store: ShadowMountPlus could not be started again: " + why.toStdString());
		return false;
	}
	PayloadSender::Options options;
	options.listenMs = 1500;
	options.cancel = &cancel_;
	const PayloadSender::Result sent = PayloadSender::send(address, payloads::loaderPort(payloads::Kind::Ps5, "ShadowMountPlus.elf"), bytes, options);
	if(!sent.sent)
	{
		logWarning("Store: ShadowMountPlus could not be started again: " + sent.error);
		return false;
	}
	logInfo("Store: ShadowMountPlus started again (" + (found.empty() ? std::string("from the payload library") : found)
		+ ") to scan for the new app.");
	return true;
}

bool StoreController::askShadowMount(const std::string &address, const std::string &route,
	const std::string &body) const
{
	// Its API listens only on the PS5 itself unless "allow LAN access" is on;
	// then this goes straight to it. A short wait: most do not answer.
	HttpClient http(3000);
	const HttpResponse response = http.post("http://" + address + ":10101" + route, body);
	return response.transportOk && response.status == 200;
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
			askShadowMount(address, "/api/v1/scan", "{\"reset_attempts\":true}");
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

void StoreController::install(const QString &titleId)
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
	const std::string cache = cacheDir();
	startWork(titleId, tr("Getting %1…").arg(name), QStringLiteral("install"), [this, id, address, name, console, config, cache]() {
		auto finish = [this, id](const QString &message, bool error, const std::string &version) {
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
			}, Qt::QueuedConnection);
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
			return finish(why == "cancelled" ? tr("Installing %1 was cancelled.").arg(name)
											 : tr("%1 could not be installed: %2").arg(name, QString::fromStdString(why)),
				why != "cancelled", {});
		fs::remove(fs::u8path(zip), ignored);
		// ShadowMountPlus finds it at its next scan; sooner when its API
		// lets this ask.
		// Look now, and try again titles it gave up on (an earlier failed
		// try leaves them out until it is reset or restarted).
		const bool scanned = askShadowMount(address, "/api/v1/scan", "{\"reset_attempts\":true}");
		// Its API only listens on the PS5 itself unless told otherwise: then
		// ShadowMountPlus is started again, which scans at once.
		bool restarted = false;
		if(!scanned)
		{
			setProgress(QStringLiteral("shadowmount"), 0, 0);
			restarted = restartShadowMount(config, address);
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
		finish(message, false, detail.contentVersion);
	});
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
		const bool unregistered = askShadowMount(address, "/api/v1/games/uninstall", "{\"title_id\":\"" + id + "\"}");
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
