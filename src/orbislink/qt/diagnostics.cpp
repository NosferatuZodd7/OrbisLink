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

void titulo(QTextStream &out, const QString &texto)
{
	out << "\n" << texto << "\n" << QString(texto.size(), QLatin1Char('-')) << "\n";
}

QString sim(bool valor) { return valor ? QStringLiteral("yes") : QStringLiteral("no"); }

} // namespace

QString Diagnostics::suggestedFileName()
{
	return QStringLiteral("orbislink-diagnostics-%1.txt")
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

QString Diagnostics::report(AppController *app)
{
	QString texto;
	QTextStream out(&texto);

	out << "OrbisLink diagnostics\n";
	out << "=====================\n";
	out << "Generated at " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";

	titulo(out, "Versions");
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

	titulo(out, "Network");
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
		titulo(out, "Console and services");
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

		titulo(out, "Settings");
		// Only what helps diagnose. The Account ID and the keys are left
		// out on purpose.
		const QVariantMap definicoes = app->settingsMap();
		static const QStringList segredos { QStringLiteral("streamAccountId"),
			QStringLiteral("accountId") };
		for(auto it = definicoes.constBegin(); it != definicoes.constEnd(); ++it)
		{
			if(segredos.contains(it.key()))
				continue;
			out << "  " << it.key().leftJustified(34) << it.value().toString() << "\n";
		}
	}

#ifdef ORBISLINK_HAS_STREAM
	if(app)
	{
		titulo(out, "Drag and drop");
		out << app->dragSummary() << "\n";
	}

	titulo(out, "Remote Play — video");
	if(app && !app->videoProbe().isEmpty())
		out << app->videoProbe();
	else
		out << "(no information)\n";

	titulo(out, "Remote Play — sound path");
	if(app && !app->audioProbe().isEmpty())
		out << app->audioProbe();
	else
		out << "(no information)\n";

	titulo(out, "Remote Play — last attempt");
	out << QString::fromStdString(StreamTrace::instance().summary());

	titulo(out, "Registered consoles");
	const CredentialStore store(CredentialStore::defaultPath());
	const std::vector<StreamCredentials> registadas = store.all();
	if(registadas.empty())
	{
		out << "None. Without registration there is no Remote Play.\n";
	}
	else
	{
		for(const StreamCredentials &credencial : registadas)
		{
			// The keys do not go in here. What matters is knowing that
			// they exist and have the right size.
			out << "  " << QString::fromStdString(credencial.nickname) << "  host-id "
				<< QString::fromStdString(credencial.hostId) << "  target " << credencial.target
				<< "  ps5 " << sim(credencial.ps5) << "  regist key "
				<< credencial.registKey.size() << " characters"
				<< "  rp_key " << credencial.rpKeyHex.size() << " characters\n";
		}
	}
#endif

	titulo(out, "Files");
	out << "Settings         " << QString::fromStdString(SettingsStore::defaultSettingsPath())
		<< "\n";
	out << "Log              " << QString::fromStdString(SettingsStore::defaultLogPath()) << "\n";
	out << "Data folder      " << QString::fromStdString(SettingsStore::defaultDirectory())
		<< "\n";

	titulo(out, "Log (last lines)");
	const std::vector<std::string> linhas = Logger::instance().recent();
	if(linhas.empty())
	{
		out << "(empty)\n";
	}
	else
	{
		for(const std::string &linha : linhas)
			out << QString::fromStdString(linha) << "\n";
	}

	out << "\n-- end of diagnostics --\n";
	return texto;
}

QString Diagnostics::write(AppController *app, const QString &directory, QString *error)
{
	QString pasta = directory;
	if(pasta.startsWith(QStringLiteral("file:")))
		pasta = QUrl(pasta).toLocalFile();
	if(pasta.trimmed().isEmpty())
	{
		pasta = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
		if(pasta.isEmpty() || !QDir(pasta).exists())
			pasta = QDir::homePath();
	}
	QDir().mkpath(pasta);

	const QString caminho = QDir(pasta).filePath(suggestedFileName());
	QFile ficheiro(caminho);
	if(!ficheiro.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		if(error)
			*error = ficheiro.errorString();
		return {};
	}
	QTextStream out(&ficheiro);
	out.setEncoding(QStringConverter::Utf8);
	out << report(app);
	ficheiro.close();
	logInfo("Diagnostics exported to " + caminho.toStdString());
	return caminho;
}

} // namespace orbislink
