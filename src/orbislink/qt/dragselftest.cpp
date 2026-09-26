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
		finished(-1, QStringLiteral("não encontrei a sobreposição ou as zonas"));
		return;
	}

	auto *mime = new QMimeData;
	mime->setUrls({ QUrl::fromLocalFile(QStringLiteral("/tmp/orbislink-selftest.pkg")) });

	const qreal w = window->width();
	const qreal h = window->height();
	auto *passos = new QList<Passo> {
		{ QPointF(w * 0.50, h * 0.08), QStringLiteral("entrar pelo cimo da janela") },
		{ QPointF(w * 0.20, h * 0.50), QStringLiteral("apontar a zona da esquerda") },
		{ QPointF(w * 0.25, h * 0.60), QStringLiteral("mexer dentro da esquerda") },
		{ QPointF(w * 0.50, h * 0.50), QStringLiteral("atravessar para o meio") },
		{ QPointF(w * 0.75, h * 0.50), QStringLiteral("apontar a zona da direita") },
		{ QPointF(w * 0.80, h * 0.60), QStringLiteral("mexer dentro da direita") },
		{ QPointF(w * 0.20, h * 0.50), QStringLiteral("voltar à da esquerda") },
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

			// Qual é a zona por baixo do cursor, medida na geometria real, e
			// qual é a que a interface está a acender.
			const int esperada = contem(esquerda, passo.ponto) ? 0
				: (contem(direita, passo.ponto) ? 1 : -1);
			// "highlighted" é o que a zona sabe do cursor; "active" é isso
			// mais o serviço estar disponível. O que se testa aqui é o
			// primeiro: a interface tem de acompanhar o cursor mesmo quando
			// o serviço está em baixo (aí a zona fica acesa mas recusada).
			const int acesa = esquerda->property("highlighted").toBool() ? 0
				: (direita->property("highlighted").toBool() ? 1 : -1);

			auto nome = [](int zona) {
				return zona == 0 ? "esquerda" : (zona == 1 ? "direita" : "nenhuma");
			};
			qInfo("  t=%02d  sobreposição=%-7s  sob o cursor=%-8s  acesa=%-8s  (%s)", *indice,
				aberta ? "aberta" : "FECHADA", nome(esperada), nome(acesa),
				qPrintable(passo.descricao));

			if(!aberta)
			{
				++(*erros);
				relato->append(QStringLiteral("fechou a meio, depois de \"%1\"").arg(passo.descricao));
			}
			else if(esperada != acesa)
			{
				++(*erros);
				relato->append(QStringLiteral("em \"%1\" o cursor estava na zona %2 mas acendeu %3")
						.arg(passo.descricao, QString::fromLatin1(nome(esperada)),
							QString::fromLatin1(nome(acesa))));
			}

			if(*indice >= passos->size())
			{
				timer->stop();
				// Largar na zona da esquerda tem de ser aceite.
				QDropEvent largar(passos->last().ponto, Qt::CopyAction, mime, Qt::LeftButton,
					Qt::NoModifier);
				QGuiApplication::sendEvent(window, &largar);
				if(!largar.isAccepted())
				{
					++(*erros);
					relato->append(QStringLiteral("largar sobre a zona da esquerda não foi aceite"));
				}

				const int total = *erros;
				const QString texto = relato->isEmpty()
					? QStringLiteral("a sobreposição acompanhou o cursor e aceitou o ficheiro")
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
