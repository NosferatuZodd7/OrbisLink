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

	const QAudioDevice entrada = QMediaDevices::defaultAudioInput();
	if(entrada.isNull())
	{
		if(error)
			*error = tr("There is no microphone available on this PC.");
		return false;
	}

	QAudioFormat formato;
	formato.setSampleRate(kRate);
	formato.setChannelCount(kChannels);
	formato.setSampleFormat(QAudioFormat::Int16);

	// Nem todos os microfones fazem estéreo. Quando só há um canal,
	// captura-se mono e duplica-se — enviar um canal vazio daria uma voz só
	// de um lado na consola.
	duplicateMono_ = false;
	if(!entrada.isFormatSupported(formato))
	{
		QAudioFormat mono = formato;
		mono.setChannelCount(1);
		if(entrada.isFormatSupported(mono))
		{
			formato = mono;
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

	source_ = std::make_unique<QAudioSource>(entrada, formato);
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
	deviceName_ = entrada.description();
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

	const int canaisCapturados = duplicateMono_ ? 1 : kChannels;
	const int bytesPorTrama =
		kFrameSamples * canaisCapturados * static_cast<int>(sizeof(int16_t));

	std::vector<int16_t> estereo(static_cast<size_t>(kFrameSamples) * kChannels);
	while(pending_.size() >= bytesPorTrama)
	{
		const int16_t *origem = reinterpret_cast<const int16_t *>(pending_.constData());
		if(duplicateMono_)
		{
			for(int i = 0; i < kFrameSamples; ++i)
			{
				estereo[static_cast<size_t>(i) * 2] = origem[i];
				estereo[static_cast<size_t>(i) * 2 + 1] = origem[i];
			}
		}
		else
			std::memcpy(estereo.data(), origem, static_cast<size_t>(bytesPorTrama));

		onFrame_(estereo.data(), static_cast<size_t>(kFrameSamples));
		pending_.remove(0, bytesPorTrama);
	}

	// Se a captura andar mais depressa do que o consumo, a fila não pode
	// crescer sem fim: o que interessa numa conversa é o som de agora.
	const int limite = bytesPorTrama * 10;
	if(pending_.size() > limite)
	{
		logDebug("Microphone capture fell behind; dropping the backlog.");
		pending_.clear();
	}
}

} // namespace orbislink
