// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/audio_output.h"

#include "orbislink/common/log.h"

#include <QAudioSink>
#include <QMediaDevices>
#include <QMetaObject>
#include <QMutexLocker>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <cstring>

namespace orbislink {

namespace {

// The timer that feeds the card. 20 ms leaves plenty of margin over the
// 40 ms of silence the queue starts with.
constexpr int kPushTickMs = 20;

QString stateName(QAudio::State estado)
{
	switch(estado)
	{
		case QAudio::ActiveState: return QStringLiteral("Active");
		case QAudio::SuspendedState: return QStringLiteral("Suspended");
		case QAudio::StoppedState: return QStringLiteral("Stopped");
		case QAudio::IdleState: return QStringLiteral("Idle");
	}
	return QStringLiteral("?");
}

} // namespace

PcmQueue::PcmQueue(QObject *parent) : QIODevice(parent) {}

qint64 PcmQueue::bytesAvailable() const
{
	QMutexLocker lock(&mutex_);
	return buffer_.size() + QIODevice::bytesAvailable();
}

qint64 PcmQueue::pulledBytes() const
{
	QMutexLocker lock(&mutex_);
	return pulled_;
}

qint64 PcmQueue::pushedBytes() const
{
	QMutexLocker lock(&mutex_);
	return pushed_;
}

qint64 PcmQueue::queuedBytes() const
{
	QMutexLocker lock(&mutex_);
	return buffer_.size();
}

void PcmQueue::setLimit(qint64 bytes)
{
	QMutexLocker lock(&mutex_);
	limit_ = bytes;
}

void PcmQueue::clear()
{
	QMutexLocker lock(&mutex_);
	buffer_.clear();
	pulled_ = 0;
	pushed_ = 0;
}

void PcmQueue::push(const char *data, qint64 size)
{
	{
		QMutexLocker lock(&mutex_);
		buffer_.append(data, size);
		pushed_ += size;
		if(limit_ > 0 && buffer_.size() > limit_)
		{
			// Fell behind: drop the oldest. Better a skip in the sound
			// than having it seconds behind the picture, growing without
			// end.
			buffer_.remove(0, buffer_.size() - limit_);
		}
	}
	// A sequential QIODevice must announce that something arrived: some Qt
	// audio backends only fetch samples after this signal, and without it
	// they would wait forever. Emitted outside the lock: whoever receives it
	// comes straight in to read.
	emit readyRead();
}

qint64 PcmQueue::take(char *dest, qint64 max)
{
	QMutexLocker lock(&mutex_);
	const qint64 quanto = qMin<qint64>(buffer_.size(), max);
	if(quanto > 0)
	{
		std::memcpy(dest, buffer_.constData(), static_cast<size_t>(quanto));
		buffer_.remove(0, quanto);
		pulled_ += quanto;
	}
	return quanto;
}

qint64 PcmQueue::readData(char *data, qint64 maxSize)
{
	QMutexLocker lock(&mutex_);
	const qint64 available = qMin<qint64>(buffer_.size(), maxSize);
	if(available > 0)
	{
		std::memcpy(data, buffer_.constData(), static_cast<size_t>(available));
		buffer_.remove(0, available);
	}
	pulled_ += available;
	// Whatever is missing is zero-filled: returning less than asked makes
	// QAudioSink go into suspend and the sound only comes back on the next start.
	if(available < maxSize)
		std::memset(data + available, 0, static_cast<size_t>(maxSize - available));
	return maxSize;
}

AudioOutput::AudioOutput(QObject *parent) : QObject(parent), queue_(this) {}

AudioOutput::~AudioOutput() { stop(); }

QString AudioOutput::state() const
{
	QMutexLocker lock(&mutex_);
	return state_;
}

QString AudioOutput::deviceName() const
{
	QMutexLocker lock(&mutex_);
	return deviceName_;
}

qint64 AudioOutput::samplesPlayed() const
{
	QMutexLocker lock(&mutex_);
	return samplesPlayed_;
}

void AudioOutput::configure(unsigned int channels, unsigned int rate)
{
	{
		QMutexLocker lock(&mutex_);
		format_.setSampleRate(static_cast<int>(rate));
		format_.setChannelCount(static_cast<int>(channels));
		format_.setSampleFormat(QAudioFormat::Int16);
		configured_ = true;
		terminada_ = false;
		samplesPlayed_ = 0;
		framesReceived_ = 0;
		underruns_ = 0;
	}
	// Half a second of slack: beyond that the latency is worse than the gap.
	queue_.setLimit(static_cast<qint64>(rate) * channels * 2 / 2);
	queue_.clear();

	// The QAudioSink must be created on the thread it belongs to, not on
	// chiaki's, which brought this notice.
	QMetaObject::invokeMethod(this, [this]() { ensureStarted(); }, Qt::QueuedConnection);
}

void AudioOutput::ensureStarted()
{
	QAudioDevice device;
	QAudioFormat formatoDaPlaca;
	{
		QMutexLocker lock(&mutex_);
		if(!configured_ || sink_)
			return;

		device = QMediaDevices::defaultAudioOutput();
		if(device.isNull())
		{
			state_ = QStringLiteral("no-device");
			logWarning("Remote Play: no audio output; the stream has no sound. "
				"Is the Qt multimedia backend missing?");
		}
		else
		{
			deviceName_ = device.description();
			logInfo("Remote Play: sound output \"" + deviceName_.toStdString() + "\"; the console "
				"sends " + std::to_string(format_.sampleRate()) + " Hz, "
				+ std::to_string(format_.channelCount()) + " channels.");

			// If the card does not accept exactly 48 kHz stereo — and the console
			// sends nothing else — convert to what the card accepts, instead of
			// everything going silent.
			deviceFormat_ = format_;
			if(!device.isFormatSupported(format_))
			{
				QAudioFormat preferido = device.preferredFormat();
				preferido.setSampleFormat(QAudioFormat::Int16);
				if(!device.isFormatSupported(preferido))
				{
					// Last resort: the preferred format as is, even if
					// it is not Int16 — better to try than to give up.
					preferido = device.preferredFormat();
				}
				logWarning("Remote Play: the sound card does not accept "
					+ std::to_string(format_.sampleRate()) + " Hz/"
					+ std::to_string(format_.channelCount()) + " channels; converting to "
					+ std::to_string(preferido.sampleRate()) + " Hz/"
					+ std::to_string(preferido.channelCount()) + " channels.");
				deviceFormat_ = preferido;
			}

			converter_.configure(format_.sampleRate(), format_.channelCount(),
				deviceFormat_.sampleRate(), deviceFormat_.channelCount());

			// A folga da fila passa a ser medida no formato da placa.
			queue_.setLimit(static_cast<qint64>(deviceFormat_.sampleRate())
				* deviceFormat_.channelCount() * 2 / 2);
			formatoDaPlaca = deviceFormat_;
		}
	}

	if(device.isNull())
	{
		emit failed(tr("This PC has no active sound output."));
		return;
	}

	if(!queue_.isOpen())
		queue_.open(QIODevice::ReadOnly);

	// Direct writing, not the mode where the card comes to fetch.
	//
	// QAudioSink's "pull" mode, the one the documentation shows first, does
	// not work on every machine: on Windows 10 with Qt 6.8.1 the card never
	// fetches a single sample — the sink says it is active, the queue fills,
	// and the stream stays silent with no error. Direct writing works on all
	// of them, and so it is the only one used.
	auto novo = std::make_unique<QAudioSink>(device, formatoDaPlaca);
	connect(novo.get(), &QAudioSink::stateChanged, this, &AudioOutput::handleSinkState,
		Qt::QueuedConnection);
	QIODevice *alvo = novo->start();

	if(!alvo || novo->error() != QAudio::NoError)
	{
		const int codigo = static_cast<int>(novo->error());
		{
			QMutexLocker lock(&mutex_);
			state_ = QStringLiteral("error");
		}
		logError("Remote Play: QAudioSink failed to start, error " + std::to_string(codigo));
		emit failed(tr("The sound card refused the stream (error %1).").arg(codigo));
		return;
	}

	QString dispositivo;
	{
		QMutexLocker lock(&mutex_);
		sink_ = std::move(novo);
		pushTarget_ = alvo;
		pushMode_ = true;
		state_ = QStringLiteral("playing");
		sinkState_ = stateName(sink_->state());
		dispositivo = deviceName_;
		logInfo("Remote Play: sound playing through \"" + deviceName_.toStdString() + "\" ("
			+ sinkState_.toStdString() + ", buffer of "
			+ std::to_string(sink_->bufferSize()) + " bytes, direct write).");
	}

	// This timer is the pump: it wakes up, checks how much room the card
	// has, and fills it with whatever is in the queue.
	if(!watchdog_)
	{
		watchdog_ = new QTimer(this);
		connect(watchdog_, &QTimer::timeout, this, &AudioOutput::watchdogTick);
	}
	watchdog_->setInterval(kPushTickMs);
	watchdog_->start();
	feedPushMode();

	emit started(dispositivo);
}

void AudioOutput::handleSinkState(QAudio::State estado)
{
	QString falha;
	{
		QMutexLocker lock(&mutex_);
		if(!sink_)
			return;
		sinkState_ = stateName(estado);
		// Every transition is logged. Without this, a sink that goes Idle and
		// never comes back is indistinguishable from one that never started.
		logInfo("Remote Play: QAudioSink -> " + sinkState_.toStdString() + " (queued "
			+ std::to_string(queue_.queuedBytes()) + " bytes, delivered so far "
			+ std::to_string(queue_.pulledBytes()) + ").");
		if(estado == QAudio::IdleState)
			++underruns_;
		if(estado == QAudio::StoppedState && sink_->error() != QAudio::NoError)
		{
			state_ = QStringLiteral("error");
			falha = tr("The sound stopped (error %1).").arg(static_cast<int>(sink_->error()));
			logError("Remote Play: the sound stopped with error "
				+ std::to_string(static_cast<int>(sink_->error())));
		}
	}
	if(!falha.isEmpty())
		emit failed(falha);
}

void AudioOutput::watchdogTick()
{
	feedPushMode();
}

void AudioOutput::feedPushMode()
{
	QIODevice *alvo = nullptr;
	qint64 espaco = 0;
	{
		QMutexLocker lock(&mutex_);
		if(!pushMode_ || !sink_ || !pushTarget_)
			return;
		alvo = pushTarget_;
		// The card's buffer is the only brake we need: write everything
		// it accepts and not a byte more.
		espaco = sink_->bytesFree();
	}

	const qint64 quanto = qMin(espaco, queue_.queuedBytes());
	if(quanto <= 0)
		return;
	if(scratch_.size() < quanto)
		scratch_.resize(static_cast<int>(quanto));
	const qint64 lidos = queue_.take(scratch_.data(), quanto);
	if(lidos > 0)
		alvo->write(scratch_.constData(), lidos);
}

QString AudioOutput::pipelineSummary() const
{
	QMutexLocker lock(&mutex_);
	// One line per stretch, in the order the sound flows through. Whoever
	// reads this wants to know where the flow drops to zero.
	QStringList linhas;
	linhas << QStringLiteral("  1. console announced %1")
			.arg(configured_ || terminada_ ? QStringLiteral("%1 Hz, %2 channels")
						.arg(format_.sampleRate())
						.arg(format_.channelCount())
					: QStringLiteral("(nothing — the audio header never arrived)"));
	linhas << QStringLiteral("  2. frames received   %1 (%2 samples per channel)")
			.arg(framesReceived_)
			.arg(samplesPlayed_);
	linhas << QStringLiteral("  3. output chosen     %1")
			.arg(deviceName_.isEmpty() ? QStringLiteral("(none)") : deviceName_);
	linhas << QStringLiteral("  4. card format       %1 Hz, %2 channels%3")
			.arg(deviceFormat_.sampleRate())
			.arg(deviceFormat_.channelCount())
			.arg(converter_.needed() ? QStringLiteral("  (converting)") : QString());
	linhas << QStringLiteral("  5. bytes queued      %1").arg(queue_.pushedBytes());
	linhas << QStringLiteral("  6. bytes delivered   %1").arg(queue_.pulledBytes());
	linhas << QStringLiteral("  7. bytes waiting     %1").arg(queue_.queuedBytes());
	linhas << QStringLiteral("  8. QAudioSink        %1 (%2 pauses for lack of data)")
			.arg(sinkState_)
			.arg(underruns_);
	linhas << QStringLiteral("  9. state             %1%2")
			.arg(state_)
			.arg(muted_ ? QStringLiteral("  (muted on request)") : QString());

	// The conclusion, written by hand, because it is the only part anyone reads.
	QString veredicto;
	if(terminada_)
		veredicto = framesReceived_ > 0
			? QStringLiteral("the session ended; sound arrived and was played (%1 frames) — "
							 "the numbers above are from that session").arg(framesReceived_)
			: QStringLiteral("the session ended without any sound arriving");
	else if(!configured_)
		veredicto = QStringLiteral("the console never announced the audio format");
	else if(queue_.pushedBytes() == 0)
		veredicto = QStringLiteral("the decoder did not deliver a single frame");
	else if(deviceName_.isEmpty())
		veredicto = QStringLiteral("there is no sound output on this PC — is the Qt "
			"multimedia backend missing?");
	else if(queue_.pulledBytes() == 0)
		veredicto = QStringLiteral("sound reaches the queue but never goes out to the card");
	else
		veredicto = QStringLiteral("the sound path is running");
	linhas << QStringLiteral(" => %1").arg(veredicto);

	return linhas.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

void AudioOutput::write(const int16_t *pcm, size_t samples)
{
	if(!pcm || samples == 0)
		return;

	int channels = 0;
	bool converter = false;
	{
		QMutexLocker lock(&mutex_);
		if(muted_ || !configured_)
			return;
		channels = format_.channelCount();
		converter = converter_.needed();
		samplesPlayed_ += static_cast<qint64>(samples);
		++framesReceived_;
	}

	if(!converter)
	{
		queue_.push(reinterpret_cast<const char *>(pcm),
			static_cast<qint64>(samples) * channels * 2);
		return;
	}

	// Conversion happens on chiaki's thread, the same one that always
	// wrote here; converter_ is not touched by anyone else once
	// configured.
	const std::vector<int16_t> &convertido = converter_.convert(pcm, samples);
	if(!convertido.empty())
		queue_.push(reinterpret_cast<const char *>(convertido.data()),
			static_cast<qint64>(convertido.size()) * 2);
}

void AudioOutput::setMuted(bool muted)
{
	{
		QMutexLocker lock(&mutex_);
		muted_ = muted;
	}
	if(muted)
		queue_.clear();
}

void AudioOutput::stop()
{
	if(watchdog_ && watchdog_->thread() == QThread::currentThread())
		watchdog_->stop();

	std::unique_ptr<QAudioSink> morto;
	{
		QMutexLocker lock(&mutex_);
		morto = std::move(sink_);
		pushTarget_ = nullptr;
		pushMode_ = false;
		// The session's numbers stay for the diagnostics; only mark that
		// it ended, so it does not look like the sound never arrived.
		terminada_ = configured_;
		configured_ = false;
	}
	if(morto)
		morto->stop();
	{
		QMutexLocker lock(&mutex_);
		state_ = QStringLiteral("stopped");
		sinkState_ = QStringLiteral("no-sink");
	}
	queue_.clear();
	if(queue_.isOpen())
		queue_.close();
}

} // namespace orbislink
