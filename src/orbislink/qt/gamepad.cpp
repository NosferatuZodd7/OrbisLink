// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/gamepad.h"

#include "orbislink/common/log.h"

#ifdef ORBISLINK_HAS_GAMEPAD
#include <chiaki/controller.h>

#include <SDL.h>
#endif

namespace orbislink {

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

uint32_t buttonFor(SDL_GameControllerButton button)
{
	switch(button)
	{
		case SDL_CONTROLLER_BUTTON_A: return CHIAKI_CONTROLLER_BUTTON_CROSS;
		case SDL_CONTROLLER_BUTTON_B: return CHIAKI_CONTROLLER_BUTTON_MOON;
		case SDL_CONTROLLER_BUTTON_X: return CHIAKI_CONTROLLER_BUTTON_BOX;
		case SDL_CONTROLLER_BUTTON_Y: return CHIAKI_CONTROLLER_BUTTON_PYRAMID;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
		case SDL_CONTROLLER_BUTTON_DPAD_UP: return CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return CHIAKI_CONTROLLER_BUTTON_L1;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return CHIAKI_CONTROLLER_BUTTON_R1;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK: return CHIAKI_CONTROLLER_BUTTON_L3;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return CHIAKI_CONTROLLER_BUTTON_R3;
		case SDL_CONTROLLER_BUTTON_START: return CHIAKI_CONTROLLER_BUTTON_OPTIONS;
		case SDL_CONTROLLER_BUTTON_BACK: return CHIAKI_CONTROLLER_BUTTON_SHARE;
		case SDL_CONTROLLER_BUTTON_GUIDE: return CHIAKI_CONTROLLER_BUTTON_PS;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLER_BUTTON_TOUCHPAD: return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
#endif
		default: return 0;
	}
}

// Os gatilhos do SDL vão de 0 a 32767; a consola espera 0 a 255.
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
	// 8 ms: 125 leituras por segundo, o dobro da cadência de imagem, que
	// chega e sobra sem dar trabalho de mais ao processador.
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
		// Sem vídeo nem áudio: só os comandos. O SDL não abre janela nenhuma.
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		{
			logWarning(std::string("Remote Play: o SDL não arrancou (") + SDL_GetError()
				+ "); o comando não vai funcionar.");
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
	// O SDL usa 0-65535; a consola manda 0-255.
	const Uint16 baixa = static_cast<Uint16>(left) * 257;
	const Uint16 alta = static_cast<Uint16>(right) * 257;
	// 200 ms renovados a cada pedido: se a consola parar de pedir, o
	// comando pára sozinho em vez de ficar a vibrar para sempre.
	SDL_GameControllerRumble(static_cast<SDL_GameController *>(controller_), baixa, alta, 200);
}

void Gamepad::poll()
{
	SDL_Event event;
	while(SDL_PollEvent(&event))
	{
		// Ligar e desligar o comando a meio do jogo tem de funcionar.
		if(event.type == SDL_CONTROLLERDEVICEREMOVED && controller_
			&& event.cdevice.which
				== SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(
					static_cast<SDL_GameController *>(controller_))))
		{
			SDL_GameControllerClose(static_cast<SDL_GameController *>(controller_));
			controller_ = nullptr;
			name_.clear();
			logInfo("Remote Play: comando desligado.");
			emit connectedChanged(name_);
			// Larga tudo, senão fica um botão preso do outro lado.
			emit stateChanged({});
			haveLast_ = false;
		}
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
			const char *nome = SDL_GameControllerName(opened);
			name_ = QString::fromUtf8(nome ? nome : "comando");
			logInfo("Remote Play: comando ligado — " + name_.toStdString());
			emit connectedChanged(name_);
			break;
		}
		if(!controller_)
			return;
	}

	auto *pad = static_cast<SDL_GameController *>(controller_);
	StreamSession::ControllerState state;
	for(int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button)
	{
		if(SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(button)))
			state.buttons |= buttonFor(static_cast<SDL_GameControllerButton>(button));
	}
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

#else // sem SDL

Gamepad::Gamepad(QObject *parent) : QObject(parent) {}
Gamepad::~Gamepad() = default;
void Gamepad::start() {}
void Gamepad::stop() {}
void Gamepad::poll() {}
void Gamepad::rumble(quint8, quint8) {}

#endif

} // namespace orbislink
