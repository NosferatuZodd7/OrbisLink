// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

namespace orbislink {

// Game pictures for QML, made ready to draw at the screen's own pixels:
//
//   image://cover/<width>x<height>/<radius>/<crop|fit>/<background>/<source>
//
// the frame and the corner radius in device pixels, the background a colour
// ("#AARRGGBB"), the source the picture's own URL (file:// or data:),
// each part percent-encoded. A 512-pixel icon drawn straight into a small
// tile loses pixels on the way — thin lines break up, small text frays;
// here it is scaled down area by area, its corners cut with antialiasing,
// and the tile shows it 1:1. Made on QML's loading threads.
class CoverImageProvider : public QQuickImageProvider
{
public:
	CoverImageProvider();
	QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

	// `frame` filled by the picture (`crop`), or, when its shape is not
	// the frame's, the whole picture over a dimmed copy of itself on
	// `background`; the corners rounded by `radius`.
	static QImage render(const QImage &picture, const QSize &frame, qreal radius, bool crop, const QColor &background);
	// The picture a source URL names: a file, a resource or a data: URL.
	static QImage load(const QString &source);
};

} // namespace orbislink
