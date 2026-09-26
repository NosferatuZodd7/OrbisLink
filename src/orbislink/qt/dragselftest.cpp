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
	QPointF ponto;
	QString descricao;
};

bool contem(QQuickItem *item, const QPointF &pontoDaJanela)
{
	const QPointF local = item->mapFromScene(pontoDaJanela);
	return local.x() >= 0 && local.y() >= 0 && local.x() <= item->width()
		&& local.y() <= item->height();
}

} // namespace

void run(QQuickWindow *window, std::function<void(int, const QString &)> finished)
{
	auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("dropOverlay"));
	auto *esquerda = window->findChild<QQuickItem *>(QStringLiteral("installZone"));
	auto *direita = window->findChild<QQuickItem *>(QStringLiteral("ftpZone"));
	if(!overlay || !esquerda || !direita)
	{
		finished(-1, QStringLiteral("could not find the overlay or the zones"));
		return;
	}

	auto *mime = new QMimeData;
	mime->setUrls({ QUrl::fromLocalFile(QStringLiteral("/tmp/orbislink-selftest.pkg")) });

	const qreal w = window->width();
	const qreal h = window->height();
	auto *passos = new QList<Passo> {
		{ QPointF(w * 0.50, h * 0.08), QStringLiteral("enter from the top of the window") },
		{ QPointF(w * 0.20, h * 0.50), QStringLiteral("point at the left zone") },
		{ QPointF(w * 0.25, h * 0.60), QStringLiteral("move inside the left zone") },
		{ QPointF(w * 0.50, h * 0.50), QStringLiteral("cross to the middle") },
		{ QPointF(w * 0.75, h * 0.50), QStringLiteral("point at the right zone") },
		{ QPointF(w * 0.80, h * 0.60), QStringLiteral("move inside the right zone") },
		{ QPointF(w * 0.20, h * 0.50), QStringLiteral("back to the left zone") },
	};

	auto *indice = new int(0);
	auto *erros = new int(0);
	auto *relato = new QStringList;
	auto *timer = new QTimer(window);
	timer->setInterval(60);

	{
		QDragEnterEvent entrada(passos->at(0).ponto.toPoint(), Qt::CopyAction, mime,
			Qt::LeftButton, Qt::NoModifier);
		QGuiApplication::sendEvent(window, &entrada);
	}

	QObject::connect(timer, &QTimer::timeout, window,
		[window, overlay, esquerda, direita, mime, passos, indice, erros, relato, timer,
			finished]() {
			const Passo &passo = passos->at(qMax(0, *indice - 1));
			const bool aberta = overlay->property("active").toBool();

			// Which zone is under the cursor, measured on the real geometry,
			// and which one the interface is lighting up.
			const int esperada = contem(esquerda, passo.ponto) ? 0
				: (contem(direita, passo.ponto) ? 1 : -1);
			// "highlighted" is what the zone knows about the cursor; "active"
			// is that plus the service being available. What is tested here is
			// the first: the interface must follow the cursor even when the
			// service is down (then the zone lights up but refuses).
			const int acesa = esquerda->property("highlighted").toBool() ? 0
				: (direita->property("highlighted").toBool() ? 1 : -1);

			auto nome = [](int zona) {
				return zona == 0 ? "left" : (zona == 1 ? "right" : "none");
			};
			qInfo("  t=%02d  overlay=%-7s  under cursor=%-8s  lit=%-8s  (%s)", *indice,
				aberta ? "open" : "CLOSED", nome(esperada), nome(acesa),
				qPrintable(passo.descricao));

			if(!aberta)
			{
				++(*erros);
				relato->append(QStringLiteral("closed halfway, after \"%1\"").arg(passo.descricao));
			}
			else if(esperada != acesa)
			{
				++(*erros);
				relato->append(QStringLiteral("at \"%1\" the cursor was on zone %2 but %3 lit up")
						.arg(passo.descricao, QString::fromLatin1(nome(esperada)),
							QString::fromLatin1(nome(acesa))));
			}

			if(*indice >= passos->size())
			{
				timer->stop();
				// Dropping on the left zone must be accepted.
				QDropEvent largar(passos->last().ponto, Qt::CopyAction, mime, Qt::LeftButton,
					Qt::NoModifier);
				QGuiApplication::sendEvent(window, &largar);
				if(!largar.isAccepted())
				{
					++(*erros);
					relato->append(QStringLiteral("dropping on the left zone was not accepted"));
				}

				const int total = *erros;
				const QString texto = relato->isEmpty()
					? QStringLiteral("the overlay followed the cursor and accepted the file")
					: relato->join(QStringLiteral("; "));
				delete mime;
				delete passos;
				delete indice;
				delete erros;
				delete relato;
				timer->deleteLater();
				finished(total, texto);
				return;
			}

			QDragMoveEvent mover(passos->at(*indice).ponto.toPoint(), Qt::CopyAction, mime,
				Qt::LeftButton, Qt::NoModifier);
			QGuiApplication::sendEvent(window, &mover);
			++(*indice);
		});
	timer->start();
}

} // namespace orbislink::dragselftest
