// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/gamepad.h"

#include "orbislink/common/log.h"
#include "orbislink/qt/input_map.h"

#ifdef ORBISLINK_HAS_GAMEPAD
#include <chiaki/controller.h>

#include <SDL.h>
#endif

namespace orbislink {

void Gamepad::setButtonMap(const ButtonMap &overrides) { map_ = PadMap::effective(overrides); }

bool Gamepad::supported()
{
#ifdef ORBISLINK_HAS_GAMEPAD
	return true;
#else
	return false;
#endif
}

#ifdef ORBISLINK_HAS_GAMEPAD

namespace {

// The chiaki button an action id presses (0 for none).
uint32_t buttonOfAction(const std::string &action)
{
	for(const KeyboardMap::Action &candidate : KeyboardMap::actions())
		if(action == candidate.id)
			return candidate.button;
	return 0;
}

// SDL triggers range from 0 to 32767; the console expects 0 to 255.
uint8_t triggerFrom(int16_t value)
{
	if(value <= 0)
		return 0;
	return static_cast<uint8_t>(value / 129);
}

bool equals(const StreamSession::ControllerState &a, const StreamSession::ControllerState &b)
{
	return a.buttons == b.buttons && a.l2 == b.l2 && a.r2 == b.r2 && a.leftX == b.leftX
		&& a.leftY == b.leftY && a.rightX == b.rightX && a.rightY == b.rightY;
}

} // namespace

Gamepad::Gamepad(QObject *parent) : QObject(parent)
{
	setButtonMap({});
	// 8 ms: 125 reads per second, twice the frame rate, which is more
	// than enough without overworking the CPU.
	timer_.setInterval(8);
	connect(&timer_, &QTimer::timeout, this, &Gamepad::poll);
}

Gamepad::~Gamepad() { stop(); }

void Gamepad::start()
{
	if(timer_.isActive())
		return;

	if(!initialised_)
	{
		// No video or audio: just controllers. SDL opens no window.
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		// SDL would take over SIGINT/SIGTERM for its own quit event, and
		// the app would no longer close when asked to.
		SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
		if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		{
			logWarning(std::string("Remote Play: SDL did not start (") + SDL_GetError()
				+ "); the controller will not work.");
			return;
		}
		initialised_ = true;
	}

	timer_.start();
	poll();
}

void Gamepad::stop()
{
	timer_.stop();
	if(controller_)
	{
		SDL_GameControllerClose(static_cast<SDL_GameController *>(controller_));
		controller_ = nullptr;
		name_.clear();
		emit connectedChanged(name_);
	}
	haveLast_ = false;
	down_.clear();
	if(initialised_)
	{
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
		initialised_ = false;
	}
}

void Gamepad::rumble(quint8 left, quint8 right)
{
	if(!rumbleEnabled_ || !controller_)
		return;
	// SDL uses 0-65535; the console sends 0-255.
	const Uint16 low = static_cast<Uint16>(left) * 257;
	const Uint16 high = static_cast<Uint16>(right) * 257;
	// 200 ms renewed on every request: if the console stops asking, the
	// controller stops by itself instead of rumbling forever.
	SDL_GameControllerRumble(static_cast<SDL_GameController *>(controller_), low, high, 200);
}

void Gamepad::poll()
{
	SDL_Event event;
	while(SDL_PollEvent(&event))
	{
		// Plugging and unplugging the controller mid-game has to work.
		if(event.type == SDL_CONTROLLERDEVICEREMOVED && controller_
			&& event.cdevice.which
				== SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(
					static_cast<SDL_GameController *>(controller_))))
		{
			SDL_GameControllerClose(static_cast<SDL_GameController *>(controller_));
			controller_ = nullptr;
			name_.clear();
			logInfo("Remote Play: controller disconnected.");
			emit connectedChanged(name_);
			// Release everything, otherwise a button stays stuck on the other side.
			emit stateChanged({});
			for(int finger = 0; finger < 2; ++finger)
				emit touchChanged(finger, false, 0.0, 0.0);
			haveLast_ = false;
			down_.clear();
		}
#if SDL_VERSION_ATLEAST(2, 0, 14)
		if((event.type == SDL_CONTROLLERTOUCHPADDOWN || event.type == SDL_CONTROLLERTOUCHPADMOTION
			   || event.type == SDL_CONTROLLERTOUCHPADUP)
			&& controller_ && event.ctouchpad.touchpad == 0
			&& event.ctouchpad.finger >= 0 && event.ctouchpad.finger < 2
			&& event.ctouchpad.which
				== SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(
					static_cast<SDL_GameController *>(controller_))))
		{
			emit touchChanged(event.ctouchpad.finger, event.type != SDL_CONTROLLERTOUCHPADUP,
				event.ctouchpad.x, event.ctouchpad.y);
		}
#endif
	}

	if(!controller_)
	{
		for(int i = 0; i < SDL_NumJoysticks(); ++i)
		{
			if(!SDL_IsGameController(i))
				continue;
			SDL_GameController *opened = SDL_GameControllerOpen(i);
			if(!opened)
				continue;
			controller_ = opened;
			const char *name = SDL_GameControllerName(opened);
			name_ = QString::fromUtf8(name ? name : "controller");
			logInfo("Remote Play: controller connected — " + name_.toStdString());
			emit connectedChanged(name_);
			break;
		}
		if(!controller_)
			return;
	}

	auto *pad = static_cast<SDL_GameController *>(controller_);
	StreamSession::ControllerState state;
	std::set<std::string> down;
	for(int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button)
	{
		const auto which = static_cast<SDL_GameControllerButton>(button);
		if(!SDL_GameControllerGetButton(pad, which))
			continue;
		const char *name = SDL_GameControllerGetStringForButton(which);
		if(!name)
			continue;
		down.insert(name);
		const auto mapped = map_.find(name);
		if(mapped != map_.end())
			state.buttons |= buttonOfAction(mapped->second);
	}
	for(const std::string &name : down)
		if(!down_.count(name))
			emit buttonDown(QString::fromStdString(name));
	down_ = std::move(down);
	state.leftX = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
	state.leftY = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
	state.rightX = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
	state.rightY = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
	state.l2 = triggerFrom(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
	state.r2 = triggerFrom(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

	if(haveLast_ && equals(state, last_))
		return;
	last_ = state;
	haveLast_ = true;
	emit stateChanged(state);
}

#else // no SDL

Gamepad::Gamepad(QObject *parent) : QObject(parent) { setButtonMap({}); }
Gamepad::~Gamepad() = default;
void Gamepad::start() {}
void Gamepad::stop() {}
void Gamepad::poll() {}
void Gamepad::rumble(quint8, quint8) {}

#endif

} // namespace orbislink
