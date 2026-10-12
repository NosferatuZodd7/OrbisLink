// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/queue/install_queue.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace orbislink {

// Queue model for the TransferPanel. It is fed from the queue's thread,
// so every update is marshalled to the UI thread.
class QueueModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
	Q_PROPERTY(QString totalSpeedText READ totalSpeedText NOTIFY summaryChanged)
	Q_PROPERTY(QString remainingText READ remainingText NOTIFY summaryChanged)
	// The size of everything still to go, as a whole (what is left is part of it).
	Q_PROPERTY(QString totalText READ totalText NOTIFY summaryChanged)
	// The same as numbers, for the panel to add the other jobs' (conversions,
	// copies, the store) to them.
	Q_PROPERTY(double bytesPerSecond READ bytesPerSecond NOTIFY summaryChanged)
	Q_PROPERTY(double bytesLeft READ bytesLeft NOTIFY summaryChanged)
	Q_PROPERTY(double bytesTotal READ bytesTotal NOTIFY summaryChanged)

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
		LocalKeyRole,
		// A finished task of a file that has a later task (the upload once
		// the install of the same package is there): one card per file.
		SupersededRole,
		// Whether ↑ / ↓ can move this waiting task (never past the one running).
		CanMoveUpRole,
		CanMoveDownRole,
		// The firmware the package needs ("9.00"), and what to know before
		// it goes (an official package; one above the PS4's firmware).
		FirmwareRole,
		WarningRole,
	};

	explicit QueueModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	QString totalSpeedText() const;
	QString remainingText() const;
	QString totalText() const;
	double bytesPerSecond() const;
	double bytesLeft() const;
	double bytesTotal() const;
	// "147.5 MB", as everywhere in the app.
	Q_INVOKABLE QString bytesText(double bytes) const;
	// The cards the panel shows: rows neither superseded nor followed by a
	// conversion card (`followed`: their localKey → true).
	Q_INVOKABLE int shownCount(const QVariantMap &followed) const;

	// Chamado de qualquer thread.
	void applySnapshot(const std::vector<QueueTask> &tasks);
	void setIcon(const QString &taskId, const QString &dataUri);
	void setPackageFacts(const QString &taskId, const QString &firmware, const QString &warning);
	// The same file, however its path is written (case aside on Windows).
	static QString fileKey(const QString &localPath);

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
		qint64 totalBytes = 0;
		QString localKey;
		bool superseded = false;
		bool canMoveUp = false;
		bool canMoveDown = false;
		bool operator==(const Row &o) const
		{
			return id == o.id && title == o.title && titleId == o.titleId && category == o.category
				&& state == o.state && stateLabel == o.stateLabel && percent == o.percent
				&& sizeText == o.sizeText && speedText == o.speedText && etaText == o.etaText
				&& message == o.message && mode == o.mode && active == o.active
				&& bytesPerSecond == o.bytesPerSecond && remainingBytes == o.remainingBytes
				&& totalBytes == o.totalBytes
				&& localKey == o.localKey && superseded == o.superseded
				&& canMoveUp == o.canMoveUp && canMoveDown == o.canMoveDown;
		}
	};

	QList<Row> rows_;
	QHash<QString, QString> icons_;
	QHash<QString, QString> firmware_;
	QHash<QString, QString> warnings_;
};

} // namespace orbislink
