// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <vector>

namespace orbislink {

// The FtpBrowser's list: the folder's entries, in the order chosen (by name,
// size or date, either way — folders always before files) and narrowed to
// the names that hold the filter's text.
class FtpModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
	// Everything in the folder, whatever the filter shows.
	Q_PROPERTY(int total READ total NOTIFY countChanged)
	// "name", "size" or "date".
	Q_PROPERTY(QString sortKey READ sortKey NOTIFY sortChanged)
	Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)
	Q_PROPERTY(QString filter READ filter NOTIFY filterChanged)

public:
	enum Roles {
		NameRole = Qt::UserRole + 1,
		PathRole,
		IsDirectoryRole,
		SizeTextRole,
		SizeRole,
		ModifiedRole,
		PermissionsRole,
		IsLinkRole,
	};

	explicit FtpModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	void setEntries(const std::vector<FtpEntry> &entries);
	void clear();
	// A row as { name, path, isDirectory, size, permissions, modified }, for
	// picking several.
	Q_INVOKABLE QVariantMap get(int row) const;
	// Where the row with that path is now (-1: not shown).
	Q_INVOKABLE int indexOfPath(const QString &path) const;
	// Whether the folder has it, shown or not.
	bool containsPath(const QString &path) const;

	int total() const { return static_cast<int>(all_.size()); }
	QString sortKey() const { return sortKey_; }
	bool sortAscending() const { return sortAscending_; }
	QString filter() const { return filter_; }
	// The same key again turns the order round.
	Q_INVOKABLE void sortBy(const QString &key);
	Q_INVOKABLE void setSort(const QString &key, bool ascending);
	Q_INVOKABLE void setFilter(const QString &text);

signals:
	void countChanged();
	void sortChanged();
	void filterChanged();

private:
	struct Row
	{
		QString name;
		QString path;
		bool isDirectory = false;
		bool isLink = false;
		QString sizeText;
		qint64 size = 0;
		QString modified;
		qint64 when = 0;
		QString permissions;
	};

	void rebuild();

	std::vector<Row> all_;
	QList<Row> rows_;
	QString sortKey_ = QStringLiteral("name");
	bool sortAscending_ = true;
	QString filter_;
};

} // namespace orbislink
