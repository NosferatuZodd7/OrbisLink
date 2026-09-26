// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <functional>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace orbislink::dragselftest {

// Arrasta um ficheiro falso por cima da janela com eventos sintéticos e
// verifica que a sobreposição não pisca.
//
// Guarda contra um erro concreto: com um DropArea a cobrir a janela e
// mais um por cada zona ("instalar" e "enviar por FTP"), ao entrar numa
// zona o da janela diz "saiu", a sobreposição fecha, o cursor volta a
// estar sobre o da janela, que diz "entrou" — e assim em ciclo, e seria
// preciso largar duas vezes para acertar na opção.
//
// O teste percorre: entrar na janela → entrar na zona da esquerda → mexer
// lá dentro → atravessar para a da direita → mexer → largar. Durante todo
// esse percurso a sobreposição tem de estar aberta sem interrupção.
// Chama `finished` com o número de vezes que ela se fechou a meio: 0 passa.
void run(QQuickWindow *window, std::function<void(int fechouAMeio, const QString &relato)> finished);

} // namespace orbislink::dragselftest
