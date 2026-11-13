// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QString>

namespace orbislink {

// A picture of one's own behind the glass (Settings → Personalisation).
//
// The glass has no live blur: what is behind it is painted already out of
// focus, which is what makes a translucent pane read as frosted. So the
// picture chosen is kept as a copy, and the window shows a blurred version
// of it, made once for each blur strength.
namespace backdrop {

// The colours a picture is made of, enough for the theme to build a palette
// from: its overall colour, its most present clear hue and a second one
// apart from it. A colour with no clear hue (a grey picture) is invalid.
struct Tones
{
	QColor average;
	QColor primary;
	QColor secondary;
};

// Reads the picture and brings it to at most `longest` pixels on its long
// side. Null when it cannot be read.
QImage load(const QString &path, int longest = 2560);

// The picture out of focus: 0 is as it is, 100 a soft wash of its colours.
QImage blurred(const QImage &image, int strength);

// What the picture is made of (see Tones).
Tones analyse(const QImage &image);

} // namespace backdrop
} // namespace orbislink
