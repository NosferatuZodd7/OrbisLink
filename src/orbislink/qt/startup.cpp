// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/startup.h"

#include "orbislink/settings/settings_store.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>
#include <QtGlobal>

#ifdef Q_OS_WIN
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#	include <cstdio>
#	include <cstring>
#endif

namespace orbislink {
namespace startup {

namespace {

QMutex g_logMutex;
QtMessageHandler g_previousHandler = nullptr;
QString g_logPath;
QAtomicInt g_windowCreationFailed { 0 };

QString dataDirectory()
{
	const QString directory = QString::fromStdString(SettingsStore::defaultDirectory());
	QDir().mkpath(directory);
	return directory;
}

QString markerPath() { return QDir(dataDirectory()).filePath(QStringLiteral("startup.lock")); }

// O registo só serve se existir mesmo. Tenta a pasta de dados, depois a
// pasta do próprio executável (o caso do zip portátil numa sandbox, onde a
// pasta de dados pode não ser gravável) e por fim a pasta temporária.
QString resolveLogPath()
{
	const QStringList candidates = {
		QDir(dataDirectory()).filePath(QStringLiteral("orbislink-gui.log")),
		QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("orbislink-gui.log")),
		QDir::temp().filePath(QStringLiteral("orbislink-gui.log")),
	};
	for(const QString &candidate : candidates)
	{
		QFile file(candidate);
		if(file.open(QIODevice::Append | QIODevice::Text))
		{
			file.close();
			return candidate;
		}
	}
	return candidates.constLast();
}

void writeLine(const QString &line)
{
	QMutexLocker locker(&g_logMutex);
	QFile file(logPath());
	// Não deixa o registo crescer sem fim.
	if(file.exists() && file.size() > 2 * 1024 * 1024)
		file.remove();
	if(!file.open(QIODevice::Append | QIODevice::Text))
		return;
	QTextStream stream(&file);
	stream.setEncoding(QStringConverter::Utf8);
	stream << line << '\n';
}

// Avisos do Qt que, na prática, significam "a janela não vai aparecer".
// Sem isto ficariam no registo como simples avisos, e a aplicação
// pareceria viva sem nunca mostrar nada.
bool isWindowCreationFailure(const QString &message)
{
	static const char *fatalPatterns[] = {
		"Failed to create platform window",
		"CreateWindowEx failed",
		"Failed to create OpenGL context",
		"Failed to initialize graphics backend",
	};
	for(const char *pattern : fatalPatterns)
	{
		if(message.contains(QLatin1String(pattern), Qt::CaseInsensitive))
			return true;
	}
	return false;
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
	const char *level = "info";
	switch(type)
	{
		case QtDebugMsg: level = "debug"; break;
		case QtInfoMsg: level = "info"; break;
		case QtWarningMsg: level = "aviso"; break;
		case QtCriticalMsg: level = "erro"; break;
		case QtFatalMsg: level = "fatal"; break;
	}
	const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
	QString line = stamp + QStringLiteral(" [") + QLatin1String(level) + QStringLiteral("] ") + message;
	if(context.file && type >= QtWarningMsg)
		line += QStringLiteral("  (") + QString::fromLatin1(context.file) + QStringLiteral(":")
			+ QString::number(context.line) + QStringLiteral(")");
	writeLine(line);

	if(type >= QtWarningMsg && isWindowCreationFailure(message))
		g_windowCreationFailed.storeRelaxed(1);

	if(g_previousHandler)
		g_previousHandler(type, context, message);
}

} // namespace

#ifdef Q_OS_WIN
// Último recurso: se o processo estoirar, fica registado em vez de
// desaparecer sem deixar rasto. Escreve-se com a API do Windows e sem
// alocar memória — dentro de um manipulador de exceções não se pode confiar
// no estado do processo.
LONG WINAPI crashHandler(EXCEPTION_POINTERS *info)
{
	wchar_t path[MAX_PATH];
	const QString target = logPath();
	const int copied = target.toWCharArray(path);
	path[copied < MAX_PATH ? copied : MAX_PATH - 1] = L'\0';

	HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(file != INVALID_HANDLE_VALUE)
	{
		char buffer[256];
		const DWORD code = info && info->ExceptionRecord
			? info->ExceptionRecord->ExceptionCode
			: 0;
		const void *address = info && info->ExceptionRecord
			? info->ExceptionRecord->ExceptionAddress
			: nullptr;
		// Resolve o endereço para módulo + deslocamento: sem isto o endereço
		// muda a cada arranque (ASLR) e não diz nada a ninguém.
		char module[MAX_PATH] = "desconhecido";
		unsigned long long offset = 0;
		HMODULE handle = nullptr;
		if(address
			&& GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
					| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCSTR>(address), &handle)
			&& handle)
		{
			char full[MAX_PATH] = { 0 };
			if(GetModuleFileNameA(handle, full, MAX_PATH))
			{
				const char *name = strrchr(full, '\\');
				strncpy_s(module, sizeof(module), name ? name + 1 : full, _TRUNCATE);
			}
			offset = static_cast<unsigned long long>(
				reinterpret_cast<const unsigned char *>(address)
				- reinterpret_cast<const unsigned char *>(handle));
		}

		const int length = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"FATAL: o processo estoirou (codigo 0x%08lX em %s+0x%llX)\r\n",
			static_cast<unsigned long>(code), module, offset);
		DWORD written = 0;
		if(length > 0)
			WriteFile(file, buffer, static_cast<DWORD>(length), &written, nullptr);

		// E a pilha de chamadas, módulo a módulo.
		void *frames[24];
		const USHORT captured = CaptureStackBackTrace(0, 24, frames, nullptr);
		for(USHORT i = 0; i < captured; ++i)
		{
			HMODULE frameModule = nullptr;
			char frameName[MAX_PATH] = "?";
			unsigned long long frameOffset = 0;
			if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
						| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					static_cast<LPCSTR>(frames[i]), &frameModule)
				&& frameModule)
			{
				char full[MAX_PATH] = { 0 };
				if(GetModuleFileNameA(frameModule, full, MAX_PATH))
				{
					const char *name = strrchr(full, '\\');
					strncpy_s(frameName, sizeof(frameName), name ? name + 1 : full, _TRUNCATE);
				}
				frameOffset = static_cast<unsigned long long>(
					reinterpret_cast<const unsigned char *>(frames[i])
					- reinterpret_cast<const unsigned char *>(frameModule));
			}
			const int frameLength = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
				"  pilha %02u: %s+0x%llX\r\n", static_cast<unsigned>(i), frameName, frameOffset);
			if(frameLength > 0)
				WriteFile(file, buffer, static_cast<DWORD>(frameLength), &written, nullptr);
		}
		CloseHandle(file);
	}

	MessageBoxW(nullptr, L"O OrbisLink terminou inesperadamente.\n\n"
						 L"Corre o diagnostico.bat e envia o relatório.",
		L"OrbisLink", MB_OK | MB_ICONERROR);
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

QString logPath()
{
	if(g_logPath.isEmpty())
		g_logPath = resolveLogPath();
	return g_logPath;
}

void installFileLogger()
{
#ifdef Q_OS_WIN
	// Sem isto, correr a aplicação a partir de uma linha de comandos não
	// mostra nada: é um executável de janela, não tem consola própria.
	if(AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE *stream = nullptr;
		freopen_s(&stream, "CONOUT$", "w", stdout);
		freopen_s(&stream, "CONOUT$", "w", stderr);
	}
#endif
	g_logPath = resolveLogPath();
#ifdef Q_OS_WIN
	SetUnhandledExceptionFilter(crashHandler);
#endif
	g_previousHandler = qInstallMessageHandler(handler);
	writeLine(QStringLiteral("──────────── arranque ────────────"));
	qInfo("Registo em %s", qPrintable(g_logPath));
}

bool previousLaunchFailed() { return QFileInfo::exists(markerPath()); }

void markLaunchStarted()
{
	QFile file(markerPath());
	if(file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		QTextStream(&file) << QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
		file.close();
	}
}

void markLaunchSucceeded() { QFile::remove(markerPath()); }

bool windowCreationFailed() { return g_windowCreationFailed.loadRelaxed() != 0; }

void reportFatal(const QString &title, const QString &message)
{
	writeLine(QStringLiteral("FATAL: ") + title + QStringLiteral(" — ") + message);
#ifdef Q_OS_WIN
	const QString full = message + QStringLiteral("\n\nDetalhes em:\n") + logPath();
	MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(full.utf16()),
		reinterpret_cast<const wchar_t *>(title.utf16()), MB_OK | MB_ICONERROR);
#else
	fprintf(stderr, "%s: %s\n", qPrintable(title), qPrintable(message));
#endif
}

} // namespace startup
} // namespace orbislink
