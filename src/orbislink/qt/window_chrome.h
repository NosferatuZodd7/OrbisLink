// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace orbislink {

// The title bar belongs to the system, not to us — and on Windows it comes
// white on top of a dark application. This asks the window manager to
// paint it like the rest.
//
// There is no single way that works everywhere, so the best is tried
// first, stepping down until one the system accepts:
//
//   1. the system's translucent material (acrylic or mica) — Windows 11 22H2
//   2. the theme's solid colour on the bar and frame — Windows 11
//   3. a dark bar, without choosing the colour — Windows 10 1809
//   4. nothing, and the window stays as the system draws it
//
// What stuck is stated in the log and in the diagnostics: an interface that
// promises glass and delivers white is worse than one that says what it managed.
class WindowChrome : public QObject
{
	Q_OBJECT

public:
	explicit WindowChrome(QWindow *window, QObject *parent = nullptr);

	// Called whenever the theme changes. `translucent` asks for the system
	// material; when it does not exist, it falls back to the solid colour.
	Q_INVOKABLE void applyTheme(const QColor &caption, const QColor &text, const QColor &border,
		bool dark, bool translucent);

	// A sentence about what the system accepted, for the diagnostics.
	static QString summary();

	// True when the process runs with administrator privileges.
	//
	// This is not idle curiosity: Windows does not allow dragging files from a
	// non-elevated window (Explorer) onto an elevated one. It blocks the
	// messages and says nothing — drag and drop simply stops working,
	// with no error to hold on to.
	static bool runningElevated();
	// Explanation of what that implies, or empty when there is
	// nothing to say.
	static QString elevationWarning();

private:
	QWindow *window_ = nullptr;
};

} // namespace orbislink
