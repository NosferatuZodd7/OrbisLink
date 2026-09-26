// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/qt/audio_input.h"
#include "orbislink/qt/audio_output.h"
#include "orbislink/qt/gamepad.h"
#include "orbislink/qt/input_map.h"
#include "orbislink/qt/video_bridge.h"
#include "orbislink/stream/credentials.h"
#include "orbislink/stream/discovery.h"
#include "orbislink/stream/registration.h"
#include "orbislink/settings/settings_store.h"
#include "orbislink/stream/session.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>

namespace orbislink {

// O Remote Play visto do QML.
//
// Tudo o que o chiaki faz corre nas threads dele; aqui os resultados são
// reencaminhados para a thread da UI antes de tocarem em propriedades.
class StreamController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool available READ available CONSTANT)
	Q_PROPERTY(QString consoleState READ consoleState NOTIFY consoleChanged)
	// Verdadeiro enquanto o "Procurar" está a falar com a consola, para o
	// botão poder dizer que está a fazer alguma coisa.
	Q_PROPERTY(bool searching READ searching NOTIFY consoleChanged)
	Q_PROPERTY(QString consoleName READ consoleName NOTIFY consoleChanged)
	// PS5 ou PS4, pelo que a consola disse na descoberta.
	Q_PROPERTY(bool consolePs5 READ consolePs5 NOTIFY consoleChanged)
	Q_PROPERTY(QString runningApp READ runningApp NOTIFY consoleChanged)
	Q_PROPERTY(bool registered READ registered NOTIFY registrationChanged)
	Q_PROPERTY(bool registering READ registering NOTIFY registrationChanged)
	Q_PROPERTY(QString sessionState READ sessionState NOTIFY sessionChanged)
	Q_PROPERTY(QString sessionDetail READ sessionDetail NOTIFY sessionChanged)
	Q_PROPERTY(bool streaming READ streaming NOTIFY sessionChanged)
	Q_PROPERTY(int frameWidth READ frameWidth NOTIFY videoChanged)
	Q_PROPERTY(int frameHeight READ frameHeight NOTIFY videoChanged)
	// Os fps medidos, não os pedidos. O que se pede à consola e o que ela
	// manda podem ser coisas diferentes, e é o segundo que se vê.
	Q_PROPERTY(int measuredFps READ measuredFps NOTIFY videoChanged)
	Q_PROPERTY(orbislink::VideoBridge *video READ video CONSTANT)
	Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
	// "parado", "a-tocar", "sem-dispositivo" ou "erro" — para o stream não
	// ficar mudo sem explicação.
	Q_PROPERTY(QString audioState READ audioState NOTIFY audioChanged)
	Q_PROPERTY(QString audioDevice READ audioDevice NOTIFY audioChanged)
	// Microfone: "desligado", "a-falar" ou "em-silencio". É de propósito
	// que há três estados e não um booleano — quem está a ser ouvido tem de
	// o ver de relance.
	Q_PROPERTY(QString microphoneState READ microphoneState NOTIFY microphoneChanged)
	Q_PROPERTY(QString microphoneDevice READ microphoneDevice NOTIFY microphoneChanged)
	Q_PROPERTY(QString gamepadName READ gamepadName NOTIFY gamepadChanged)
	Q_PROPERTY(bool hardwareDecoder READ hardwareDecoder NOTIFY sessionChanged)
	Q_PROPERTY(bool fullscreenOnConnect READ fullscreenOnConnect NOTIFY settingsApplied)
	Q_PROPERTY(QString savedAccountId READ accountId NOTIFY settingsApplied)
	Q_PROPERTY(QVariantMap keyBindings READ keyBindings NOTIFY keyBindingsChanged)

public:
	explicit StreamController(QObject *parent = nullptr);
	~StreamController() override;

	bool available() const { return true; }
	QString consoleState() const { return consoleState_; }
	bool searching() const { return searching_; }
	QString consoleName() const { return consoleName_; }
	bool consolePs5() const { return host_.ps5; }
	QString runningApp() const { return runningApp_; }
	bool registered() const { return credentials_.valid; }
	bool registering() const { return registering_; }
	QString sessionState() const { return sessionState_; }
	QString sessionDetail() const { return sessionDetail_; }
	bool streaming() const { return streaming_; }
	int frameWidth() const { return frameWidth_; }
	int frameHeight() const { return frameHeight_; }
	int measuredFps() const { return measuredFps_; }
	VideoBridge *video() { return &video_; }
	bool muted() const { return audio_.muted(); }
	QString audioState() const { return audio_.state(); }
	QString audioDevice() const { return audio_.deviceName(); }
	// Para o diagnóstico: onde é que o som para, troço a troço.
	QString audioPipeline() const { return audio_.pipelineSummary(); }
	// O que foi pedido à consola e o que ela está a mandar. São coisas
	// diferentes mais vezes do que se pensa.
	QString videoSummary() const;
	QString microphoneState() const;
	QString microphoneDevice() const { return microphone_.deviceName(); }
	QString gamepadName() const { return gamepad_.name(); }
	bool hardwareDecoder() const { return hardwareDecoder_; }
	bool fullscreenOnConnect() const { return fullscreenOnConnect_; }
	void setMuted(bool muted);
	// Liga e desliga a captura. O silêncio mantém a ligação aberta e só
	// deixa de enviar, que é o que a consola espera de um "mute".
	Q_INVOKABLE void setMicrophoneEnabled(bool enabled);
	Q_INVOKABLE void toggleMicrophone();
	Q_INVOKABLE void setMicrophoneMuted(bool muted);

	// Vem das definições do OrbisLink.
	void setAddress(const QString &address);
	void applySettings(const Settings &settings);
	QString accountId() const { return accountId_; }

	// Pergunta à consola em que estado está (descoberta na 987/UDP).
	Q_INVOKABLE void refreshConsole();
	// Acorda uma consola em repouso.
	Q_INVOKABLE void wakeUp();
	// Regista este PC na consola. O Account ID pode vir em hexadecimal, em
	// decimal ou em base64 — a conversão é feita aqui, para nenhum caminho
	// da interface conseguir mandar à consola uma forma que ela recusa.
	Q_INVOKABLE void registerConsole(const QString &pin, const QString &accountIdBase64);
	// O mesmo Account ID nas três formas, para a interface poder mostrar as
	// outras duas enquanto se escreve numa delas.
	Q_INVOKABLE QVariantMap accountIdForms(const QString &texto) const;
	// Para as ferramentas que mostram os bytes em bruto, e portanto ao
	// contrário do número.
	Q_INVOKABLE QVariantMap accountIdReversed(const QString &texto) const;
	Q_INVOKABLE void cancelRegistration();
	Q_INVOKABLE void forgetConsole();
	Q_INVOKABLE void startStream();
	Q_INVOKABLE void stopStream();
	// A consola pediu o PIN da conta (não é o do registo).
	Q_INVOKABLE void sendLoginPin(const QString &pin);

	// Teclado: o QML entrega as teclas enquanto o vídeo tiver o foco.
	// Devolvem true quando a tecla foi consumida pelo comando.
	Q_INVOKABLE bool keyPressed(int key);
	Q_INVOKABLE bool keyReleased(int key);
	Q_INVOKABLE void releaseAllKeys();

	// As teclas do teclado como comando, para a janela do mapa: acção →
	// código da tecla (Qt::Key).
	QVariantMap keyBindings() const;
	// Muda a tecla de uma acção (se estiver ocupada, as duas trocam).
	// Devolve false para uma tecla que não se pode usar (Esc, F11).
	Q_INVOKABLE bool setKeyBinding(const QString &action, int key);
	Q_INVOKABLE void resetKeyBindings();
	// O nome da tecla como o sistema o escreve ("Enter", "Espaço", "Q").
	Q_INVOKABLE QString keyName(int key) const;

	// Touchpad a partir do rato. As coordenadas vêm normalizadas (0 a 1)
	// para o QML não ter de saber o tamanho do touchpad do comando.
	Q_INVOKABLE void touchBegin(double x, double y);
	Q_INVOKABLE void touchMove(double x, double y);
	Q_INVOKABLE void touchEnd();
	Q_INVOKABLE bool touchpadFromMouse() const { return touchpadFromMouse_; }

signals:
	void consoleChanged();
	void registrationChanged();
	void sessionChanged();
	void videoChanged();
	void mutedChanged();
	void audioChanged();
	void microphoneChanged();
	// O registo correu bem e este Account ID presta. Quem guarda as
	// definições liga-se a isto, para não ser preciso escrevê-lo outra vez.
	void accountIdAccepted(const QString &accountIdBase64);
	void gamepadChanged();
	void settingsApplied();
	void keyBindingsChanged();
	// Pedido para gravar as teclas nas definições; quem as guarda liga-se
	// aqui, e o mapa novo volta por applySettings().
	void keyBindingsEdited(const std::map<std::string, int> &bindings);
	void notify(const QString &title, const QString &message, bool error);
	void loginPinRequested(bool incorrect);

private:
	void applyHost(const HostInfo &info);
	void loadCredentials();

	QString address_;
	QString consoleState_ = QStringLiteral("unknown");
	QString consoleName_;
	QString runningApp_;
	QString sessionState_ = QStringLiteral("idle");
	QString sessionDetail_;
	bool registering_ = false;
	bool streaming_ = false;
	int frameWidth_ = 0;
	int frameHeight_ = 0;
	class QTimer *fpsTimer_ = nullptr;
	qint64 lastFrameCount_ = 0;
	int measuredFps_ = 0;
	int resolution_ = 720;
	int fps_ = 60;
	int bitrateKbps_ = 0;
	bool wantHardware_ = true;
	bool hardwareDecoder_ = false;
	bool fullscreenOnConnect_ = false;
	bool rumbleEnabled_ = true;
	bool touchpadFromMouse_ = true;
	int touchId_ = -1;
	QString accountId_;
	bool searching_ = false;

	HostInfo host_;
	StreamCredentials credentials_;
	CredentialStore store_;
	VideoBridge video_;
	AudioOutput audio_;
	AudioInput microphone_;
	Gamepad gamepad_;
	std::unique_ptr<StreamRegistration> registration_;
	std::unique_ptr<StreamSession> session_;
	KeyboardMap keyboard_;
};

} // namespace orbislink
