// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <vector>

namespace orbislink {

// Modelo do FtpBrowser.
class FtpModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
	enum Roles {
		NameRole = Qt::UserRole + 1,
		PathRole,
		IsDirectoryRole,
		SizeTextRole,
		SizeRole,
		ModifiedRole,
	};

	explicit FtpModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	void setEntries(const std::vector<FtpEntry> &entries);
	void clear();

signals:
	void countChanged();

private:
	struct Row
	{
		QString name;
		QString path;
		bool isDirectory = false;
		QString sizeText;
		qint64 size = 0;
		QString modified;
	};

	QList<Row> rows_;
};

} // namespace orbislink
