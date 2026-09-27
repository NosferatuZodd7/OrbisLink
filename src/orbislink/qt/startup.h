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

// Forwards Qt's messages to <app data>/orbislink-gui.log.
void installFileLogger();

// Log path, to show the user when something goes wrong.
QString logPath();

// true if the previous startup never drew a single frame.
bool previousLaunchFailed();

// Marks "starting up" (a file that is only deleted once the window draws).
void markLaunchStarted();

// Call when the window draws the first frame.
void markLaunchSucceeded();

// true if Qt has already complained it could not create the window.
bool windowCreationFailed();

// Shows the error to the user. On Windows it is a dialog box, because the
// application has no console and the message would be lost.
void reportFatal(const QString &title, const QString &message);

} // namespace startup
} // namespace orbislink
