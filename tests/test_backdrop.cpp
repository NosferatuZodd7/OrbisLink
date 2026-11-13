// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A picture of one's own behind the glass: the colours read from it and the
// blurred version the window shows.
#include "orbislink/qt/backdrop.h"
#include "test_support.h"

#include <QPainter>

using namespace orbislink;

namespace {

// Mostly a deep red, with a band of blue across the bottom.
QImage redWithBlue()
{
	QImage image(400, 300, QImage::Format_RGB32);
	image.fill(QColor(170, 30, 40));
	QPainter painter(&image);
	painter.fillRect(0, 220, 400, 80, QColor(40, 80, 190));
	return image;
}

} // namespace

ORBISLINK_TEST(the_main_hue_and_a_second_one_come_from_the_picture)
{
	const backdrop::Tones tones = backdrop::analyse(redWithBlue());
	CHECK(tones.primary.isValid());
	CHECK(tones.secondary.isValid());
	CHECK(tones.primary.red() > tones.primary.blue());
	CHECK(tones.secondary.blue() > tones.secondary.red());
	CHECK(tones.average.isValid());
}

ORBISLINK_TEST(a_grey_picture_has_no_hue_to_give)
{
	QImage image(200, 200, QImage::Format_RGB32);
	image.fill(QColor(120, 120, 120));
	const backdrop::Tones tones = backdrop::analyse(image);
	CHECK(tones.average.isValid());
	CHECK(!tones.primary.isValid());
}

ORBISLINK_TEST(the_blurred_picture_keeps_its_shape_and_softens_edges)
{
	const QImage picture = redWithBlue();
	const QImage soft = backdrop::blurred(picture, 80);
	CHECK_EQ(soft.width(), 400);
	CHECK_EQ(soft.height(), 300);
	// Where red meets blue there is now something in between.
	const QColor edge(soft.pixel(200, 220));
	CHECK(edge.red() < 165 && edge.red() > 45);
	// Nothing to blur: the same picture.
	CHECK(backdrop::blurred(picture, 0).pixel(10, 10) == picture.pixel(10, 10));
}

TEST_MAIN()
