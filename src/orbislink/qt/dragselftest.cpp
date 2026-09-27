// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/dragselftest.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariant>

namespace orbislink::dragselftest {

namespace {

struct Passo
{
	QPointF dot;
	QString description;
};

bool itemContains(QQuickItem *item, const QPointF &windowPoint)
{
	const QPointF local = item->mapFromScene(windowPoint);
	return local.x() >= 0 && local.y() >= 0 && local.x() <= item->width()
		&& local.y() <= item->height();
}

} // namespace

void run(QQuickWindow *window, std::function<void(int, const QString &)> finished)
{
	auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("dropOverlay"));
	auto *left = window->findChild<QQuickItem *>(QStringLiteral("installZone"));
	auto *right = window->findChild<QQuickItem *>(QStringLiteral("ftpZone"));
	if(!overlay || !left || !right)
	{
		finished(-1, QStringLiteral("could not find the overlay or the zones"));
		return;
	}

	auto *mime = new QMimeData;
	mime->setUrls({ QUrl::fromLocalFile(QStringLiteral("/tmp/orbislink-selftest.pkg")) });

	const qreal w = window->width();
	const qreal h = window->height();
	auto *steps = new QList<Passo> {
		{ QPointF(w * 0.50, h * 0.08), QStringLiteral("enter from the top of the window") },
		{ QPointF(w * 0.20, h * 0.50), QStringLiteral("point at the left zone") },
		{ QPointF(w * 0.25, h * 0.60), QStringLiteral("move inside the left zone") },
		{ QPointF(w * 0.50, h * 0.50), QStringLiteral("cross to the middle") },
		{ QPointF(w * 0.75, h * 0.50), QStringLiteral("point at the right zone") },
		{ QPointF(w * 0.80, h * 0.60), QStringLiteral("move inside the right zone") },
		{ QPointF(w * 0.20, h * 0.50), QStringLiteral("back to the left zone") },
	};

	auto *idx = new int(0);
	auto *errors = new int(0);
	auto *report = new QStringList;
	auto *timer = new QTimer(window);
	timer->setInterval(60);

	{
		QDragEnterEvent input(steps->at(0).dot.toPoint(), Qt::CopyAction, mime,
			Qt::LeftButton, Qt::NoModifier);
		QGuiApplication::sendEvent(window, &input);
	}

	QObject::connect(timer, &QTimer::timeout, window,
		[window, overlay, left, right, mime, steps, idx, errors, report, timer,
			finished]() {
			const Passo &step = steps->at(qMax(0, *idx - 1));
			const bool isOpen = overlay->property("active").toBool();

			// Which zone is under the cursor, measured on the real geometry,
			// and which one the interface is lighting up.
			const int expected = itemContains(left, step.dot) ? 0
				: (itemContains(right, step.dot) ? 1 : -1);
			// "highlighted" is what the zone knows about the cursor; "active"
			// is that plus the service being available. What is tested here is
			// the first: the interface must follow the cursor even when the
			// service is down (then the zone lights up but refuses).
			const int lit = left->property("highlighted").toBool() ? 0
				: (right->property("highlighted").toBool() ? 1 : -1);

			auto name = [](int zoneName) {
				return zoneName == 0 ? "left" : (zoneName == 1 ? "right" : "none");
			};
			qInfo("  t=%02d  overlay=%-7s  under cursor=%-8s  lit=%-8s  (%s)", *idx,
				isOpen ? "open" : "CLOSED", name(expected), name(lit),
				qPrintable(step.description));

			if(!isOpen)
			{
				++(*errors);
				report->append(QStringLiteral("closed halfway, after \"%1\"").arg(step.description));
			}
			else if(expected != lit)
			{
				++(*errors);
				report->append(QStringLiteral("at \"%1\" the cursor was on zone %2 but %3 lit up")
						.arg(step.description, QString::fromLatin1(name(expected)),
							QString::fromLatin1(name(lit))));
			}

			if(*idx >= steps->size())
			{
				timer->stop();
				// Dropping on the left zone must be accepted.
				QDropEvent drop(steps->last().dot, Qt::CopyAction, mime, Qt::LeftButton,
					Qt::NoModifier);
				QGuiApplication::sendEvent(window, &drop);
				if(!drop.isAccepted())
				{
					++(*errors);
					report->append(QStringLiteral("dropping on the left zone was not accepted"));
				}

				const int total = *errors;
				const QString message = report->isEmpty()
					? QStringLiteral("the overlay followed the cursor and accepted the file")
					: report->join(QStringLiteral("; "));
				delete mime;
				delete steps;
				delete idx;
				delete errors;
				delete report;
				timer->deleteLater();
				finished(total, message);
				return;
			}

			QDragMoveEvent moveEvent(steps->at(*idx).dot.toPoint(), Qt::CopyAction, mime,
				Qt::LeftButton, Qt::NoModifier);
			QGuiApplication::sendEvent(window, &moveEvent);
			++(*idx);
		});
	timer->start();
}

} // namespace orbislink::dragselftest
