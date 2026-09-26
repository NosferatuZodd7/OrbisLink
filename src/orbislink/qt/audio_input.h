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

// Captura do microfone para o Remote Play.
//
// A consola não negoceia o formato: quer 48 kHz, 2 canais, 16 bits, em
// tramas de 480 amostras por canal. O microfone da máquina raramente é
// estéreo, por isso um canal é duplicado quando for preciso; o resto do
// caminho (Opus, pacotes) é do chiaki.
//
// A captura é deliberadamente explícita: só arranca quando alguém chama
// start(), e para quando a sessão acaba. Não há aqui nenhum caminho que a
// ligue sozinha.
class AudioInput : public QObject
{
	Q_OBJECT

public:
	// Uma trama pronta a enviar: `samples` amostras por canal, já
	// intercaladas em estéreo.
	using FrameCallback = std::function<void(const int16_t *pcm, size_t samples)>;

	explicit AudioInput(QObject *parent = nullptr);
	~AudioInput() override;

	void setFrameCallback(FrameCallback callback);

	// Arranca a captura no dispositivo de entrada por omissão.
	// Devolve false e preenche `error` quando não há microfone, quando o
	// sistema recusa o acesso, ou quando o formato não é aceite.
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
	// Quando o microfone é mono, cada amostra é duplicada para os dois
	// canais em vez de se enviar metade do estéreo em silêncio.
	bool duplicateMono_ = false;
};

} // namespace orbislink
