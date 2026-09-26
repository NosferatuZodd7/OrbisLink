// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/window_chrome.h"

#include "orbislink/common/log.h"

#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace orbislink {

namespace {

QString g_resumo = QStringLiteral("the title bar is the system's (nothing to adjust on this "
								  "platform)");

#ifdef Q_OS_WIN

// The numbers come from Microsoft's dwmapi.h. They are here by hand on
// purpose: old headers do not have them all, and the build should not
// depend on the SDK version installed on the runner.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kUseImmersiveDarkModeAntigo = 19; // Windows 10 1809 a 1903
constexpr DWORD kBorderColor = 34;
constexpr DWORD kCaptionColor = 35;
constexpr DWORD kTextColor = 36;
constexpr DWORD kSystemBackdropType = 38;

// DWMSBT_*: 2 is mica (main window), 3 is acrylic (transient window).
constexpr int kBackdropMica = 2;
constexpr int kBackdropAcrylic = 3;

constexpr DWORD kColorDefault = 0xFFFFFFFF;

using SetAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

SetAttributeFn resolveSetAttribute()
{
	// Loaded by hand so the application still starts on a Windows without
	// composition, instead of dying at startup for lack of a symbol.
	static SetAttributeFn fn = []() -> SetAttributeFn {
		HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
		if(!dwm)
			return nullptr;
		return reinterpret_cast<SetAttributeFn>(
			reinterpret_cast<void *>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
	}();
	return fn;
}

// Returns true when the system accepted the attribute. An E_INVALIDARG here
// is no error at all: it is this Windows version saying it does not know
// the feature, and that is how we find out how far we can go without a
// table of build numbers to maintain.
bool trySet(HWND hwnd, DWORD atributo, const void *valor, DWORD tamanho)
{
	SetAttributeFn fn = resolveSetAttribute();
	if(!fn)
		return false;
	return SUCCEEDED(fn(hwnd, atributo, valor, tamanho));
}

DWORD toColorRef(const QColor &cor)
{
	// Windows wants 0x00BBGGRR, unlike everyone else.
	return static_cast<DWORD>(cor.red()) | (static_cast<DWORD>(cor.green()) << 8)
		| (static_cast<DWORD>(cor.blue()) << 16);
}

#endif // Q_OS_WIN

} // namespace

WindowChrome::WindowChrome(QWindow *window, QObject *parent) : QObject(parent), window_(window) {}

QString WindowChrome::summary() { return g_resumo; }

bool WindowChrome::runningElevated()
{
#ifdef Q_OS_WIN
	// Computed once: the process token does not change during its life.
	static const bool elevado = []() {
		HANDLE token = nullptr;
		if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;
		TOKEN_ELEVATION elevacao {};
		DWORD tamanho = 0;
		const bool ok = GetTokenInformation(token, TokenElevation, &elevacao,
							sizeof(elevacao), &tamanho)
			&& elevacao.TokenIsElevated != 0;
		CloseHandle(token);
		return ok;
	}();
	return elevado;
#else
	// On other systems running as root does not prevent dragging files, and
	// so there is nothing to warn about here.
	return false;
#endif
}

QString WindowChrome::elevationWarning()
{
	if(!runningElevated())
		return {};
	return QObject::tr("OrbisLink is running as administrator. Windows does not allow dragging "
		"files from Explorer into an elevated window, so drag and drop will not "
		"work. Close it and open it from the normal Start menu shortcut.");
}

void WindowChrome::applyTheme(const QColor &caption, const QColor &text, const QColor &border,
	bool dark, bool translucent)
{
#ifdef Q_OS_WIN
	if(!window_)
		return;
	HWND hwnd = reinterpret_cast<HWND>(window_->winId());
	if(!hwnd)
		return;

	// The dark bar is the base of everything else: even when the colour is
	// chosen, this is what decides the colour of the minimise and close buttons.
	const BOOL escuro = dark ? TRUE : FALSE;
	if(!trySet(hwnd, kUseImmersiveDarkMode, &escuro, sizeof(escuro)))
		trySet(hwnd, kUseImmersiveDarkModeAntigo, &escuro, sizeof(escuro));

	QString conseguido;

	// 1. The system material. It is the only one giving real translucency:
	//    Windows blurs what is behind the window, something we cannot do
	//    from here.
	bool comMaterial = false;
	if(translucent)
	{
		const int material = kBackdropAcrylic;
		comMaterial = trySet(hwnd, kSystemBackdropType, &material, sizeof(material));
		if(comMaterial)
			conseguido = QStringLiteral("system acrylic (what is behind the window shows "
										"blurred)");
	}
	if(!comMaterial)
	{
		const int material = kBackdropMica;
		comMaterial = trySet(hwnd, kSystemBackdropType, &material, sizeof(material));
		if(comMaterial)
			conseguido = QStringLiteral("system mica (the bar takes its colour from the "
										"desktop wallpaper)");
	}

	if(comMaterial)
	{
		// With a material, choosing the bar colour ruins it: DWM starts
		// painting it solid and the effect disappears.
		const DWORD porOmissao = kColorDefault;
		trySet(hwnd, kCaptionColor, &porOmissao, sizeof(porOmissao));
		trySet(hwnd, kTextColor, &porOmissao, sizeof(porOmissao));
	}
	else
	{
		// 2. Without a material, paint the bar in the theme colour. It is not
		//    glass, but there is no longer a white strip on top of everything.
		const DWORD corBarra = toColorRef(caption);
		if(trySet(hwnd, kCaptionColor, &corBarra, sizeof(corBarra)))
		{
			const DWORD corTexto = toColorRef(text);
			trySet(hwnd, kTextColor, &corTexto, sizeof(corTexto));
			conseguido = QStringLiteral("bar painted in the theme colour (%1)")
				.arg(caption.name());
		}
		else
		{
			// 3. What is left is dark mode, already requested above.
			conseguido = dark ? QStringLiteral("dark system bar, colour not chosen")
							  : QStringLiteral("light system bar, colour not chosen");
		}
	}

	// The window frame follows, when the system allows.
	const DWORD corMoldura = toColorRef(border);
	trySet(hwnd, kBorderColor, &corMoldura, sizeof(corMoldura));

	// Without this, the bar only changes when the window is redrawn for
	// some other reason — and switching theme would have no visible effect.
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	if(conseguido != g_resumo)
	{
		g_resumo = conseguido;
		logInfo("Janela: " + conseguido.toStdString() + ".");
	}
#else
	// Outside Windows the decoration belongs to the window manager and there
	// is nothing to ask it here. The parameters are marked used so the compiler does not warn.
	Q_UNUSED(caption)
	Q_UNUSED(text)
	Q_UNUSED(border)
	Q_UNUSED(dark)
	Q_UNUSED(translucent)
#endif
}

} // namespace orbislink
