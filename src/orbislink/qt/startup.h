// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QString>

namespace orbislink {

// Interface startup: diagnostics and recovery.
//
// On machines without graphics acceleration (Windows Sandbox, virtual
// machines, remote sessions) Qt Quick cannot create the rendering context and
// the application dies without saying anything. These functions deal with
// that: they write everything to a log file, mark the startup and, if the
// previous startup never got to draw, switch to software rendering
// by themselves.
namespace startup {

// Encaminha as mensagens do Qt para <dados da app>/orbislink-gui.log.
void installFileLogger();

// Caminho do registo, para o mostrar ao utilizador quando algo corre mal.
QString logPath();

// true if the previous startup never drew a single frame.
bool previousLaunchFailed();

// Marks "starting up" (a file that is only deleted once the window draws).
void markLaunchStarted();

// Chamar quando a janela desenhar o primeiro fotograma.
void markLaunchSucceeded();

// true if Qt has already complained it could not create the window.
bool windowCreationFailed();

// Shows the error to the user. On Windows it is a dialog box, because the
// application has no console and the message would be lost.
void reportFatal(const QString &title, const QString &message);

} // namespace startup
} // namespace orbislink
