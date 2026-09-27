// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/diagnostics.h"

#include "orbislink/common/log.h"
#include "orbislink/net/net_utils.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/window_chrome.h"
#include "orbislink/settings/settings_store.h"

#ifdef ORBISLINK_HAS_STREAM
#include "orbislink/stream/credentials.h"
#include "orbislink/stream/stream_trace.h"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QUrl>

namespace orbislink {

namespace {

void heading(QTextStream &out, const QString &message)
{
	out << "\n" << message << "\n" << QString(message.size(), QLatin1Char('-')) << "\n";
}

QString sim(bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); }

} // namespace

QString Diagnostics::suggestedFileName()
{
	return QStringLiteral("orbislink-diagnostics-%1.txt")
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

QString Diagnostics::report(AppController *app)
{
	QString message;
	QTextStream out(&message);

	out << "OrbisLink diagnostics\n";
	out << "=====================\n";
	out << "Generated at " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";

	heading(out, "Versions");
	out << "OrbisLink        " << (app ? app->version() : QStringLiteral("?")) << "\n";
	out << "Qt               " << qVersion() << " (built with " << QT_VERSION_STR << ")\n";
	out << "System           " << QSysInfo::prettyProductName() << " ("
		<< QSysInfo::currentCpuArchitecture() << ")\n";
	out << "Kernel           " << QSysInfo::kernelType() << " " << QSysInfo::kernelVersion()
		<< "\n";
	out << "Qt platform      " << QGuiApplication::platformName() << "\n";
	out << "Title bar        " << WindowChrome::summary() << "\n";
	out << "Privileges       "
		<< (WindowChrome::runningElevated()
				? "ADMINISTRATOR — drag and drop does not work like this"
				: "normal")
		<< "\n";
#ifdef ORBISLINK_HAS_STREAM
	out << "Remote Play      built in\n";
#else
	out << "Remote Play      NOT built into this version\n";
#endif

	heading(out, "Network");
	const std::vector<LocalInterface> interfaces = localInterfaces();
	if(interfaces.empty())
	{
		out << "No network interface found.\n";
	}
	else
	{
		for(const LocalInterface &interface : interfaces)
		{
			out << "  " << QString::fromStdString(interface.name) << "  "
				<< QString::fromStdString(interface.address) << "  mask "
				<< QString::fromStdString(interface.netmask)
				<< (interface.loopback ? "  (loopback)" : "") << "\n";
		}
	}

	if(app)
	{
		heading(out, "Console and services");
		out << "Name             " << app->consoleName() << "\n";
		out << "Address          " << app->consoleAddress() << "\n";
		out << "Remote Play      " << app->remotePlayState();
		if(!app->remotePlayHint().isEmpty())
			out << "  — " << app->remotePlayHint();
		out << "\n";
		out << "FTP              " << app->ftpState();
		if(!app->ftpHint().isEmpty())
			out << "  — " << app->ftpHint();
		out << "\n";
		out << "Installer        " << app->installerState();
		if(!app->installerHint().isEmpty())
			out << "  — " << app->installerHint();
		out << "\n";
		out << "HTTP server      " << app->httpServerAddress() << "\n";

		heading(out, "Settings");
		// Only what helps diagnose. The Account ID and the keys are left
		// out on purpose.
		const QVariantMap settings = app->settingsMap();
		static const QStringList secrets { QStringLiteral("streamAccountId"),
			QStringLiteral("accountId") };
		for(auto it = settings.constBegin(); it != settings.constEnd(); ++it)
		{
			if(secrets.contains(it.key()))
				continue;
			out << "  " << it.key().leftJustified(34) << it.value().toString() << "\n";
		}
	}

#ifdef ORBISLINK_HAS_STREAM
	if(app)
	{
		heading(out, "Drag and drop");
		out << app->dragSummary() << "\n";
	}

	heading(out, "Remote Play — video");
	if(app && !app->videoProbe().isEmpty())
		out << app->videoProbe();
	else
		out << "(no information)\n";

	heading(out, "Remote Play — sound path");
	if(app && !app->audioProbe().isEmpty())
		out << app->audioProbe();
	else
		out << "(no information)\n";

	heading(out, "Remote Play — last attempt");
	out << QString::fromStdString(StreamTrace::instance().summary());

	heading(out, "Registered consoles");
	const CredentialStore store(CredentialStore::defaultPath());
	const std::vector<StreamCredentials> registeredOnes = store.all();
	if(registeredOnes.empty())
	{
		out << "None. Without registration there is no Remote Play.\n";
	}
	else
	{
		for(const StreamCredentials &credential : registeredOnes)
		{
			// The keys do not go in here. What matters is knowing that
			// they exist and have the right size.
			out << "  " << QString::fromStdString(credential.nickname) << "  host-id "
				<< QString::fromStdString(credential.hostId) << "  target " << credential.target
				<< "  ps5 " << sim(credential.ps5) << "  regist key "
				<< credential.registKey.size() << " characters"
				<< "  rp_key " << credential.rpKeyHex.size() << " characters\n";
		}
	}
#endif

	heading(out, "Files");
	out << "Settings         " << QString::fromStdString(SettingsStore::defaultSettingsPath())
		<< "\n";
	out << "Log              " << QString::fromStdString(SettingsStore::defaultLogPath()) << "\n";
	out << "Data folder      " << QString::fromStdString(SettingsStore::defaultDirectory())
		<< "\n";

	heading(out, "Log (last lines)");
	const std::vector<std::string> lines = Logger::instance().recent();
	if(lines.empty())
	{
		out << "(empty)\n";
	}
	else
	{
		for(const std::string &line : lines)
			out << QString::fromStdString(line) << "\n";
	}

	out << "\n-- end of diagnostics --\n";
	return message;
}

QString Diagnostics::write(AppController *app, const QString &directory, QString *error)
{
	QString folder = directory;
	if(folder.startsWith(QStringLiteral("file:")))
		folder = QUrl(folder).toLocalFile();
	if(folder.trimmed().isEmpty())
	{
		folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
		if(folder.isEmpty() || !QDir(folder).exists())
			folder = QDir::homePath();
	}
	QDir().mkpath(folder);

	const QString path = QDir(folder).filePath(suggestedFileName());
	QFile file(path);
	if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		if(error)
			*error = file.errorString();
		return {};
	}
	QTextStream out(&file);
	out.setEncoding(QStringConverter::Utf8);
	out << report(app);
	file.close();
	logInfo("Diagnostics exported to " + path.toStdString());
	return path;
}

} // namespace orbislink
