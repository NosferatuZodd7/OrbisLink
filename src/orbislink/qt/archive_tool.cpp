// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/archive_tool.h"

#include "orbislink/common/util.h"
#include "orbislink/pkg/pkg_parts.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace orbislink {

namespace {

bool runnable(const QString &path)
{
	const QFileInfo file(path);
	return file.isFile() && (file.isExecutable() || path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive));
}

// The password goes in through standard input: with none, "-p" alone says
// there is none, so 7-Zip never stops to ask.
void startWith(QProcess &process, const QString &program, QStringList arguments, const QString &password)
{
	if(password.isEmpty())
	{
		// Before "--": after it, it would be a file name.
		const int names = arguments.indexOf(QStringLiteral("--"));
		arguments.insert(names < 0 ? arguments.size() : names, QStringLiteral("-p"));
	}
	process.start(program, arguments);
	if(!process.waitForStarted(15000))
		return;
	if(!password.isEmpty())
	{
		QByteArray line = password.toUtf8();
		line += '\n';
		process.write(line);
		line.fill('\0');
	}
	process.closeWriteChannel();
}

void stopAll(QProcess &process)
{
	if(process.state() == QProcess::NotRunning)
		return;
#ifdef Q_OS_UNIX
	if(process.processId() > 0)
		::kill(static_cast<pid_t>(process.processId()), SIGKILL);
#endif
	process.kill();
	process.waitForFinished(5000);
}

QStringList quiet()
{
	// No questions (-y), no wildcards in names (-spd), UTF-8 names.
	return { QStringLiteral("-y"), QStringLiteral("-spd"), QStringLiteral("-sccUTF-8") };
}

} // namespace

QString ArchiveTool::find(const QString &configured)
{
	const QStringList names = {
#ifdef Q_OS_WIN
		QStringLiteral("7z.exe"), QStringLiteral("7zz.exe")
#else
		QStringLiteral("7zz"), QStringLiteral("7z")
#endif
	};
	QStringList places;
	auto inFolder = [&](const QString &folder) {
		for(const QString &name : names)
			places << QDir(folder).filePath(name);
	};
	if(!configured.isEmpty())
	{
		if(QFileInfo(configured).isDir())
			inFolder(configured);
		else
			places << configured;
	}
	const QString fromEnvironment = qEnvironmentVariable("ORBISLINK_7ZIP");
	if(!fromEnvironment.isEmpty())
		places << fromEnvironment;
	// The one that comes with OrbisLink (beside it, or in a folder of its own).
	const QString own = QCoreApplication::applicationDirPath();
	if(!own.isEmpty())
	{
		inFolder(own);
		inFolder(QDir(own).filePath(QStringLiteral("7-Zip")));
	}
#ifdef Q_OS_WIN
	for(const QString &root : { qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
			 qEnvironmentVariable("ProgramFiles(x86)") })
		if(!root.isEmpty())
			places << QDir(root).filePath(QStringLiteral("7-Zip/7z.exe"));
#elif defined(Q_OS_MACOS)
	places << QStringLiteral("/opt/homebrew/bin/7zz") << QStringLiteral("/usr/local/bin/7zz");
#endif
	for(const QString &place : places)
		if(runnable(place))
			return QDir::toNativeSeparators(place);
	for(const QString &name : { QStringLiteral("7zz"), QStringLiteral("7z") })
	{
		const QString onPath = QStandardPaths::findExecutable(name);
		if(!onPath.isEmpty())
			return QDir::toNativeSeparators(onPath);
	}
	return QString();
}

library::ArchiveListing ArchiveTool::list(const QString &program, const QString &archive, const QString &password,
	std::string *output)
{
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	startWith(process, program, QStringList { QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-ba") } + quiet()
		<< QStringLiteral("--") << archive, password);
	if(process.state() == QProcess::NotRunning && process.error() == QProcess::FailedToStart)
	{
		library::ArchiveListing failed;
		failed.error = "7-Zip did not start (" + process.errorString().toStdString() + ")";
		return failed;
	}
	if(!process.waitForFinished(60000))
		stopAll(process);
	const std::string said = process.readAll().toStdString();
	if(output)
		*output = said;
	const int code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
	return library::parseSevenZipListing(said, code);
}

namespace {

std::string nameOf(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

const library::ArchiveEntry *ArchiveTool::packageIn(const library::ArchiveListing &listing, int64_t *wholeSize)
{
	// The first part of a package in parts (the game, cut up), else the
	// biggest package.
	const library::ArchiveEntry *best = nullptr;
	const library::ArchiveEntry *firstPart = nullptr;
	std::string partsBase;
	for(const library::ArchiveEntry &entry : listing.entries)
	{
		if(entry.folder || !endsWith(toLower(entry.path), ".pkg"))
			continue;
		std::string base;
		int index = -1;
		if(parsePkgPartName(nameOf(entry.path), &base, &index) && index == 0
			&& (!firstPart || entry.size > firstPart->size))
		{
			firstPart = &entry;
			partsBase = base;
		}
		if(!best || entry.size > best->size)
			best = &entry;
	}
	int64_t whole = 0;
	int parts = 0;
	if(firstPart)
	{
		const std::string folder = firstPart->path.substr(0, firstPart->path.size() - nameOf(firstPart->path).size());
		for(const library::ArchiveEntry &entry : listing.entries)
		{
			std::string base;
			int index = -1;
			if(!entry.folder && entry.path.compare(0, folder.size(), folder) == 0
				&& nameOf(entry.path).size() + folder.size() == entry.path.size()
				&& parsePkgPartName(nameOf(entry.path), &base, &index) && base == partsBase)
			{
				whole += entry.size;
				++parts;
			}
		}
	}
	// A lone "_0" is a package of its own.
	if(firstPart && parts > 1)
	{
		if(wholeSize)
			*wholeSize = whole;
		return firstPart;
	}
	if(wholeSize)
		*wholeSize = best ? best->size : 0;
	return best;
}

QByteArray ArchiveTool::peek(const QString &program, const QString &archive, const QString &entry,
	const QString &password, qint64 maxBytes)
{
	QProcess process;
	// What it says goes away: only the file comes out on standard output.
	process.setStandardErrorFile(QProcess::nullDevice());
	startWith(process, program, QStringList { QStringLiteral("e"), QStringLiteral("-so"), QStringLiteral("-bd") } + quiet()
		<< QStringLiteral("--") << archive << entry, password);
	QByteArray data;
	if(process.state() == QProcess::NotRunning)
		return data;
	while(data.size() < maxBytes)
	{
		if(!process.waitForReadyRead(30000))
		{
			if(process.state() == QProcess::NotRunning)
				data += process.readAllStandardOutput();
			break;
		}
		data += process.read(maxBytes - data.size());
	}
	const bool whole = process.state() == QProcess::NotRunning && process.exitStatus() == QProcess::NormalExit
		&& process.exitCode() == 0;
	stopAll(process);
	data.truncate(static_cast<int>(std::min<qint64>(data.size(), maxBytes)));
	// Cut short by a wrong password or a volume missing (not by us): what came
	// out is not to be trusted.
	if(data.size() < maxBytes && !whole)
		data.clear();
	return data;
}

int ArchiveTool::extract(const QString &program, const QString &archive, const QString &folder,
	const QString &password, const std::function<bool()> &stop, const std::function<void(double)> &progress,
	std::string *output)
{
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	// Progress (-bsp1) and errors (-bse1) on standard output; the names of
	// the files done are left out (-bso0, -bb0).
	startWith(process, program,
		QStringList { QStringLiteral("x"), QStringLiteral("-bsp1"), QStringLiteral("-bse1"), QStringLiteral("-bso0"),
			QStringLiteral("-bb0"), QStringLiteral("-aoa"), QStringLiteral("-o") + folder }
			+ quiet() << QStringLiteral("--") << archive,
		password);
	if(process.state() == QProcess::NotRunning && process.error() == QProcess::FailedToStart)
	{
		if(output)
			*output = "7-Zip did not start (" + process.errorString().toStdString() + ")";
		return kNotStarted;
	}
	static const QRegularExpression percent(QStringLiteral("(\\d{1,3})%"));
	QByteArray said;
	QByteArray pending;
	while(process.state() != QProcess::NotRunning)
	{
		if(stop && stop())
		{
			stopAll(process);
			if(output)
				*output = said.toStdString();
			return kStopped;
		}
		process.waitForReadyRead(250);
		const QByteArray chunk = process.readAll();
		pending += chunk;
		// Its progress line is rewritten with backspaces: the last
		// percentage in what came is the one.
		QRegularExpressionMatchIterator it = percent.globalMatch(QString::fromUtf8(pending));
		int last = -1;
		while(it.hasNext())
			last = it.next().captured(1).toInt();
		if(last >= 0 && progress)
			progress(std::min(100, last));
		// Kept: what is not progress (its errors), at most a few kilobytes.
		QByteArray text = chunk;
		text.replace('\b', "");
		said += text;
		if(said.size() > 64 * 1024)
			said.remove(0, said.size() - 64 * 1024);
		const int lastBreak = std::max(pending.lastIndexOf('\n'), pending.lastIndexOf('\b'));
		if(lastBreak >= 0)
			pending.remove(0, lastBreak + 1);
	}
	process.waitForFinished(-1);
	said += process.readAll();
	if(output)
		*output = said.toStdString();
	return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

} // namespace orbislink
