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

// O temporizador que alimenta a placa. 20 ms dá margem de sobra sobre os
// 40 ms de silêncio com que a fila arranca.
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
			// Atrasou-se: deita-se fora o mais antigo. Melhor um salto no
			// som do que ele ficar segundos atrás da imagem, a crescer sem
			// fim.
			buffer_.remove(0, buffer_.size() - limit_);
		}
	}
	// Um QIODevice sequencial tem de avisar que chegou alguma coisa: há
	// backends de áudio do Qt que só vão buscar amostras depois deste sinal,
	// e sem ele ficariam à espera para sempre. Sai fora do lock: quem o
	// recebe vem logo ler.
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
	// O que faltar vai a zeros: devolver menos do que o pedido faz o
	// QAudioSink entrar em suspensão e o som só voltar no arranque seguinte.
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
		samplesPlayed_ = 0;
		framesReceived_ = 0;
		underruns_ = 0;
	}
	// Meio segundo de folga: acima disso a latência é pior que o corte.
	queue_.setLimit(static_cast<qint64>(rate) * channels * 2 / 2);
	queue_.clear();

	// O QAudioSink tem de ser criado na thread a que pertence, não na do
	// chiaki que trouxe este aviso.
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
			state_ = QStringLiteral("sem-dispositivo");
			logWarning("Remote Play: não há saída de áudio; o stream fica sem som. "
				"Falta o backend multimédia do Qt?");
		}
		else
		{
			deviceName_ = device.description();
			logInfo("Remote Play: saída de som \"" + deviceName_.toStdString() + "\"; a consola "
				"manda " + std::to_string(format_.sampleRate()) + " Hz, "
				+ std::to_string(format_.channelCount()) + " canais.");

			// Se a placa não aceitar exactamente 48 kHz estéreo — e a consola não
			// manda outra coisa — converte-se para o que a placa aceita, em vez de
			// ficar tudo mudo.
			deviceFormat_ = format_;
			if(!device.isFormatSupported(format_))
			{
				QAudioFormat preferido = device.preferredFormat();
				preferido.setSampleFormat(QAudioFormat::Int16);
				if(!device.isFormatSupported(preferido))
				{
					// Último recurso: o formato preferido tal e qual,
					// mesmo que não seja Int16 — melhor tentar que desistir.
					preferido = device.preferredFormat();
				}
				logWarning("Remote Play: a placa não aceita "
					+ std::to_string(format_.sampleRate()) + " Hz/"
					+ std::to_string(format_.channelCount()) + " canais; a converter para "
					+ std::to_string(preferido.sampleRate()) + " Hz/"
					+ std::to_string(preferido.channelCount()) + " canais.");
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
		emit failed(tr("Este PC não tem nenhuma saída de som activa."));
		return;
	}

	if(!queue_.isOpen())
		queue_.open(QIODevice::ReadOnly);

	// Escrita directa, e não o modo em que a placa vem buscar.
	//
	// O modo "pull" do QAudioSink, o que a documentação mostra primeiro, não
	// funciona em todas as máquinas: em Windows 10 com Qt 6.8.1 a placa
	// nunca vem buscar uma amostra — o sink diz-se activo, a fila enche, e o
	// stream fica mudo sem erro nenhum. A escrita directa funciona em todas,
	// e por isso é a única que se usa.
	auto novo = std::make_unique<QAudioSink>(device, formatoDaPlaca);
	connect(novo.get(), &QAudioSink::stateChanged, this, &AudioOutput::handleSinkState,
		Qt::QueuedConnection);
	QIODevice *alvo = novo->start();

	if(!alvo || novo->error() != QAudio::NoError)
	{
		const int codigo = static_cast<int>(novo->error());
		{
			QMutexLocker lock(&mutex_);
			state_ = QStringLiteral("erro");
		}
		logError("Remote Play: o QAudioSink falhou a arrancar, erro " + std::to_string(codigo));
		emit failed(tr("A placa de som recusou o stream (erro %1).").arg(codigo));
		return;
	}

	QString dispositivo;
	{
		QMutexLocker lock(&mutex_);
		sink_ = std::move(novo);
		pushTarget_ = alvo;
		pushMode_ = true;
		state_ = QStringLiteral("a-tocar");
		sinkState_ = stateName(sink_->state());
		dispositivo = deviceName_;
		logInfo("Remote Play: som a sair por \"" + deviceName_.toStdString() + "\" ("
			+ sinkState_.toStdString() + ", buffer de "
			+ std::to_string(sink_->bufferSize()) + " bytes, escrita directa).");
	}

	// É este temporizador que faz de bomba: acorda, vê quanto espaço a
	// placa tem, e enche-o com o que estiver na fila.
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
		// Cada transição fica registada. Sem isto, um sink que vai a Idle e
		// nunca mais volta é indistinguível de um que nunca arrancou.
		logInfo("Remote Play: QAudioSink -> " + sinkState_.toStdString() + " (na fila "
			+ std::to_string(queue_.queuedBytes()) + " bytes, já entregues "
			+ std::to_string(queue_.pulledBytes()) + ").");
		if(estado == QAudio::IdleState)
			++underruns_;
		if(estado == QAudio::StoppedState && sink_->error() != QAudio::NoError)
		{
			state_ = QStringLiteral("erro");
			falha = tr("O som parou (erro %1).").arg(static_cast<int>(sink_->error()));
			logError("Remote Play: o som parou com erro "
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
		// O buffer da placa é o único travão de que precisamos: escrever
		// tudo o que ele aceita e nem mais um byte.
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
	// Uma linha por troço, pela ordem por que o som passa. Quem lê isto quer
	// saber onde é que o caudal chega a zero.
	QStringList linhas;
	linhas << QStringLiteral("  1. consola anunciou   %1")
			.arg(configured_ ? QStringLiteral("%1 Hz, %2 canais")
						.arg(format_.sampleRate())
						.arg(format_.channelCount())
					: QStringLiteral("(nada — o cabeçalho de áudio não chegou)"));
	linhas << QStringLiteral("  2. tramas recebidas   %1 (%2 amostras por canal)")
			.arg(framesReceived_)
			.arg(samplesPlayed_);
	linhas << QStringLiteral("  3. saída escolhida    %1")
			.arg(deviceName_.isEmpty() ? QStringLiteral("(nenhuma)") : deviceName_);
	linhas << QStringLiteral("  4. formato da placa   %1 Hz, %2 canais%3")
			.arg(deviceFormat_.sampleRate())
			.arg(deviceFormat_.channelCount())
			.arg(converter_.needed() ? QStringLiteral("  (a converter)") : QString());
	linhas << QStringLiteral("  5. bytes para a fila  %1").arg(queue_.pushedBytes());
	linhas << QStringLiteral("  6. bytes entregues    %1").arg(queue_.pulledBytes());
	linhas << QStringLiteral("  7. bytes à espera     %1").arg(queue_.queuedBytes());
	linhas << QStringLiteral("  8. QAudioSink         %1 (%2 pausas por falta de dados)")
			.arg(sinkState_)
			.arg(underruns_);
	linhas << QStringLiteral("  9. estado             %1%2")
			.arg(state_)
			.arg(muted_ ? QStringLiteral("  (em silêncio a pedido)") : QString());

	// A conclusão, escrita à mão, porque é a única parte que alguém lê.
	QString veredicto;
	if(!configured_)
		veredicto = QStringLiteral("a consola nunca anunciou o formato de áudio");
	else if(queue_.pushedBytes() == 0)
		veredicto = QStringLiteral("o descodificador não entregou uma única trama");
	else if(deviceName_.isEmpty())
		veredicto = QStringLiteral("não há saída de som neste PC — falta o backend "
			"multimédia do Qt?");
	else if(queue_.pulledBytes() == 0)
		veredicto = QStringLiteral("o som chega à fila mas nunca sai para a placa");
	else
		veredicto = QStringLiteral("o caminho do som está a correr");
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

	// A conversão acontece na thread do chiaki, que é a mesma que sempre
	// escreveu aqui; o converter_ não é tocado por mais ninguém depois de
	// configurado.
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
		configured_ = false;
	}
	if(morto)
		morto->stop();
	{
		QMutexLocker lock(&mutex_);
		state_ = QStringLiteral("parado");
		sinkState_ = QStringLiteral("sem-sink");
	}
	queue_.clear();
	if(queue_.isOpen())
		queue_.close();
}

} // namespace orbislink
