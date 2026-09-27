// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/common/pcm_convert.h"

#include <QAudio>
#include <QAudioFormat>
#include <QByteArray>
#include <QIODevice>
#include <QMutex>
#include <QObject>
#include <memory>

QT_BEGIN_NAMESPACE
class QAudioSink;
class QTimer;
QT_END_NAMESPACE

namespace orbislink {

// Sample queue between chiaki's thread and the sound card.
//
// QAudioSink pulls from here when it needs to ("pull" mode), on its own thread.
// That is why this class exists instead of writing directly to the
// device: producer and consumer are on different threads, and
// QAudioSink's QIODevice is not safe to use from outside.
class PcmQueue : public QIODevice
{
	Q_OBJECT

public:
	explicit PcmQueue(QObject *parent = nullptr);

	bool isSequential() const override { return true; }
	qint64 bytesAvailable() const override;

	void push(const char *data, qint64 size);
	void setLimit(qint64 bytes);
	void clear();
	// Drops what is waiting but keeps the byte counts, for the diagnostics.
	void discard();
	// How many bytes the sound card has already fetched. Zero with the
	// session running means nobody is pulling — and that is a different
	// problem from "no sound arrived".
	qint64 pulledBytes() const;
	// How many bytes have been put in. The difference between the two tells
	// which end of the pipe the sound got lost at.
	qint64 pushedBytes() const;
	qint64 queuedBytes() const;

	// Takes up to `max` real bytes, without padding the rest with silence.
	// This is what "push" mode needs: there we do the writing, and writing
	// silence that does not exist only fills the card's buffer.
	qint64 take(char *dest, qint64 max);

protected:
	qint64 readData(char *data, qint64 maxSize) override;
	qint64 writeData(const char *, qint64) override { return -1; }

private:
	mutable QMutex mutex_;
	QByteArray buffer_;
	qint64 limit_ = 0;
	qint64 pulled_ = 0;
	qint64 pushed_ = 0;
};

// Remote Play audio playback.
class AudioOutput : public QObject
{
	Q_OBJECT

public:
	explicit AudioOutput(QObject *parent = nullptr);
	~AudioOutput() override;

	// Called when the console announces the format (chiaki's thread).
	void configure(unsigned int channels, unsigned int rate);
	// PCM intercalado, `samples` por canal (thread do chiaki).
	void write(const int16_t *pcm, size_t samples);
	void stop();

	bool muted() const { return muted_; }
	void setMuted(bool muted);

	// So the interface can say what is going on with the sound instead of
	// leaving the person staring at a silent stream: "stopped", "playing",
	// "no-device" or "error".
	QString state() const;
	QString deviceName() const;
	// How many samples have been handed to the card. Zero with the session
	// running means the sound is not arriving.
	qint64 samplesPlayed() const;
	// One line with the state of each stretch of the audio path, so the
	// diagnostics answer the question instead of leaving it open.
	QString pipelineSummary() const;

signals:
	// Emitted when the sound cannot start, with the reason.
	void failed(const QString &reason);
	void started(const QString &device);

private slots:
	void handleSinkState(QAudio::State state);
	// The pump: wakes up, checks how much room the card has, and fills it.
	void watchdogTick();

private:
	void ensureStarted();
	void feedPushMode();

	mutable QMutex mutex_;
	PcmQueue queue_;
	std::unique_ptr<QAudioSink> sink_;
	QAudioFormat format_;      // what the console sends
	QAudioFormat deviceFormat_; // what the card accepts (may differ)
	PcmConverter converter_;
	QTimer *watchdog_ = nullptr;
	// We write into the QIODevice QAudioSink returns, instead of waiting
	// for it to come and fetch. See the note in ensureStarted(): the mode
	// where the card pulls did not work on Windows 10 with Qt 6.8.1.
	QIODevice *pushTarget_ = nullptr;
	QByteArray scratch_;
	QString deviceName_;
	QString state_ = QStringLiteral("stopped");
	QString sinkState_ = QStringLiteral("no-sink");
	qint64 samplesPlayed_ = 0;
	qint64 framesReceived_ = 0;
	qint64 underruns_ = 0;
	bool pushMode_ = false;
	bool configured_ = false;
	bool ended_ = false;   // there was a session and it has ended
	bool muted_ = false;
};

} // namespace orbislink
