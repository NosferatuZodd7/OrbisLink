// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"
#include "orbislink/payloads/payload_catalog.h"
#include "orbislink/payloads/payload_layout.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace orbislink {

class AppController;

// The payload manager for QML (`payloads`): what a jailbroken PS4 (GoldHEN)
// or PS5 (etaHEN, the autoloader) keeps in its payload and plugin folders,
// over FTP — adding, renaming, deleting, downloading, choosing what starts
// by itself, editing the settings files — and sending a payload to the
// console's loader to run now, from the console or from this PC. One action
// at a time, on a thread of its own.
class PayloadsController : public QObject
{
	Q_OBJECT
	// The saved consoles: [{ address, name, type, active, ftp, target }].
	Q_PROPERTY(QVariantList consoles READ consoles NOTIFY consolesChanged)
	// The console shown (the one in use unless another was picked), what it
	// is ("ps4"/"ps5") and whether its FTP answers.
	Q_PROPERTY(QString target READ target NOTIFY consolesChanged)
	Q_PROPERTY(QString targetName READ targetName NOTIFY consolesChanged)
	Q_PROPERTY(QString kind READ kind NOTIFY consolesChanged)
	Q_PROPERTY(bool online READ online NOTIFY consolesChanged)
	// [{ id, path, exists, autoStart ("none"/"marker"/"list"/"ini"),
	//    configPath, files: [{ name, path, size, autoStart, sendable,
	//    critical, port, detailsName, version }] }] — the last two from
	//    PLDMGR's "<file>.json", when there is one.
	Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
	// [{ path, name, exists }]
	Q_PROPERTY(QVariantList configs READ configs NOTIFY foldersChanged)
	Q_PROPERTY(bool scanned READ scanned NOTIFY foldersChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY busyChanged)
	// What the last payloads sent printed back (the PS5 ELF loader passes
	// it on), and what happened while sending.
	Q_PROPERTY(QStringList output READ output NOTIFY outputChanged)
	// The community payload library (PS5): [{ name, version, category,
	// description, lastUpdate, source, filename, port, sendable, installed,
	// installedVersion, update (a copy is older), copies: [{ folder, path,
	// name, version, autoStart }] }].
	Q_PROPERTY(QVariantList catalog READ catalog NOTIFY catalogChanged)
	Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogChanged)
	Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogChanged)
	// The list shown is the last one kept: the site did not answer.
	Q_PROPERTY(bool catalogOffline READ catalogOffline NOTIFY catalogChanged)

public:
	explicit PayloadsController(AppController *app, QObject *parent = nullptr);
	~PayloadsController() override;

	QVariantList consoles() const;
	QString target() const;
	QString targetName() const;
	QString kind() const;
	bool online() const;
	QVariantList folders() const { return folders_; }
	QVariantList configs() const { return configs_; }
	bool scanned() const { return scanned_; }
	bool busy() const { return busy_; }
	QString status() const { return status_; }
	QStringList output() const { return output_; }
	QVariantList catalog() const { return catalog_; }
	bool catalogLoading() const { return catalogLoading_; }
	QString catalogError() const { return catalogError_; }
	bool catalogOffline() const { return catalogOffline_; }

	Q_INVOKABLE void refresh();
	Q_INVOKABLE void setTarget(const QString &address);
	// Files from this PC (paths or file:// URLs) into a folder (its id).
	Q_INVOKABLE void upload(const QString &folderId, const QStringList &files);
	// Deletes a file, with its auto-start mark and its line in the lists.
	Q_INVOKABLE void remove(const QString &path);
	// Renames a file in its folder; its auto-start goes with it.
	Q_INVOKABLE void rename(const QString &path, const QString &newName);
	Q_INVOKABLE void setAutoStart(const QString &path, bool on);
	// Copies a file to this PC's downloads folder and shows it.
	Q_INVOKABLE void download(const QString &path);
	// A settings file's text, through textLoaded; and back.
	Q_INVOKABLE void loadText(const QString &path);
	Q_INVOKABLE void saveText(const QString &path, const QString &text);
	// Runs a payload now: from the console (its path) or from this PC (a path
	// or file:// URL), to the loader port given (0: the usual one for it).
	Q_INVOKABLE void sendFromConsole(const QString &path, int port);
	Q_INVOKABLE void sendFromPc(const QString &file, int port);
	// The loader port a file usually goes to on the console shown.
	Q_INVOKABLE int portFor(const QString &fileName) const;
	Q_INVOKABLE void clearOutput();

	// The library: read again; run one now; put one in a folder (its id) on
	// the console, replacing an older one there (its auto-start follows).
	Q_INVOKABLE void refreshCatalog();
	Q_INVOKABLE void runFromCatalog(const QString &name);
	Q_INVOKABLE void installFromCatalog(const QString &name, const QString &folderId);
	// An autoload list as steps [{ name, delayMs }] and back to text (the
	// comment lines of `previous` kept).
	Q_INVOKABLE QVariantList autoloadSteps(const QString &text) const;
	Q_INVOKABLE QString autoloadText(const QVariantList &steps, const QString &previous) const;

	// The library's payload of that name, whatever the case: from this PC's
	// copy, else downloaded and checked; the list itself from the site, else
	// the copy kept. On any thread.
	static bool fetchFromLibrary(const QString &name, const std::atomic<bool> *cancel, std::vector<uint8_t> *bytes,
		QString *fileName, QString *error);

signals:
	void consolesChanged();
	void foldersChanged();
	void busyChanged();
	void outputChanged();
	void catalogChanged();
	void textLoaded(const QString &path, const QString &text, const QString &error);
	// The outcome of an action, for a notice.
	void finished(const QString &message, bool error);

private:
	using Job = std::function<QString(FtpClient &ftp, bool *error)>;
	// Runs a job on the worker, then (when asked) reads the folders again.
	void run(const QString &what, Job job, bool rescan);
	void appendOutput(const QStringList &lines);
	// On the worker: the payload to the loader, its words into the output.
	QString deliver(const std::vector<uint8_t> &payload, const QString &name, int port,
		const std::string &host, const std::atomic<bool> *cancel, bool *error);
	payloads::Kind consoleKind() const;
	FtpClient::Config ftpConfig() const;
	// The folder a console path belongs to, if it is one of the known ones.
	const payloads::Folder *folderOf(const std::string &path, std::vector<payloads::Folder> &all) const;

	AppController *app_;
	QString target_;
	QVariantList folders_;
	QVariantList configs_;
	bool scanned_ = false;
	bool busy_ = false;
	QString status_;
	QStringList output_;
	std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
	std::thread worker_;

	// The library.
	void publishCatalog();
	// Where this PC keeps the list and the payloads it downloaded.
	static QString cacheRoot();
	// On the worker: the payload's file, from this PC's copy when its
	// checksum still matches, else downloaded and checked.
	static bool fetchPayload(const payloads::CatalogPayload &payload, const std::atomic<bool> *cancel,
		std::vector<uint8_t> *bytes, QString *error);
	const payloads::CatalogPayload *catalogEntry(const QString &name) const;
	// The copies of a payload in the folders read: [{ folder, path, name,
	// version, autoStart }].
	QVariantList copiesOf(const payloads::CatalogPayload &payload) const;
	std::vector<payloads::CatalogPayload> catalogList_;
	QVariantList catalog_;
	bool catalogLoading_ = false;
	bool catalogOffline_ = false;
	QString catalogError_;
	std::thread catalogThread_;
};

} // namespace orbislink
