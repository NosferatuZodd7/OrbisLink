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
#ifdef Q_OS_WIN
DWORD g_mainThreadId = 0;
// The log's path, ready before any crash: the handler must not allocate.
wchar_t g_crashLogPath[MAX_PATH] = { 0 };
#endif

QString dataDirectory()
{
	const QString directory = QString::fromStdString(SettingsStore::defaultDirectory());
	QDir().mkpath(directory);
	return directory;
}

QString markerPath() { return QDir(dataDirectory()).filePath(QStringLiteral("startup.lock")); }

// The log is only useful if it actually exists. Try the data folder, then
// the executable's own folder (the portable zip in a sandbox, where the data
// folder may not be writable) and finally the temporary folder.
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
	// Do not let the log grow without end.
	if(file.exists() && file.size() > 2 * 1024 * 1024)
		file.remove();
	if(!file.open(QIODevice::Append | QIODevice::Text))
		return;
	QTextStream stream(&file);
	stream.setEncoding(QStringConverter::Utf8);
	stream << line << '\n';
}

// Qt warnings that, in practice, mean "the window is not going to appear".
// Without this they would stay in the log as plain warnings, and the
// application would look alive without ever showing anything.
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
		case QtWarningMsg: level = "warning"; break;
		case QtCriticalMsg: level = "error"; break;
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
namespace {

// Module and offset of an address ("Qt6Core.dll+0xF2088"): the address
// alone changes on every start (ASLR) and tells nobody anything.
void describeAddress(const void *address, char *out, size_t size)
{
	char module[MAX_PATH] = "unknown";
	unsigned long long offset = reinterpret_cast<unsigned long long>(address);
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
		offset = static_cast<unsigned long long>(reinterpret_cast<const unsigned char *>(address)
			- reinterpret_cast<const unsigned char *>(handle));
	}
	_snprintf_s(out, size, _TRUNCATE, "%s+0x%llX", module, offset);
}

void writeText(HANDLE file, const char *text)
{
	DWORD written = 0;
	WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
}

#if defined(_MSC_VER) && defined(_M_X64)
// The crashed thread's own calls, unwound from the moment of the crash
// with the tables every x64 module carries. Stack memory may be what broke,
// so a fault while reading it only ends the walk.
int walkCrashedStack(const CONTEXT *crashed, void **frames, int max)
{
	CONTEXT context = *crashed;
	int count = 0;
	__try
	{
		// A call through a null pointer lands on address 0 with the
		// caller's return address on top of the stack.
		if(context.Rip == 0 && context.Rsp != 0)
		{
			context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
			context.Rsp += 8;
		}
		while(count < max && context.Rip != 0)
		{
			frames[count++] = reinterpret_cast<void *>(context.Rip);
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
			if(!function)
			{
				// A leaf function: the return address is right on top.
				context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
				context.Rsp += 8;
				continue;
			}
			void *handlerData = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context,
				&handlerData, &establisher, nullptr);
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
	}
	return count;
}
#endif

} // namespace

// Last resort: if the process crashes, it is logged instead of vanishing
// without a trace. Written with the Windows API and without allocating
// memory — inside an exception handler the process state cannot be
// trusted.
LONG WINAPI crashHandler(EXCEPTION_POINTERS *info)
{
	// Only the first crash is reported: another thread crashing while this
	// one writes would interleave the two.
	static volatile LONG entered = 0;
	if(InterlockedExchange(&entered, 1) != 0)
	{
		Sleep(INFINITE);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	HANDLE file = CreateFileW(g_crashLogPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(file != INVALID_HANDLE_VALUE)
	{
		char buffer[320];
		char where[MAX_PATH + 32];
		const EXCEPTION_RECORD *record = info ? info->ExceptionRecord : nullptr;
		const DWORD code = record ? record->ExceptionCode : 0;
		describeAddress(record ? record->ExceptionAddress : nullptr, where, sizeof(where));
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"FATAL: the process crashed (code 0x%08lX at %s), thread %lu%s\r\n",
			static_cast<unsigned long>(code), where, GetCurrentThreadId(),
			GetCurrentThreadId() == g_mainThreadId ? " (the window's)" : "");
		writeText(file, buffer);

		// Which memory it tried to touch, and how: reading address 0 is a
		// null pointer, a small address a field of one, anything else freed
		// or overwritten memory.
		if(record && code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
		{
			const ULONG_PTR kind = record->ExceptionInformation[0];
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "  %s address 0x%llX\r\n",
				kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
				static_cast<unsigned long long>(record->ExceptionInformation[1]));
			writeText(file, buffer);
		}

#if defined(_MSC_VER) && defined(_M_X64)
		void *frames[32];
		const int walked = info && info->ContextRecord
			? walkCrashedStack(info->ContextRecord, frames, 32)
			: 0;
		for(int i = 0; i < walked; ++i)
		{
			describeAddress(frames[i], where, sizeof(where));
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "  at %02d: %s\r\n", i, where);
			writeText(file, buffer);
		}
		if(walked == 0)
#endif
		{
			// The way into this handler, when the crashed stack cannot be
			// walked.
			void *handlerFrames[24];
			const USHORT captured = CaptureStackBackTrace(0, 24, handlerFrames, nullptr);
			for(USHORT i = 0; i < captured; ++i)
			{
				describeAddress(handlerFrames[i], where, sizeof(where));
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "  stack %02u: %s\r\n",
					static_cast<unsigned>(i), where);
				writeText(file, buffer);
			}
		}
		CloseHandle(file);
	}

	MessageBoxW(nullptr, L"OrbisLink closed unexpectedly.\n\n"
						 L"Run diagnostics.bat and send the report.",
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
	// Without this, running the application from a command line shows
	// nothing: it is a windowed executable, it has no console of its own.
	// Output already sent to a file or a pipe (diagnostics.bat does that)
	// stays there; only output going nowhere is pointed at the console.
	const auto redirected = [](DWORD which) {
		const HANDLE handle = GetStdHandle(which);
		if(!handle || handle == INVALID_HANDLE_VALUE)
			return false;
		const DWORD type = GetFileType(handle);
		return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
	};
	const bool outRedirected = redirected(STD_OUTPUT_HANDLE);
	const bool errRedirected = redirected(STD_ERROR_HANDLE);
	if((!outRedirected || !errRedirected) && AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE *stream = nullptr;
		if(!outRedirected)
			freopen_s(&stream, "CONOUT$", "w", stdout);
		if(!errRedirected)
			freopen_s(&stream, "CONOUT$", "w", stderr);
	}
#endif
	g_logPath = resolveLogPath();
#ifdef Q_OS_WIN
	g_mainThreadId = GetCurrentThreadId();
	const int copied = g_logPath.left(MAX_PATH - 1).toWCharArray(g_crashLogPath);
	g_crashLogPath[copied] = L'\0';
	SetUnhandledExceptionFilter(crashHandler);
#endif
	g_previousHandler = qInstallMessageHandler(handler);
	writeLine(QStringLiteral("──────────── startup ────────────"));
	qInfo("Log at %s", qPrintable(g_logPath));
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
	const QString full = message + QStringLiteral("\n\nDetails in:\n") + logPath();
	MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(full.utf16()),
		reinterpret_cast<const wchar_t *>(title.utf16()), MB_OK | MB_ICONERROR);
#else
	fprintf(stderr, "%s: %s\n", qPrintable(title), qPrintable(message));
#endif
}

} // namespace startup
} // namespace orbislink
