// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/video_bridge.h"

#include <QMetaObject>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include "orbislink/common/log.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

namespace orbislink {

namespace {

// Copia um plano linha a linha: o AVFrame tem um "linesize" que quase
// nunca é igual à largura, e o QVideoFrame tem o seu próprio.
void copyPlane(uchar *destination, int destinationStride, const uint8_t *source, int sourceStride,
	int width, int height)
{
	for(int y = 0; y < height; ++y)
		memcpy(destination + static_cast<qsizetype>(y) * destinationStride,
			source + static_cast<qsizetype>(y) * sourceStride, static_cast<size_t>(width));
}

} // namespace

VideoBridge::VideoBridge(QObject *parent) : QObject(parent) {}
VideoBridge::~VideoBridge() = default;

void VideoBridge::setVideoSink(QVideoSink *sink)
{
	sink_ = sink;
	announced_ = false;
}

void VideoBridge::clear()
{
	announced_ = false;
	frames_.store(0, std::memory_order_relaxed);
	QMetaObject::invokeMethod(this, [this]() {
		if(sink_)
			sink_->setVideoFrame(QVideoFrame());
	}, Qt::QueuedConnection);
}

void VideoBridge::presentFrame(AVFrame *frame)
{
	if(!frame || frame->width <= 0 || frame->height <= 0)
		return;

	// Quando a descodificação é feita pela placa gráfica, o fotograma está
	// na memória dela e não se pode ler directamente. Traz-se para a
	// memória do sistema — que é o que o av_hwframe_transfer_data faz — e
	// segue o mesmo caminho dos outros.
	AVFrame *software = nullptr;
	AVFrame *fonte = frame;
	if(frame->hw_frames_ctx)
	{
		software = av_frame_alloc();
		if(!software)
			return;
		if(av_hwframe_transfer_data(software, frame, 0) < 0)
		{
			av_frame_free(&software);
			// Não se mostra lixo: falha-se este fotograma e espera-se o
			// seguinte. Se for sistemático, o registo do chiaki di-lo.
			return;
		}
		software->width = frame->width;
		software->height = frame->height;
		fonte = software;
	}

	QVideoFrameFormat::PixelFormat formato = QVideoFrameFormat::Format_Invalid;
	switch(fonte->format)
	{
		case AV_PIX_FMT_YUV420P:
			formato = QVideoFrameFormat::Format_YUV420P;
			break;
		case AV_PIX_FMT_NV12:
			// É o que a maior parte dos descodificadores por hardware devolve.
			formato = QVideoFrameFormat::Format_NV12;
			break;
		case AV_PIX_FMT_P010LE:
			formato = QVideoFrameFormat::Format_P010;
			break;
		default:
			break;
	}
	if(formato == QVideoFrameFormat::Format_Invalid)
	{
		// Melhor não mostrar nada do que mostrar cores trocadas. Diz-se uma
		// vez, para o registo não ficar cheio da mesma linha.
		if(!unsupportedReported_)
		{
			unsupportedReported_ = true;
			logWarning("Remote Play: formato de imagem não suportado ("
				+ std::to_string(fonte->format) + "); sem vídeo.");
		}
		if(software)
			av_frame_free(&software);
		return;
	}

	QVideoFrameFormat descricao(QSize(fonte->width, fonte->height), formato);
	QVideoFrame videoFrame(descricao);
	if(!videoFrame.map(QVideoFrame::WriteOnly))
	{
		if(software)
			av_frame_free(&software);
		return;
	}

	const int metadeLargura = (fonte->width + 1) / 2;
	const int metadeAltura = (fonte->height + 1) / 2;
	if(formato == QVideoFrameFormat::Format_YUV420P)
	{
		copyPlane(videoFrame.bits(0), videoFrame.bytesPerLine(0), fonte->data[0],
			fonte->linesize[0], fonte->width, fonte->height);
		copyPlane(videoFrame.bits(1), videoFrame.bytesPerLine(1), fonte->data[1],
			fonte->linesize[1], metadeLargura, metadeAltura);
		copyPlane(videoFrame.bits(2), videoFrame.bytesPerLine(2), fonte->data[2],
			fonte->linesize[2], metadeLargura, metadeAltura);
	}
	else
	{
		// NV12 e P010: dois planos, o segundo com as duas cores entrelaçadas
		// (por isso a largura em bytes é a mesma da luminância).
		const int bytesPorAmostra = formato == QVideoFrameFormat::Format_P010 ? 2 : 1;
		copyPlane(videoFrame.bits(0), videoFrame.bytesPerLine(0), fonte->data[0],
			fonte->linesize[0], fonte->width * bytesPorAmostra, fonte->height);
		copyPlane(videoFrame.bits(1), videoFrame.bytesPerLine(1), fonte->data[1],
			fonte->linesize[1], fonte->width * bytesPorAmostra, metadeAltura);
	}
	videoFrame.unmap();

	if(software)
		av_frame_free(&software);

	QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection,
		Q_ARG(QVideoFrame, videoFrame));
}

void VideoBridge::deliver(const QVideoFrame &frame)
{
	if(!sink_)
		return;
	sink_->setVideoFrame(frame);
	// Contado aqui e não à entrada: o que interessa é o que chega ao ecrã,
	// não o que o descodificador produziu e depois se perdeu pelo caminho.
	frames_.fetch_add(1, std::memory_order_relaxed);
	if(!announced_)
	{
		announced_ = true;
		emit firstFrame(frame.width(), frame.height());
	}
}

} // namespace orbislink
