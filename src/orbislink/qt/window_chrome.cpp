// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/window_chrome.h"

#include "orbislink/common/log.h"

#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace orbislink {

namespace {

QString g_resumo = QStringLiteral("a barra de título é a do sistema (nesta plataforma não há "
								  "nada a fazer-lhe)");

#ifdef Q_OS_WIN

// Os números vêm do dwmapi.h da Microsoft. Estão aqui à mão de propósito:
// os cabeçalhos antigos não os têm todos, e não quero que a compilação
// dependa da versão do SDK instalada no runner.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kUseImmersiveDarkModeAntigo = 19; // Windows 10 1809 a 1903
constexpr DWORD kBorderColor = 34;
constexpr DWORD kCaptionColor = 35;
constexpr DWORD kTextColor = 36;
constexpr DWORD kSystemBackdropType = 38;

// DWMSBT_*: 2 é mica (janela principal), 3 é acrílico (janela passageira).
constexpr int kBackdropMica = 2;
constexpr int kBackdropAcrylic = 3;

constexpr DWORD kColorDefault = 0xFFFFFFFF;

using SetAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

SetAttributeFn resolveSetAttribute()
{
	// Carregado à mão para a aplicação continuar a arrancar num Windows sem
	// composição, em vez de morrer no arranque por falta de um símbolo.
	static SetAttributeFn fn = []() -> SetAttributeFn {
		HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
		if(!dwm)
			return nullptr;
		return reinterpret_cast<SetAttributeFn>(
			reinterpret_cast<void *>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
	}();
	return fn;
}

// Devolve true quando o sistema aceitou o atributo. Um E_INVALIDARG aqui
// não é erro nenhum: é esta versão do Windows a dizer que não conhece a
// funcionalidade, e é assim que se descobre até onde se pode ir sem ter uma
// tabela de números de build para manter.
bool trySet(HWND hwnd, DWORD atributo, const void *valor, DWORD tamanho)
{
	SetAttributeFn fn = resolveSetAttribute();
	if(!fn)
		return false;
	return SUCCEEDED(fn(hwnd, atributo, valor, tamanho));
}

DWORD toColorRef(const QColor &cor)
{
	// O Windows quer 0x00BBGGRR, ao contrário de toda a gente.
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
	// Calculado uma vez: o token do processo não muda durante a vida dele.
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
	// Noutros sistemas correr como root não impede arrastar ficheiros, e
	// portanto não há aqui nada a avisar.
	return false;
#endif
}

QString WindowChrome::elevationWarning()
{
	if(!runningElevated())
		return {};
	return QObject::tr("O OrbisLink está a correr como administrador. O Windows não deixa "
					   "arrastar ficheiros do Explorador para uma janela com privilégios "
					   "elevados, por isso o arrastar e largar não vai funcionar. Fecha e "
					   "abre pelo atalho normal do menu Iniciar.");
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

	// A barra escura é a base de tudo o resto: mesmo quando se escolhe a
	// cor, é isto que decide a cor dos botões de minimizar e fechar.
	const BOOL escuro = dark ? TRUE : FALSE;
	if(!trySet(hwnd, kUseImmersiveDarkMode, &escuro, sizeof(escuro)))
		trySet(hwnd, kUseImmersiveDarkModeAntigo, &escuro, sizeof(escuro));

	QString conseguido;

	// 1. O material do sistema. É o único que dá translucidez a sério: o
	//    Windows desfoca o que está por trás da janela, coisa que nós não
	//    conseguimos fazer a partir daqui.
	bool comMaterial = false;
	if(translucent)
	{
		const int material = kBackdropAcrylic;
		comMaterial = trySet(hwnd, kSystemBackdropType, &material, sizeof(material));
		if(comMaterial)
			conseguido = QStringLiteral("acrílico do sistema (o que está por trás da janela "
										"vê-se desfocado)");
	}
	if(!comMaterial)
	{
		const int material = kBackdropMica;
		comMaterial = trySet(hwnd, kSystemBackdropType, &material, sizeof(material));
		if(comMaterial)
			conseguido = QStringLiteral("mica do sistema (a barra tira a cor do fundo do "
										"ambiente de trabalho)");
	}

	if(comMaterial)
	{
		// Com material, escolher a cor da barra estraga-o: o DWM passa a
		// pintá-la de sólido e o efeito desaparece.
		const DWORD porOmissao = kColorDefault;
		trySet(hwnd, kCaptionColor, &porOmissao, sizeof(porOmissao));
		trySet(hwnd, kTextColor, &porOmissao, sizeof(porOmissao));
	}
	else
	{
		// 2. Sem material, pinta-se a barra da cor do tema. Não é vidro,
		//    mas deixa de haver uma faixa branca por cima de tudo.
		const DWORD corBarra = toColorRef(caption);
		if(trySet(hwnd, kCaptionColor, &corBarra, sizeof(corBarra)))
		{
			const DWORD corTexto = toColorRef(text);
			trySet(hwnd, kTextColor, &corTexto, sizeof(corTexto));
			conseguido = QStringLiteral("barra pintada com a cor do tema (%1)")
				.arg(caption.name());
		}
		else
		{
			// 3. Resta o modo escuro, que já foi pedido acima.
			conseguido = dark ? QStringLiteral("barra escura do sistema, sem escolher a cor")
							  : QStringLiteral("barra clara do sistema, sem escolher a cor");
		}
	}

	// A moldura da janela acompanha, quando o sistema deixa.
	const DWORD corMoldura = toColorRef(border);
	trySet(hwnd, kBorderColor, &corMoldura, sizeof(corMoldura));

	// Sem isto, a barra só muda quando a janela for redesenhada por outro
	// motivo qualquer — e trocar de tema ficaria sem efeito visível.
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	if(conseguido != g_resumo)
	{
		g_resumo = conseguido;
		logInfo("Janela: " + conseguido.toStdString() + ".");
	}
#else
	// Fora do Windows a decoração é do gestor de janelas e não há aqui nada
	// a pedir-lhe. Os parâmetros ficam usados para o compilador não avisar.
	Q_UNUSED(caption)
	Q_UNUSED(text)
	Q_UNUSED(border)
	Q_UNUSED(dark)
	Q_UNUSED(translucent)
#endif
}

} // namespace orbislink
