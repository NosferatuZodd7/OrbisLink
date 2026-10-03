// SPDX-License-Identifier: AGPL-3.0-or-later
//
// orbislink-gui — OrbisLink's main window (Qt 6 / QML).

#include "orbislink/common/log.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/games_controller.h"
#include "orbislink/qt/saves_controller.h"
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

#include <QFont>
#include <QFontDatabase>
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
	// The version comes from the git tag ("v1.2.3"), and the "v" belongs to
	// the tag, not the version. Without stripping it here, the status bar —
	// which writes "v" + version — would show "vv1.2.3". It is stripped once,
	// at the place everyone reads from.
	QString version = QStringLiteral(ORBISLINK_VERSION_STRING);
	if(version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
		version.remove(0, 1);
	QCoreApplication::setApplicationVersion(version);

	startup::installFileLogger();

	// Software rendering: required where there is no GPU (Windows Sandbox,
	// virtual machines, remote desktop). Enabled by option, by environment
	// variable, or by itself when the previous startup never got to draw.
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
		qWarning("The previous start never drew anything; trying software rendering.");
	}
	if(forceSoftware)
	{
		QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
		// Also in case Qt falls back to an accelerated backend.
		qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
		qInfo("Rendering mode: software.");
	}

	QGuiApplication app(argc, argv);
	// The window and taskbar icon. On Windows the file's own icon comes
	// from the .rc resource; this is the one the running application shows.
	app.setWindowIcon(QIcon(QStringLiteral(":/icons/mark.png")));
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	// Inter is shipped with the app, so the interface looks the same on every
	// system instead of taking whatever font each one has.
	for(const char *weight : {"Regular", "Medium", "SemiBold", "Bold"})
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-%1.ttf").arg(QLatin1String(weight)));
	{
		QFont uiFont(QStringLiteral("Inter"));
		uiFont.setPixelSize(13);
		uiFont.setHintingPreference(QFont::PreferNoHinting);
		app.setFont(uiFont);
	}
	qInfo("OrbisLink %s starting.", ORBISLINK_VERSION_STRING);
	// Files from different versions in the same folder are a likely cause of
	// crashes: it happens when installing over the top with the application
	// open and the locked files are skipped.
	if(qstrcmp(qVersion(), QT_VERSION_STR) != 0)
	{
		startup::reportFatal(QStringLiteral("OrbisLink — inconsistent installation"),
			QStringLiteral("This copy was built with Qt %1 but found Qt %2 next to the "
						   "executable.\n\nDelete the app folder and install again, with "
						   "OrbisLink closed.")
				.arg(QLatin1String(QT_VERSION_STR), QLatin1String(qVersion())));
		return 1;
	}
	qInfo("Qt %s (built with %s), platform \"%s\"", qVersion(), QT_VERSION_STR,
		qPrintable(app.platformName()));

	// Development options: capture the screen and quit, for documentation
	// and so CI can prove the window opens.
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
	bool demoGames = false;
	QString demoTheme;
	bool printDiagnostics = false;
	bool selfTestDrag = false;
	QStringList enqueuePaths;
	QStringList uploadPaths;
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
		else if(argument == QStringLiteral("--demo-games"))
			demoGames = true;
		else if(argument == QStringLiteral("--demo-theme") && i + 1 < arguments.size())
			demoTheme = arguments[++i];
		else if(argument == QStringLiteral("--print-diagnostics"))
			printDiagnostics = true;
		else if(argument == QStringLiteral("--selftest-drag"))
			selfTestDrag = true;
		else if(argument == QStringLiteral("--enqueue") && i + 1 < arguments.size())
			enqueuePaths << arguments[++i];
		else if(argument == QStringLiteral("--enqueue-ftp") && i + 1 < arguments.size())
			uploadPaths << arguments[++i];
	}

	// Translations. The source language is English; Portuguese is chosen in
	// the settings.
	QTranslator translator;
	{
		Settings settings;
		SettingsStore(SettingsStore::defaultSettingsPath()).load(&settings);
		const QString file = settings.language == "pt_PT"
			? QStringLiteral(":/i18n/orbislink_pt_PT.qm")
			: QStringLiteral(":/i18n/orbislink_en.qm");
		if(translator.load(file))
		{
			app.installTranslator(&translator);
			qInfo("Language: %s", qPrintable(file));
		}
		else
		{
			qWarning("Could not load %s; the app stays in English.", qPrintable(file));
		}
	}

	qmlRegisterUncreatableType<QueueModel>("OrbisLink", 1, 0, "QueueModel",
		QStringLiteral("Provided by the controller."));
#ifdef ORBISLINK_HAS_STREAM
	qmlRegisterUncreatableType<VideoBridge>("OrbisLink", 1, 0, "VideoBridge",
		QStringLiteral("Provided by the controller."));
#endif
	qmlRegisterUncreatableType<FtpModel>("OrbisLink", 1, 0, "FtpModel",
		QStringLiteral("Provided by the controller."));

	// An exception here would kill the process without a trace, and the
	// application would look installed but would not open.
	std::unique_ptr<AppController> controller;
	try
	{
		controller = std::make_unique<AppController>();
	}
	catch(const std::exception &error)
	{
		startup::reportFatal(QCoreApplication::translate("main", "OrbisLink could not start"),
			QCoreApplication::translate("main", "Setting up the services failed:\n%1")
				.arg(QString::fromUtf8(error.what())));
		return 1;
	}
	catch(...)
	{
		startup::reportFatal(QCoreApplication::translate("main", "OrbisLink could not start"),
			QCoreApplication::translate("main", "Setting up the services failed (unknown error)."));
		return 1;
	}
	if(!enqueuePaths.isEmpty())
		controller->addPaths(enqueuePaths, 0);
	// The same as dropping on "Send over FTP", after the window is up (the
	// question about names already taken needs it).
	if(!uploadPaths.isEmpty())
		QTimer::singleShot(1500, controller.get(),
			[&controller, uploadPaths]() { controller->addPaths(uploadPaths, 1); });

	// Important notices also go to the system, so they arrive with the
	// window minimised. Where there is no notification area, this does
	// nothing and the application carries on the same.
	Notifier notifier;
	QObject::connect(controller.get(), &AppController::notify, &notifier,
		[&notifier](const QString &title, const QString &message, bool error) {
			notifier.show(title, message, error);
		});

	startup::markLaunchStarted();

	// PS1/PS2 discs: found, sent or converted. Made before the engine, so it
	// outlives the interface that reads it when the application closes.
	auto games = std::make_unique<GamesController>(controller.get());
	// The save vault, the same way.
	auto saves = std::make_unique<SavesController>(controller.get());

	QQmlApplicationEngine engine;
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
		[](const QUrl &url) {
			startup::reportFatal(QStringLiteral("OrbisLink"),
				QCoreApplication::translate("main", "Could not load the interface (%1).").arg(url.toString()));
			QCoreApplication::exit(1);
		});
	engine.rootContext()->setContextProperty(QStringLiteral("app"), controller.get());
	engine.rootContext()->setContextProperty(QStringLiteral("games"), games.get());
	engine.rootContext()->setContextProperty(QStringLiteral("saves"), saves.get());

#ifdef ORBISLINK_HAS_STREAM
	// Remote Play is a separate controller, but it follows the console
	// address set in the OrbisLink settings.
	auto stream = std::make_unique<StreamController>();
	stream->setAddress(controller->consoleAddress());
	stream->applySettings(controller->settings());
	QObject::connect(controller.get(), &AppController::settingsChanged, stream.get(),
		[&controller, &stream]() {
			stream->setAddress(controller->consoleAddress());
			stream->applySettings(controller->settings());
		});
	// The keys edited in the map window are kept in the settings; the new
	// map comes back to the stream through settingsChanged above.
	QObject::connect(stream.get(), &StreamController::keyBindingsEdited, controller.get(),
		[&controller](const std::map<std::string, int> &bindings) {
			controller->saveKeyBindings(bindings);
		});
	QObject::connect(stream.get(), &StreamController::padBindingsEdited, controller.get(),
		[&controller](const std::map<std::string, std::string> &bindings) {
			controller->savePadBindings(bindings);
		});
	// The "Remote Play" indicator in the top bar shows what the console
	// answered to discovery, instead of staying grey forever.
	QObject::connect(stream.get(), &StreamController::consoleChanged, controller.get(),
		[&controller, &stream]() {
			controller->reportRemotePlayState(stream->consoleState(), stream->runningApp());
		});
	// Stream notifications appear in the same bar as the others.
	QObject::connect(stream.get(), &StreamController::notify, controller.get(),
		&AppController::notify);
	// The Account ID the console accepted is kept in the settings.
	QObject::connect(stream.get(), &StreamController::accountIdAccepted, controller.get(),
		&AppController::rememberAccountId);
	// Diagnostics can now answer "the sound dies here" instead of leaving
	// the question open.
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
	engine.rootContext()->setContextProperty(QStringLiteral("demoGames"), demoGames);
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
			QCoreApplication::translate("main", "The interface was never created."));
		return 1;
	}

	auto *drawn = new QAtomicInt(0);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
	if(window)
	{
		notifier.setWindow(window);
		// The title bar belongs to the system. Without this, on Windows, a
		// white strip appears on top of a dark application. QML calls this
		// when the theme changes.
		auto *chrome = new WindowChrome(window, window);
		engine.rootContext()->setContextProperty(QStringLiteral("chrome"), chrome);
		QMetaObject::invokeMethod(window, "applyTheme", Qt::QueuedConnection);
		// When the first frame appears, startup went well.
		QObject::connect(window, &QQuickWindow::frameSwapped, &app, [drawn]() {
			drawn->storeRelaxed(1);
			startup::markLaunchSucceeded();
		});

		// Running elevated kills drag and drop, and Windows tells nobody.
		// Better for the application to say so than for the person to think
		// the feature disappeared.
		const QString elevationNotice = WindowChrome::elevationWarning();
		if(!elevationNotice.isEmpty())
		{
			logWarning("Running with administrator privileges: drag and drop will not "
					   "work.");
			QTimer::singleShot(1200, controller.get(), [ptr = controller.get(), elevationNotice]() {
				emit ptr->notify(QCoreApplication::translate("main", "Drag and drop"),
					elevationNotice, true);
			});
		}

		// If the rendering engine fails, say why instead of dying silently.
		QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
			[forceSoftware](QQuickWindow::SceneGraphError, const QString &message) {
				const QString hint = forceSoftware
					? QCoreApplication::translate("main", "It was already in software mode.")
					: QCoreApplication::translate("main",
						  "Try the \"OrbisLink (compatibility mode)\" shortcut, or run it "
						  "with the --software option.");
				startup::reportFatal(QCoreApplication::translate("main", "OrbisLink — graphics error"),
					message + QStringLiteral("\n\n") + hint);
			});
	}

	// If after a few seconds nothing was drawn, say why and where to
	// find the log.
	if(screenshotPath.isEmpty())
	{
		QTimer::singleShot(9000, &app, [drawn, forceSoftware]() {
			if(drawn->loadRelaxed() != 0)
				return;
			QString reason = startup::windowCreationFailed()
				? QCoreApplication::translate("main", "Windows refused to create the window.")
				: QCoreApplication::translate("main", "The window was created but nothing was ever drawn.");
			if(!forceSoftware)
				reason += QStringLiteral("\n\n") + QCoreApplication::translate("main",
					"Try the \"OrbisLink (compatibility mode)\" shortcut, or run it with the "
					"--software option.");
			startup::reportFatal(QCoreApplication::translate("main", "OrbisLink could not open"), reason);
		});
	}

	// Update check at startup, when enabled in the settings. Silent when
	// there is nothing new: it only interrupts to say there is a new
	// version. Waits a few seconds so as not to compete with the window
	// startup or the first service check.
	if(controller->settings().checkForUpdates && screenshotPath.isEmpty() && !printDiagnostics
		&& !selfTestDrag)
	{
		QTimer::singleShot(6000, &app, [&controller]() {
			controller->checkForUpdatesNow(true);
		});
	}

	// Writes the diagnostics to standard output and quits. Useful to get
	// the report without exporting it through the window.
	if(printDiagnostics)
	{
		QTimer::singleShot(2500, &app, [&controller]() {
			const QString report = Diagnostics::report(controller.get());
			fputs(report.toUtf8().constData(), stdout);
			QCoreApplication::quit();
		});
	}

	// Automatic drag test: the window opens, a fake file is dragged over
	// it and the overlay is checked not to flicker.
	if(selfTestDrag && window)
	{
		QTimer::singleShot(1500, &app, [window]() {
			dragselftest::run(window, [](int closedMidway, const QString &report) {
				if(closedMidway == 0)
				{
					qInfo("selftest-drag: PASSED — %s", qPrintable(report));
					QCoreApplication::exit(0);
				}
				else
				{
					qWarning("selftest-drag: FAILED — the overlay closed %d time(s) during the "
							 "drag: %s", closedMidway, qPrintable(report));
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
					qInfo("Screenshot saved to %s", qPrintable(screenshotPath));
				else
					qWarning("Could not save the screenshot.");
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
		startup::reportFatal(QCoreApplication::translate("main", "OrbisLink ended with an error"),
			QString::fromUtf8(error.what()));
		return 1;
	}
}
