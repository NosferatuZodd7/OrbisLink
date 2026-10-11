// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/notifier.h"

#include "orbislink/common/log.h"

#include <QGuiApplication>
#include <QWindow>

#ifdef ORBISLINK_HAS_DBUS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariantMap>
#endif
#ifdef Q_OS_MACOS
#include <QProcess>
#endif

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <cstring>
#endif

namespace orbislink {

namespace {

#ifdef _WIN32

// A single icon in the notification area, created on the first message and
// removed at the end. The fixed GUID stops Windows from creating a new icon
// on every start.
NOTIFYICONDATAW &iconData()
{
	static NOTIFYICONDATAW data {};
	return data;
}

bool createIcon()
{
	NOTIFYICONDATAW &data = iconData();
	data.cbSize = sizeof(NOTIFYICONDATAW);
	// The owner window: any will do, as long as it exists while the icon
	// exists. The application's window is used.
	const QWindowList windowList = QGuiApplication::allWindows();
	if(windowList.isEmpty())
		return false;
	data.hWnd = reinterpret_cast<HWND>(windowList.first()->winId());
	if(!data.hWnd)
		return false;
	data.uID = 1;
	data.uFlags = NIF_ICON | NIF_TIP;
	data.hIcon = static_cast<HICON>(
		LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
			LR_DEFAULTSIZE | LR_SHARED));
	if(!data.hIcon)
		data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	wcscpy_s(data.szTip, L"OrbisLink");
	return Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

#endif // _WIN32

} // namespace

Notifier::Notifier(QObject *parent) : QObject(parent) {}

Notifier::~Notifier()
{
#ifdef _WIN32
	if(registered_)
		Shell_NotifyIconW(NIM_DELETE, &iconData());
#endif
}

bool Notifier::available() const
{
#if defined(_WIN32) || defined(Q_OS_MACOS)
	return true;
#elif defined(ORBISLINK_HAS_DBUS)
	QDBusConnection bus = QDBusConnection::sessionBus();
	return bus.isConnected() && bus.interface()
		&& bus.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.Notifications"));
#else
	return false;
#endif
}

void Notifier::show(const QString &title, const QString &message, bool error)
{
	if(!enabled_)
		return;

#ifdef _WIN32
	if(!registered_)
		registered_ = createIcon();
	if(registered_)
	{
		NOTIFYICONDATAW &data = iconData();
		data.uFlags = NIF_INFO;
		data.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
		const QString header = title.isEmpty() ? QStringLiteral("OrbisLink") : title;
		wcsncpy_s(data.szInfoTitle, reinterpret_cast<const wchar_t *>(header.utf16()),
			_TRUNCATE);
		wcsncpy_s(data.szInfo, reinterpret_cast<const wchar_t *>(message.utf16()), _TRUNCATE);
		Shell_NotifyIconW(NIM_MODIFY, &data);
		return;
	}
#endif

#ifdef ORBISLINK_HAS_DBUS
	if(available())
	{
		QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
			QStringLiteral("/org/freedesktop/Notifications"), QStringLiteral("org.freedesktop.Notifications"),
			QStringLiteral("Notify"));
		QVariantMap hints;
		// 1 normal, 2 critical: a failure stays until it is seen.
		hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(error ? 2 : 1));
		call << QStringLiteral("OrbisLink") << 0u << QString()
			 << (title.isEmpty() ? QStringLiteral("OrbisLink") : title) << message << QStringList() << hints << -1;
		QDBusConnection::sessionBus().send(call);
		return;
	}
#endif

#ifdef Q_OS_MACOS
	{
		auto quoted = [](QString text) {
			text.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
			return QLatin1Char('"') + text + QLatin1Char('"');
		};
		const QString header = title.isEmpty() ? QStringLiteral("OrbisLink") : title;
		QProcess::startDetached(QStringLiteral("osascript"),
			{ QStringLiteral("-e"),
				QStringLiteral("display notification %1 with title %2").arg(quoted(message), quoted(header)) });
		return;
	}
#endif

	// No system notification: the window flashes in the taskbar, which at
	// least draws attention. Only for errors — flashing on every piece of
	// information would be unbearable.
	if(error && window_ && !window_->isActive())
		window_->alert(0);
}

} // namespace orbislink
