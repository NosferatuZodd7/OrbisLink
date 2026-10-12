// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/cheats/cheats.h"
#include "orbislink/ftp/ftp_client.h"

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

// Cheats and game patches for QML (`cheats`), for a game the console in use
// has installed: what the cheat repositories have for it (GoldHEN's, and on
// a PS5 etaHEN's too) with the cheats each file holds, which of them the
// console has, and — on a PS4 with GoldHEN — the game patches of
// illusion0001's archive, each with its switch. The lists are kept on this
// PC (fetched again after half a day, or when asked); everything on the
// console goes over FTP (see cheats/cheats.h for where). One thing at a
// time, on a thread of its own.
class CheatsController : public QObject
{
	Q_OBJECT
	// The game open: { titleId, title, version, platform }.
	Q_PROPERTY(QVariantMap game READ game NOTIFY gameChanged)
	// Where the cheats go: etaHEN's folders on a PS5, GoldHEN's on a PS4.
	Q_PROPERTY(bool ps5 READ ps5 NOTIFY gameChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY busyChanged)
	// The lists were read (from the internet or this PC's copy).
	Q_PROPERTY(bool loaded READ loaded NOTIFY listsChanged)
	// The repositories did not answer: what is shown is this PC's copy, or
	// nothing ("error" says why).
	Q_PROPERTY(bool offline READ offline NOTIFY listsChanged)
	Q_PROPERTY(QString error READ error NOTIFY listsChanged)
	// The console's FTP answered: "installed" and "on" are what it has.
	Q_PROPERTY(bool consoleRead READ consoleRead NOTIFY listsChanged)
	// The game's cheat files: [{ key, file, consoleName, format, version,
	// variant, exact (for the version installed), installed, readable (the
	// names could be read), cheats: [{ name, description }], author }],
	// those for the version installed first.
	Q_PROPERTY(QVariantList cheats READ cheats NOTIFY listsChanged)
	// How many of them are for the version installed.
	Q_PROPERTY(int exactCheats READ exactCheats NOTIFY listsChanged)
	// Cheat files on the console for this game that no list names (put
	// there another way): [{ name, format, path }].
	Q_PROPERTY(QVariantList otherFiles READ otherFiles NOTIFY listsChanged)
	// GoldHEN's game patches for it (PS4): [{ key, name, note, author,
	// appVersion, lines, applies (to the version installed), on }].
	Q_PROPERTY(bool patchesSupported READ patchesSupported NOTIFY gameChanged)
	Q_PROPERTY(QVariantList patches READ patches NOTIFY listsChanged)
	// The other versions of the game the archive has patches for.
	Q_PROPERTY(QStringList patchVersions READ patchVersions NOTIFY listsChanged)
	// game_patch is in GoldHEN's plugins.ini, for every game or this one
	// (false too when it could not be read).
	Q_PROPERTY(bool patchPlugin READ patchPlugin NOTIFY listsChanged)

public:
	explicit CheatsController(AppController *app, QObject *parent = nullptr);
	~CheatsController() override;

	QVariantMap game() const { return game_; }
	bool ps5() const { return context_.ps5; }
	bool busy() const { return busy_; }
	QString status() const { return status_; }
	bool loaded() const { return loaded_; }
	bool offline() const { return offline_; }
	QString error() const { return error_; }
	bool consoleRead() const { return consoleRead_; }
	QVariantList cheats() const { return cheats_; }
	int exactCheats() const { return exactCheats_; }
	QVariantList otherFiles() const { return otherFiles_; }
	bool patchesSupported() const { return !context_.ps5; }
	QVariantList patches() const { return patches_; }
	QStringList patchVersions() const { return patchVersions_; }
	bool patchPlugin() const { return patchPlugin_; }

	// A game's card (titleId, title, version, platform): what there is for
	// it, read.
	Q_INVOKABLE void open(const QVariantMap &card);
	// The lists fetched again from the internet, then read again.
	Q_INVOKABLE void reload();
	// A cheat file (its key) onto the console, under the name it loads by.
	Q_INVOKABLE void installCheat(const QString &key);
	// A cheat file off the console (its name there).
	Q_INVOKABLE void removeCheat(const QString &name);
	// A patch (its key) on or off: the game's patch file goes onto the
	// console with it, and its switch says "1" or "0".
	Q_INVOKABLE void setPatch(const QString &key, bool on);

signals:
	void gameChanged();
	void busyChanged();
	void listsChanged();
	// The outcome of an action, for a notice.
	void finished(const QString &message, bool error);

private:
	// The game and the console, as the worker reads them.
	struct Context
	{
		std::string titleId;
		std::string version;
		std::string title;
		bool ps5 = false;
	};
	struct Listing
	{
		cheats::CheatListing item;
		std::string base; // the repository it comes from (its raw files)
	};
	// What the worker found, handed to the interface whole.
	struct Snapshot
	{
		bool loaded = false;
		bool offline = false;
		QString error;
		bool consoleRead = false;
		QVariantList cheats;
		int exactCheats = 0;
		QVariantList otherFiles;
		QVariantList patches;
		QStringList patchVersions;
		bool patchPlugin = false;
		// For the actions: what each key stands for.
		std::vector<Listing> listings;
		std::vector<cheats::Patch> patchList;
		std::string patchXml;
	};
	using Job = std::function<QString(FtpClient &ftp, bool *error)>;
	// Runs `job` (may be empty) on the worker, then reads everything again.
	void run(const QString &what, Job job, bool forceLists);
	// On the worker: the lists, the files and what the console has.
	static Snapshot read(FtpClient &ftp, const Context &context, bool forceLists, const std::atomic<bool> &cancel);
	static QString cacheRoot();

	AppController *app_;
	QVariantMap game_;
	Context context_;
	bool busy_ = false;
	QString status_;
	bool loaded_ = false;
	bool offline_ = false;
	QString error_;
	bool consoleRead_ = false;
	QVariantList cheats_;
	int exactCheats_ = 0;
	QVariantList otherFiles_;
	QVariantList patches_;
	QStringList patchVersions_;
	bool patchPlugin_ = false;
	// What the worker last read, for the actions.
	std::vector<Listing> listings_;
	std::vector<cheats::Patch> patchList_;
	std::string patchXml_;
	quint64 generation_ = 0;
	// A game opened while the worker was busy: read once it is free.
	bool pendingRead_ = false;
	std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
	std::thread worker_;
};

} // namespace orbislink
