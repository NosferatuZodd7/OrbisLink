// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/ftp_model.h"

#include "orbislink/common/util.h"
#include "orbislink/library/shelf.h"

#include <QDateTime>

#include <algorithm>

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
		{ PermissionsRole, "permissions" },
		{ IsLinkRole, "isLink" },
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
		case PermissionsRole: return row.permissions;
		case IsLinkRole: return row.isLink;
		default: return {};
	}
}

void FtpModel::setEntries(const std::vector<FtpEntry> &entries)
{
	all_.clear();
	all_.reserve(entries.size());
	const int64_t now = QDateTime::currentSecsSinceEpoch();
	for(const FtpEntry &entry : entries)
	{
		Row row;
		row.name = QString::fromStdString(entry.name);
		row.path = QString::fromStdString(entry.path);
		row.isDirectory = entry.isDirectory;
		row.isLink = entry.isSymlink;
		row.sizeText = entry.isDirectory ? QStringLiteral("—")
										 : QString::fromStdString(humanBytes(entry.size));
		row.size = static_cast<qint64>(entry.size);
		// The listing's date ("Nov 17 22:13", "Mar 05 2024") in one form,
		// on the console's own clock (no time zone is applied).
		row.when = library::ftpListingTime(entry.modified, now);
		row.modified = row.when > 0
			? QDateTime::fromSecsSinceEpoch(row.when, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
			: QString::fromStdString(entry.modified);
		row.permissions = QString::fromStdString(entry.permissions);
		all_.push_back(row);
	}
	rebuild();
}

void FtpModel::rebuild()
{
	std::vector<Row> shown;
	shown.reserve(all_.size());
	for(const Row &row : all_)
		if(filter_.isEmpty() || row.name.contains(filter_, Qt::CaseInsensitive))
			shown.push_back(row);
	const QString key = sortKey_;
	const bool up = sortAscending_;
	std::stable_sort(shown.begin(), shown.end(), [&key, up](const Row &a, const Row &b) {
		// Folders always first, whichever way the rest goes.
		if(a.isDirectory != b.isDirectory)
			return a.isDirectory;
		const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
		int order = 0;
		if(key == QLatin1String("size") && a.size != b.size)
			order = a.size < b.size ? -1 : 1;
		else if(key == QLatin1String("date") && a.when != b.when)
			order = a.when < b.when ? -1 : 1;
		else if(key != QLatin1String("name"))
			return byName < 0; // A tie on size or date: by name, A to Z.
		else
			order = byName;
		return up ? order < 0 : order > 0;
	});
	beginResetModel();
	rows_.clear();
	rows_.reserve(static_cast<int>(shown.size()));
	for(const Row &row : shown)
		rows_.push_back(row);
	endResetModel();
	emit countChanged();
}

QVariantMap FtpModel::get(int row) const
{
	if(row < 0 || row >= rows_.size())
		return {};
	const Row &entry = rows_.at(row);
	return { { QStringLiteral("name"), entry.name }, { QStringLiteral("path"), entry.path },
		{ QStringLiteral("isDirectory"), entry.isDirectory }, { QStringLiteral("size"), static_cast<double>(entry.size) },
		{ QStringLiteral("permissions"), entry.permissions }, { QStringLiteral("modified"), entry.modified } };
}

int FtpModel::indexOfPath(const QString &path) const
{
	for(int i = 0; i < rows_.size(); ++i)
		if(rows_.at(i).path == path)
			return i;
	return -1;
}

bool FtpModel::containsPath(const QString &path) const
{
	for(const Row &row : all_)
		if(row.path == path)
			return true;
	return false;
}

void FtpModel::sortBy(const QString &key)
{
	setSort(key, key == sortKey_ ? !sortAscending_ : key == QLatin1String("name"));
}

void FtpModel::setSort(const QString &key, bool ascending)
{
	const QString chosen = key == QLatin1String("size") || key == QLatin1String("date") ? key : QStringLiteral("name");
	if(chosen == sortKey_ && ascending == sortAscending_)
		return;
	sortKey_ = chosen;
	sortAscending_ = ascending;
	emit sortChanged();
	rebuild();
}

void FtpModel::setFilter(const QString &text)
{
	const QString trimmed = text.trimmed();
	if(trimmed == filter_)
		return;
	filter_ = trimmed;
	emit filterChanged();
	rebuild();
}

void FtpModel::clear()
{
	all_.clear();
	beginResetModel();
	rows_.clear();
	endResetModel();
	emit countChanged();
}

} // namespace orbislink
