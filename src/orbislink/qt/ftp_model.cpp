// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/ftp_model.h"

#include "orbislink/common/util.h"

namespace orbislink {

FtpModel::FtpModel(QObject *parent) : QAbstractListModel(parent) {}

int FtpModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : rows_.size();
}

QHash<int, QByteArray> FtpModel::roleNames() const
{
	return {
		{ NameRole, "name" },
		{ PathRole, "path" },
		{ IsDirectoryRole, "isDirectory" },
		{ SizeTextRole, "sizeText" },
		{ SizeRole, "size" },
		{ ModifiedRole, "modified" },
	};
}

QVariant FtpModel::data(const QModelIndex &index, int role) const
{
	if(index.row() < 0 || index.row() >= rows_.size())
		return {};
	const Row &row = rows_[index.row()];
	switch(role)
	{
		case NameRole: return row.name;
		case PathRole: return row.path;
		case IsDirectoryRole: return row.isDirectory;
		case SizeTextRole: return row.sizeText;
		case SizeRole: return row.size;
		case ModifiedRole: return row.modified;
		default: return {};
	}
}

void FtpModel::setEntries(const std::vector<FtpEntry> &entries)
{
	beginResetModel();
	rows_.clear();
	rows_.reserve(static_cast<int>(entries.size()));
	for(const FtpEntry &entry : entries)
	{
		Row row;
		row.name = QString::fromStdString(entry.name);
		row.path = QString::fromStdString(entry.path);
		row.isDirectory = entry.isDirectory;
		row.sizeText = entry.isDirectory ? QStringLiteral("—")
										 : QString::fromStdString(humanBytes(entry.size));
		row.size = static_cast<qint64>(entry.size);
		row.modified = QString::fromStdString(entry.modified);
		rows_.push_back(row);
	}
	endResetModel();
	emit countChanged();
}

void FtpModel::clear()
{
	beginResetModel();
	rows_.clear();
	endResetModel();
	emit countChanged();
}

} // namespace orbislink
