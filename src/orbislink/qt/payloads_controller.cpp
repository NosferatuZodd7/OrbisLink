// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/payloads_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/net/http_client.h"
#include "orbislink/net/payload_sender.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/translate_message.h"
#include "orbislink/update/sha256.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPair>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace orbislink {

using namespace payloads;

namespace {

QString localPathOf(const QString &file)
{
	const QUrl url(file);
	return url.isLocalFile() ? url.toLocalFile() : file;
}

QString scratchDir()
{
	const QString dir = QDir(QDir::tempPath()).filePath(
		QStringLiteral("orbislink-payloads-%1").arg(QCoreApplication::applicationPid()));
	QDir().mkpath(dir);
	return dir;
}

QString scratchFile(const QString &name)
{
	return QDir(scratchDir()).filePath(QStringLiteral("%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(name));
}

std::string parentOf(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

std::string nameOf(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool endsWith(const std::string &text, const std::string &suffix)
{
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Local files through Qt: a Windows user folder with accents in its name
// is not a path the narrow C++ streams can open.
QByteArray readLocal(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

std::vector<uint8_t> bytesOf(const QByteArray &data)
{
	return std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(data.constData()),
		reinterpret_cast<const uint8_t *>(data.constData()) + data.size());
}

// A small text file on the console; a missing one reads as empty.
bool readRemoteText(FtpClient &ftp, const std::string &path, std::string *text, bool *exists)
{
	text->clear();
	const QString local = scratchFile(QStringLiteral("read.txt"));
	const FtpResult got = ftp.download(path, local.toStdString());
	if(exists)
		*exists = got.ok;
	if(got.ok)
	{
		const QByteArray bytes = readLocal(local);
		text->assign(bytes.constData(), static_cast<size_t>(bytes.size()));
	}
	QFile::remove(local);
	return got.ok;
}

FtpResult writeRemoteText(FtpClient &ftp, const std::string &path, const std::string &text)
{
	const QString local = scratchFile(QStringLiteral("write.txt"));
	{
		QFile file(local);
		if(file.open(QIODevice::WriteOnly | QIODevice::Truncate))
			file.write(text.data(), static_cast<qint64>(text.size()));
	}
	ftp.makeDirectory(parentOf(path));
	const FtpResult sent = ftp.upload(local.toStdString(), path);
	QFile::remove(local);
	return sent;
}

QString autoStartName(AutoStart style)
{
	switch(style)
	{
		case AutoStart::Marker: return QStringLiteral("marker");
		case AutoStart::List: return QStringLiteral("list");
		case AutoStart::Ini: return QStringLiteral("ini");
		case AutoStart::None: break;
	}
	return QStringLiteral("none");
}

// "v1.10" and "1.10" are the same version.
QString bareVersion(const QString &version)
{
	QString text = version.trimmed().toLower();
	if(text.size() > 1 && text[0] == QLatin1Char('v') && text[1].isDigit())
		text.remove(0, 1);
	return text;
}

// The version in a file named the library's way ("kstuff-lite_v1.10.elf":
// "v1.10"); "" when its name has none.
QString versionInName(const CatalogPayload &payload, const QString &fileName)
{
	const QString name = QString::fromStdString(payload.name);
	if(fileName.size() <= name.size() + 1 || !fileName.startsWith(name, Qt::CaseInsensitive))
		return QString();
	const QChar separator = fileName[name.size()];
	if(separator != QLatin1Char('_') && separator != QLatin1Char('-'))
		return QString();
	const int dot = fileName.lastIndexOf(QLatin1Char('.'));
	return dot > name.size() + 1 ? fileName.mid(name.size() + 1, dot - name.size() - 1) : QString();
}

// A folder name made of a payload's name.
std::string folderNameOf(const std::string &name)
{
	std::string out = name;
	for(char &c : out)
		if(c == '/' || c == '\\' || c == ':')
			c = '_';
	return out == "." || out == ".." ? std::string("payload") : out;
}

QString uniqueLocal(const QString &dir, const QString &name)
{
	const QString wanted = QDir(dir).filePath(name);
	if(!QFileInfo::exists(wanted))
		return wanted;
	const QFileInfo info(wanted);
	const QString suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
	for(int n = 2; n < 1000; ++n)
	{
		const QString candidate = QDir(dir).filePath(
			QStringLiteral("%1 (%2)%3").arg(info.completeBaseName()).arg(n).arg(suffix));
		if(!QFileInfo::exists(candidate))
			return candidate;
	}
	return wanted;
}

} // namespace

PayloadsController::PayloadsController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	connect(app_, &AppController::settingsChanged, this, &PayloadsController::consolesChanged);
	connect(app_, &AppController::statusChanged, this, &PayloadsController::consolesChanged);
	connect(app_, &AppController::ftpReachableChanged, this, &PayloadsController::consolesChanged);
	// What is installed, and which is older, follows each reading.
	connect(this, &PayloadsController::foldersChanged, this, &PayloadsController::publishCatalog);
}

PayloadsController::~PayloadsController()
{
	cancel_->store(true);
	if(worker_.joinable())
		worker_.join();
	if(catalogThread_.joinable())
		catalogThread_.join();
	QDir(scratchDir()).removeRecursively();
}

QVariantList PayloadsController::consoles() const
{
	QVariantList items;
	const Settings &settings = app_->settings();
	const QString shown = target();
	for(const ConsoleEntry &console : settings.consoles)
	{
		const QString address = QString::fromStdString(console.address);
		QVariantMap item;
		item[QStringLiteral("address")] = address;
		item[QStringLiteral("name")] = QString::fromStdString(console.name.empty() ? console.address : console.name);
		item[QStringLiteral("type")] = QString::fromStdString(console.type);
		item[QStringLiteral("active")] = console.address == settings.consoleAddress;
		item[QStringLiteral("ftp")] = app_->ftpAnswers(console.address);
		item[QStringLiteral("target")] = address == shown;
		items << item;
	}
	return items;
}

QString PayloadsController::target() const
{
	const Settings &settings = app_->settings();
	if(!target_.isEmpty())
		for(const ConsoleEntry &console : settings.consoles)
			if(QString::fromStdString(console.address) == target_)
				return target_;
	return QString::fromStdString(settings.consoleAddress);
}

QString PayloadsController::targetName() const
{
	const QString address = target();
	for(const ConsoleEntry &console : app_->settings().consoles)
		if(QString::fromStdString(console.address) == address && !console.name.empty())
			return QString::fromStdString(console.name);
	return address;
}

Kind PayloadsController::consoleKind() const
{
	const QString address = target();
	for(const ConsoleEntry &console : app_->settings().consoles)
		if(QString::fromStdString(console.address) == address)
		{
			if(console.type == "ps5")
				return Kind::Ps5;
			if(console.type == "ps4")
				return Kind::Ps4;
		}
	// Never answered Remote Play: the jailbreak it runs says which it is.
	const QString jailbreak = app_->jailbreaks().value(address).toString();
	return jailbreak.contains(QLatin1String("etaHEN"), Qt::CaseInsensitive) ? Kind::Ps5 : Kind::Ps4;
}

QString PayloadsController::kind() const
{
	return consoleKind() == Kind::Ps5 ? QStringLiteral("ps5") : QStringLiteral("ps4");
}

bool PayloadsController::online() const { return app_->ftpAnswers(target().toStdString()); }

FtpClient::Config PayloadsController::ftpConfig() const
{
	return app_->ftpClientConfigFor(target().toStdString());
}

void PayloadsController::setTarget(const QString &address)
{
	const QString chosen = address.trimmed();
	const QString active = QString::fromStdString(app_->settings().consoleAddress);
	const QString next = chosen == active ? QString() : chosen;
	if(next == target_)
		return;
	target_ = next;
	folders_.clear();
	configs_.clear();
	scanned_ = false;
	emit consolesChanged();
	emit foldersChanged();
	refresh();
}

int PayloadsController::portFor(const QString &fileName) const
{
	return loaderPort(consoleKind(), fileName.toStdString());
}

const Folder *PayloadsController::folderOf(const std::string &path, std::vector<Folder> &all) const
{
	const std::string parent = parentOf(path);
	for(const Folder &folder : all)
		if(folder.path == parent || (folder.nested && parentOf(parent) == folder.path))
			return &folder;
	return nullptr;
}

void PayloadsController::run(const QString &what, Job job, bool rescan)
{
	if(busy_)
		return;
	if(worker_.joinable())
		worker_.join();
	busy_ = true;
	status_ = what;
	emit busyChanged();

	const FtpClient::Config config = ftpConfig();
	const Kind kind = consoleKind();
	const QString shown = target();
	worker_ = std::thread([this, config, kind, shown, job, rescan]() {
		FtpClient ftp(config);
		bool error = false;
		const QString message = job ? job(ftp, &error) : QString();

		// What each folder holds now, read here and handed over whole.
		QVariantList folders;
		QVariantList configs;
		if(rescan)
		{
			for(const Folder &folder : payloads::folders(kind))
			{
				std::vector<FtpEntry> entries;
				const bool exists = ftp.list(folder.path, &entries).ok;
				// A folder per payload (PLDMGR): what is in each, with its
				// ".json" left out but read: the name and version it says.
				QHash<QString, QPair<QString, QString>> details;
				if(exists && folder.nested)
				{
					std::vector<FtpEntry> inside;
					for(const FtpEntry &entry : entries)
					{
						if(!entry.isDirectory || entry.name == "." || entry.name == "..")
							continue;
						std::vector<FtpEntry> files;
						if(!ftp.list(entry.path, &files).ok)
							continue;
						QSet<QString> sidecars;
						for(const FtpEntry &file : files)
							if(!file.isDirectory && endsWith(file.name, ".json"))
								sidecars.insert(QString::fromStdString(file.name));
						for(const FtpEntry &file : files)
						{
							if(file.isDirectory || endsWith(file.name, ".json"))
								continue;
							inside.push_back(file);
							if(!sidecars.contains(QString::fromStdString(file.name + ".json")))
								continue;
							std::string json;
							std::string name;
							std::string version;
							if(readRemoteText(ftp, file.path + ".json", &json, nullptr))
								readDetails(json, &name, &version);
							details.insert(QString::fromStdString(file.path),
								{ QString::fromStdString(name), QString::fromStdString(version) });
						}
					}
					entries = inside;
				}
				std::string list;
				if(exists && !folder.configPath.empty())
					readRemoteText(ftp, folder.configPath, &list, nullptr);
				QSet<QString> markers;
				for(const FtpEntry &entry : entries)
					if(!entry.isDirectory && endsWith(entry.name, ".auto_start"))
						markers.insert(QString::fromStdString(entry.name));

				QVariantList files;
				for(const FtpEntry &entry : entries)
				{
					if(entry.isDirectory || entry.name == "." || entry.name == ".."
						|| endsWith(entry.name, ".auto_start") || entry.path == folder.configPath)
						continue;
					bool autoStart = false;
					switch(folder.autoStart)
					{
						case AutoStart::Marker:
							autoStart = markers.contains(QString::fromStdString(entry.name + ".auto_start"));
							break;
						case AutoStart::List: autoStart = autoloadListed(list, entry.name); break;
						case AutoStart::Ini: autoStart = pluginEnabled(list, entry.path); break;
						case AutoStart::None: break;
					}
					QVariantMap file;
					file[QStringLiteral("name")] = QString::fromStdString(entry.name);
					file[QStringLiteral("path")] = QString::fromStdString(entry.path);
					file[QStringLiteral("size")] = static_cast<double>(entry.size);
					file[QStringLiteral("autoStart")] = autoStart;
					file[QStringLiteral("sendable")] = sendable(kind, entry.name);
					file[QStringLiteral("critical")] = !folder.criticalFile.empty() && entry.name == folder.criticalFile;
					file[QStringLiteral("port")] = loaderPort(kind, entry.name);
					const auto said = details.constFind(QString::fromStdString(entry.path));
					file[QStringLiteral("detailsName")] = said == details.constEnd() ? QString() : said->first;
					file[QStringLiteral("version")] = said == details.constEnd() ? QString() : said->second;
					files << file;
				}
				std::sort(files.begin(), files.end(), [](const QVariant &a, const QVariant &b) {
					return a.toMap().value(QStringLiteral("name")).toString().compare(
							   b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
				});
				QVariantMap item;
				item[QStringLiteral("id")] = QString::fromStdString(folder.id);
				item[QStringLiteral("path")] = QString::fromStdString(folder.path);
				item[QStringLiteral("exists")] = exists;
				item[QStringLiteral("autoStart")] = autoStartName(folder.autoStart);
				item[QStringLiteral("configPath")] = QString::fromStdString(folder.configPath);
				item[QStringLiteral("files")] = files;
				folders << item;
			}
			for(const std::string &path : configFiles(kind))
			{
				int64_t size = -1;
				QVariantMap item;
				item[QStringLiteral("path")] = QString::fromStdString(path);
				item[QStringLiteral("name")] = QString::fromStdString(nameOf(path));
				item[QStringLiteral("exists")] = ftp.remoteSize(path, &size).ok;
				configs << item;
			}
		}

		QMetaObject::invokeMethod(this, [this, message, error, rescan, folders, configs, shown]() {
			busy_ = false;
			status_.clear();
			// Another console was picked meanwhile: this reading is not its.
			if(rescan && shown == target())
			{
				folders_ = folders;
				configs_ = configs;
				scanned_ = true;
				emit foldersChanged();
			}
			emit busyChanged();
			if(!message.isEmpty())
				emit finished(message, error);
		}, Qt::QueuedConnection);
	});
}

void PayloadsController::refresh()
{
	if(target().isEmpty() || !online())
		return;
	run(tr("Reading the console's payloads…"), nullptr, true);
}

void PayloadsController::upload(const QString &folderId, const QStringList &files)
{
	std::vector<Folder> all = payloads::folders(consoleKind());
	const auto found = std::find_if(all.begin(), all.end(),
		[&folderId](const Folder &f) { return f.id == folderId.toStdString(); });
	if(found == all.end() || files.isEmpty())
		return;
	const std::string folder = found->path;
	const bool nested = found->nested;
	QStringList locals;
	for(const QString &file : files)
		locals << localPathOf(file);
	run(tr("Sending to the console…"), [folder, nested, locals](FtpClient &ftp, bool *error) {
		ftp.makeDirectory(parentOf(folder));
		ftp.makeDirectory(folder);
		int sent = 0;
		QString problem;
		for(const QString &local : locals)
		{
			// PLDMGR keeps each payload in a folder of its own name.
			std::string into = folder;
			if(nested)
			{
				into = folder + "/" + QFileInfo(local).completeBaseName().toStdString();
				ftp.makeDirectory(into);
			}
			const std::string remote = into + "/" + QFileInfo(local).fileName().toStdString();
			const FtpResult result = ftp.upload(local.toStdString(), remote);
			if(result.ok)
				++sent;
			else
				problem = translateMessage(result.message);
		}
		*error = !problem.isEmpty();
		return problem.isEmpty()
			? tr("%n file(s) sent to %1.", "", sent).arg(QString::fromStdString(folder))
			: tr("Not everything went: %1").arg(problem);
	}, true);
}

void PayloadsController::remove(const QString &path)
{
	std::vector<Folder> all = payloads::folders(consoleKind());
	const Folder *folder = folderOf(path.toStdString(), all);
	const Folder copy = folder ? *folder : Folder {};
	const std::string remote = path.toStdString();
	run(tr("Deleting…"), [copy, remote](FtpClient &ftp, bool *error) {
		const FtpResult removed = ftp.removeFile(remote);
		if(!removed.ok)
		{
			*error = true;
			return tr("Could not delete %1: %2").arg(QString::fromStdString(nameOf(remote)),
				translateMessage(removed.message));
		}
		// Nothing is left pointing at it.
		if(copy.autoStart == AutoStart::Marker)
			ftp.removeFile(remote + ".auto_start");
		if(copy.nested)
		{
			// Its PLDMGR details, and its folder once empty.
			ftp.removeFile(remote + ".json");
			ftp.removeDirectory(parentOf(remote));
		}
		if(copy.autoStart == AutoStart::List || copy.autoStart == AutoStart::Ini)
		{
			std::string text;
			bool exists = false;
			readRemoteText(ftp, copy.configPath, &text, &exists);
			const std::string changed = copy.autoStart == AutoStart::List
				? autoloadSet(text, nameOf(remote), false)
				: pluginRemove(text, remote);
			if(exists && changed != text)
				writeRemoteText(ftp, copy.configPath, changed);
		}
		return tr("%1 deleted.").arg(QString::fromStdString(nameOf(remote)));
	}, true);
}

void PayloadsController::rename(const QString &path, const QString &newName)
{
	const QString name = newName.trimmed();
	if(name.isEmpty() || name.contains(QLatin1Char('/')) || name == QFileInfo(path).fileName())
		return;
	std::vector<Folder> all = payloads::folders(consoleKind());
	const Folder *folder = folderOf(path.toStdString(), all);
	const Folder copy = folder ? *folder : Folder {};
	const std::string from = path.toStdString();
	const std::string to = parentOf(from) + "/" + name.toStdString();
	run(tr("Renaming…"), [copy, from, to](FtpClient &ftp, bool *error) {
		const FtpResult moved = ftp.rename(from, to);
		if(!moved.ok)
		{
			*error = true;
			return tr("Could not rename %1: %2").arg(QString::fromStdString(nameOf(from)),
				translateMessage(moved.message));
		}
		if(copy.autoStart == AutoStart::Marker)
			ftp.rename(from + ".auto_start", to + ".auto_start");
		if(copy.nested)
			ftp.rename(from + ".json", to + ".json");
		if(copy.autoStart == AutoStart::List || copy.autoStart == AutoStart::Ini)
		{
			std::string text;
			bool exists = false;
			readRemoteText(ftp, copy.configPath, &text, &exists);
			const std::string changed = copy.autoStart == AutoStart::List
				? autoloadRename(text, nameOf(from), nameOf(to))
				: pluginRename(text, from, to);
			if(exists && changed != text)
				writeRemoteText(ftp, copy.configPath, changed);
		}
		return QString();
	}, true);
}

void PayloadsController::setAutoStart(const QString &path, bool on)
{
	std::vector<Folder> all = payloads::folders(consoleKind());
	const Folder *folder = folderOf(path.toStdString(), all);
	if(!folder || folder->autoStart == AutoStart::None)
		return;
	const Folder copy = *folder;
	const std::string remote = path.toStdString();
	run(on ? tr("Turning auto-start on…") : tr("Turning auto-start off…"),
		[copy, remote, on](FtpClient &ftp, bool *error) {
			FtpResult result = FtpResult::success();
			if(copy.autoStart == AutoStart::Marker)
				result = on ? writeRemoteText(ftp, remote + ".auto_start", std::string())
							: ftp.removeFile(remote + ".auto_start");
			else
			{
				std::string text;
				readRemoteText(ftp, copy.configPath, &text, nullptr);
				const std::string changed = copy.autoStart == AutoStart::List
					? autoloadSet(text, nameOf(remote), on)
					: pluginSet(text, remote, on);
				if(changed != text)
					result = writeRemoteText(ftp, copy.configPath, changed);
			}
			if(result.ok)
				return QString();
			*error = true;
			return tr("Could not change it: %1").arg(translateMessage(result.message));
		}, true);
}

void PayloadsController::download(const QString &path)
{
	const QString dir = app_->defaultDownloadDirectory();
	const std::string remote = path.toStdString();
	run(tr("Downloading…"), [this, dir, remote](FtpClient &ftp, bool *error) {
		QDir().mkpath(dir);
		const QString local = uniqueLocal(dir, QString::fromStdString(nameOf(remote)));
		const FtpResult got = ftp.download(remote, local.toStdString());
		if(!got.ok)
		{
			*error = true;
			return tr("Could not download %1: %2").arg(QString::fromStdString(nameOf(remote)),
				translateMessage(got.message));
		}
		QMetaObject::invokeMethod(this, [this, local]() { app_->openLocalFolder(local); },
			Qt::QueuedConnection);
		return tr("Saved to %1").arg(local);
	}, false);
}

void PayloadsController::loadText(const QString &path)
{
	const std::string remote = path.toStdString();
	run(tr("Reading %1…").arg(QFileInfo(path).fileName()), [this, path, remote](FtpClient &ftp, bool *) {
		std::string text;
		bool exists = false;
		readRemoteText(ftp, remote, &text, &exists);
		// A missing file opens empty: saving makes it.
		const QString content = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
		QMetaObject::invokeMethod(this, [this, path, content]() { emit textLoaded(path, content, QString()); },
			Qt::QueuedConnection);
		return QString();
	}, false);
}

void PayloadsController::saveText(const QString &path, const QString &text)
{
	const std::string remote = path.toStdString();
	const QByteArray bytes = text.toUtf8();
	const std::string content(bytes.constData(), static_cast<size_t>(bytes.size()));
	run(tr("Saving %1…").arg(QFileInfo(path).fileName()), [path, remote, content](FtpClient &ftp, bool *error) {
		const FtpResult sent = writeRemoteText(ftp, remote, content);
		if(sent.ok)
			return tr("%1 saved on the console.").arg(QFileInfo(path).fileName());
		*error = true;
		return tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), translateMessage(sent.message));
	}, true);
}

void PayloadsController::appendOutput(const QStringList &lines)
{
	output_ << lines;
	// The last few hundred lines are plenty to see what a payload said.
	while(output_.size() > 400)
		output_.removeFirst();
	emit outputChanged();
}

void PayloadsController::clearOutput()
{
	output_.clear();
	emit outputChanged();
}

QString PayloadsController::deliver(const std::vector<uint8_t> &payload, const QString &name, int port,
	const std::string &host, const std::atomic<bool> *cancel, bool *error)
{
	PayloadSender::Options options;
	// Long enough for a payload to say it started; the loader goes on
	// running it after the connection closes.
	options.listenMs = 5000;
	options.cancel = cancel;
	options.onLine = [this](const std::string &line) {
		const QString text = QString::fromStdString(line);
		QMetaObject::invokeMethod(this, [this, text]() { appendOutput({ text }); }, Qt::QueuedConnection);
		return true;
	};
	const PayloadSender::Result sent = PayloadSender::send(host, static_cast<uint16_t>(port), payload, options);
	if(!sent.sent)
	{
		*error = true;
		return tr("%1 did not go: %2 (port %3)").arg(name, translateMessage(sent.error)).arg(port);
	}
	logInfo("Payloads: " + name.toStdString() + " sent to port " + std::to_string(port) + ".");
	return tr("%1 sent to the console.").arg(name);
}

void PayloadsController::sendFromConsole(const QString &path, int port)
{
	const QString name = QFileInfo(path).fileName();
	const int chosen = port > 0 ? port : portFor(name);
	const std::string host = target().toStdString();
	const std::string remote = path.toStdString();
	const auto cancel = cancel_;
	appendOutput({ tr("→ %1 to %2:%3").arg(name, QString::fromStdString(host)).arg(chosen) });
	run(tr("Sending %1…").arg(name), [this, name, chosen, host, remote, cancel](FtpClient &ftp, bool *error) {
		const QString local = scratchFile(name);
		const FtpResult got = ftp.download(remote, local.toStdString());
		const std::vector<uint8_t> payload = got.ok ? bytesOf(readLocal(local)) : std::vector<uint8_t>();
		QFile::remove(local);
		if(!got.ok)
		{
			*error = true;
			return tr("Could not read %1 from the console: %2").arg(name, translateMessage(got.message));
		}
		return deliver(payload, name, chosen, host, cancel.get(), error);
	}, false);
}

void PayloadsController::sendFromPc(const QString &file, int port)
{
	const QString local = localPathOf(file);
	const QString name = QFileInfo(local).fileName();
	const int chosen = port > 0 ? port : portFor(name);
	const std::string host = target().toStdString();
	const auto cancel = cancel_;
	appendOutput({ tr("→ %1 to %2:%3").arg(name, QString::fromStdString(host)).arg(chosen) });
	run(tr("Sending %1…").arg(name), [this, local, name, chosen, host, cancel](FtpClient &, bool *error) {
		const std::vector<uint8_t> payload = bytesOf(readLocal(local));
		if(payload.empty())
		{
			*error = true;
			return tr("Could not read %1 on this PC.").arg(name);
		}
		return deliver(payload, name, chosen, host, cancel.get(), error);
	}, false);
}

// ───────────────────────────── the library

QString PayloadsController::cacheRoot()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("payloads"));
}

void PayloadsController::refreshCatalog()
{
	if(catalogLoading_)
		return;
	if(catalogThread_.joinable())
		catalogThread_.join();
	catalogLoading_ = true;
	catalogError_.clear();
	emit catalogChanged();

	const QString kept = QDir(cacheRoot()).filePath(QStringLiteral("catalog.json"));
	const auto cancel = cancel_;
	catalogThread_ = std::thread([this, kept, cancel]() {
		HttpClient http(20000);
		HttpClient::FetchOptions options;
		options.cancel = cancel.get();
		const HttpResponse reply = http.fetch(kCatalogUrl, options);
		std::string why;
		std::vector<CatalogPayload> list;
		if(reply.transportOk && reply.status == 200)
			list = parseCatalog(reply.body, &why);
		bool offline = false;
		QString error;
		if(!list.empty())
		{
			QDir().mkpath(QFileInfo(kept).path());
			QSaveFile file(kept);
			if(file.open(QIODevice::WriteOnly))
			{
				file.write(reply.body.data(), static_cast<qint64>(reply.body.size()));
				file.commit();
			}
		}
		else
		{
			// The list from last time, said to be so.
			const QByteArray saved = readLocal(kept);
			list = parseCatalog(std::string(saved.constData(), static_cast<size_t>(saved.size())), nullptr);
			offline = !list.empty();
			const QString reason = !reply.transportOk ? translateMessage(reply.error)
				: reply.status != 200 ? tr("the site answered %1").arg(reply.status)
				: QString::fromStdString(why);
			error = tr("The payload list could not be read: %1").arg(reason);
			logWarning("Payloads: the payload list could not be read: " + reason.toStdString());
		}
		if(cancel->load())
			return;
		QMetaObject::invokeMethod(this, [this, list, offline, error]() {
			catalogList_ = list;
			catalogLoading_ = false;
			catalogOffline_ = offline;
			catalogError_ = error;
			publishCatalog();
		}, Qt::QueuedConnection);
	});
}

const CatalogPayload *PayloadsController::catalogEntry(const QString &name) const
{
	for(const CatalogPayload &payload : catalogList_)
		if(QString::fromStdString(payload.name) == name)
			return &payload;
	return nullptr;
}

QVariantList PayloadsController::copiesOf(const CatalogPayload &payload) const
{
	QVariantList copies;
	const QString wanted = QString::fromStdString(payload.name);
	const QString latestFile = QString::fromStdString(payload.filename);
	for(const QVariant &f : folders_)
	{
		const QVariantMap folder = f.toMap();
		for(const QVariant &v : folder.value(QStringLiteral("files")).toList())
		{
			const QVariantMap file = v.toMap();
			const QString fileName = file.value(QStringLiteral("name")).toString();
			// What PLDMGR wrote beside it says which it is; else its name.
			const QString detailsName = file.value(QStringLiteral("detailsName")).toString();
			const bool same = detailsName.isEmpty() ? isFileOf(payload, fileName.toStdString())
													: detailsName.compare(wanted, Qt::CaseInsensitive) == 0;
			if(!same)
				continue;
			QString version = file.value(QStringLiteral("version")).toString();
			if(version.isEmpty())
				version = fileName.compare(latestFile, Qt::CaseInsensitive) == 0 ? QString::fromStdString(payload.version)
																				  : versionInName(payload, fileName);
			QVariantMap copy;
			copy[QStringLiteral("folder")] = folder.value(QStringLiteral("id"));
			copy[QStringLiteral("path")] = file.value(QStringLiteral("path"));
			copy[QStringLiteral("name")] = fileName;
			copy[QStringLiteral("version")] = version;
			copy[QStringLiteral("autoStart")] = file.value(QStringLiteral("autoStart"));
			copies << copy;
		}
	}
	return copies;
}

void PayloadsController::publishCatalog()
{
	QVariantList items;
	const Kind kind = consoleKind();
	for(const CatalogPayload &payload : catalogList_)
	{
		const QVariantList copies = copiesOf(payload);
		const QString latest = bareVersion(QString::fromStdString(payload.version));
		QString installedVersion;
		bool update = false;
		for(const QVariant &c : copies)
		{
			const QString version = c.toMap().value(QStringLiteral("version")).toString();
			if(version.isEmpty())
				continue;
			if(installedVersion.isEmpty())
				installedVersion = version;
			if(bareVersion(version) != latest)
				update = true;
		}
		QVariantMap item;
		item[QStringLiteral("name")] = QString::fromStdString(payload.name);
		item[QStringLiteral("version")] = QString::fromStdString(payload.version);
		item[QStringLiteral("category")] = QString::fromStdString(payload.category);
		item[QStringLiteral("description")] = QString::fromStdString(payload.description);
		item[QStringLiteral("lastUpdate")] = QString::fromStdString(payload.lastUpdate);
		item[QStringLiteral("source")] = QString::fromStdString(payload.source);
		item[QStringLiteral("filename")] = QString::fromStdString(payload.filename);
		item[QStringLiteral("port")] = loaderPort(kind, payload.filename);
		item[QStringLiteral("sendable")] = sendable(kind, payload.filename);
		item[QStringLiteral("installed")] = !copies.isEmpty();
		item[QStringLiteral("installedVersion")] = installedVersion;
		item[QStringLiteral("update")] = update;
		item[QStringLiteral("copies")] = copies;
		items << item;
	}
	catalog_ = items;
	emit catalogChanged();
}

bool PayloadsController::fetchPayload(const CatalogPayload &payload, const std::atomic<bool> *cancel,
	std::vector<uint8_t> *bytes, QString *error)
{
	const auto vouched = [&payload](const QByteArray &data) {
		return payload.checksum.empty()
			|| sha256Hex(std::string(data.constData(), static_cast<size_t>(data.size()))) == payload.checksum;
	};
	const QString kept = QDir(cacheRoot()).filePath(
		QStringLiteral("files/%1").arg(QString::fromStdString(payload.filename)));
	QByteArray data = readLocal(kept);
	if(data.isEmpty() || !vouched(data))
	{
		HttpClient http(120000);
		HttpClient::FetchOptions options;
		options.cancel = cancel;
		const HttpResponse reply = http.fetch(payload.url, options);
		if(!reply.transportOk || reply.status != 200 || reply.body.empty())
		{
			*error = !reply.transportOk ? translateMessage(reply.error) : tr("the site answered %1").arg(reply.status);
			return false;
		}
		data = QByteArray(reply.body.data(), static_cast<int>(reply.body.size()));
		if(!vouched(data))
		{
			logWarning("Payloads: " + payload.filename + " does not match the list's SHA-256; not used.");
			*error = tr("it is not the file the list vouches for (its checksum differs)");
			return false;
		}
		QDir().mkpath(QFileInfo(kept).path());
		QSaveFile file(kept);
		if(file.open(QIODevice::WriteOnly))
		{
			file.write(data);
			file.commit();
		}
	}
	*bytes = bytesOf(data);
	return true;
}

bool PayloadsController::fetchFromLibrary(const QString &name, const std::atomic<bool> *cancel,
	std::vector<uint8_t> *bytes, QString *fileName, QString *error)
{
	const auto find = [&name](const std::vector<CatalogPayload> &list) -> const CatalogPayload * {
		for(const CatalogPayload &payload : list)
			if(QString::fromStdString(payload.name).compare(name, Qt::CaseInsensitive) == 0)
				return &payload;
		return nullptr;
	};
	const QByteArray kept = readLocal(QDir(cacheRoot()).filePath(QStringLiteral("catalog.json")));
	std::vector<CatalogPayload> list = parseCatalog(std::string(kept.constData(), static_cast<size_t>(kept.size())), nullptr);
	if(!find(list))
	{
		HttpClient http(20000);
		HttpClient::FetchOptions options;
		options.cancel = cancel;
		const HttpResponse reply = http.fetch(kCatalogUrl, options);
		if(reply.transportOk && reply.status == 200)
			list = parseCatalog(reply.body, nullptr);
	}
	const CatalogPayload *payload = find(list);
	if(!payload)
	{
		*error = tr("%1 is not in the payload list.").arg(name);
		return false;
	}
	if(fileName)
		*fileName = QString::fromStdString(payload->filename);
	return fetchPayload(*payload, cancel, bytes, error);
}

void PayloadsController::runFromCatalog(const QString &name)
{
	const CatalogPayload *found = catalogEntry(name);
	if(!found || target().isEmpty())
		return;
	const CatalogPayload payload = *found;
	const QString file = QString::fromStdString(payload.filename);
	const int port = loaderPort(consoleKind(), payload.filename);
	const std::string host = target().toStdString();
	const auto cancel = cancel_;
	appendOutput({ tr("→ %1 to %2:%3").arg(file, QString::fromStdString(host)).arg(port) });
	run(tr("Sending %1…").arg(file), [this, payload, name, file, port, host, cancel](FtpClient &, bool *error) {
		std::vector<uint8_t> bytes;
		QString why;
		if(!fetchPayload(payload, cancel.get(), &bytes, &why))
		{
			*error = true;
			return tr("%1 could not be downloaded: %2").arg(name, why);
		}
		return deliver(bytes, file, port, host, cancel.get(), error);
	}, false);
}

void PayloadsController::installFromCatalog(const QString &name, const QString &folderId)
{
	const CatalogPayload *found = catalogEntry(name);
	const std::vector<Folder> all = payloads::folders(consoleKind());
	const auto folder = std::find_if(all.begin(), all.end(),
		[&folderId](const Folder &f) { return f.id == folderId.toStdString(); });
	if(!found || folder == all.end())
		return;
	const CatalogPayload payload = *found;
	const Folder into = *folder;

	// The copies already in that folder make way for this one, and whether
	// they started by themselves carries over.
	struct Older
	{
		std::string path;
		bool autoStart = false;
	};
	std::vector<Older> older;
	for(const QVariant &c : copiesOf(payload))
	{
		const QVariantMap copy = c.toMap();
		if(copy.value(QStringLiteral("folder")).toString() == folderId)
			older.push_back({ copy.value(QStringLiteral("path")).toString().toStdString(),
				copy.value(QStringLiteral("autoStart")).toBool() });
	}

	const auto cancel = cancel_;
	run(tr("Installing %1…").arg(name), [payload, name, into, older, cancel](FtpClient &ftp, bool *error) {
		std::vector<uint8_t> bytes;
		QString why;
		if(!fetchPayload(payload, cancel.get(), &bytes, &why))
		{
			*error = true;
			return tr("%1 could not be downloaded: %2").arg(name, why);
		}
		// PLDMGR keeps each payload in a folder of its name.
		const std::string dir = into.nested ? into.path + "/" + folderNameOf(payload.name) : into.path;
		const std::string remote = dir + "/" + payload.filename;
		ftp.makeDirectory(parentOf(into.path));
		ftp.makeDirectory(into.path);
		if(into.nested)
			ftp.makeDirectory(dir);
		const QString local = scratchFile(QString::fromStdString(payload.filename));
		{
			QFile file(local);
			if(file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size()));
		}
		const FtpResult sent = ftp.upload(local.toStdString(), remote);
		QFile::remove(local);
		if(!sent.ok)
		{
			*error = true;
			return tr("Could not copy %1 to the console: %2").arg(name, translateMessage(sent.message));
		}
		// What PLDMGR itself writes beside the payloads it installs.
		if(into.nested)
			writeRemoteText(ftp, remote + ".json",
				detailsJson(payload, QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()));

		bool autoStart = false;
		std::vector<std::string> replaced;
		for(const Older &old : older)
		{
			autoStart = autoStart || old.autoStart;
			if(old.path == remote)
				continue;
			replaced.push_back(nameOf(old.path));
			ftp.removeFile(old.path);
			if(into.autoStart == AutoStart::Marker)
				ftp.removeFile(old.path + ".auto_start");
			if(into.nested)
			{
				ftp.removeFile(old.path + ".json");
				if(parentOf(old.path) != dir)
					ftp.removeDirectory(parentOf(old.path));
			}
		}
		if(into.autoStart == AutoStart::Marker && autoStart)
			writeRemoteText(ftp, remote + ".auto_start", std::string());
		if(into.autoStart == AutoStart::List && !replaced.empty())
		{
			// The new file takes the old one's place in the order.
			std::string text;
			bool exists = false;
			readRemoteText(ftp, into.configPath, &text, &exists);
			std::string changed = text;
			for(const std::string &old : replaced)
				changed = autoloadListed(changed, payload.filename) ? autoloadSet(changed, old, false)
																   : autoloadRename(changed, old, payload.filename);
			if(exists && changed != text)
				writeRemoteText(ftp, into.configPath, changed);
		}
		logInfo("Payloads: " + payload.name + " " + payload.version + " installed in " + dir + ".");
		return autoStart ? tr("%1 %2 is in %3, and still starts by itself.")
							   .arg(name, QString::fromStdString(payload.version), QString::fromStdString(dir))
						 : tr("%1 %2 is in %3.").arg(name, QString::fromStdString(payload.version),
							   QString::fromStdString(dir));
	}, true);
}

QVariantList PayloadsController::autoloadSteps(const QString &text) const
{
	QVariantList steps;
	const QByteArray bytes = text.toUtf8();
	for(const AutoloadStep &step :
		payloads::autoloadSteps(std::string(bytes.constData(), static_cast<size_t>(bytes.size()))))
	{
		QVariantMap item;
		item[QStringLiteral("name")] = QString::fromStdString(step.name);
		item[QStringLiteral("delayMs")] = step.delayMs;
		steps << item;
	}
	return steps;
}

QString PayloadsController::autoloadText(const QVariantList &steps, const QString &previous) const
{
	std::vector<AutoloadStep> list;
	for(const QVariant &v : steps)
	{
		const QVariantMap step = v.toMap();
		list.push_back({ step.value(QStringLiteral("name")).toString().trimmed().toStdString(),
			std::max(0, step.value(QStringLiteral("delayMs")).toInt()) });
	}
	const QByteArray before = previous.toUtf8();
	const std::string text =
		payloads::autoloadText(list, std::string(before.constData(), static_cast<size_t>(before.size())));
	return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

} // namespace orbislink
