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

QString sim(bool valor) { return valor ? QStringLiteral("sim") : QStringLiteral("não"); }

} // namespace

QString Diagnostics::suggestedFileName()
{
	return QStringLiteral("orbislink-diagnostico-%1.txt")
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

QString Diagnostics::report(AppController *app)
{
	QString texto;
	QTextStream out(&texto);

	out << "Diagnóstico do OrbisLink\n";
	out << "========================\n";
	out << "Gerado em " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";

	titulo(out, "Versões");
	out << "OrbisLink        " << (app ? app->version() : QStringLiteral("?")) << "\n";
	out << "Qt               " << qVersion() << " (compilado com " << QT_VERSION_STR << ")\n";
	out << "Sistema          " << QSysInfo::prettyProductName() << " ("
		<< QSysInfo::currentCpuArchitecture() << ")\n";
	out << "Kernel           " << QSysInfo::kernelType() << " " << QSysInfo::kernelVersion()
		<< "\n";
	out << "Plataforma Qt    " << QGuiApplication::platformName() << "\n";
	out << "Barra de título  " << WindowChrome::summary() << "\n";
	out << "Privilégios      "
		<< (WindowChrome::runningElevated()
				? "ADMINISTRADOR — o arrastar e largar não funciona assim"
				: "normais")
		<< "\n";
#ifdef ORBISLINK_HAS_STREAM
	out << "Remote Play      compilado\n";
#else
	out << "Remote Play      NÃO compilado nesta versão\n";
#endif

	titulo(out, "Rede");
	const std::vector<LocalInterface> interfaces = localInterfaces();
	if(interfaces.empty())
	{
		out << "Nenhuma interface de rede encontrada.\n";
	}
	else
	{
		for(const LocalInterface &interface : interfaces)
		{
			out << "  " << QString::fromStdString(interface.name) << "  "
				<< QString::fromStdString(interface.address) << "  máscara "
				<< QString::fromStdString(interface.netmask)
				<< (interface.loopback ? "  (loopback)" : "") << "\n";
		}
	}

	if(app)
	{
		titulo(out, "Consola e serviços");
		out << "Nome             " << app->consoleName() << "\n";
		out << "Endereço         " << app->consoleAddress() << "\n";
		out << "Remote Play      " << app->remotePlayState();
		if(!app->remotePlayHint().isEmpty())
			out << "  — " << app->remotePlayHint();
		out << "\n";
		out << "FTP              " << app->ftpState();
		if(!app->ftpHint().isEmpty())
			out << "  — " << app->ftpHint();
		out << "\n";
		out << "Instalador       " << app->installerState();
		if(!app->installerHint().isEmpty())
			out << "  — " << app->installerHint();
		out << "\n";
		out << "Servidor HTTP    " << app->httpServerAddress() << "\n";

		titulo(out, "Definições");
		// Só o que ajuda a diagnosticar. O Account ID e as chaves ficam de
		// fora de propósito.
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
		titulo(out, "Arrastar e largar");
		out << app->dragSummary() << "\n";
	}

	titulo(out, "Remote Play — vídeo");
	if(app && !app->videoProbe().isEmpty())
		out << app->videoProbe();
	else
		out << "(sem informação)\n";

	titulo(out, "Remote Play — caminho do som");
	if(app && !app->audioProbe().isEmpty())
		out << app->audioProbe();
	else
		out << "(sem informação)\n";

	titulo(out, "Remote Play — última tentativa");
	out << QString::fromStdString(StreamTrace::instance().summary());

	titulo(out, "Consolas registadas");
	const CredentialStore store(CredentialStore::defaultPath());
	const std::vector<StreamCredentials> registadas = store.all();
	if(registadas.empty())
	{
		out << "Nenhuma. Sem registo não há Remote Play.\n";
	}
	else
	{
		for(const StreamCredentials &credencial : registadas)
		{
			// As chaves não entram aqui. O que interessa é saber que
			// existem e se têm o tamanho certo.
			out << "  " << QString::fromStdString(credencial.nickname) << "  host-id "
				<< QString::fromStdString(credencial.hostId) << "  alvo " << credencial.target
				<< "  ps5 " << sim(credencial.ps5) << "  chave de registo "
				<< credencial.registKey.size() << " caracteres"
				<< "  rp_key " << credencial.rpKeyHex.size() << " caracteres\n";
		}
	}
#endif

	titulo(out, "Ficheiros");
	out << "Definições       " << QString::fromStdString(SettingsStore::defaultSettingsPath())
		<< "\n";
	out << "Registo          " << QString::fromStdString(SettingsStore::defaultLogPath()) << "\n";
	out << "Pasta de dados   " << QString::fromStdString(SettingsStore::defaultDirectory())
		<< "\n";

	titulo(out, "Registo (últimas linhas)");
	const std::vector<std::string> linhas = Logger::instance().recent();
	if(linhas.empty())
	{
		out << "(vazio)\n";
	}
	else
	{
		for(const std::string &linha : linhas)
			out << QString::fromStdString(linha) << "\n";
	}

	out << "\n-- fim do diagnóstico --\n";
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
	logInfo("Diagnóstico exportado para " + caminho.toStdString());
	return caminho;
}

} // namespace orbislink
