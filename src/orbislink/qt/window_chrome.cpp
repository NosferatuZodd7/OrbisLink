// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/window_chrome.h"

#include "orbislink/common/log.h"

#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace orbislink {

namespace {

QString g_summary = QStringLiteral("the title bar is the system's (nothing to adjust on this "
								  "platform)");

#ifdef Q_OS_WIN

// The numbers come from Microsoft's dwmapi.h. They are here by hand on
// purpose: old headers do not have them all, and the build should not
// depend on the SDK version installed on the runner.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kUseImmersiveDarkModeLegacy = 19; // Windows 10 1809 a 1903
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
bool trySet(HWND hwnd, DWORD attribute, const void *value, DWORD size)
{
	SetAttributeFn fn = resolveSetAttribute();
	if(!fn)
		return false;
	return SUCCEEDED(fn(hwnd, attribute, value, size));
}

DWORD toColorRef(const QColor &tone)
{
	// Windows wants 0x00BBGGRR, unlike everyone else.
	return static_cast<DWORD>(tone.red()) | (static_cast<DWORD>(tone.green()) << 8)
		| (static_cast<DWORD>(tone.blue()) << 16);
}

#endif // Q_OS_WIN

} // namespace

WindowChrome::WindowChrome(QWindow *window, QObject *parent) : QObject(parent), window_(window) {}

QString WindowChrome::summary() { return g_summary; }

bool WindowChrome::runningElevated()
{
#ifdef Q_OS_WIN
	// Computed once: the process token does not change during its life.
	static const bool elevated = []() {
		HANDLE token = nullptr;
		if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;
		TOKEN_ELEVATION elevation {};
		DWORD size = 0;
		const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
							sizeof(elevation), &size)
			&& elevation.TokenIsElevated != 0;
		CloseHandle(token);
		return ok;
	}();
	return elevated;
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
	const BOOL dark = dark ? TRUE : FALSE;
	if(!trySet(hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark)))
		trySet(hwnd, kUseImmersiveDarkModeLegacy, &dark, sizeof(dark));

	QString achieved;

	// 1. The system material. It is the only one giving real translucency:
	//    Windows blurs what is behind the window, something we cannot do
	//    from here.
	bool withMaterial = false;
	if(translucent)
	{
		const int material = kBackdropAcrylic;
		withMaterial = trySet(hwnd, kSystemBackdropType, &material, sizeof(material));
		if(withMaterial)
			achieved = QStringLiteral("system acrylic (what is behind the window shows "
										"blurred)");
	}
	if(!withMaterial)
	{
		const int material = kBackdropMica;
		withMaterial = trySet(hwnd, kSystemBackdropType, &material, sizeof(material));
		if(withMaterial)
			achieved = QStringLiteral("system mica (the bar takes its colour from the "
										"desktop wallpaper)");
	}

	if(withMaterial)
	{
		// With a material, choosing the bar colour ruins it: DWM starts
		// painting it solid and the effect disappears.
		const DWORD byDefault = kColorDefault;
		trySet(hwnd, kCaptionColor, &byDefault, sizeof(byDefault));
		trySet(hwnd, kTextColor, &byDefault, sizeof(byDefault));
	}
	else
	{
		// 2. Without a material, paint the bar in the theme colour. It is not
		//    glass, but there is no longer a white strip on top of everything.
		const DWORD barColor = toColorRef(caption);
		if(trySet(hwnd, kCaptionColor, &barColor, sizeof(barColor)))
		{
			const DWORD textColor = toColorRef(text);
			trySet(hwnd, kTextColor, &textColor, sizeof(textColor));
			achieved = QStringLiteral("bar painted in the theme colour (%1)")
				.arg(caption.name());
		}
		else
		{
			// 3. What is left is dark mode, already requested above.
			achieved = dark ? QStringLiteral("dark system bar, colour not chosen")
							  : QStringLiteral("light system bar, colour not chosen");
		}
	}

	// The window frame follows, when the system allows.
	const DWORD frameColor = toColorRef(border);
	trySet(hwnd, kBorderColor, &frameColor, sizeof(frameColor));

	// Without this, the bar only changes when the window is redrawn for
	// some other reason — and switching theme would have no visible effect.
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	if(achieved != g_summary)
	{
		g_summary = achieved;
		logInfo("Window: " + achieved.toStdString() + ".");
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
