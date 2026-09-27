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

// Copies a plane line by line: AVFrame has a "linesize" that is almost
// never equal to the width, and QVideoFrame has its own.
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

	// When decoding is done on the GPU, the frame is in its memory and
	// cannot be read directly. It is brought into system memory — which is
	// what av_hwframe_transfer_data does — and follows the same path as the
	// others.
	AVFrame *software = nullptr;
	AVFrame *source = frame;
	if(frame->hw_frames_ctx)
	{
		software = av_frame_alloc();
		if(!software)
			return;
		if(av_hwframe_transfer_data(software, frame, 0) < 0)
		{
			av_frame_free(&software);
			// No garbage is shown: this frame is dropped and the next one
			// awaited. If it is systematic, chiaki's log says so.
			return;
		}
		software->width = frame->width;
		software->height = frame->height;
		source = software;
	}

	QVideoFrameFormat::PixelFormat format = QVideoFrameFormat::Format_Invalid;
	switch(source->format)
	{
		case AV_PIX_FMT_YUV420P:
			format = QVideoFrameFormat::Format_YUV420P;
			break;
		case AV_PIX_FMT_NV12:
			// It is what most hardware decoders return.
			format = QVideoFrameFormat::Format_NV12;
			break;
		case AV_PIX_FMT_P010LE:
			format = QVideoFrameFormat::Format_P010;
			break;
		default:
			break;
	}
	if(format == QVideoFrameFormat::Format_Invalid)
	{
		// Better to show nothing than swapped colours. Said once, so the
		// log does not fill up with the same line.
		if(!unsupportedReported_)
		{
			unsupportedReported_ = true;
			logWarning("Remote Play: unsupported picture format ("
				+ std::to_string(source->format) + "); no video.");
		}
		if(software)
			av_frame_free(&software);
		return;
	}

	QVideoFrameFormat description(QSize(source->width, source->height), format);
	QVideoFrame videoFrame(description);
	if(!videoFrame.map(QVideoFrame::WriteOnly))
	{
		if(software)
			av_frame_free(&software);
		return;
	}

	const int halfWidth = (source->width + 1) / 2;
	const int halfHeight = (source->height + 1) / 2;
	if(format == QVideoFrameFormat::Format_YUV420P)
	{
		copyPlane(videoFrame.bits(0), videoFrame.bytesPerLine(0), source->data[0],
			source->linesize[0], source->width, source->height);
		copyPlane(videoFrame.bits(1), videoFrame.bytesPerLine(1), source->data[1],
			source->linesize[1], halfWidth, halfHeight);
		copyPlane(videoFrame.bits(2), videoFrame.bytesPerLine(2), source->data[2],
			source->linesize[2], halfWidth, halfHeight);
	}
	else
	{
		// NV12 and P010: two planes, the second with both colours interleaved
		// (so the width in bytes is the same as the luma's).
		const int bytesPerSample = format == QVideoFrameFormat::Format_P010 ? 2 : 1;
		copyPlane(videoFrame.bits(0), videoFrame.bytesPerLine(0), source->data[0],
			source->linesize[0], source->width * bytesPerSample, source->height);
		copyPlane(videoFrame.bits(1), videoFrame.bytesPerLine(1), source->data[1],
			source->linesize[1], source->width * bytesPerSample, halfHeight);
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
	// Counted here and not on entry: what matters is what reaches the
	// screen, not what the decoder produced and then got lost on the way.
	frames_.fetch_add(1, std::memory_order_relaxed);
	if(!announced_)
	{
		announced_ = true;
		emit firstFrame(frame.width(), frame.height());
	}
}

} // namespace orbislink
