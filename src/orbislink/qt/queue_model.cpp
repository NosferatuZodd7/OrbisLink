// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/queue_model.h"

#include "orbislink/common/util.h"

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
		case IconRole: return icons_.value(row.id);
		default: return {};
	}
}

QString QueueModel::totalSpeedText() const
{
	double total = 0.0;
	for(const Row &row : rows_)
		total += row.bytesPerSecond;
	if(total < 1.0)
		return QStringLiteral("—");
	return QString::fromStdString(humanBytes(static_cast<int64_t>(total))) + QStringLiteral("/s");
}

QString QueueModel::remainingText() const
{
	qint64 remaining = 0;
	for(const Row &row : rows_)
		remaining += row.remainingBytes;
	if(remaining <= 0)
		return QStringLiteral("—");
	return QString::fromStdString(humanBytes(remaining));
}

void QueueModel::applySnapshot(const std::vector<QueueTask> &tasks)
{
	beginResetModel();
	rows_.clear();
	rows_.reserve(static_cast<int>(tasks.size()));
	for(const QueueTask &task : tasks)
	{
		Row row;
		row.id = QString::fromStdString(task.id);
		row.title = QString::fromStdString(task.title);
		row.titleId = QString::fromStdString(task.titleId);
		row.category = QString::fromUtf8(pkgCategoryLabelPt(task.category));
		row.state = QString::fromUtf8(taskStateName(task.state));
		row.stateLabel = QString::fromUtf8(taskStateLabelPt(task.state));
		row.percent = task.percent();
		row.sizeText = QString::fromStdString(humanBytes(task.totalBytes));
		row.speedText = task.bytesPerSecond > 1.0
			? QString::fromStdString(humanBytes(static_cast<int64_t>(task.bytesPerSecond))) + QStringLiteral("/s")
			: QStringLiteral("—");
		row.etaText = task.etaSeconds > 0
			? QString::fromStdString(humanDuration(task.etaSeconds))
			: QStringLiteral("—");
		row.message = QString::fromStdString(task.message);
		row.mode = task.mode == TransferMode::DirectInstall ? QStringLiteral("Instalação direta")
															: QStringLiteral("Envio por FTP");
		row.active = task.state == TaskState::Installing || task.state == TaskState::Sending
			|| task.state == TaskState::Validating;
		row.bytesPerSecond = row.active ? task.bytesPerSecond : 0.0;
		row.remainingBytes = task.totalBytes > task.doneBytes ? task.totalBytes - task.doneBytes : 0;
		rows_.push_back(row);
	}
	endResetModel();
	emit countChanged();
	emit summaryChanged();
}

void QueueModel::setIcon(const QString &taskId, const QString &dataUri)
{
	icons_.insert(taskId, dataUri);
	for(int i = 0; i < rows_.size(); ++i)
	{
		if(rows_[i].id == taskId)
		{
			emit dataChanged(index(i), index(i), { IconRole });
			break;
		}
	}
}

} // namespace orbislink
