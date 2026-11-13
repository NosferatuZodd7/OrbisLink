// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/backdrop.h"

#include <QImageReader>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace orbislink {
namespace backdrop {

namespace {

// One pass of a box blur along the rows (or, transposed, the columns), with
// a running sum: the cost does not grow with the radius.
void boxPass(const QImage &source, QImage &target, int radius, bool horizontal)
{
	const int width = source.width();
	const int height = source.height();
	const int lines = horizontal ? height : width;
	const int length = horizontal ? width : height;
	const int span = radius * 2 + 1;
	for(int line = 0; line < lines; ++line)
	{
		auto at = [&](int i) -> QRgb {
			const int clamped = std::clamp(i, 0, length - 1);
			return horizontal ? reinterpret_cast<const QRgb *>(source.constScanLine(line))[clamped]
							  : reinterpret_cast<const QRgb *>(source.constScanLine(clamped))[line];
		};
		int r = 0, g = 0, b = 0;
		for(int i = -radius; i <= radius; ++i)
		{
			const QRgb px = at(i);
			r += qRed(px);
			g += qGreen(px);
			b += qBlue(px);
		}
		for(int i = 0; i < length; ++i)
		{
			const QRgb out = qRgb(r / span, g / span, b / span);
			if(horizontal)
				reinterpret_cast<QRgb *>(target.scanLine(line))[i] = out;
			else
				reinterpret_cast<QRgb *>(target.scanLine(i))[line] = out;
			const QRgb leaving = at(i - radius);
			const QRgb entering = at(i + radius + 1);
			r += qRed(entering) - qRed(leaving);
			g += qGreen(entering) - qGreen(leaving);
			b += qBlue(entering) - qBlue(leaving);
		}
	}
}

// Three box passes each way come close to a Gaussian.
QImage softened(const QImage &image, int radius)
{
	QImage a = image.convertToFormat(QImage::Format_RGB32);
	QImage b(a.size(), QImage::Format_RGB32);
	for(int pass = 0; pass < 3; ++pass)
	{
		boxPass(a, b, radius, true);
		boxPass(b, a, radius, false);
	}
	return a;
}

QSize longSide(const QSize &size, int longest)
{
	if(size.width() >= size.height())
		return QSize(longest, std::max(1, size.height() * longest / std::max(1, size.width())));
	return QSize(std::max(1, size.width() * longest / std::max(1, size.height())), longest);
}

} // namespace

QImage load(const QString &path, int longest)
{
	QImageReader reader(path);
	// Phone pictures carry their rotation apart from the pixels.
	reader.setAutoTransform(true);
	QImage image = reader.read();
	if(image.isNull())
		return image;
	const QSize size = image.size();
	if(std::max(size.width(), size.height()) > longest)
		image = image.scaled(longSide(size, longest), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	return image.convertToFormat(QImage::Format_RGB32);
}

QImage blurred(const QImage &image, int strength)
{
	if(image.isNull())
		return image;
	strength = std::clamp(strength, 0, 100);
	const QSize full = image.size();
	const int fullLongest = std::max(full.width(), full.height());
	const int outLongest = std::min(1920, fullLongest);
	if(strength == 0)
		return outLongest == fullLongest ? image
			: image.scaled(longSide(full, outLongest), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	// Blur at a small size and bring it back up: the stronger the blur, the
	// smaller the picture it is made at. At 100 it is about 60 pixels long.
	const double factor = std::pow(0.5, strength / 100.0 * 5.0);
	const int smallLongest = std::max(32, static_cast<int>(std::lround(1920 * factor)));
	const QImage small = image.scaled(longSide(full, std::min(smallLongest, fullLongest)),
		Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	const QImage soft = softened(small, 2);
	return soft.scaled(longSide(full, outLongest), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

Tones analyse(const QImage &image)
{
	Tones tones;
	if(image.isNull())
		return tones;
	const QImage small = image.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
							 .convertToFormat(QImage::Format_RGB32);

	// Hue in 24 slices of 15°: how much of each there is, weighted by how
	// clear it is (saturation × value), and the colour it averages to.
	constexpr int kBins = 24;
	std::array<double, kBins> weight {};
	std::array<std::array<double, 3>, kBins> sum {};
	double r = 0, g = 0, b = 0;
	const int count = small.width() * small.height();
	for(int y = 0; y < small.height(); ++y)
	{
		const QRgb *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
		for(int x = 0; x < small.width(); ++x)
		{
			const QColor c(line[x]);
			r += c.redF();
			g += c.greenF();
			b += c.blueF();
			const float s = c.hsvSaturationF();
			const float v = c.valueF();
			const float h = c.hsvHueF();
			if(h < 0 || s < 0.18f || v < 0.15f)
				continue;
			const int bin = std::min(kBins - 1, static_cast<int>(h * kBins));
			const double w = s * v;
			weight[bin] += w;
			sum[bin][0] += c.redF() * w;
			sum[bin][1] += c.greenF() * w;
			sum[bin][2] += c.blueF() * w;
		}
	}
	tones.average = QColor::fromRgbF(static_cast<float>(r / count), static_cast<float>(g / count),
		static_cast<float>(b / count));

	// A slice with its neighbours, so a hue split across two still wins.
	auto around = [&](int bin) {
		return weight[bin] + 0.5 * (weight[(bin + kBins - 1) % kBins] + weight[(bin + 1) % kBins]);
	};
	auto colourOf = [&](int bin) {
		double cr = 0, cg = 0, cb = 0, w = 0;
		for(int d = -1; d <= 1; ++d)
		{
			const int i = (bin + d + kBins) % kBins;
			cr += sum[i][0];
			cg += sum[i][1];
			cb += sum[i][2];
			w += weight[i];
		}
		if(w <= 0)
			return QColor();
		return QColor::fromRgbF(static_cast<float>(cr / w), static_cast<float>(cg / w),
			static_cast<float>(cb / w));
	};

	int best = -1;
	for(int i = 0; i < kBins; ++i)
		if(best < 0 || around(i) > around(best))
			best = i;
	// Too little clear colour to speak of: a grey picture keeps the theme's.
	if(best < 0 || around(best) < count * 0.02)
		return tones;
	tones.primary = colourOf(best);

	int second = -1;
	for(int i = 0; i < kBins; ++i)
	{
		const int apart = std::min((i - best + kBins) % kBins, (best - i + kBins) % kBins);
		if(apart < 3)
			continue;
		if(second < 0 || around(i) > around(second))
			second = i;
	}
	if(second >= 0 && around(second) >= around(best) * 0.12)
		tones.secondary = colourOf(second);
	else
	{
		// One colour only: its neighbour on the wheel, a little warmer.
		float h, s, v;
		tones.primary.getHsvF(&h, &s, &v);
		tones.secondary = QColor::fromHsvF(std::fmod(h + 1.0f - 30.0f / 360.0f, 1.0f), s, v);
	}
	return tones;
}

} // namespace backdrop
} // namespace orbislink
