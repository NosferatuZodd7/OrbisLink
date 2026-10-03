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

// One position of the drag. Positions inside a zone are given as a fraction
// of that zone and worked out only when the step is taken: the overlay is laid
// out when it opens, and the zones may be anywhere in it.
enum class Target { Window, Install, Ftp, Between };

struct Step
{
	Target target;
	QPointF fraction;
	QString description;
	QPointF dot;
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
	auto *installItem = window->findChild<QQuickItem *>(QStringLiteral("installZone"));
	auto *ftpItem = window->findChild<QQuickItem *>(QStringLiteral("ftpZone"));
	if(!overlay || !installItem || !ftpItem)
	{
		finished(-1, QStringLiteral("could not find the overlay or the zones"));
		return;
	}

	auto *mime = new QMimeData;
	mime->setUrls({ QUrl::fromLocalFile(QStringLiteral("/tmp/orbislink-selftest.pkg")) });

	auto *steps = new QList<Step> {
		{ Target::Window, QPointF(0.50, 0.02), QStringLiteral("enter from the top of the window"), {} },
		{ Target::Install, QPointF(0.50, 0.50), QStringLiteral("point at the install zone"), {} },
		{ Target::Install, QPointF(0.30, 0.70), QStringLiteral("move inside the install zone"), {} },
		{ Target::Between, QPointF(), QStringLiteral("cross the gap between them"), {} },
		{ Target::Ftp, QPointF(0.50, 0.50), QStringLiteral("point at the FTP zone"), {} },
		{ Target::Ftp, QPointF(0.70, 0.30), QStringLiteral("move inside the FTP zone"), {} },
		{ Target::Install, QPointF(0.50, 0.50), QStringLiteral("back to the install zone"), {} },
	};
	// Where a step lands in the window, from the zones' real geometry.
	auto place = [window, installItem, ftpItem](Step &step) {
		auto inside = [](QQuickItem *item, const QPointF &fraction) {
			return item->mapToScene(QPointF(item->width() * fraction.x(),
				item->height() * fraction.y()));
		};
		switch(step.target)
		{
			case Target::Window:
				step.dot = QPointF(window->width() * step.fraction.x(),
					window->height() * step.fraction.y());
				break;
			case Target::Install: step.dot = inside(installItem, step.fraction); break;
			case Target::Ftp: step.dot = inside(ftpItem, step.fraction); break;
			case Target::Between:
			{
				const QPointF a = inside(installItem, QPointF(0.5, 0.5));
				const QPointF b = inside(ftpItem, QPointF(0.5, 0.5));
				// The middle of the gap: halfway between the facing edges.
				const bool installFirst = a.x() < b.x();
				QQuickItem *first = installFirst ? installItem : ftpItem;
				QQuickItem *second = installFirst ? ftpItem : installItem;
				const qreal edge1 = first->mapToScene(QPointF(first->width(), 0)).x();
				const qreal edge2 = second->mapToScene(QPointF(0, 0)).x();
				step.dot = QPointF((edge1 + edge2) / 2, (a.y() + b.y()) / 2);
				break;
			}
		}
	};
	place((*steps)[0]);

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
		[window, overlay, installItem, ftpItem, mime, steps, idx, errors, report, timer,
			finished, place]() {
			const Step &step = steps->at(qMax(0, *idx - 1));
			const bool isOpen = overlay->property("active").toBool();

			// Which zone is under the cursor, measured on the real geometry,
			// and which one the interface is lighting up.
			const int expected = itemContains(installItem, step.dot) ? 0
				: (itemContains(ftpItem, step.dot) ? 1 : -1);
			// "highlighted" is what the zone knows about the cursor; "active"
			// is that plus the service being available. What is tested here is
			// the first: the interface must follow the cursor even when the
			// service is down (then the zone lights up but refuses).
			const int lit = installItem->property("highlighted").toBool() ? 0
				: (ftpItem->property("highlighted").toBool() ? 1 : -1);

			auto name = [](int zoneName) {
				return zoneName == 0 ? "install" : (zoneName == 1 ? "ftp" : "none");
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
				// Dropping on the install zone must be accepted.
				QDropEvent drop(steps->last().dot, Qt::CopyAction, mime, Qt::LeftButton,
					Qt::NoModifier);
				QGuiApplication::sendEvent(window, &drop);
				if(!drop.isAccepted())
				{
					++(*errors);
					report->append(QStringLiteral("dropping on the install zone was not accepted"));
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

			place((*steps)[*idx]);
			QDragMoveEvent moveEvent(steps->at(*idx).dot.toPoint(), Qt::CopyAction, mime,
				Qt::LeftButton, Qt::NoModifier);
			QGuiApplication::sendEvent(window, &moveEvent);
			++(*idx);
		});
	timer->start();
}

} // namespace orbislink::dragselftest
