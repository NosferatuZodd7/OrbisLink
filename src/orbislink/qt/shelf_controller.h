// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"
#include "orbislink/library/shelf.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace orbislink {

class AppController;
class GamesController;
class LibraryController;
class PayloadsController;

// The shelf's cards laid out for the view: each row is a section's heading
// or as many cards as fit across. A card that changes (its picture arrived)
// changes only its row, so the view keeps its place while a folder fills in.
class ShelfRows : public QAbstractListModel
{
	Q_OBJECT

public:
	enum Role
	{
		HeaderRole = Qt::UserRole + 1, // the section of a heading row, "" for cards
		CountRole,                     // how many cards the section has
		CardsRole,                     // the row's cards, each with its "index"
		FirstRole,                     // the index of the row's first card
	};

	explicit ShelfRows(QObject *parent = nullptr) : QAbstractListModel(parent) {}
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	// The cards in order; with `sectioned`, a heading before each change of
	// their "section". Lays the rows out again only when their shape
	// changed; otherwise only the rows whose cards changed are told.
	void setCards(const QVariantList &cards, bool sectioned, int columns);
	void updateCard(int index, const QVariantMap &card);
	QVariantMap card(int index) const;
	int count() const { return static_cast<int>(cards_.size()); }
	int rowOfCard(int index) const;
	// The card in the same column `step` rows of cards away (clamped).
	int neighbour(int index, int step) const;

private:
	struct Row
	{
		QString header;
		int count = 0;
		int first = 0;
		int length = 0;
	};
	void layout(bool sectioned, int columns, std::vector<Row> *rows, std::vector<int> *rowOf) const;

	QVariantList cards_;
	std::vector<Row> rows_;
	std::vector<int> rowOf_;
	bool sectioned_ = false;
	int columns_ = 1;
};

// The games page's library (`shelf`): every place games can be — the
// console's library, its data/pkg and USB drives, this PC's USB drives,
// Desktop, Downloads, drives and folders added by hand — as cards with the
// covers of what is in them. A card opens: a place shows its folders (each
// with a few covers of its own) and its games, as cards too; the view keeps
// the same look all the way down. What each thing is and its picture come
// from library/shelf.h, read on a thread of its own, a folder at a time.
class ShelfController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QObject *rows READ rows CONSTANT)
	// Cards shown (after the search and the filter), and before them.
	Q_PROPERTY(int count READ count NOTIFY viewChanged)
	Q_PROPERTY(int total READ total NOTIFY viewChanged)
	// "home" (the places), "folder" (inside one), "search" (the search
	// across places, from home).
	Q_PROPERTY(QString level READ level NOTIFY placeChanged)
	Q_PROPERTY(QString title READ title NOTIFY placeChanged)
	// [{ title }]: the library, the place, then each folder down.
	Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY placeChanged)
	// The kind of the place open ("console", "usb", "pc", "drive",
	// "custom"), and whether it is on the console.
	Q_PROPERTY(QString placeKind READ placeKind NOTIFY placeChanged)
	Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
	// Why there is nothing to show: a folder that cannot be read, a drive
	// taken out ("gone").
	Q_PROPERTY(QString message READ message NOTIFY stateChanged)
	Q_PROPERTY(bool gone READ gone NOTIFY stateChanged)
	Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY viewChanged)
	// "all", "games", "pkg", "folders", "other".
	Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY viewChanged)
	// "type" (in sections), "name", "date".
	Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY viewChanged)
	Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY viewChanged)
	// Whether the tab is on screen: drives are watched only then.
	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
	// The places kept in the settings, for managing them: [{ id, name,
	// shownName, path, where, favorite, inSearch, kind }].
	Q_PROPERTY(QVariantList locations READ locations NOTIFY locationsChanged)
	// This PC's drives, to add one: [{ path, name }].
	Q_PROPERTY(QVariantList volumes READ volumes NOTIFY locationsChanged)
	// Goes up each time a cover arrives: a binding that reads it and calls
	// cover() is evaluated again then.
	Q_PROPERTY(int coverRevision READ coverRevision NOTIFY coversChanged)

public:
	ShelfController(AppController *app, GamesController *games, LibraryController *consoleLibrary,
		PayloadsController *payloads, QObject *parent = nullptr);
	~ShelfController() override;

	QObject *rows() { return &rows_; }
	int count() const { return rows_.count(); }
	int total() const;
	QString level() const;
	QString title() const;
	QVariantList crumbs() const;
	QString placeKind() const;
	bool loading() const { return loading_; }
	QString message() const { return message_; }
	bool gone() const { return gone_; }
	QString query() const { return query_; }
	void setQuery(const QString &query);
	QString filter() const { return filter_; }
	void setFilter(const QString &filter);
	QString sort() const { return sort_; }
	void setSort(const QString &sort);
	int columns() const { return columns_; }
	void setColumns(int columns);
	bool active() const { return active_; }
	void setActive(bool active);
	QVariantList locations() const;
	QVariantList volumes() const;
	int coverRevision() const { return coverRevision_; }

	// A game's cover for any page of the app, by its serial or title ID:
	// the file URL of one the app has, or "" — and then, for a PS1/PS2 disc,
	// it is looked for in the cover collection, and coversChanged follows
	// when it arrives.
	Q_INVOKABLE QString cover(const QString &platform, const QString &serial);

	// Reads again where it is (at home: every place's covers).
	Q_INVOKABLE void refresh();
	Q_INVOKABLE QVariantMap card(int index) const;
	// A place or a folder opens; a game shows its details in the view.
	Q_INVOKABLE void openCard(int index);
	Q_INVOKABLE void back();
	Q_INVOKABLE void home();
	Q_INVOKABLE void goTo(int crumb);
	Q_INVOKABLE int rowOf(int index) const { return rows_.rowOfCard(index); }
	Q_INVOKABLE int neighbour(int index, int step) const { return rows_.neighbour(index, step); }
	// What a card's buttons do (the ids are in its "actions").
	Q_INVOKABLE void act(int index, const QString &action);

	// The places: added, taken off (the folder stays), renamed, marked.
	Q_INVOKABLE void addFolder(const QUrl &folder);
	Q_INVOKABLE void addPlace(const QString &path, const QString &where);
	Q_INVOKABLE void addPreset(const QString &which);
	Q_INVOKABLE void removeLocation(const QString &id);
	Q_INVOKABLE void renameLocation(const QString &id, const QString &name);
	Q_INVOKABLE void setLocationPath(const QString &id, const QString &path);
	Q_INVOKABLE void setFavorite(const QString &id, bool on);
	Q_INVOKABLE void setInSearch(const QString &id, bool on);

signals:
	void placeChanged();
	void stateChanged();
	void viewChanged();
	void activeChanged();
	void locationsChanged();
	void coversChanged();
	void notice(const QString &message, bool error);

private:
	struct Source
	{
		QString id;
		QString name;
		QString path;
		// "console" (the console's library and folders), "usb" (this PC's
		// USB drives), "pc" (Desktop, Downloads, Documents), "drive" (a
		// drive of this PC), "custom" (a folder chosen by hand).
		QString kind;
		QString where; // "pc" or "console"
		bool available = true;
		bool favorite = false;
		bool inSearch = true;
		bool location = false; // kept in the settings
	};
	struct Place
	{
		QString sourceId;
		QString root;
		QString path;
	};

	// The work thread: the folder open first, the places' covers after.
	void post(bool urgent, std::function<void()> job);
	void workLoop();
	// Back on the interface's thread.
	void onUi(std::function<void()> fn);

	void seedLocations();
	void rebuildSources();
	const Source *source(const QString &id) const;
	QVariantMap sourceCard(const Source &source) const;
	void refreshSourcePreview(const Source &source);
	void scanConsoleDrives();
	void checkVolumes();

	void openPlace(const QString &sourceId, const QString &path);
	void loadPlace();
	void loadConsoleLibrary();
	void watchPlace();
	void setGone(bool gone, const QString &message);
	void setLoading(bool loading);

	// Everything of the current folder, filtered and sorted into the rows.
	void showCards();
	QVariantList viewCards() const;
	bool matches(const QVariantMap &card) const;
	void updateCards(const QVariantList &cards);

	library::DiscProbe probe() const;
	QVariantMap cardFor(const library::ShelfItem &item, const Source &source, const QSet<QString> &installed,
		bool ps5) const;
	QSet<QString> installedOnConsole(FtpClient *ftp);
	// Step 1 and 4: a picture the app has for it, or a cover collection's.
	void findCover(library::ShelfItem *item, const std::atomic<bool> *cancel);
	FtpClient::Config ftpConfig() const;
	bool consoleUsable() const;

	AppController *app_;
	GamesController *games_;
	LibraryController *consoleLibrary_;
	PayloadsController *payloads_;
	ShelfRows rows_;
	std::unique_ptr<library::ShelfCache> cache_;

	std::vector<Source> sources_;
	QHash<QString, QVariantMap> sourceLooks_;  // id → { count, previews }
	QStringList consoleDrives_;                // /mnt/usb0… that the console has
	QStringList pcDrives_;                     // this PC's removable drives' roots
	QHash<QString, QVariantList> searchIndex_; // place id → the cards seen there

	Place place_;
	QVariantList all_;
	QString query_;
	QString filter_ = QStringLiteral("all");
	QString sort_ = QStringLiteral("type");
	int columns_ = 5;
	bool active_ = false;
	bool loading_ = false;
	bool gone_ = false;
	QString message_;

	QFileSystemWatcher watcher_;
	QTimer reloadTimer_;
	QTimer volumesTimer_;
	QTimer sourcesTimer_;
	QSet<QString> staleSources_;

	std::thread worker_;
	std::mutex jobsMutex_;
	std::condition_variable jobsWake_;
	std::deque<std::function<void()>> urgent_;
	std::deque<std::function<void()>> later_;
	bool stopping_ = false;
	std::shared_ptr<std::atomic<bool>> placeCancel_;
	std::shared_ptr<std::atomic<bool>> sourcesCancel_;
	std::atomic<uint64_t> placeGeneration_ { 0 };

	std::mutex consoleMutex_;
	QSet<QString> installed_;
	QElapsedTimer installedAge_;
	QSet<QString> noCover_;
	// Covers asked for by the pages, on their way (interface thread only).
	QSet<QString> coverPending_;
	int coverRevision_ = 0;
	std::shared_ptr<std::atomic<bool>> coversCancel_;
};

} // namespace orbislink
