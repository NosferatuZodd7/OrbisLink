// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QAudioSource;
class QIODevice;
QT_END_NAMESPACE

namespace orbislink {

// Microphone capture for Remote Play.
//
// The console does not negotiate the format: it wants 48 kHz, 2 channels,
// 16 bits, in frames of 480 samples per channel. The machine's microphone is
// rarely stereo, so one channel is duplicated when needed; the rest of the
// path (Opus, packets) is chiaki's.
//
// Capture is deliberately explicit: it only starts when someone calls
// start(), and stops when the session ends. There is no path here that
// turns it on by itself.
class AudioInput : public QObject
{
	Q_OBJECT

public:
	// A frame ready to send: `samples` samples per channel, already
	// interleaved in stereo.
	using FrameCallback = std::function<void(const int16_t *pcm, size_t samples)>;

	explicit AudioInput(QObject *parent = nullptr);
	~AudioInput() override;

	void setFrameCallback(FrameCallback callback);

	// Starts capture on the default input device.
	// Returns false and fills `error` when there is no microphone, when the
	// system denies access, or when the format is not accepted.
	bool start(QString *error = nullptr);
	void stop();
	bool active() const { return active_; }

	// O nome do dispositivo em uso, para a interface poder dizer de onde
	// vem o som.
	QString deviceName() const { return deviceName_; }

	static constexpr int kRate = 48000;
	static constexpr int kChannels = 2;
	static constexpr int kFrameSamples = 480;

private slots:
	void drain();

private:
	std::unique_ptr<QAudioSource> source_;
	QIODevice *device_ = nullptr;
	QByteArray pending_;
	FrameCallback onFrame_;
	QString deviceName_;
	bool active_ = false;
	// When the microphone is mono, each sample is duplicated to both
	// channels instead of sending half of the stereo as silence.
	bool duplicateMono_ = false;
};

} // namespace orbislink
