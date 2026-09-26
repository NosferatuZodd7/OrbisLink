// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QVideoFrame>
#include <atomic>

QT_BEGIN_NAMESPACE
class QVideoSink;
QT_END_NAMESPACE

struct AVFrame;

namespace orbislink {

// Leva os fotogramas do descodificador (thread do chiaki) para o item de
// vídeo do QML (thread da UI).
//
// O descodificador entrega AVFrame em YUV420P. Em vez de o converter para
// RGB no processador, copia-se plano a plano para um QVideoFrame e é a
// placa gráfica que faz a conversão ao desenhar — é o que o VideoOutput do
// Qt faz por nós.
class VideoBridge : public QObject
{
	Q_OBJECT

public:
	explicit VideoBridge(QObject *parent = nullptr);
	~VideoBridge() override;

	// Chamado pelo QML com o videoSink do VideoOutput.
	Q_INVOKABLE void setVideoSink(QVideoSink *sink);

	// Chamado da thread do descodificador.
	void presentFrame(AVFrame *frame);
	// Limpa a imagem (fim de sessão).
	void clear();

	// Quantos fotogramas já foram entregues. Serve para medir os fps a
	// sério, em vez de mostrar o número que se pediu à consola: o que foi
	// pedido e o que está a chegar podem ser coisas diferentes, e é o
	// segundo que a pessoa vê.
	qint64 framesDelivered() const { return frames_.load(std::memory_order_relaxed); }

signals:
	void firstFrame(int width, int height);

private slots:
	void deliver(const QVideoFrame &frame);

private:
	QVideoSink *sink_ = nullptr;
	std::atomic<qint64> frames_ { 0 };
	bool announced_ = false;
	bool unsupportedReported_ = false;
};

} // namespace orbislink
