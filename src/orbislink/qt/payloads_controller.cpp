// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/payloads_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/net/payload_sender.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/translate_message.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSet>
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
}

PayloadsController::~PayloadsController()
{
	cancel_->store(true);
	if(worker_.joinable())
		worker_.join();
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
				// ".json" left out.
				if(exists && folder.nested)
				{
					std::vector<FtpEntry> inside;
					for(const FtpEntry &entry : entries)
					{
						if(!entry.isDirectory || entry.name == "." || entry.name == "..")
							continue;
						std::vector<FtpEntry> files;
						if(ftp.list(entry.path, &files).ok)
							for(const FtpEntry &file : files)
								if(!file.isDirectory && !endsWith(file.name, ".json"))
									inside.push_back(file);
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

} // namespace orbislink
