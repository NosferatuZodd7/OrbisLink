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

// Fila de amostras entre a thread do chiaki e a placa de som.
//
// O QAudioSink puxa daqui quando precisa (modo "pull"), numa thread dele.
// É por isso que existe esta classe em vez de se escrever directamente no
// dispositivo: quem produz e quem consome estão em threads diferentes, e o
// QIODevice do QAudioSink não é seguro de usar de fora.
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
	// Quantos bytes a placa de som já veio buscar. Zero com a sessão a
	// correr quer dizer que ninguém está a puxar — e isso é outro problema
	// que não "não chegou som".
	qint64 pulledBytes() const;
	// Quantos bytes já lá foram postos. A diferença entre os dois diz de que
	// lado do tubo é que o som se perdeu.
	qint64 pushedBytes() const;
	qint64 queuedBytes() const;

	// Tira até `max` bytes reais, sem encher o resto com silêncio. É o que o
	// modo "push" precisa: aí somos nós a escrever, e escrever silêncio que
	// não existe só serve para encher o buffer da placa.
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

// Reprodução do áudio do Remote Play.
class AudioOutput : public QObject
{
	Q_OBJECT

public:
	explicit AudioOutput(QObject *parent = nullptr);
	~AudioOutput() override;

	// Chamado quando a consola anuncia o formato (thread do chiaki).
	void configure(unsigned int channels, unsigned int rate);
	// PCM intercalado, `samples` por canal (thread do chiaki).
	void write(const int16_t *pcm, size_t samples);
	void stop();

	bool muted() const { return muted_; }
	void setMuted(bool muted);

	// Para a interface poder dizer o que se passa com o som em vez de
	// deixar a pessoa a olhar para um stream mudo: "parado", "a-tocar",
	// "sem-dispositivo" ou "erro".
	QString state() const;
	QString deviceName() const;
	// Quantas amostras já foram entregues à placa. Zero com a sessão a
	// correr significa que o som não está a chegar.
	qint64 samplesPlayed() const;
	// Uma linha com o estado de cada troço do caminho do som, para o
	// diagnóstico responder à pergunta em vez de a deixar em aberto.
	QString pipelineSummary() const;

signals:
	// Emitido quando o som não consegue arrancar, com a razão.
	void failed(const QString &reason);
	void started(const QString &device);

private slots:
	void handleSinkState(QAudio::State state);
	// A bomba: acorda, vê quanto espaço a placa tem, e enche-o.
	void watchdogTick();

private:
	void ensureStarted();
	void feedPushMode();

	mutable QMutex mutex_;
	PcmQueue queue_;
	std::unique_ptr<QAudioSink> sink_;
	QAudioFormat format_;      // o que a consola manda
	QAudioFormat deviceFormat_; // o que a placa aceita (pode ser diferente)
	PcmConverter converter_;
	QTimer *watchdog_ = nullptr;
	// Somos nós a escrever no QIODevice que o QAudioSink devolve, em vez de
	// esperar que ele venha buscar. Ver a nota em ensureStarted(): o modo
	// em que a placa puxa não funcionou num Windows 10 com Qt 6.8.1.
	QIODevice *pushTarget_ = nullptr;
	QByteArray scratch_;
	QString deviceName_;
	QString state_ = QStringLiteral("parado");
	QString sinkState_ = QStringLiteral("sem-sink");
	qint64 samplesPlayed_ = 0;
	qint64 framesReceived_ = 0;
	qint64 underruns_ = 0;
	bool pushMode_ = false;
	bool configured_ = false;
	bool muted_ = false;
};

} // namespace orbislink
