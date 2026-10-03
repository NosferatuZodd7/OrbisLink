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

// Remote Play as seen from QML.
//
// Everything chiaki does runs on its threads; here the results are
// forwarded to the UI thread before touching properties.
class StreamController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool available READ available CONSTANT)
	Q_PROPERTY(QString consoleState READ consoleState NOTIFY consoleChanged)
	// True while the check is talking to the console, so the button
	// can show it is doing something.
	Q_PROPERTY(bool searching READ searching NOTIFY consoleChanged)
	// The step the one-click connection is at: "", "checking", "waking".
	Q_PROPERTY(QString connectStage READ connectStage NOTIFY connectStageChanged)
	Q_PROPERTY(QString consoleName READ consoleName NOTIFY consoleChanged)
	// PS5 or PS4, from what the console said in discovery.
	Q_PROPERTY(bool consolePs5 READ consolePs5 NOTIFY consoleChanged)
	Q_PROPERTY(QString runningApp READ runningApp NOTIFY consoleChanged)
	// The title ID of the game open on the console ("" at the home screen):
	// its saves are not touched while it runs.
	Q_PROPERTY(QString runningAppTitleId READ runningAppTitleId NOTIFY consoleChanged)
	Q_PROPERTY(bool registered READ registered NOTIFY registrationChanged)
	// The host-id (MAC) of the console in use, once it has answered.
	Q_PROPERTY(QString hostId READ hostId NOTIFY consoleChanged)
	// Every console this PC is registered on: [{ hostId, name, ps5 }].
	Q_PROPERTY(QVariantList registrations READ registrations NOTIFY registrationChanged)
	Q_PROPERTY(bool registering READ registering NOTIFY registrationChanged)
	Q_PROPERTY(QString sessionState READ sessionState NOTIFY sessionChanged)
	Q_PROPERTY(QString sessionDetail READ sessionDetail NOTIFY sessionChanged)
	Q_PROPERTY(bool streaming READ streaming NOTIFY sessionChanged)
	Q_PROPERTY(int frameWidth READ frameWidth NOTIFY videoChanged)
	Q_PROPERTY(int frameHeight READ frameHeight NOTIFY videoChanged)
	// The measured fps, not the requested ones. What is asked of the console
	// and what it sends can differ, and the second is what you see.
	Q_PROPERTY(int measuredFps READ measuredFps NOTIFY videoChanged)
	Q_PROPERTY(orbislink::VideoBridge *video READ video CONSTANT)
	Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
	// "stopped", "playing", "no-device" or "error" — so the stream does not
	// go silent without an explanation.
	Q_PROPERTY(QString audioState READ audioState NOTIFY audioChanged)
	Q_PROPERTY(QString audioDevice READ audioDevice NOTIFY audioChanged)
	// Microphone: "off", "talking" or "muted". There are deliberately
	// three states and not a boolean — whoever is being heard has to
	// see it at a glance.
	Q_PROPERTY(QString microphoneState READ microphoneState NOTIFY microphoneChanged)
	Q_PROPERTY(QString microphoneDevice READ microphoneDevice NOTIFY microphoneChanged)
	Q_PROPERTY(QString gamepadName READ gamepadName NOTIFY gamepadChanged)
	Q_PROPERTY(bool hardwareDecoder READ hardwareDecoder NOTIFY sessionChanged)
	Q_PROPERTY(bool fullscreenOnConnect READ fullscreenOnConnect NOTIFY settingsApplied)
	Q_PROPERTY(QString savedAccountId READ accountId NOTIFY settingsApplied)
	Q_PROPERTY(QVariantMap keyBindings READ keyBindings NOTIFY keyBindingsChanged)
	// Physical controller button → action ("a" → "cross"…), all of them.
	Q_PROPERTY(QVariantMap padBindings READ padBindings NOTIFY keyBindingsChanged)
	// The actions the physical controller is pressing right now ("cross",
	// "l2", "lstick_left"…), for the key map window to light them up.
	Q_PROPERTY(QStringList padPressed READ padPressed NOTIFY padPressedChanged)
	// The state of the other saved consoles, by address:
	// { state, name, ps5, registered, hostId }.
	Q_PROPERTY(QVariantMap consoleStates READ consoleStates NOTIFY consoleStatesChanged)
	// The network search of the "Add console" window.
	Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
	Q_PROPERTY(QVariantList scanResults READ scanResults NOTIFY scanChanged)

public:
	explicit StreamController(QObject *parent = nullptr);
	~StreamController() override;

	bool available() const { return true; }
	QString consoleState() const { return consoleState_; }
	bool searching() const { return searching_; }
	QString consoleName() const { return consoleName_; }
	bool consolePs5() const { return host_.ps5; }
	QString hostId() const { return QString::fromStdString(host_.id); }
	QVariantList registrations() const;
	QString runningApp() const { return runningApp_; }
	QString runningAppTitleId() const { return runningAppTitleId_; }
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
	// For diagnostics: where the sound stops, stretch by stretch.
	QString audioPipeline() const { return audio_.pipelineSummary(); }
	// What was asked of the console and what it is sending. They differ
	// more often than one would think.
	QString videoSummary() const;
	QString microphoneState() const;
	QString microphoneDevice() const { return microphone_.deviceName(); }
	QString gamepadName() const { return gamepad_.name(); }
	bool hardwareDecoder() const { return hardwareDecoder_; }
	bool fullscreenOnConnect() const { return fullscreenOnConnect_; }
	void setMuted(bool muted);
	// Turns capture on and off. Muting keeps the connection open and only
	// stops sending, which is what the console expects from a "mute".
	Q_INVOKABLE void setMicrophoneEnabled(bool enabled);
	Q_INVOKABLE void toggleMicrophone();
	Q_INVOKABLE void setMicrophoneMuted(bool muted);

	// Comes from the OrbisLink settings.
	void setAddress(const QString &address);
	void applySettings(const Settings &settings);
	QString accountId() const { return accountId_; }

	// Asks the console what state it is in (discovery) and reports the result
	// in a notification: this is what runs when the refresh is requested.
	Q_INVOKABLE void refreshConsole();
	// Wakes a console in rest mode.
	Q_INVOKABLE void wakeUp();
	// Registers this PC on the console. The Account ID may come in hexadecimal,
	// decimal or base64 — the conversion is done here, so no path in the
	// interface can send the console a form it refuses.
	Q_INVOKABLE void registerConsole(const QString &pin, const QString &accountIdBase64);
	// The same Account ID in the three forms, so the interface can show the
	// other two while one of them is being typed.
	Q_INVOKABLE QVariantMap accountIdForms(const QString &message) const;
	// For tools that show the raw bytes, and therefore the reverse of the
	// number.
	Q_INVOKABLE QVariantMap accountIdReversed(const QString &message) const;
	Q_INVOKABLE void cancelRegistration();
	Q_INVOKABLE void forgetConsole();
	// Removes this PC's registration on the console with that host-id.
	Q_INVOKABLE void forgetRegistration(const QString &hostId);
	Q_INVOKABLE void startStream();
	// One-click connect: asks the console how it is, wakes it if it is in
	// rest mode, waits for it to be ready and connects. If it is not
	// registered yet, asks for registration (registrationNeeded).
	Q_INVOKABLE void connectOneClick();
	Q_INVOKABLE void cancelOneClick();
	QString connectStage() const { return connectStage_; }
	Q_INVOKABLE void stopStream();
	// The console asked for the account PIN (not the registration one).
	Q_INVOKABLE void sendLoginPin(const QString &pin);

	// Keyboard: QML hands over the keys while the video has focus.
	// They return true when the key was consumed by the controller.
	Q_INVOKABLE bool keyPressed(int key);
	Q_INVOKABLE bool keyReleased(int key);
	Q_INVOKABLE void releaseAllKeys();

	// The keyboard-as-controller keys, for the map window: action →
	// key code (Qt::Key).
	QVariantMap keyBindings() const;
	// Changes an action's key (if it is taken, the two swap).
	// Returns false for a key that cannot be used (Esc, F11).
	Q_INVOKABLE bool setKeyBinding(const QString &action, int key);
	Q_INVOKABLE void resetKeyBindings();
	QVariantMap padBindings() const;
	QStringList padPressed() const { return padPressed_; }
	// Moves `action` to the physical button `physical`; the two swap.
	Q_INVOKABLE void setPadBinding(const QString &action, const QString &physical);
	// Reads the controller while the key map window is open, even without
	// a session, so its buttons light up there.
	Q_INVOKABLE void setInputPreview(bool on);
	// The key name as the system writes it ("Enter", "Space", "Q").
	Q_INVOKABLE QString keyName(int key) const;

	// Asks each address how its console is, in the background. The
	// result arrives through consoleStates.
	Q_INVOKABLE void probeConsoles(const QStringList &addresses);
	QVariantMap consoleStates() const { return consoleStates_; }

	// Sweeps the local network for consoles.
	Q_INVOKABLE void scanNetwork();
	bool scanning() const { return scanning_; }
	QVariantList scanResults() const { return scanResults_; }

	// Touchpad from the mouse. The coordinates come normalised (0 to 1)
	// so QML does not need to know the size of the controller's touchpad.
	Q_INVOKABLE void touchBegin(double x, double y);
	Q_INVOKABLE void touchMove(double x, double y);
	Q_INVOKABLE void touchEnd();
	Q_INVOKABLE bool touchpadFromMouse() const { return touchpadFromMouse_; }

signals:
	void connectStageChanged();
	// The console answered but this PC is not registered on it yet.
	void registrationNeeded();
	// The console accepted this PC: the registration dialog closes and the
	// window goes back to the console's card.
	void registrationSucceeded();
	void consoleChanged();
	void registrationChanged();
	void sessionChanged();
	void videoChanged();
	void mutedChanged();
	void audioChanged();
	void microphoneChanged();
	// Registration succeeded and this Account ID is good. Whoever stores the
	// settings connects to this, so it does not have to be typed again.
	void accountIdAccepted(const QString &accountIdBase64);
	void gamepadChanged();
	void settingsApplied();
	void keyBindingsChanged();
	void padPressedChanged();
	// A physical controller button went down (its SDL name).
	void padButtonDown(const QString &physical);
	void consoleStatesChanged();
	void scanChanged();
	// Request to save the keys in the settings; whoever stores them connects
	// here, and the new map comes back through applySettings().
	void keyBindingsEdited(const std::map<std::string, int> &bindings);
	void padBindingsEdited(const std::map<std::string, std::string> &bindings);
	void notify(const QString &title, const QString &message, bool error);
	void loginPinRequested(bool incorrect);

private:
	void applyHost(const HostInfo &info);
	void loadCredentials();
	// The same as refreshConsole(), with or without a notification at the end.
	void probe(bool reportResult);

	QString address_;
	QString consoleState_ = QStringLiteral("unknown");
	QString consoleName_;
	QString runningApp_;
	QString runningAppTitleId_;
	QString sessionState_ = QStringLiteral("idle");
	QString sessionDetail_;
	bool registering_ = false;
	bool streaming_ = false;
	int frameWidth_ = 0;
	int frameHeight_ = 0;
	// The size of the last session's picture, kept for the diagnostics.
	int lastFrameWidth_ = 0;
	int lastFrameHeight_ = 0;
	bool lastHardwareDecoder_ = false;
	class QTimer *fpsTimer_ = nullptr;
	// One-click connection.
	void setConnectStage(const QString &stage);
	void oneClickDecide(const HostInfo &info);
	void oneClickPoll();
	QString connectStage_;
	class QTimer *wakeTimer_ = nullptr;
	int wakeAttempts_ = 0;
	// "Remote Play already in use" right after a session ended: the console
	// is still closing the old one, so the connection is tried again a few
	// times before it counts as a failure.
	class QTimer *inUseRetryTimer_ = nullptr;
	int inUseRetries_ = 0;
	bool retryingInUse_ = false;
	qint64 lastSessionEndMs_ = 0;
	bool retryAfterInUse();
	quint64 oneClickRun_ = 0;
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

	// What the keyboard and the controller hold, sent to the console as one.
	void sendInput();
	void padTouch(int finger, bool down, double x, double y);
	void resetInput();
	StreamSession::ControllerState keyState_;
	StreamSession::ControllerState padState_;
	QStringList padPressed_;
	bool inputPreview_ = false;
	void updatePadPressed();
	int padTouchIds_[2] = { -1, -1 };
	// A touchpad click with no finger on the pad (the T key, or a controller
	// whose touches do not come in): a finger is put on the left half first
	// and the click follows a moment later, as on a real pad — PS2 games
	// ignore a click they cannot place (left half: Select).
	bool clickDown_ = false;
	bool clickArmed_ = false;
	int clickTouch_ = -1;
	quint64 clickRun_ = 0;
	QString accountId_;
	bool searching_ = false;
	bool notifyWhenDone_ = false;

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
	QVariantMap consoleStates_;
	bool probing_ = false;
	bool scanning_ = false;
	QVariantList scanResults_;
	QVariantMap describeHost(const HostInfo &info);
};

} // namespace orbislink
