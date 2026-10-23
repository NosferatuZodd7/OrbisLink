// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/cover_provider.h"

#include <QByteArray>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace orbislink {

namespace {

// Bigger than any tile on any screen: a request past it is a mistake.
constexpr int kMaxSide = 2048;

// The picture scaled to `size` keeping its shape, smoothly: QImage's smooth
// scaling averages the pixels each one covers when it shrinks.
QImage scaledTo(const QImage &picture, const QSize &size, Qt::AspectRatioMode mode)
{
	return picture.scaled(size, mode, Qt::SmoothTransformation);
}

void drawCentred(QPainter *painter, const QSize &frame, const QImage &image)
{
	painter->drawImage(QPointF((frame.width() - image.width()) / 2.0, (frame.height() - image.height()) / 2.0), image);
}

} // namespace

CoverImageProvider::CoverImageProvider()
	: QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
}

QImage CoverImageProvider::load(const QString &source)
{
	QImage image;
	if(source.startsWith(QLatin1String("data:")))
	{
		const int comma = source.indexOf(QLatin1Char(','));
		if(comma < 0)
			return image;
		const QString header = source.left(comma);
		const QByteArray payload = source.mid(comma + 1).toLatin1();
		const QByteArray bytes = header.endsWith(QLatin1String(";base64")) ? QByteArray::fromBase64(payload)
																		   : QByteArray::fromPercentEncoding(payload);
		image.loadFromData(bytes);
		return image;
	}
	const QUrl url(source);
	if(url.isLocalFile())
		image.load(url.toLocalFile());
	else if(url.scheme() == QLatin1String("qrc"))
		image.load(QLatin1Char(':') + url.path());
	else
		image.load(source);
	return image;
}

QImage CoverImageProvider::render(const QImage &picture, const QSize &frame, qreal radius, bool crop,
	const QColor &background)
{
	if(picture.isNull() || frame.isEmpty())
		return QImage();
	const QImage source = picture.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	QImage content(frame, QImage::Format_ARGB32_Premultiplied);
	content.fill(background);
	{
		QPainter painter(&content);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		const qreal ratio = static_cast<qreal>(source.width()) / std::max(1, source.height());
		const qreal frameRatio = static_cast<qreal>(frame.width()) / std::max(1, frame.height());
		const QImage cover = scaledTo(source, frame, Qt::KeepAspectRatioByExpanding);
		if(crop || std::abs(ratio - frameRatio) < 0.12)
			drawCentred(&painter, frame, cover);
		else
		{
			// Another shape (a PS2 box in a square): whole, over itself dimmed.
			painter.setOpacity(0.3);
			drawCentred(&painter, frame, cover);
			painter.setOpacity(1.0);
			drawCentred(&painter, frame, scaledTo(source, frame, Qt::KeepAspectRatio));
		}
	}
	// The corners cut by filling a rounded shape with the picture: its edge
	// is antialiased (a clip would not be).
	QImage out(frame, QImage::Format_ARGB32_Premultiplied);
	out.fill(Qt::transparent);
	QPainter painter(&out);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QBrush(content));
	const qreal r = std::max<qreal>(0, std::min({ radius, frame.width() / 2.0, frame.height() / 2.0 }));
	painter.drawRoundedRect(QRectF(QPointF(0, 0), QSizeF(frame)), r, r);
	return out;
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
	Q_UNUSED(requestedSize)
	// <width>x<height>/<radius>/<crop|fit>/<background>/<source>
	const QStringList parts = id.split(QLatin1Char('/'));
	QImage result;
	if(parts.size() >= 5)
	{
		const QStringList dims = parts[0].split(QLatin1Char('x'));
		const int width = dims.size() == 2 ? dims[0].toInt() : 0;
		const int height = dims.size() == 2 ? dims[1].toInt() : 0;
		const qreal radius = parts[1].toDouble();
		const bool crop = parts[2] == QLatin1String("crop");
		const QColor background(QUrl::fromPercentEncoding(parts[3].toUtf8()));
		const QString source = QUrl::fromPercentEncoding(parts.mid(4).join(QLatin1Char('/')).toUtf8());
		if(width > 0 && height > 0 && width <= kMaxSide && height <= kMaxSide)
			result = render(load(source), QSize(width, height), radius, crop,
				background.isValid() ? background : QColor(Qt::transparent));
	}
	if(size)
		*size = result.size();
	return result;
}

} // namespace orbislink
