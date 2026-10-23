// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/queue_model.h"

#include "orbislink/common/util.h"
#include "orbislink/qt/translate_message.h"

#include <QDir>
#include <QSet>

namespace orbislink {

QueueModel::QueueModel(QObject *parent) : QAbstractListModel(parent) {}

int QueueModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : rows_.size();
}

QHash<int, QByteArray> QueueModel::roleNames() const
{
	return {
		{ TaskIdRole, "taskId" },
		{ TitleRole, "title" },
		{ TitleIdRole, "titleId" },
		{ CategoryRole, "category" },
		{ StateRole, "state" },
		{ StateLabelRole, "stateLabel" },
		{ PercentRole, "percent" },
		{ SizeTextRole, "sizeText" },
		{ SpeedTextRole, "speedText" },
		{ EtaTextRole, "etaText" },
		{ MessageRole, "message" },
		{ ModeRole, "mode" },
		{ IconRole, "iconSource" },
		{ ActiveRole, "active" },
		{ LocalKeyRole, "localKey" },
		{ SupersededRole, "superseded" },
		{ CanMoveUpRole, "canMoveUp" },
		{ CanMoveDownRole, "canMoveDown" },
		{ FirmwareRole, "firmware" },
		{ WarningRole, "warning" },
	};
}

QVariant QueueModel::data(const QModelIndex &index, int role) const
{
	if(index.row() < 0 || index.row() >= rows_.size())
		return {};
	const Row &row = rows_[index.row()];
	switch(role)
	{
		case TaskIdRole: return row.id;
		case TitleRole: return row.title;
		case TitleIdRole: return row.titleId;
		case CategoryRole: return row.category;
		case StateRole: return row.state;
		case StateLabelRole: return row.stateLabel;
		case PercentRole: return row.percent;
		case SizeTextRole: return row.sizeText;
		case SpeedTextRole: return row.speedText;
		case EtaTextRole: return row.etaText;
		case MessageRole: return row.message;
		case ModeRole: return row.mode;
		case ActiveRole: return row.active;
		case LocalKeyRole: return row.localKey;
		case SupersededRole: return row.superseded;
		case CanMoveUpRole: return row.canMoveUp;
		case CanMoveDownRole: return row.canMoveDown;
		case IconRole: return icons_.value(row.id);
		case FirmwareRole: return firmware_.value(row.id);
		case WarningRole: return warnings_.value(row.id);
		default: return {};
	}
}

double QueueModel::bytesPerSecond() const
{
	double total = 0.0;
	for(const Row &row : rows_)
		total += row.bytesPerSecond;
	return total;
}

double QueueModel::bytesLeft() const
{
	qint64 remaining = 0;
	for(const Row &row : rows_)
		remaining += row.remainingBytes;
	return static_cast<double>(remaining);
}

double QueueModel::bytesTotal() const
{
	qint64 total = 0;
	for(const Row &row : rows_)
		if(row.state != QLatin1String("completed") && row.state != QLatin1String("error")
			&& row.state != QLatin1String("cancelled"))
			total += row.totalBytes;
	return static_cast<double>(total);
}

QString QueueModel::bytesText(double bytes) const
{
	return QString::fromStdString(humanBytes(static_cast<int64_t>(bytes)));
}

QString QueueModel::durationText(double seconds) const
{
	return QString::fromStdString(humanDuration(static_cast<int64_t>(seconds + 0.5)));
}

QVariantList QueueModel::rows() const
{
	QVariantList list;
	list.reserve(rows_.size());
	for(const Row &row : rows_)
	{
		QVariantMap map;
		map[QStringLiteral("taskId")] = row.id;
		map[QStringLiteral("title")] = row.title;
		map[QStringLiteral("titleId")] = row.titleId;
		map[QStringLiteral("category")] = row.category;
		map[QStringLiteral("state")] = row.state;
		map[QStringLiteral("stateLabel")] = row.stateLabel;
		map[QStringLiteral("percent")] = row.percent;
		map[QStringLiteral("sizeText")] = row.sizeText;
		map[QStringLiteral("speedText")] = row.speedText;
		map[QStringLiteral("etaText")] = row.etaText;
		map[QStringLiteral("message")] = row.message;
		map[QStringLiteral("mode")] = row.mode;
		map[QStringLiteral("install")] = row.install;
		map[QStringLiteral("active")] = row.active;
		map[QStringLiteral("localKey")] = row.localKey;
		map[QStringLiteral("superseded")] = row.superseded;
		map[QStringLiteral("canMoveUp")] = row.canMoveUp;
		map[QStringLiteral("canMoveDown")] = row.canMoveDown;
		map[QStringLiteral("iconSource")] = icons_.value(row.id);
		map[QStringLiteral("firmware")] = firmware_.value(row.id);
		map[QStringLiteral("warning")] = warnings_.value(row.id);
		map[QStringLiteral("totalBytes")] = static_cast<double>(row.totalBytes);
		map[QStringLiteral("doneBytes")] = static_cast<double>(row.totalBytes - row.remainingBytes);
		map[QStringLiteral("bytesPerSecond")] = row.bytesPerSecond;
		list << map;
	}
	return list;
}

QString QueueModel::totalSpeedText() const
{
	const double total = bytesPerSecond();
	if(total < 1.0)
		return QStringLiteral("—");
	return QString::fromStdString(humanBytes(static_cast<int64_t>(total))) + QStringLiteral("/s");
}

QString QueueModel::remainingText() const
{
	const double remaining = bytesLeft();
	if(remaining <= 0)
		return QStringLiteral("—");
	return QString::fromStdString(humanBytes(static_cast<int64_t>(remaining)));
}

QString QueueModel::totalText() const
{
	const double total = bytesTotal();
	if(total <= 0)
		return QStringLiteral("—");
	return QString::fromStdString(humanBytes(static_cast<int64_t>(total)));
}

int QueueModel::shownCount(const QVariantMap &followed) const
{
	int shown = 0;
	for(const Row &row : rows_)
		if(!row.superseded && !followed.value(row.localKey).toBool())
			++shown;
	return shown;
}

void QueueModel::applySnapshot(const std::vector<QueueTask> &tasks)
{
	QVector<Row> rows;
	rows.reserve(static_cast<int>(tasks.size()));
	for(const QueueTask &task : tasks)
	{
		Row row;
		row.id = QString::fromStdString(task.id);
		row.title = QString::fromStdString(task.title);
		row.titleId = QString::fromStdString(task.titleId);
		row.category = translateMessage(pkgCategoryLabel(task.category));
		row.state = QString::fromUtf8(taskStateName(task.state));
		row.stateLabel = translateMessage(taskStateLabel(task.state));
		row.percent = task.percent();
		row.sizeText = QString::fromStdString(humanBytes(task.totalBytes));
		row.speedText = task.bytesPerSecond > 1.0
			? QString::fromStdString(humanBytes(static_cast<int64_t>(task.bytesPerSecond))) + QStringLiteral("/s")
			: QStringLiteral("—");
		row.etaText = task.etaSeconds > 0
			? QString::fromStdString(humanDuration(task.etaSeconds))
			: QStringLiteral("—");
		row.message = translateMessage(task.message);
		row.mode = task.mode == TransferMode::DirectInstall
			? QCoreApplication::translate("QueueModel", "Direct install")
			: task.mode == TransferMode::ConsoleInstall
			? QCoreApplication::translate("QueueModel", "Install on the console")
			: QCoreApplication::translate("QueueModel", "FTP upload");
		row.active = task.state == TaskState::Installing || task.state == TaskState::Sending
			|| task.state == TaskState::Validating;
		// An install (from the PC, or of what is on the console), not a copy.
		row.install = task.mode != TransferMode::FtpUpload;
		row.bytesPerSecond = row.active ? task.bytesPerSecond : 0.0;
		row.remainingBytes = task.totalBytes > task.doneBytes ? task.totalBytes - task.doneBytes : 0;
		row.totalBytes = task.totalBytes;
		row.localKey = fileKey(QString::fromStdString(task.localPath));
		rows.push_back(row);
	}
	// The queue moves a waiting task by swapping it with its neighbour, and
	// never with the task under way; finished ones (the history) come after.
	for(size_t i = 0; i < tasks.size(); ++i)
	{
		if(tasks[i].state != TaskState::Pending)
			continue;
		Row &row = rows[static_cast<int>(i)];
		row.canMoveUp = i > 0 && tasks[i - 1].state == TaskState::Pending;
		row.canMoveDown = i + 1 < tasks.size() && !tasks[i + 1].isTerminal();
	}

	// A package sent over FTP and then installed: the install's card tells
	// the whole story, the upload's would be a second card for the same game.
	QSet<QString> installed;
	for(size_t i = 0; i < tasks.size(); ++i)
		if(tasks[i].mode == TransferMode::DirectInstall)
			installed.insert(rows[static_cast<int>(i)].localKey);
	for(size_t i = 0; i < tasks.size(); ++i)
	{
		Row &row = rows[static_cast<int>(i)];
		row.superseded = tasks[i].mode != TransferMode::DirectInstall
			&& tasks[i].state == TaskState::Completed && !row.localKey.isEmpty()
			&& installed.contains(row.localKey);
	}

	// The same tasks in the same order (each progress tick): only the rows
	// that changed are told, so the cards stay and their bars move instead
	// of being made again from zero.
	bool sameTasks = rows.size() == rows_.size();
	for(int i = 0; sameTasks && i < rows.size(); ++i)
		sameTasks = rows[i].id == rows_[i].id;
	if(sameTasks)
	{
		for(int i = 0; i < rows.size(); ++i)
		{
			if(rows[i] == rows_[i])
				continue;
			rows_[i] = rows[i];
			emit dataChanged(index(i), index(i));
		}
		emit summaryChanged();
		emit rowsChanged();
		return;
	}
	beginResetModel();
	rows_ = rows;
	endResetModel();
	emit countChanged();
	emit summaryChanged();
	emit rowsChanged();
}

QString QueueModel::fileKey(const QString &localPath)
{
	QString key = QDir::cleanPath(QDir::fromNativeSeparators(localPath));
#ifdef Q_OS_WIN
	key = key.toLower();
#endif
	return key;
}

void QueueModel::setIcon(const QString &taskId, const QString &dataUri)
{
	icons_.insert(taskId, dataUri);
	for(int i = 0; i < rows_.size(); ++i)
	{
		if(rows_[i].id == taskId)
		{
			emit dataChanged(index(i), index(i), { IconRole });
			emit rowsChanged();
			break;
		}
	}
}

void QueueModel::setPackageFacts(const QString &taskId, const QString &firmware, const QString &warning)
{
	firmware_.insert(taskId, firmware);
	warnings_.insert(taskId, warning);
	for(int i = 0; i < rows_.size(); ++i)
	{
		if(rows_[i].id == taskId)
		{
			emit dataChanged(index(i), index(i), { FirmwareRole, WarningRole });
			emit rowsChanged();
			break;
		}
	}
}

} // namespace orbislink
