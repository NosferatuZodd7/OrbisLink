// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/session.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace orbislink {

// Physical controller (DualShock 4, DualSense, or any other SDL
// recognises) translated into the state the console expects.
//
// SDL is optional: without it the application builds and works, only
// the controller does not come in and the keyboard is the only way to play.
class Gamepad : public QObject
{
	Q_OBJECT

public:
	explicit Gamepad(QObject *parent = nullptr);
	~Gamepad() override;

	// True when the application was built with controller support.
	static bool supported();

	// Starts and stops reading the controller. Only meaningful during a session.
	void start();
	void stop();

	// Name of the connected controller, empty if there is none.
	QString name() const { return name_; }

	// Rumble requested by the console. 0-255 per motor; the duration is short
	// and renewed on every request, so it stops by itself if the console goes quiet.
	void rumble(quint8 left, quint8 right);
	void setRumbleEnabled(bool enabled) { rumbleEnabled_ = enabled; }

signals:
	// Emitted only when something changes.
	void stateChanged(const StreamSession::ControllerState &state);
	void connectedChanged(const QString &name);

private:
	void poll();

	QTimer timer_;
	QString name_;
	void *controller_ = nullptr; // SDL_GameController*, without dragging SDL in here
	StreamSession::ControllerState last_;
	bool haveLast_ = false;
	bool initialised_ = false;
	bool rumbleEnabled_ = true;
};

} // namespace orbislink
