// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/stream_controller.h"

#include "orbislink/stream/account_id.h"

#include <QKeySequence>
#include <QTimer>

#include "orbislink/common/log.h"

#include <QMetaObject>
#include <thread>

namespace orbislink {

namespace {

// O mesmo Account ID nas três formas, para o QML poder mostrar as outras
// duas enquanto se escreve numa delas.
QVariantMap formsFromAccountId(const AccountId &id)
{
	QVariantMap mapa;
	mapa[QStringLiteral("valid")] = id.valid;
	mapa[QStringLiteral("base64")] = QString::fromStdString(id.base64);
	mapa[QStringLiteral("hex")] = QString::fromStdString(id.hex);
	mapa[QStringLiteral("decimal")] = QString::fromStdString(id.decimal);
	mapa[QStringLiteral("format")] = QString::fromStdString(id.format);
	mapa[QStringLiteral("error")] = QString::fromStdString(id.error);
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
	// Cada trama captada segue para o codificador do chiaki. A captura
	// corre na thread da interface (o QAudioSource avisa por sinal), e a
	// sessão protege-se com o seu próprio mutex.
	microphone_.setFrameCallback([this](const int16_t *pcm, size_t samples) {
		session_->sendMicrophoneFrame(pcm, samples);
	});

	session_->setStateCallback([this](SessionState state, const std::string &detail) {
		const QString slug = sessionStateSlug(state);
		const QString texto = QString::fromStdString(detail);
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

	// Uma falha do som não pode ficar só numa linha do registo: avisa-se.
	connect(&audio_, &AudioOutput::failed, this, [this](const QString &razao) {
		emit audioChanged();
		emit notify(tr("Som"),
			tr("%1 O vídeo continua; o som do jogo não vai ouvir-se.").arg(razao), true);
	});
	connect(&audio_, &AudioOutput::started, this, [this](const QString &dispositivo) {
		emit audioChanged();
		logInfo("Som do Remote Play por " + dispositivo.toStdString());
	});

	session_->setAudioCallbacks(
		[this](unsigned int channels, unsigned int rate) { audio_.configure(channels, rate); },
		[this](const int16_t *pcm, size_t samples) { audio_.write(pcm, samples); });

	session_->setLoginPinCallback([this](bool incorrect) {
		QMetaObject::invokeMethod(
			this, [this, incorrect]() { emit loginPinRequested(incorrect); },
			Qt::QueuedConnection);
	});

	// O comando físico só é lido durante a sessão.
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

		// A consola pode mandar menos do que lhe foi pedido e não avisar
		// ninguém. Uma PS4 que não seja Pro não faz 1080p: o chiaki baixa
		// o pedido sozinho (video_profile_auto_downgrade) e o stream sai a
		// 720p. Sem isto, quem escolheu 1080p fica a olhar para a mesma
		// imagem de sempre a pensar que a definição não serve para nada.
		if(height > 0 && resolution_ > 0 && height < resolution_)
		{
			const QString recebido = tr("%1×%2").arg(width).arg(height);
			if(resolution_ == 1080 && height == 720)
			{
				emit notify(tr("Remote Play"),
					tr("Pediste 1080p e a consola está a enviar %1. O Remote Play de uma "
					   "PS4 que não seja Pro não passa de 720p, e o pedido é baixado "
					   "automaticamente.").arg(recebido),
					false);
			}
			else
			{
				emit notify(tr("Remote Play"),
					tr("Pediste %1p e a consola está a enviar %2.")
						.arg(resolution_).arg(recebido),
					false);
			}
		}
	});

	// Os fps medidos, uma vez por segundo. É a única forma honesta de
	// responder a "isto está mesmo a 60?": contar o que chega ao ecrã.
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
	loadCredentials();
	emit registrationChanged();
	refreshConsole();
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
	accountId_ = QString::fromStdString(settings.streamAccountId);
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
		// O QKeySequence escreve estas em inglês e por extenso; no desenho
		// do teclado cabem melhor assim.
		case Qt::Key_Return: return QStringLiteral("Enter");
		case Qt::Key_Backspace: return QStringLiteral("⌫");
		case Qt::Key_Up: return QStringLiteral("↑");
		case Qt::Key_Down: return QStringLiteral("↓");
		case Qt::Key_Left: return QStringLiteral("←");
		case Qt::Key_Right: return QStringLiteral("→");
		case Qt::Key_Space: return tr("Espaço");
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

void StreamController::refreshConsole()
{
	// Diz sempre alguma coisa: quando falta o endereço e quando acaba. Um
	// "Procurar" calado parece não fazer nada.
	if(address_.isEmpty())
	{
		emit notify(tr("Procurar"),
			tr("Falta o endereço IP da consola. Define-o nas definições."), true);
		return;
	}
	if(searching_)
		return;
	searching_ = true;
	emit consoleChanged();

	const std::string address = address_.toStdString();
	std::thread([this, address]() {
		const HostInfo info = StreamDiscovery::probe(address, 1500);
		QMetaObject::invokeMethod(
			this,
			[this, info]() {
				searching_ = false;
				applyHost(info);
				if(!info.found)
				{
					emit notify(tr("Procurar"),
						tr("A consola em %1 não respondeu. Está ligada, na mesma rede, e com "
						   "o Remote Play activado?").arg(address_),
						true);
					return;
				}
				const QString nome = QString::fromStdString(info.name);
				if(consoleState() == QLatin1String("standby"))
					emit notify(tr("Procurar"),
						tr("%1 encontrada, em repouso. Usa \"Acordar consola\".")
							.arg(nome.isEmpty() ? address_ : nome),
						false);
				else
					emit notify(tr("Procurar"),
						tr("%1 encontrada e pronta.").arg(nome.isEmpty() ? address_ : nome),
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
			tr("Regista primeiro a consola: sem a chave de registo ela ignora o pedido."), true);
		return;
	}
	const std::string address = address_.toStdString();
	const uint64_t credential = credentials_.wakeupCredential();
	const bool ps5 = credentials_.ps5;
	std::thread([this, address, credential, ps5]() {
		std::string erro;
		const bool ok = StreamDiscovery::wakeup(address, credential, ps5, &erro);
		const QString mensagem = ok
			? tr("Pedido enviado. A consola demora alguns segundos a acordar.")
			: QString::fromStdString(erro);
		QMetaObject::invokeMethod(
			this,
			[this, ok, mensagem]() {
				emit notify(tr("Acordar consola"), mensagem, !ok);
				if(ok)
					refreshConsole();
			},
			Qt::QueuedConnection);
	}).detach();
}

QString StreamController::videoSummary() const
{
	QString texto;
	texto += QStringLiteral("  pedido       %1p, %2 fps%3\n")
		.arg(resolution_)
		.arg(fps_)
		.arg(bitrateKbps_ > 0 ? QStringLiteral(", %1 kbps").arg(bitrateKbps_)
							  : QStringLiteral(", bitrate automático"));
	if(frameWidth_ > 0)
	{
		texto += QStringLiteral("  a chegar     %1×%2, %3 fps medidos\n")
			.arg(frameWidth_).arg(frameHeight_).arg(measuredFps_);
		if(frameHeight_ < resolution_)
			texto += QStringLiteral("  => a consola baixou a resolução; uma PS4 que não "
									"seja Pro não passa de 720p\n");
	}
	else
	{
		texto += QStringLiteral("  a chegar     (nenhum fotograma ainda)\n");
	}
	texto += QStringLiteral("  descodificação %1\n")
		.arg(hardwareDecoder_ ? QStringLiteral("placa gráfica")
							  : QStringLiteral("processador"));
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
		emit notify(tr("Registo"), tr("Define primeiro o endereço IP da consola."), true);
		return;
	}

	// Sem descoberta não se sabe o alvo — e sem alvo o chiaki fala o
	// protocolo errado. Se ainda não se perguntou, pergunta-se fora da
	// thread da UI e volta-se aqui.
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
						emit notify(tr("Registo"),
							tr("A consola não respondeu. Confirma o IP e que está ligada "
							   "(não em repouso)."),
							true);
						return;
					}
					registerConsole(pinCopy, accountCopy);
				},
				Qt::QueuedConnection);
		}).detach();
		return;
	}

	// O que vem daqui pode estar em hexadecimal, em decimal ou já em
	// base64: converte-se aqui, para nenhum caminho da interface conseguir
	// mandar para a consola uma forma que ela não entende.
	const AccountId conta = parseAccountId(accountIdBase64.toStdString());
	if(!conta.valid)
	{
		emit notify(tr("Registo"), QString::fromStdString(conta.error), true);
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
		emit notify(tr("Registo"),
			tr("A consola respondeu mas não disse a versão de sistema. "
			   "Tenta \"Procurar\" outra vez."),
			true);
		return;
	}

	std::string erro;
	// Guarda-se sempre em base64, seja qual for a forma em que foi escrito:
	// é a única que a consola aceita, e assim não há duas coisas guardadas
	// com o mesmo nome.
	const QString accountParaGuardar = QString::fromStdString(conta.base64);
	const bool started = registration_->start(
		request,
		[this, accountParaGuardar](bool ok, StreamCredentials credentials, std::string error) {
			const QString mensagem = QString::fromStdString(error);
			QMetaObject::invokeMethod(
				this,
				[this, ok, credentials, mensagem, accountParaGuardar]() {
					registering_ = false;
					if(ok)
					{
						credentials_ = credentials;
						store_.save(credentials);
						// Guardado só depois de a consola o aceitar: um ID
						// errado não fica a estorvar a próxima tentativa.
						if(!accountParaGuardar.isEmpty())
						{
							accountId_ = accountParaGuardar;
							emit accountIdAccepted(accountParaGuardar);
							emit settingsApplied();
						}
						emit notify(tr("Registo"),
							tr("Consola registada. O Account ID fica guardado — da próxima "
							   "só precisas do PIN."), false);
					}
					else
					{
						emit notify(tr("Registo"), mensagem, true);
					}
					emit registrationChanged();
				},
				Qt::QueuedConnection);
		},
		&erro);

	if(!started)
	{
		emit notify(tr("Registo"), QString::fromStdString(erro), true);
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
	emit notify(tr("Remote Play"), tr("Registo apagado deste PC."), false);
}

void StreamController::startStream()
{
	if(streaming_)
	{
		emit notify(tr("Remote Play"), tr("A sessão já está a decorrer."), false);
		return;
	}
	if(address_.isEmpty())
	{
		emit notify(tr("Remote Play"),
			tr("Falta o endereço IP da consola. Define-o nas definições."), true);
		return;
	}
	if(!credentials_.valid)
	{
		emit notify(tr("Remote Play"),
			tr("Regista primeiro a consola: carrega em \"Registar consola\"."), true);
		return;
	}
	// Dizer que se está a ligar antes de bloquear a pensar: o clique tem de
	// ter resposta imediata.
	sessionState_ = QStringLiteral("connecting");
	sessionDetail_ = tr("A ligar a %1…").arg(address_);
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
		sessionDetail_ = QString::fromStdString(erro);
		emit sessionChanged();
		emit notify(tr("Remote Play"), sessionDetail_, true);
	}
}

void StreamController::stopStream()
{
	// O microfone não sobrevive à sessão: se ficasse aberto, a luz do
	// microfone ficaria acesa sem nada do outro lado.
	microphone_.stop();
	session_->stopMicrophone();
	emit microphoneChanged();

	// O stop do chiaki espera pelas threads dele; fora da thread da UI para
	// a janela não congelar.
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
		return QStringLiteral("desligado");
	return session_->microphoneMuted() ? QStringLiteral("em-silencio")
									   : QStringLiteral("a-falar");
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
		emit notify(tr("Microfone"), tr("Liga primeiro o Remote Play."), true);
		return;
	}

	// A consola primeiro: se ela recusar, não vale a pena abrir o
	// microfone da máquina e deixar a luz acesa sem se enviar nada.
	std::string erro;
	if(!session_->startMicrophone(&erro))
	{
		emit notify(tr("Microfone"),
			tr("A consola não aceitou o microfone: %1").arg(QString::fromStdString(erro)), true);
		return;
	}

	QString erroCaptura;
	if(!microphone_.start(&erroCaptura))
	{
		session_->stopMicrophone();
		emit notify(tr("Microfone"), erroCaptura, true);
		emit microphoneChanged();
		return;
	}
	emit notify(tr("Microfone"),
		tr("A falar para a consola (%1).").arg(microphone_.deviceName()), false);
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
	// Ao perder o foco larga-se tudo: senão uma tecla fica presa e o
	// personagem continua a andar sozinho do outro lado.
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
