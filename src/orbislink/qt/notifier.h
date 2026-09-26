// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace orbislink {

// System notifications: the install finished, the queue stopped, registration
// failed — things that matter even with the window minimised.
//
// QSystemTrayIcon is deliberately not used. It lives in QtWidgets and needs
// a QApplication instead of the QGuiApplication this application uses;
// switching would bring in a whole Qt module and a different startup with no
// gain to justify it. Instead:
//
//   Windows — Shell_NotifyIconW, the long-standing notification area API.
//   Others — the window flashes in the taskbar (QWindow::alert), which is
//   what Qt offers without new dependencies.
//
// Inside the window the usual notice still appears; this is an extra.
class Notifier : public QObject
{
	Q_OBJECT

public:
	explicit Notifier(QObject *parent = nullptr);
	~Notifier() override;

	// The window flashes when there is no system notification.
	void setWindow(QWindow *window) { window_ = window; }

	// True when there are real notifications (not just the flashing).
	bool available() const;

	void show(const QString &title, const QString &message, bool error);
	void setEnabled(bool enabled) { enabled_ = enabled; }

private:
	QWindow *window_ = nullptr;
	bool enabled_ = true;
	bool registered_ = false;
};

} // namespace orbislink
