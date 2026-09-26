// SPDX-License-Identifier: AGPL-3.0-or-later
//
// orbislink-gui — janela principal do OrbisLink (Qt 6 / QML).

#include "orbislink/common/log.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/settings/settings_store.h"
#include "orbislink/qt/ftp_model.h"
#include "orbislink/qt/queue_model.h"
#include "orbislink/qt/diagnostics.h"
#include "orbislink/qt/dragselftest.h"
#include "orbislink/qt/notifier.h"
#ifdef ORBISLINK_HAS_STREAM
#include "orbislink/qt/stream_controller.h"
#include "orbislink/qt/video_bridge.h"
#endif
#include "orbislink/qt/startup.h"
#include "orbislink/qt/window_chrome.h"

#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QAtomicInt>
#include <QQuickWindow>
#include <memory>
#include <QSGRendererInterface>
#include <QTimer>

using namespace orbislink;

int main(int argc, char **argv)
{
	QCoreApplication::setApplicationName(QStringLiteral("OrbisLink"));
	QCoreApplication::setOrganizationName(QStringLiteral("OrbisLink"));
	// A versão vem da tag do git ("v1.2.3"), e o "v" é da tag e não da
	// versão. Sem o cortar aqui, a barra de estado — que escreve "v" +
	// versão — mostraria "vv1.2.3". Corta-se uma vez, no sítio de onde
	// todos leem.
	QString versao = QStringLiteral(ORBISLINK_VERSION_STRING);
	if(versao.startsWith(QLatin1Char('v')) || versao.startsWith(QLatin1Char('V')))
		versao.remove(0, 1);
	QCoreApplication::setApplicationVersion(versao);

	startup::installFileLogger();

	// Desenho por software: obrigatório onde não há GPU (Windows Sandbox,
	// máquinas virtuais, ambiente remoto). Liga-se por opção, por variável de
	// ambiente, ou sozinho quando o arranque anterior não chegou a desenhar.
	bool forceSoftware = qEnvironmentVariableIsSet("ORBISLINK_SOFTWARE");
	for(int i = 1; i < argc; ++i)
	{
		if(qstrcmp(argv[i], "--software") == 0)
			forceSoftware = true;
	}
	const bool recovering = startup::previousLaunchFailed();
	if(recovering && !forceSoftware)
	{
		forceSoftware = true;
		qWarning("O arranque anterior não chegou a desenhar nada; a tentar com desenho por software.");
	}
	if(forceSoftware)
	{
		QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
		// Também para o caso de o Qt voltar a um backend acelerado.
		qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
		qInfo("Modo de desenho: software.");
	}

	QGuiApplication app(argc, argv);
	// O ícone da janela e da barra de tarefas. Em Windows o ícone do próprio
	// ficheiro vem do recurso .rc; este é o que a aplicação mostra a correr.
	app.setWindowIcon(QIcon(QStringLiteral(":/icons/mark.png")));
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInfo("OrbisLink %s a arrancar.", ORBISLINK_VERSION_STRING);
	// Ficheiros de versões diferentes na mesma pasta são causa provável de
	// estoiros: acontece quando se instala por cima com a aplicação aberta e
	// os ficheiros bloqueados são ignorados.
	if(qstrcmp(qVersion(), QT_VERSION_STR) != 0)
	{
		startup::reportFatal(QStringLiteral("OrbisLink — instalação inconsistente"),
			QStringLiteral("Esta cópia foi compilada com o Qt %1 mas encontrou o Qt %2 "
						   "ao lado do executável.\n\nApaga a pasta da aplicação e instala "
						   "de novo, com o OrbisLink fechado.")
				.arg(QLatin1String(QT_VERSION_STR), QLatin1String(qVersion())));
		return 1;
	}
	qInfo("Qt %s (compilado com %s), plataforma \"%s\"", qVersion(), QT_VERSION_STR,
		qPrintable(app.platformName()));

	// Opções de desenvolvimento: capturar o ecrã e sair, para documentação
	// e para o CI conseguir provar que a janela abre.
	QString screenshotPath;
	int screenshotDelayMs = 1200;
	bool demoOverlay = false;
	int demoTab = 0;
	bool demoSettings = false;
	bool demoMenu = false;
	bool demoRegister = false;
	bool demoKeys = false;
	bool demoLog = false;
	bool demoFullscreen = false;
	bool demoWizard = false;
	bool demoUpdate = false;
	QString demoTheme;
	bool printDiagnostics = false;
	bool selfTestDrag = false;
	QStringList enqueuePaths;
	const QStringList arguments = app.arguments();
	for(int i = 1; i < arguments.size(); ++i)
	{
		const QString &argument = arguments[i];
		if(argument == QStringLiteral("--screenshot") && i + 1 < arguments.size())
			screenshotPath = arguments[++i];
		else if(argument == QStringLiteral("--screenshot-delay") && i + 1 < arguments.size())
			screenshotDelayMs = arguments[++i].toInt();
		else if(argument == QStringLiteral("--demo-overlay"))
			demoOverlay = true;
		else if(argument == QStringLiteral("--demo-tab") && i + 1 < arguments.size())
			demoTab = arguments[++i].toInt();
		else if(argument == QStringLiteral("--demo-settings"))
			demoSettings = true;
		else if(argument == QStringLiteral("--demo-menu"))
			demoMenu = true;
		else if(argument == QStringLiteral("--demo-register"))
			demoRegister = true;
		else if(argument == QStringLiteral("--demo-keys"))
			demoKeys = true;
		else if(argument == QStringLiteral("--demo-log"))
			demoLog = true;
		else if(argument == QStringLiteral("--demo-fullscreen"))
			demoFullscreen = true;
		else if(argument == QStringLiteral("--demo-wizard"))
			demoWizard = true;
		else if(argument == QStringLiteral("--demo-update"))
			demoUpdate = true;
		else if(argument == QStringLiteral("--demo-theme") && i + 1 < arguments.size())
			demoTheme = arguments[++i];
		else if(argument == QStringLiteral("--print-diagnostics"))
			printDiagnostics = true;
		else if(argument == QStringLiteral("--selftest-drag"))
			selfTestDrag = true;
		else if(argument == QStringLiteral("--enqueue") && i + 1 < arguments.size())
			enqueuePaths << arguments[++i];
	}

	// Traduções. A língua vem das definições; "auto" segue o sistema.
	// Sem correspondência fica o português, que é a língua-fonte.
	QTranslator tradutor;
	{
		Settings definicoes;
		SettingsStore(SettingsStore::defaultSettingsPath()).load(&definicoes);
		QString lingua = QString::fromStdString(definicoes.language);
		if(lingua.isEmpty() || lingua == QLatin1String("auto"))
			lingua = QLocale::system().name(); // ex.: "en_GB", "pt_PT"
		// Uma língua que não seja português usa o inglês; o português usa a
		// língua-fonte. É o que há, e diz-se no registo qual saiu.
		const QString ficheiro = lingua.startsWith(QLatin1String("pt"))
			? QStringLiteral(":/i18n/orbislink_pt_PT.qm")
			: QStringLiteral(":/i18n/orbislink_en.qm");
		if(tradutor.load(ficheiro))
		{
			app.installTranslator(&tradutor);
			qInfo("Idioma: %s", qPrintable(ficheiro));
		}
		else
		{
			qWarning("Não consegui carregar %s; a aplicação fica em português.",
				qPrintable(ficheiro));
		}
	}

	qmlRegisterUncreatableType<QueueModel>("OrbisLink", 1, 0, "QueueModel",
		QStringLiteral("Fornecido pelo controlador."));
#ifdef ORBISLINK_HAS_STREAM
	qmlRegisterUncreatableType<VideoBridge>("OrbisLink", 1, 0, "VideoBridge",
		QStringLiteral("Fornecido pelo controlador."));
#endif
	qmlRegisterUncreatableType<FtpModel>("OrbisLink", 1, 0, "FtpModel",
		QStringLiteral("Fornecido pelo controlador."));

	// Uma exceção aqui mataria o processo sem deixar rasto, e a aplicação
	// pareceria instalada mas não abriria.
	std::unique_ptr<AppController> controller;
	try
	{
		controller = std::make_unique<AppController>();
	}
	catch(const std::exception &error)
	{
		startup::reportFatal(QStringLiteral("OrbisLink não conseguiu arrancar"),
			QStringLiteral("Falhou a preparação dos serviços:\n%1").arg(QString::fromUtf8(error.what())));
		return 1;
	}
	catch(...)
	{
		startup::reportFatal(QStringLiteral("OrbisLink não conseguiu arrancar"),
			QStringLiteral("Falhou a preparação dos serviços (erro desconhecido)."));
		return 1;
	}
	if(!enqueuePaths.isEmpty())
		controller->addPaths(enqueuePaths, 0);

	// Os avisos importantes também saem para o sistema, para chegarem com a
	// janela minimizada. Onde não houver área de notificação, isto não faz
	// nada e a aplicação continua igual.
	Notifier notifier;
	QObject::connect(controller.get(), &AppController::notify, &notifier,
		[&notifier](const QString &title, const QString &message, bool error) {
			notifier.show(title, message, error);
		});

	startup::markLaunchStarted();

	QQmlApplicationEngine engine;
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
		[](const QUrl &url) {
			startup::reportFatal(QStringLiteral("OrbisLink"),
				QStringLiteral("Não foi possível carregar a interface (%1).").arg(url.toString()));
			QCoreApplication::exit(1);
		});
	engine.rootContext()->setContextProperty(QStringLiteral("app"), controller.get());

#ifdef ORBISLINK_HAS_STREAM
	// O Remote Play é um controlador à parte, mas acompanha o endereço da
	// consola definido nas definições do OrbisLink.
	auto stream = std::make_unique<StreamController>();
	stream->setAddress(controller->consoleAddress());
	stream->applySettings(controller->settings());
	QObject::connect(controller.get(), &AppController::settingsChanged, stream.get(),
		[&controller, &stream]() {
			stream->setAddress(controller->consoleAddress());
			stream->applySettings(controller->settings());
		});
	// As teclas editadas na janela do mapa ficam nas definições; o mapa
	// novo volta ao stream pelo settingsChanged acima.
	QObject::connect(stream.get(), &StreamController::keyBindingsEdited, controller.get(),
		[&controller](const std::map<std::string, int> &bindings) {
			controller->saveKeyBindings(bindings);
		});
	// O indicador "Remote Play" na barra de cima passa a dizer o que a
	// consola respondeu à descoberta, em vez de ficar sempre cinzento.
	QObject::connect(stream.get(), &StreamController::consoleChanged, controller.get(),
		[&controller, &stream]() {
			controller->reportRemotePlayState(stream->consoleState(), stream->runningApp());
		});
	// As notificações do stream aparecem na mesma barra que as outras.
	QObject::connect(stream.get(), &StreamController::notify, controller.get(),
		&AppController::notify);
	// O Account ID que a consola aceitou fica guardado nas definições.
	QObject::connect(stream.get(), &StreamController::accountIdAccepted, controller.get(),
		&AppController::rememberAccountId);
	// O diagnóstico passa a poder responder "o som morre aqui" em vez de
	// deixar a pergunta em aberto.
	controller->setAudioProbe([ptr = stream.get()]() { return ptr->audioPipeline(); });
	controller->setVideoProbe([ptr = stream.get()]() { return ptr->videoSummary(); });
	engine.rootContext()->setContextProperty(QStringLiteral("stream"), stream.get());
	engine.rootContext()->setContextProperty(QStringLiteral("streamBuiltIn"), true);
#else
	engine.rootContext()->setContextProperty(QStringLiteral("stream"), QVariant());
	engine.rootContext()->setContextProperty(QStringLiteral("streamBuiltIn"), false);
#endif
	engine.rootContext()->setContextProperty(QStringLiteral("demoOverlay"), demoOverlay);
	engine.rootContext()->setContextProperty(QStringLiteral("demoTab"), demoTab);
	engine.rootContext()->setContextProperty(QStringLiteral("demoSettings"), demoSettings);
	engine.rootContext()->setContextProperty(QStringLiteral("demoMenu"), demoMenu);
	engine.rootContext()->setContextProperty(QStringLiteral("demoRegister"), demoRegister);
	engine.rootContext()->setContextProperty(QStringLiteral("demoKeys"), demoKeys);
	engine.rootContext()->setContextProperty(QStringLiteral("demoLog"), demoLog);
	engine.rootContext()->setContextProperty(QStringLiteral("demoFullscreen"), demoFullscreen);
	engine.rootContext()->setContextProperty(QStringLiteral("demoWizard"), demoWizard);
	engine.rootContext()->setContextProperty(QStringLiteral("demoUpdate"), demoUpdate);
	engine.rootContext()->setContextProperty(QStringLiteral("demoTheme"), demoTheme);
	engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
	if(engine.rootObjects().isEmpty())
	{
		startup::reportFatal(QStringLiteral("OrbisLink"),
			QStringLiteral("A interface não chegou a ser criada."));
		return 1;
	}

	auto *drawn = new QAtomicInt(0);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
	if(window)
	{
		notifier.setWindow(window);
		// A barra de título é do sistema. Sem isto, no Windows, aparece uma
		// faixa branca por cima de uma aplicação escura. O QML chama-lhe o tema
		// quando ele muda.
		auto *chrome = new WindowChrome(window, window);
		engine.rootContext()->setContextProperty(QStringLiteral("chrome"), chrome);
		QMetaObject::invokeMethod(window, "aplicarTema", Qt::QueuedConnection);
		// Quando o primeiro fotograma aparece, o arranque correu bem.
		QObject::connect(window, &QQuickWindow::frameSwapped, &app, [drawn]() {
			drawn->storeRelaxed(1);
			startup::markLaunchSucceeded();
		});

		// Correr elevado mata o arrastar e largar, e o Windows não diz nada
		// a ninguém. Mais vale a aplicação dizê-lo do que a pessoa achar
		// que a funcionalidade desapareceu.
		const QString avisoElevacao = WindowChrome::elevationWarning();
		if(!avisoElevacao.isEmpty())
		{
			logWarning("A correr com privilégios de administrador: o arrastar e largar "
					   "não vai funcionar.");
			QTimer::singleShot(1200, controller.get(), [ptr = controller.get(), avisoElevacao]() {
				emit ptr->notify(QCoreApplication::translate("main", "Arrastar e largar"),
					avisoElevacao, true);
			});
		}

		// Se o motor de desenho falhar, diz-se porquê em vez de morrer calado.
		QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
			[forceSoftware](QQuickWindow::SceneGraphError, const QString &message) {
				const QString hint = forceSoftware
					? QStringLiteral("Já estava em modo de software.")
					: QStringLiteral("Tenta o atalho \"OrbisLink (modo compatível)\", "
									 "ou corre com a opção --software.");
				startup::reportFatal(QStringLiteral("OrbisLink — erro gráfico"),
					message + QStringLiteral("\n\n") + hint);
			});
	}

	// Se ao fim de alguns segundos nada foi desenhado, diz-se porquê e onde
	// ver o registo.
	if(screenshotPath.isEmpty())
	{
		QTimer::singleShot(9000, &app, [drawn, forceSoftware]() {
			if(drawn->loadRelaxed() != 0)
				return;
			QString reason = startup::windowCreationFailed()
				? QStringLiteral("O Windows recusou criar a janela.")
				: QStringLiteral("A janela foi criada mas nada chegou a ser desenhado.");
			if(!forceSoftware)
				reason += QStringLiteral("\n\nTenta o atalho \"OrbisLink (modo compatível)\" "
										 "ou corre com a opção --software.");
			startup::reportFatal(QStringLiteral("OrbisLink não conseguiu abrir"), reason);
		});
	}

	// Verificação de actualizações ao arrancar, quando está ligada nas
	// definições. Silenciosa quando não há novidades: só interrompe para
	// dizer que há uma versão nova. Espera uns segundos para não competir
	// com o arranque da janela nem com a primeira verificação de serviços.
	if(controller->settings().checkForUpdates && screenshotPath.isEmpty() && !printDiagnostics
		&& !selfTestDrag)
	{
		QTimer::singleShot(6000, &app, [&controller]() {
			controller->checkForUpdatesNow(true);
		});
	}

	// Escreve o diagnóstico para a saída padrão e sai. Serve para pedir o
	// relatório sem ter de o exportar pela janela.
	if(printDiagnostics)
	{
		QTimer::singleShot(2500, &app, [&controller]() {
			const QString relatorio = Diagnostics::report(controller.get());
			fputs(relatorio.toUtf8().constData(), stdout);
			QCoreApplication::quit();
		});
	}

	// Teste automático do arrastar: a janela abre, um ficheiro falso é
	// arrastado por cima dela e verifica-se que a sobreposição não pisca.
	if(selfTestDrag && window)
	{
		QTimer::singleShot(1500, &app, [window]() {
			dragselftest::run(window, [](int fechouAMeio, const QString &relato) {
				if(fechouAMeio == 0)
				{
					qInfo("selftest-drag: PASSOU — %s", qPrintable(relato));
					QCoreApplication::exit(0);
				}
				else
				{
					qWarning("selftest-drag: FALHOU — a sobreposição fechou %d vez(es) a meio "
							 "do arrastar: %s", fechouAMeio, qPrintable(relato));
					QCoreApplication::exit(1);
				}
			});
		});
	}

	if(!screenshotPath.isEmpty())
	{
		QTimer::singleShot(screenshotDelayMs, &app, [&engine, screenshotPath]() {
			auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
			if(window)
			{
				const QImage image = window->grabWindow();
				if(!image.isNull() && image.save(screenshotPath))
					qInfo("Captura guardada em %s", qPrintable(screenshotPath));
				else
					qWarning("Não foi possível guardar a captura.");
			}
			QCoreApplication::quit();
		});
	}

	try
	{
		return app.exec();
	}
	catch(const std::exception &error)
	{
		startup::reportFatal(QStringLiteral("OrbisLink terminou com erro"),
			QString::fromUtf8(error.what()));
		return 1;
	}
}
