// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/store/store_catalog.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace orbislink {

class AppController;

// The PS5 homebrew store for QML (`store`): the catalog of homebrew.page,
// used only once its signature checks out; each app's details; installing
// an app on a jailbroken PS5 over FTP — its folder goes where ShadowMountPlus
// finds it, which puts it on the home screen — updating it, removing it.
// One install or removal at a time, on a thread of its own.
class StoreController : public QObject
{
	Q_OBJECT
	// [{ titleId, name, kind, author, version, size, updated, icon, sandbox,
	//    available, installable, installed, installedVersion, update }]
	Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
	// Reading the catalog, and why it could not be (empty when it was).
	Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
	Q_PROPERTY(QString error READ error NOTIFY stateChanged)
	// The catalog shown is the last one kept: the site did not answer.
	Q_PROPERTY(bool offline READ offline NOTIFY appsChanged)
	// The app whose details were asked for, once read:
	// { titleId, ready, error, description, license, sourceRepo, page,
	//   releaseNotes, prerelease, safetyKnown, safetyRoutes, safetyNetwork,
	//   safetyHelpers, … and the fields of apps }.
	Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
	// The app being installed or removed, the stage and how far.
	Q_PROPERTY(QString working READ working NOTIFY stateChanged)
	Q_PROPERTY(QString stage READ stage NOTIFY progressChanged)
	Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
	// The PS5s apps can go to: [{ address, name, ftp, target }], and the one
	// they go to.
	Q_PROPERTY(QVariantList consoles READ consoles NOTIFY consolesChanged)
	Q_PROPERTY(QString target READ target NOTIFY consolesChanged)
	Q_PROPERTY(QString targetName READ targetName NOTIFY consolesChanged)
	Q_PROPERTY(bool canInstall READ canInstall NOTIFY consolesChanged)

public:
	explicit StoreController(AppController *app, QObject *parent = nullptr);
	~StoreController() override;

	QVariantList apps() const { return apps_; }
	bool loading() const { return loading_; }
	QString error() const { return error_; }
	bool offline() const { return offline_; }
	QVariantMap detail() const { return detail_; }
	QString working() const { return working_; }
	QString stage() const { return stage_; }
	double progress() const { return progress_; }
	QVariantList consoles() const;
	QString target() const;
	QString targetName() const;
	bool canInstall() const;

	// Reads the catalog again, and what the PS5 has installed.
	Q_INVOKABLE void refresh();
	Q_INVOKABLE void showDetails(const QString &titleId);
	// Downloads the app, checks it against the catalog's SHA-256 and puts its
	// folder on the PS5 (replacing the one there, which is kept aside).
	Q_INVOKABLE void install(const QString &titleId);
	// Takes the app away from the PS5 (its saves stay).
	Q_INVOKABLE void uninstall(const QString &titleId);
	Q_INVOKABLE void cancel();
	// The PS5 apps go to (its address).
	Q_INVOKABLE void setTarget(const QString &address);

signals:
	void appsChanged();
	void stateChanged();
	void detailChanged();
	void progressChanged();
	void consolesChanged();
	// The outcome of an install or a removal, for a notice.
	void finished(const QString &message, bool error);

private:
	struct Installed
	{
		std::string contentVersion;
	};
	void publish();
	void readInstalled(const std::string &address);
	void startWork(const QString &titleId, const QString &what, std::function<void()> work);
	void setProgress(const QString &stage, double progress);
	std::string cacheDir() const;
	// ShadowMountPlus's API on the PS5, when it lets the network in.
	bool askShadowMount(const std::string &address, const std::string &route, const std::string &titleId) const;

	AppController *app_;
	std::unique_ptr<store::StoreCatalog> catalog_;
	// The catalog is read, details are asked for and apps installed on
	// different threads: one at a time touches it.
	mutable std::mutex catalogMutex_;
	std::vector<store::StoreApp> list_;
	std::map<std::string, std::string> icons_;
	std::map<std::string, Installed> installed_;
	bool installedKnown_ = false;
	QVariantList apps_;
	bool loading_ = false;
	QString error_;
	bool offline_ = false;
	QVariantMap detail_;
	QString working_;
	QString stage_;
	double progress_ = 0.0;
	QString target_;
	std::atomic<bool> cancel_ { false };
	std::atomic<bool> stopIcons_ { false };
	std::thread loader_;
	std::thread detailer_;
	std::thread worker_;
};

} // namespace orbislink
