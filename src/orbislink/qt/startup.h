// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QString>

namespace orbislink {

// Arranque da interface: diagnóstico e recuperação.
//
// Em máquinas sem aceleração gráfica (Windows Sandbox, máquinas virtuais,
// sessões remotas) o Qt Quick não consegue criar o contexto de desenho e a
// aplicação morre sem dizer nada. Estas funções tratam disso: escrevem
// tudo num ficheiro de registo, marcam o arranque e, se o arranque
// anterior não chegou a desenhar, passam sozinhas para desenho por
// software.
namespace startup {

// Encaminha as mensagens do Qt para <dados da app>/orbislink-gui.log.
void installFileLogger();

// Caminho do registo, para o mostrar ao utilizador quando algo corre mal.
QString logPath();

// true se o arranque anterior não chegou a desenhar um único fotograma.
bool previousLaunchFailed();

// Marca "estou a arrancar" (ficheiro que só é apagado quando a janela desenha).
void markLaunchStarted();

// Chamar quando a janela desenhar o primeiro fotograma.
void markLaunchSucceeded();

// true se o Qt já se queixou de não conseguir criar a janela.
bool windowCreationFailed();

// Mostra o erro ao utilizador. Em Windows é uma caixa de diálogo, porque a
// aplicação não tem consola e a mensagem perder-se-ia.
void reportFatal(const QString &title, const QString &message);

} // namespace startup
} // namespace orbislink
