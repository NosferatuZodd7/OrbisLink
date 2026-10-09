// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QVideoFrame>
#include <atomic>

QT_BEGIN_NAMESPACE
class QVideoSink;
QT_END_NAMESPACE

struct AVFrame;

namespace orbislink {

// Carries frames from the decoder (chiaki's thread) to the QML video
// item (the UI thread).
//
// The decoder delivers AVFrame in YUV420P. Instead of converting it to
// RGB on the CPU, it is copied plane by plane into a QVideoFrame and the
// GPU does the conversion while drawing — that is what Qt's VideoOutput
// does for us.
class VideoBridge : public QObject
{
	Q_OBJECT

public:
	explicit VideoBridge(QObject *parent = nullptr);
	~VideoBridge() override;

	// Called by QML with the VideoOutput's videoSink.
	Q_INVOKABLE void setVideoSink(QVideoSink *sink);

	// Chamado da thread do descodificador.
	void presentFrame(AVFrame *frame);
	// Clears the picture (end of session).
	void clear();

	// How many frames have been delivered. Used to measure the real fps,
	// instead of showing the number asked of the console: what was asked
	// and what is arriving can differ, and the second is what the person
	// sees.
	qint64 framesDelivered() const { return frames_.load(std::memory_order_relaxed); }

signals:
	void firstFrame(int width, int height);

private slots:
	void deliver(const QVideoFrame &frame);

private:
	// The sink belongs to the QML video item, which can go before this
	// bridge (closing the window, the video part reloading).
	QPointer<QVideoSink> sink_;
	std::atomic<qint64> frames_ { 0 };
	bool announced_ = false;
	bool unsupportedReported_ = false;
};

} // namespace orbislink
