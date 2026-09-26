// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/stream_controller.h"

#include "orbislink/qt/translate_message.h"
#include "orbislink/stream/account_id.h"

#include <QKeySequence>
#include <QTimer>

#include "orbislink/common/log.h"

#include <QMetaObject>
#include <thread>

namespace orbislink {

namespace {

// The same Account ID in the three forms, so QML can show the other
// two while one of them is being typed.
QVariantMap formsFromAccountId(const AccountId &id)
{
	QVariantMap mapa;
	mapa[QStringLiteral("valid")] = id.valid;
	mapa[QStringLiteral("base64")] = QString::fromStdString(id.base64);
	mapa[QStringLiteral("hex")] = QString::fromStdString(id.hex);
	mapa[QStringLiteral("decimal")] = QString::fromStdString(id.decimal);
	mapa[QStringLiteral("format")] = QString::fromStdString(id.format);
	mapa[QStringLiteral("error")] = translateMessage(id.error);
	return mapa;
}

} // namespace

namespace {

QString stateName(HostState state)
{
	switch(state)
	{
		case HostState::Ready: return QStringLiteral("ready");
		case HostState::Standby: return QStringLiteral("standby");
		case HostState::Unknown: break;
	}
	return QStringLiteral("unknown");
}

QString sessionStateSlug(SessionState state)
{
	switch(state)
	{
		case SessionState::Idle: return QStringLiteral("idle");
		case SessionState::Connecting: return QStringLiteral("connecting");
		case SessionState::Connected: return QStringLiteral("connected");
		case SessionState::Stopped: return QStringLiteral("stopped");
		case SessionState::Failed: return QStringLiteral("failed");
	}
	return QStringLiteral("idle");
}

} // namespace

StreamController::StreamController(QObject *parent)
	: QObject(parent), store_(CredentialStore::defaultPath()),
	  registration_(std::make_unique<StreamRegistration>()),
	  session_(std::make_unique<StreamSession>())
{
	// Every captured frame goes to chiaki's encoder. Capture runs on the
	// interface thread (QAudioSource signals), and the session protects
	// itself with its own mutex.
	microphone_.setFrameCallback([this](const int16_t *pcm, size_t samples) {
		session_->sendMicrophoneFrame(pcm, samples);
	});

	session_->setStateCallback([this](SessionState state, const std::string &detail) {
		const QString slug = sessionStateSlug(state);
		const QString texto = translateMessage(detail);
		QMetaObject::invokeMethod(
			this,
			[this, slug, texto, state]() {
				sessionState_ = slug;
				sessionDetail_ = texto;
				streaming_ = state == SessionState::Connected;
				if(streaming_)
				{
					gamepad_.start();
					hardwareDecoder_ = session_->usingHardwareDecoder();
					lastFrameCount_ = video_.framesDelivered();
					measuredFps_ = 0;
					if(fpsTimer_)
						fpsTimer_->start();
				}
				else
				{
					gamepad_.stop();
					hardwareDecoder_ = false;
					touchEnd();
					if(fpsTimer_)
						fpsTimer_->stop();
					measuredFps_ = 0;
				}
				if(state == SessionState::Stopped || state == SessionState::Failed)
				{
					audio_.stop();
					video_.clear();
					frameWidth_ = frameHeight_ = 0;
					emit videoChanged();
				}
				emit sessionChanged();
				if(state == SessionState::Failed)
					emit notify(tr("Remote Play"), texto, true);
			},
			Qt::QueuedConnection);
	});

	session_->setFrameCallback([this](AVFrame *frame) { video_.presentFrame(frame); });

	session_->setRumbleCallback([this](uint8_t left, uint8_t right) {
		QMetaObject::invokeMethod(
			this, [this, left, right]() { gamepad_.rumble(left, right); },
			Qt::QueuedConnection);
	});

	// An audio failure must not stay as just a line in the log: notify.
	connect(&audio_, &AudioOutput::failed, this, [this](const QString &razao) {
		emit audioChanged();
		emit notify(tr("Sound"),
			tr("%1 The video carries on; you will not hear the game.").arg(razao), true);
	});
	connect(&audio_, &AudioOutput::started, this, [this](const QString &dispositivo) {
		emit audioChanged();
		logInfo("Remote Play sound through " + dispositivo.toStdString());
	});

	session_->setAudioCallbacks(
		[this](unsigned int channels, unsigned int rate) { audio_.configure(channels, rate); },
		[this](const int16_t *pcm, size_t samples) { audio_.write(pcm, samples); });

	session_->setLoginPinCallback([this](bool incorrect) {
		QMetaObject::invokeMethod(
			this, [this, incorrect]() { emit loginPinRequested(incorrect); },
			Qt::QueuedConnection);
	});

	// The physical controller is only read during the session.
	connect(&gamepad_, &Gamepad::stateChanged, this,
		[this](const StreamSession::ControllerState &state) {
			if(streaming_)
				session_->sendController(state);
		});
	connect(&gamepad_, &Gamepad::connectedChanged, this, &StreamController::gamepadChanged);

	connect(&video_, &VideoBridge::firstFrame, this, [this](int width, int height) {
		frameWidth_ = width;
		frameHeight_ = height;
		emit videoChanged();

		// The console may send less than it was asked for without telling
		// anyone. A non-Pro PS4 does not do 1080p: chiaki lowers the request
		// by itself (video_profile_auto_downgrade) and the stream comes out
		// at 720p. Without this, whoever chose 1080p is left looking at the
		// same picture as always thinking the setting is useless.
		if(height > 0 && resolution_ > 0 && height < resolution_)
		{
			const QString recebido = tr("%1×%2").arg(width).arg(height);
			if(resolution_ == 1080 && height == 720)
			{
				emit notify(tr("Remote Play"),
					tr("You asked for 1080p and the console is sending %1. Remote Play on a PS4 that is not a "
						"Pro does not go above 720p, and the request is downgraded automatically.").arg(recebido),
					false);
			}
			else
			{
				emit notify(tr("Remote Play"),
					tr("You asked for %1p and the console is sending %2.")
						.arg(resolution_).arg(recebido),
					false);
			}
		}
	});

	// Measured fps, once per second. It is the only honest way to answer
	// "is this really at 60?": count what reaches the screen.
	fpsTimer_ = new QTimer(this);
	fpsTimer_->setInterval(1000);
	connect(fpsTimer_, &QTimer::timeout, this, [this]() {
		const qint64 agora = video_.framesDelivered();
		measuredFps_ = static_cast<int>(agora - lastFrameCount_);
		lastFrameCount_ = agora;
		emit videoChanged();
	});
}

StreamController::~StreamController()
{
	if(session_)
		session_->stop();
}

void StreamController::setAddress(const QString &address)
{
	if(address_ == address)
		return;
	address_ = address;
	cancelOneClick();
	// What was known belonged to the previous console: the name, state, type
	// and registration key. Keeping it until the new one answered made the
	// new one's card show the old one's data (and store it as its own).
	HostInfo nova;
	nova.address = address.toStdString();
	applyHost(nova);
	consoleState_ = QStringLiteral("unknown");
	emit consoleChanged();
	// No notice: the card already shows what the console answered, and an
	// error notification just for choosing a console that is off looked
	// like something had gone wrong.
	procurar(false);
}

void StreamController::applySettings(const Settings &settings)
{
	resolution_ = settings.streamResolution;
	fps_ = settings.streamFps;
	bitrateKbps_ = settings.streamBitrateKbps;
	wantHardware_ = settings.streamHardwareDecode;
	fullscreenOnConnect_ = settings.streamFullscreenOnConnect;
	rumbleEnabled_ = settings.streamRumble;
	touchpadFromMouse_ = settings.streamTouchpadFromMouse;
	gamepad_.setRumbleEnabled(rumbleEnabled_);
	// The Account ID the console in use already accepted; if it has not
	// accepted any yet, the last one any console accepted.
	accountId_ = QString::fromStdString(settings.streamAccountId);
	for(const ConsoleEntry &consola : settings.consoles)
		if(consola.address == settings.consoleAddress && !consola.accountId.empty())
			accountId_ = QString::fromStdString(consola.accountId);
	keyboard_.setBindings(settings.keyboardBindings);
	emit settingsApplied();
	emit keyBindingsChanged();
}

QVariantMap StreamController::keyBindings() const
{
	QVariantMap mapa;
	for(const auto &par : keyboard_.bindings())
		mapa.insert(QString::fromStdString(par.first), par.second);
	return mapa;
}

bool StreamController::setKeyBinding(const QString &action, int key)
{
	KeyboardMap::Bindings novas = keyboard_.bindings();
	if(!KeyboardMap::rebind(novas, action.toStdString(), key))
		return false;
	emit keyBindingsEdited(novas);
	return true;
}

void StreamController::resetKeyBindings() { emit keyBindingsEdited({}); }

QString StreamController::keyName(int key) const
{
	switch(key)
	{
	// QKeySequence spells these out in full; they fit better like this
	// on the keyboard drawing.
		case Qt::Key_Return: return QStringLiteral("Enter");
		case Qt::Key_Backspace: return QStringLiteral("⌫");
		case Qt::Key_Up: return QStringLiteral("↑");
		case Qt::Key_Down: return QStringLiteral("↓");
		case Qt::Key_Left: return QStringLiteral("←");
		case Qt::Key_Right: return QStringLiteral("→");
		case Qt::Key_Space: return tr("Space");
		default: return QKeySequence(key).toString(QKeySequence::NativeText);
	}
}

void StreamController::loadCredentials()
{
	credentials_ = store_.load(host_.id);
}

void StreamController::applyHost(const HostInfo &info)
{
	host_ = info;
	consoleState_ = info.found ? stateName(info.state) : QStringLiteral("offline");
	consoleName_ = QString::fromStdString(info.name);
	runningApp_ = QString::fromStdString(info.runningAppName);
	loadCredentials();
	emit consoleChanged();
	emit registrationChanged();
}

QVariantMap StreamController::describeHost(const HostInfo &info)
{
	QVariantMap estado;
	estado[QStringLiteral("state")] = info.found ? stateName(info.state) : QStringLiteral("offline");
	estado[QStringLiteral("name")] = QString::fromStdString(info.name);
	estado[QStringLiteral("address")] = QString::fromStdString(info.address);
	estado[QStringLiteral("ps5")] = info.ps5;
	estado[QStringLiteral("registered")] = info.found && store_.load(info.id).valid;
	return estado;
}

void StreamController::probeConsoles(const QStringList &addresses)
{
	if(probing_ || addresses.isEmpty())
		return;
	probing_ = true;
	std::vector<std::string> lista;
	for(const QString &endereco : addresses)
		lista.push_back(endereco.toStdString());
	std::thread([this, lista]() {
		std::vector<HostInfo> respostas;
		for(const std::string &endereco : lista)
		{
			HostInfo info = StreamDiscovery::peek(endereco, 1200);
			info.address = endereco;
			respostas.push_back(info);
		}
		QMetaObject::invokeMethod(
			this,
			[this, respostas]() {
				probing_ = false;
				for(const HostInfo &info : respostas)
					consoleStates_[QString::fromStdString(info.address)] = describeHost(info);
				emit consoleStatesChanged();
			},
			Qt::QueuedConnection);
	}).detach();
}

void StreamController::scanNetwork()
{
	if(scanning_)
		return;
	scanning_ = true;
	scanResults_.clear();
	emit scanChanged();
	std::thread([this]() {
		const std::vector<HostInfo> encontradas = StreamDiscovery::scan(2500);
		QMetaObject::invokeMethod(
			this,
			[this, encontradas]() {
				scanning_ = false;
				scanResults_.clear();
				for(const HostInfo &info : encontradas)
					scanResults_.append(describeHost(info));
				emit scanChanged();
			},
			Qt::QueuedConnection);
	}).detach();
}

void StreamController::refreshConsole() { procurar(true); }

void StreamController::procurar(bool avisar)
{
	// Always say something: when the address is missing and when it ends. A
	// silent check looks like it does nothing.
	if(address_.isEmpty())
	{
		if(avisar)
			emit notify(tr("Search"),
				tr("The console IP address is missing. Set it in the settings."), true);
		return;
	}
	// A check requested while one is already running is not lost: the
	// running one reports at the end instead.
	avisarNoFim_ = avisarNoFim_ || avisar;
	if(searching_)
		return;
	searching_ = true;
	emit consoleChanged();

	const std::string address = address_.toStdString();
	std::thread([this, address]() {
		const HostInfo info = StreamDiscovery::probe(address, 1500);
		QMetaObject::invokeMethod(
			this,
			[this, info, address]() {
				searching_ = false;
				const bool avisar = avisarNoFim_;
				avisarNoFim_ = false;
				// Meanwhile another console was selected: this reply no longer
				// belongs to it. Ask the new one.
				if(address != address_.toStdString())
				{
					emit consoleChanged();
					procurar(avisar);
					return;
				}
				applyHost(info);
				if(!avisar)
					return;
				if(!info.found)
				{
					emit notify(tr("Search"),
						tr("The console at %1 did not answer. Is it on, on the same network, and with Remote Play "
							"enabled?").arg(address_),
						true);
					return;
				}
				const QString nome = QString::fromStdString(info.name);
				if(consoleState() == QLatin1String("standby"))
					emit notify(tr("Search"),
						tr("%1 found, in rest mode. Click its box to wake it.")
							.arg(nome.isEmpty() ? address_ : nome),
						false);
				else
					emit notify(tr("Search"),
						tr("%1 found and ready.").arg(nome.isEmpty() ? address_ : nome),
						false);
			},
			Qt::QueuedConnection);
	}).detach();
}

void StreamController::wakeUp()
{
	if(!credentials_.valid)
	{
		emit notify(tr("Remote Play"),
			tr("Register the console first: without the registration key it ignores the request."), true);
		return;
	}
	const std::string address = address_.toStdString();
	const uint64_t credential = credentials_.wakeupCredential();
	const bool ps5 = credentials_.ps5;
	std::thread([this, address, credential, ps5]() {
		std::string erro;
		const bool ok = StreamDiscovery::wakeup(address, credential, ps5, &erro);
		const QString mensagem = ok
			? tr("Request sent. The console takes a few seconds to wake up.")
			: translateMessage(erro);
		QMetaObject::invokeMethod(
			this,
			[this, ok, mensagem]() {
				emit notify(tr("Wake the console"), mensagem, !ok);
				// No notice: just woken, it is normal not to answer yet.
				if(ok)
					procurar(false);
			},
			Qt::QueuedConnection);
	}).detach();
}

QString StreamController::videoSummary() const
{
	QString texto;
	texto += QStringLiteral("  requested    %1p, %2 fps%3\n")
		.arg(resolution_)
		.arg(fps_)
		.arg(bitrateKbps_ > 0 ? QStringLiteral(", %1 kbps").arg(bitrateKbps_)
							  : QStringLiteral(", automatic bitrate"));
	if(frameWidth_ > 0)
	{
		texto += QStringLiteral("  arriving     %1×%2, %3 fps measured\n")
			.arg(frameWidth_).arg(frameHeight_).arg(measuredFps_);
		if(frameHeight_ < resolution_)
			texto += QStringLiteral("  => the console lowered the resolution; a PS4 that is "
									"not a Pro does not go above 720p\n");
	}
	else
	{
		texto += QStringLiteral("  arriving     (no frame yet)\n");
	}
	texto += QStringLiteral("  decoding     %1\n")
		.arg(hardwareDecoder_ ? QStringLiteral("graphics card")
							  : QStringLiteral("processor"));
	return texto;
}

QVariantMap StreamController::accountIdForms(const QString &texto) const
{
	return formsFromAccountId(parseAccountId(texto.toStdString()));
}

QVariantMap StreamController::accountIdReversed(const QString &texto) const
{
	const AccountId lido = parseAccountId(texto.toStdString());
	return formsFromAccountId(lido.valid ? reverseAccountIdBytes(lido) : lido);
}

void StreamController::registerConsole(const QString &pin, const QString &accountIdBase64)
{
	if(registering_)
		return;
	if(address_.isEmpty())
	{
		emit notify(tr("Registration"), tr("Set the console's IP address first."), true);
		return;
	}

	// Without discovery the target is unknown — and without a target chiaki
	// speaks the wrong protocol. If nobody asked yet, ask off the UI
	// thread and come back here.
	if(!host_.found)
	{
		const std::string address = address_.toStdString();
		const QString pinCopy = pin;
		const QString accountCopy = accountIdBase64;
		registering_ = true;
		emit registrationChanged();
		std::thread([this, address, pinCopy, accountCopy]() {
			const HostInfo info = StreamDiscovery::probe(address, 1500);
			QMetaObject::invokeMethod(
				this,
				[this, info, pinCopy, accountCopy]() {
					registering_ = false;
					applyHost(info);
					if(!info.found)
					{
						emit notify(tr("Registration"),
							tr("The console did not answer. Check the IP, and that it is on (not in rest mode)."),
							true);
						return;
					}
					registerConsole(pinCopy, accountCopy);
				},
				Qt::QueuedConnection);
		}).detach();
		return;
	}

	// What comes in here may be hexadecimal, decimal or already base64: it
	// is converted here, so no path in the interface can send the console
	// a form it does not understand.
	const AccountId conta = parseAccountId(accountIdBase64.toStdString());
	if(!conta.valid)
	{
		emit notify(tr("Registration"), translateMessage(conta.error), true);
		return;
	}

	StreamRegistration::Request request;
	request.address = address_.toStdString();
	request.accountIdBase64 = conta.base64;
	request.pin = pin.trimmed().toUInt();
	request.target = host_.target;
	request.ps5 = host_.ps5;

	if(request.target == 0)
	{
		emit notify(tr("Registration"),
			tr("The console answered but did not report its system version. Click its box to search again."),
			true);
		return;
	}

	std::string erro;
	// Always stored in base64, whatever form it was typed in: it is the
	// only one the console accepts, and this way there are not two things
	// stored under the same name.
	const QString accountParaGuardar = QString::fromStdString(conta.base64);
	const bool started = registration_->start(
		request,
		[this, accountParaGuardar](bool ok, StreamCredentials credentials, std::string error) {
			const QString mensagem = translateMessage(error);
			QMetaObject::invokeMethod(
				this,
				[this, ok, credentials, mensagem, accountParaGuardar]() {
					registering_ = false;
					if(ok)
					{
						credentials_ = credentials;
						store_.save(credentials);
						// Stored only after the console accepts it: a wrong
						// ID does not get in the way of the next attempt.
						if(!accountParaGuardar.isEmpty())
						{
							accountId_ = accountParaGuardar;
							emit accountIdAccepted(accountParaGuardar);
							emit settingsApplied();
						}
						emit notify(tr("Registration"),
							tr("Console registered. The Account ID is saved — next time you only need the PIN."), false);
					}
					else
					{
						emit notify(tr("Registration"), mensagem, true);
					}
					emit registrationChanged();
				},
				Qt::QueuedConnection);
		},
		&erro);

	if(!started)
	{
		emit notify(tr("Registration"), translateMessage(erro), true);
		return;
	}
	registering_ = true;
	emit registrationChanged();
}

void StreamController::cancelRegistration()
{
	if(!registering_)
		return;
	registration_->cancel();
}

void StreamController::forgetConsole()
{
	if(!credentials_.valid)
		return;
	store_.forget(credentials_.hostId);
	credentials_ = {};
	emit registrationChanged();
	emit notify(tr("Remote Play"), tr("Registration removed from this PC."), false);
}

void StreamController::startStream()
{
	if(streaming_)
	{
		emit notify(tr("Remote Play"), tr("The session is already running."), false);
		return;
	}
	if(address_.isEmpty())
	{
		emit notify(tr("Remote Play"),
			tr("The console IP address is missing. Set it in the settings."), true);
		return;
	}
	if(!credentials_.valid)
	{
		emit notify(tr("Remote Play"),
			tr("Register the console first: click its box and follow the steps."), true);
		return;
	}
	// Say it is connecting before blocking to think: the click must get
	// an immediate response.
	sessionState_ = QStringLiteral("connecting");
	sessionDetail_ = tr("Connecting to %1…").arg(address_);
	emit sessionChanged();

	StreamSession::Config config;
	config.address = address_.toStdString();
	config.credentials = credentials_;
	config.settings.resolution = resolution_;
	config.settings.fps = fps_;
	config.settings.bitrateKbps = static_cast<unsigned int>(bitrateKbps_);
	config.settings.hardwareDecoder = wantHardware_;

	std::string erro;
	if(!session_->start(config, &erro))
	{
		sessionState_ = QStringLiteral("failed");
		sessionDetail_ = translateMessage(erro);
		emit sessionChanged();
		emit notify(tr("Remote Play"), sessionDetail_, true);
	}
}

void StreamController::setConnectStage(const QString &stage)
{
	if(connectStage_ == stage)
		return;
	connectStage_ = stage;
	emit connectStageChanged();
}

void StreamController::connectOneClick()
{
	if(streaming_ || sessionState_ == QLatin1String("connecting") || !connectStage_.isEmpty())
		return;
	if(address_.isEmpty())
	{
		emit notify(tr("Remote Play"),
			tr("The console IP address is missing. Set it in the settings."), true);
		return;
	}
	// Each click is its own run: a reply from a cancelled run, or from
	// another console, no longer decides anything.
	const quint64 corrida = ++oneClickRun_;
	setConnectStage(QStringLiteral("checking"));
	const std::string address = address_.toStdString();
	std::thread([this, address, corrida]() {
		const HostInfo info = StreamDiscovery::probe(address, 1500);
		QMetaObject::invokeMethod(
			this,
			[this, info, address, corrida]() {
				if(corrida != oneClickRun_ || address != address_.toStdString())
					return;
				applyHost(info);
				oneClickDecide(info);
			},
			Qt::QueuedConnection);
	}).detach();
}

void StreamController::oneClickDecide(const HostInfo &info)
{
	if(!info.found)
	{
		setConnectStage(QString());
		emit notify(tr("Connect"),
			tr("The console at %1 did not respond. Check that it is on (or in rest mode), on the same "
				"network, with Remote Play enabled.").arg(address_),
			true);
		return;
	}
	if(!credentials_.valid)
	{
		setConnectStage(QString());
		emit registrationNeeded();
		return;
	}
	if(info.state == HostState::Standby)
	{
		// Wake it once and then ask every two seconds, until it says it
		// is ready.
		if(connectStage_ != QLatin1String("waking"))
		{
			setConnectStage(QStringLiteral("waking"));
			wakeAttempts_ = 0;
			const std::string address = address_.toStdString();
			const uint64_t credential = credentials_.wakeupCredential();
			const bool ps5 = credentials_.ps5;
			std::thread([address, credential, ps5]() {
				StreamDiscovery::wakeup(address, credential, ps5, nullptr);
			}).detach();
		}
		if(!wakeTimer_)
		{
			wakeTimer_ = new QTimer(this);
			wakeTimer_->setSingleShot(true);
			wakeTimer_->setInterval(2000);
			connect(wakeTimer_, &QTimer::timeout, this, &StreamController::oneClickPoll);
		}
		wakeTimer_->start();
		return;
	}
	if(info.state != HostState::Ready)
	{
		setConnectStage(QString());
		emit notify(tr("Connect"), tr("The console answered but did not say whether it is ready. Try "
			"again in a moment."), true);
		return;
	}
	setConnectStage(QString());
	startStream();
}

void StreamController::oneClickPoll()
{
	if(connectStage_ != QLatin1String("waking"))
		return;
	// A console takes about twenty seconds to wake up; after a minute it
	// is not going to wake up by itself.
	if(++wakeAttempts_ > 30)
	{
		setConnectStage(QString());
		emit notify(tr("Connect"),
			tr("The console did not wake up. Check in its settings that it can be turned on over the "
				"network (Stay Connected to the Internet / Enable Turning On from Network)."),
			true);
		return;
	}
	const quint64 corrida = oneClickRun_;
	const std::string address = address_.toStdString();
	std::thread([this, address, corrida]() {
		const HostInfo info = StreamDiscovery::peek(address, 1500);
		QMetaObject::invokeMethod(
			this,
			[this, info, address, corrida]() {
				if(corrida != oneClickRun_ || address != address_.toStdString()
					|| connectStage_ != QLatin1String("waking"))
					return;
				if(!info.found)
				{
					// Midway through booting it may stop answering for a bit.
					wakeTimer_->start();
					return;
				}
				applyHost(info);
				oneClickDecide(info);
			},
			Qt::QueuedConnection);
	}).detach();
}

void StreamController::cancelOneClick()
{
	++oneClickRun_;
	if(wakeTimer_)
		wakeTimer_->stop();
	setConnectStage(QString());
}

void StreamController::stopStream()
{
	// The microphone does not outlive the session: if it stayed open, the
	// microphone light would stay on with nothing on the other end.
	microphone_.stop();
	session_->stopMicrophone();
	emit microphoneChanged();

	// chiaki's stop waits for its threads; off the UI thread so the
	// window does not freeze.
	std::thread([this]() { session_->stop(); }).detach();
}

void StreamController::setMuted(bool muted)
{
	if(audio_.muted() == muted)
		return;
	audio_.setMuted(muted);
	emit mutedChanged();
}

QString StreamController::microphoneState() const
{
	if(!microphone_.active() || !session_->microphoneActive())
		return QStringLiteral("off");
	return session_->microphoneMuted() ? QStringLiteral("muted")
									   : QStringLiteral("talking");
}

void StreamController::setMicrophoneEnabled(bool enabled)
{
	if(!enabled)
	{
		microphone_.stop();
		session_->stopMicrophone();
		emit microphoneChanged();
		return;
	}

	if(!streaming_)
	{
		emit notify(tr("Microphone"), tr("Start Remote Play first."), true);
		return;
	}

	// The console first: if it refuses, there is no point opening the
	// machine's microphone and leaving the light on without sending anything.
	std::string erro;
	if(!session_->startMicrophone(&erro))
	{
		emit notify(tr("Microphone"),
			tr("The console did not accept the microphone: %1").arg(translateMessage(erro)), true);
		return;
	}

	QString erroCaptura;
	if(!microphone_.start(&erroCaptura))
	{
		session_->stopMicrophone();
		emit notify(tr("Microphone"), erroCaptura, true);
		emit microphoneChanged();
		return;
	}
	emit notify(tr("Microphone"),
		tr("Talking to the console (%1).").arg(microphone_.deviceName()), false);
	emit microphoneChanged();
}

void StreamController::toggleMicrophone()
{
	setMicrophoneEnabled(!microphone_.active());
}

void StreamController::setMicrophoneMuted(bool muted)
{
	session_->setMicrophoneMuted(muted);
	emit microphoneChanged();
}

bool StreamController::keyPressed(int key)
{
	if(!streaming_ || !keyboard_.press(key))
		return false;
	session_->sendController(keyboard_.state());
	return true;
}

bool StreamController::keyReleased(int key)
{
	if(!keyboard_.release(key))
		return false;
	if(streaming_)
		session_->sendController(keyboard_.state());
	return true;
}

void StreamController::releaseAllKeys()
{
	// On losing focus release everything: otherwise a key stays stuck and
	// the character keeps walking by itself on the other side.
	if(keyboard_.empty())
		return;
	keyboard_.clear();
	if(streaming_)
		session_->sendController(keyboard_.state());
}


namespace {

uint16_t paraTouchpad(double normalizado, uint16_t maximo)
{
	if(normalizado < 0.0)
		normalizado = 0.0;
	if(normalizado > 1.0)
		normalizado = 1.0;
	return static_cast<uint16_t>(normalizado * maximo);
}

} // namespace

void StreamController::touchBegin(double x, double y)
{
	if(!streaming_ || !touchpadFromMouse_ || touchId_ >= 0)
		return;
	touchId_ = session_->startTouch(paraTouchpad(x, StreamSession::kTouchpadWidth),
		paraTouchpad(y, StreamSession::kTouchpadHeight));
}

void StreamController::touchMove(double x, double y)
{
	if(!streaming_ || touchId_ < 0)
		return;
	session_->moveTouch(touchId_, paraTouchpad(x, StreamSession::kTouchpadWidth),
		paraTouchpad(y, StreamSession::kTouchpadHeight));
}

void StreamController::touchEnd()
{
	if(touchId_ < 0)
		return;
	session_->stopTouch(touchId_);
	touchId_ = -1;
}

void StreamController::sendLoginPin(const QString &pin)
{
	session_->setLoginPin(pin.trimmed().toStdString());
}

} // namespace orbislink
