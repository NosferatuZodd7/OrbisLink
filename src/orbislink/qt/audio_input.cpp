// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/audio_input.h"

#include "orbislink/common/log.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

#include <cstring>
#include <vector>

namespace orbislink {

AudioInput::AudioInput(QObject *parent) : QObject(parent) {}

AudioInput::~AudioInput() { stop(); }

void AudioInput::setFrameCallback(FrameCallback callback) { onFrame_ = std::move(callback); }

bool AudioInput::start(QString *error)
{
	if(active_)
		return true;

	const QAudioDevice input = QMediaDevices::defaultAudioInput();
	if(input.isNull())
	{
		if(error)
			*error = tr("There is no microphone available on this PC.");
		return false;
	}

	QAudioFormat format;
	format.setSampleRate(kRate);
	format.setChannelCount(kChannels);
	format.setSampleFormat(QAudioFormat::Int16);

	// Not every microphone does stereo. When there is only one channel,
	// capture mono and duplicate it — sending an empty channel would put the
	// voice on one side only on the console.
	duplicateMono_ = false;
	if(!input.isFormatSupported(format))
	{
		QAudioFormat mono = format;
		mono.setChannelCount(1);
		if(input.isFormatSupported(mono))
		{
			format = mono;
			duplicateMono_ = true;
			logInfo("The microphone is mono only; each sample is duplicated to both channels.");
		}
		else
		{
			if(error)
				*error = tr("The microphone does not accept 48 kHz at 16 bits, which is what the console "
					"expects.");
			return false;
		}
	}

	source_ = std::make_unique<QAudioSource>(input, format);
	device_ = source_->start();
	if(!device_)
	{
		if(error)
			*error = tr("The system refused access to the microphone.");
		source_.reset();
		return false;
	}

	connect(device_, &QIODevice::readyRead, this, &AudioInput::drain);
	pending_.clear();
	deviceName_ = input.description();
	active_ = true;
	logInfo("Microphone capturing from \"" + deviceName_.toStdString() + "\".");
	return true;
}

void AudioInput::stop()
{
	if(!active_)
		return;
	active_ = false;
	if(device_)
	{
		disconnect(device_, nullptr, this, nullptr);
		device_ = nullptr;
	}
	if(source_)
	{
		source_->stop();
		source_.reset();
	}
	pending_.clear();
	logInfo("Microphone stopped.");
}

void AudioInput::drain()
{
	if(!active_ || !device_ || !onFrame_)
		return;

	pending_.append(device_->readAll());

	const int capturedChannels = duplicateMono_ ? 1 : kChannels;
	const int bytesPerFrame =
		kFrameSamples * capturedChannels * static_cast<int>(sizeof(int16_t));

	std::vector<int16_t> stereo(static_cast<size_t>(kFrameSamples) * kChannels);
	while(pending_.size() >= bytesPerFrame)
	{
		const int16_t *origin = reinterpret_cast<const int16_t *>(pending_.constData());
		if(duplicateMono_)
		{
			for(int i = 0; i < kFrameSamples; ++i)
			{
				stereo[static_cast<size_t>(i) * 2] = origin[i];
				stereo[static_cast<size_t>(i) * 2 + 1] = origin[i];
			}
		}
		else
			std::memcpy(stereo.data(), origin, static_cast<size_t>(bytesPerFrame));

		onFrame_(stereo.data(), static_cast<size_t>(kFrameSamples));
		pending_.remove(0, bytesPerFrame);
	}

	// If capture runs faster than consumption, the queue must not grow
	// forever: what matters in a conversation is the sound of right now.
	const int limit = bytesPerFrame * 10;
	if(pending_.size() > limit)
	{
		logDebug("Microphone capture fell behind; dropping the backlog.");
		pending_.clear();
	}
}

} // namespace orbislink
