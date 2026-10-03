// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/session.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

	// Which PlayStation button each physical button presses. Physical
	// buttons go by SDL's names ("a", "b", "dpup", "leftshoulder"…), the
	// buttons they press by the key map's action ids ("cross", "l1"…).
	// The triggers are axes and stay L2/R2.
	// (See PadMap.) Physical buttons not given keep their default.
	using ButtonMap = std::map<std::string, std::string>;
	void setButtonMap(const ButtonMap &overrides);
	ButtonMap buttonMap() const { return map_; }

signals:
	// Emitted only when something changes.
	void stateChanged(const StreamSession::ControllerState &state);
	void connectedChanged(const QString &name);
	// A finger (0 or 1) on the controller's own touchpad, where it is
	// (0 to 1 both ways). PS2 games read it: a click on the left half is
	// Select, on the right half Start.
	void touchChanged(int finger, bool down, double x, double y);
	// A physical button went down (its SDL name), whatever it is mapped to:
	// the key map window learns a new mapping from it.
	void buttonDown(const QString &physical);

private:
	void poll();

	QTimer timer_;
	QString name_;
	void *controller_ = nullptr; // SDL_GameController*, without dragging SDL in here
	StreamSession::ControllerState last_;
	bool haveLast_ = false;
	bool initialised_ = false;
	bool rumbleEnabled_ = true;
	ButtonMap map_;
	std::set<std::string> down_;
};

} // namespace orbislink
