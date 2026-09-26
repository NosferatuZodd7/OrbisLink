// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/notifier.h"

#include "orbislink/common/log.h"

#include <QGuiApplication>
#include <QWindow>

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
#ifdef _WIN32
	return true;
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

	// No system notification: the window flashes in the taskbar, which at
	// least draws attention. Only for errors — flashing on every piece of
	// information would be unbearable.
	if(error && window_ && !window_->isActive())
		window_->alert(0);
}

} // namespace orbislink
