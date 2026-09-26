// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/queue/install_queue.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>

namespace orbislink {

// Modelo da fila para o TransferPanel. É alimentado a partir da thread da
// fila, por isso todas as atualizações são marshalled para a thread da UI.
class QueueModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
	Q_PROPERTY(QString totalSpeedText READ totalSpeedText NOTIFY summaryChanged)
	Q_PROPERTY(QString remainingText READ remainingText NOTIFY summaryChanged)

public:
	enum Roles {
		TaskIdRole = Qt::UserRole + 1,
		TitleRole,
		TitleIdRole,
		CategoryRole,
		StateRole,
		StateLabelRole,
		PercentRole,
		SizeTextRole,
		SpeedTextRole,
		EtaTextRole,
		MessageRole,
		ModeRole,
		IconRole,
		ActiveRole,
	};

	explicit QueueModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	QString totalSpeedText() const;
	QString remainingText() const;

	// Chamado de qualquer thread.
	void applySnapshot(const std::vector<QueueTask> &tasks);
	void setIcon(const QString &taskId, const QString &dataUri);

signals:
	void countChanged();
	void summaryChanged();

private:
	struct Row
	{
		QString id;
		QString title;
		QString titleId;
		QString category;
		QString state;
		QString stateLabel;
		double percent = 0.0;
		QString sizeText;
		QString speedText;
		QString etaText;
		QString message;
		QString mode;
		bool active = false;
		double bytesPerSecond = 0.0;
		qint64 remainingBytes = 0;
	};

	QList<Row> rows_;
	QHash<QString, QString> icons_;
};

} // namespace orbislink
