// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/input_map.h"

#include <chiaki/controller.h>

#include <Qt>

namespace orbislink {

namespace {

// As teclas seguem o mapa por omissão do chiaki-ng.
uint32_t buttonFor(int key)
{
	switch(key)
	{
		case Qt::Key_Return:
		case Qt::Key_Enter: return CHIAKI_CONTROLLER_BUTTON_CROSS;
		case Qt::Key_Backspace: return CHIAKI_CONTROLLER_BUTTON_MOON;     // círculo
		case Qt::Key_C: return CHIAKI_CONTROLLER_BUTTON_BOX;              // quadrado
		case Qt::Key_V: return CHIAKI_CONTROLLER_BUTTON_PYRAMID;          // triângulo
		case Qt::Key_Left: return CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
		case Qt::Key_Right: return CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
		case Qt::Key_Up: return CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
		case Qt::Key_Down: return CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
		case Qt::Key_1: return CHIAKI_CONTROLLER_BUTTON_L1;
		case Qt::Key_3: return CHIAKI_CONTROLLER_BUTTON_R1;
		case Qt::Key_4: return CHIAKI_CONTROLLER_BUTTON_L3;
		case Qt::Key_6: return CHIAKI_CONTROLLER_BUTTON_R3;
		case Qt::Key_O: return CHIAKI_CONTROLLER_BUTTON_OPTIONS;
		case Qt::Key_F: return CHIAKI_CONTROLLER_BUTTON_SHARE;
		case Qt::Key_T: return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
		case Qt::Key_P: return CHIAKI_CONTROLLER_BUTTON_PS;
		default: return 0;
	}
}

bool isMapped(int key)
{
	if(buttonFor(key) != 0)
		return true;
	switch(key)
	{
		case Qt::Key_2: // L2
		case Qt::Key_5: // R2
		case Qt::Key_W:
		case Qt::Key_A:
		case Qt::Key_S:
		case Qt::Key_D:
		case Qt::Key_I:
		case Qt::Key_J:
		case Qt::Key_K:
		case Qt::Key_L:
			return true;
		default:
			return false;
	}
}

// Os eixos do PS4 vão de -32768 a 32767; com teclado só há tudo ou nada.
constexpr int16_t kAxisMax = 32767;

} // namespace

bool KeyboardMap::press(int key)
{
	if(!isMapped(key))
		return false;
	pressed_.insert(key);
	return true;
}

bool KeyboardMap::release(int key)
{
	if(!isMapped(key))
		return false;
	pressed_.remove(key);
	return true;
}

void KeyboardMap::clear() { pressed_.clear(); }

StreamSession::ControllerState KeyboardMap::state() const
{
	StreamSession::ControllerState state;
	for(int key : pressed_)
		state.buttons |= buttonFor(key);

	if(pressed_.contains(Qt::Key_2))
		state.l2 = 255;
	if(pressed_.contains(Qt::Key_5))
		state.r2 = 255;
	if(pressed_.contains(Qt::Key_A))
		state.leftX = -kAxisMax;
	if(pressed_.contains(Qt::Key_D))
		state.leftX = kAxisMax;
	if(pressed_.contains(Qt::Key_W))
		state.leftY = -kAxisMax;
	if(pressed_.contains(Qt::Key_S))
		state.leftY = kAxisMax;
	if(pressed_.contains(Qt::Key_J))
		state.rightX = -kAxisMax;
	if(pressed_.contains(Qt::Key_L))
		state.rightX = kAxisMax;
	if(pressed_.contains(Qt::Key_I))
		state.rightY = -kAxisMax;
	if(pressed_.contains(Qt::Key_K))
		state.rightY = kAxisMax;
	return state;
}

} // namespace orbislink
