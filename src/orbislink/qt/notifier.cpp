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
	static NOTIFYICONDATAW dados {};
	return dados;
}

bool criarIcone()
{
	NOTIFYICONDATAW &dados = iconData();
	dados.cbSize = sizeof(NOTIFYICONDATAW);
	// The owner window: any will do, as long as it exists while the icon
	// exists. The application's window is used.
	const QWindowList janelas = QGuiApplication::allWindows();
	if(janelas.isEmpty())
		return false;
	dados.hWnd = reinterpret_cast<HWND>(janelas.first()->winId());
	if(!dados.hWnd)
		return false;
	dados.uID = 1;
	dados.uFlags = NIF_ICON | NIF_TIP;
	dados.hIcon = static_cast<HICON>(
		LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
			LR_DEFAULTSIZE | LR_SHARED));
	if(!dados.hIcon)
		dados.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	wcscpy_s(dados.szTip, L"OrbisLink");
	return Shell_NotifyIconW(NIM_ADD, &dados) != FALSE;
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
		registered_ = criarIcone();
	if(registered_)
	{
		NOTIFYICONDATAW &dados = iconData();
		dados.uFlags = NIF_INFO;
		dados.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
		const QString cabecalho = title.isEmpty() ? QStringLiteral("OrbisLink") : title;
		wcsncpy_s(dados.szInfoTitle, reinterpret_cast<const wchar_t *>(cabecalho.utf16()),
			_TRUNCATE);
		wcsncpy_s(dados.szInfo, reinterpret_cast<const wchar_t *>(message.utf16()), _TRUNCATE);
		Shell_NotifyIconW(NIM_MODIFY, &dados);
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
