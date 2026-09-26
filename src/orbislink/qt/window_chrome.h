// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace orbislink {

// A barra de título é do sistema, não nossa — e no Windows vem branca por
// cima de uma aplicação escura. Isto pede ao gestor de janelas que a
// pinte como o resto.
//
// Não há uma maneira só que funcione em todo o lado, por isso tenta-se a
// melhor primeiro e desce-se até uma que o sistema aceite:
//
//   1. material translúcido do sistema (acrílico ou mica) — Windows 11 22H2
//   2. cor sólida do tema na barra e na moldura — Windows 11
//   3. barra escura, sem escolher a cor — Windows 10 1809
//   4. nada, e a janela fica como o sistema a desenha
//
// O que ficou é dito no registo e no diagnóstico: uma interface que promete
// vidro e entrega branco é pior do que uma que diz o que conseguiu.
class WindowChrome : public QObject
{
	Q_OBJECT

public:
	explicit WindowChrome(QWindow *window, QObject *parent = nullptr);

	// Chamado sempre que o tema muda. `translucent` pede o material do
	// sistema; quando ele não existe, cai para a cor sólida.
	Q_INVOKABLE void applyTheme(const QColor &caption, const QColor &text, const QColor &border,
		bool dark, bool translucent);

	// Uma frase sobre o que o sistema aceitou, para o diagnóstico.
	static QString summary();

	// Verdadeiro quando o processo corre com privilégios de administrador.
	//
	// Não é curiosidade: o Windows não deixa arrastar ficheiros de uma
	// janela sem elevação (o Explorador) para uma janela elevada. Bloqueia
	// as mensagens e não diz nada — o arrastar e largar simplesmente deixa
	// de funcionar, sem erro nenhum a que se possa agarrar.
	static bool runningElevated();
	// Explicação em português do que isso implica, ou vazio quando não há
	// nada a dizer.
	static QString elevationWarning();

private:
	QWindow *window_ = nullptr;
};

} // namespace orbislink
