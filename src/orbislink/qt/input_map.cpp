// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/input_map.h"

#include <chiaki/controller.h>

#include <Qt>

namespace orbislink {

namespace {

// Os eixos do PS4 vão de -32768 a 32767; com teclado só há tudo ou nada.
constexpr int16_t kAxisMax = 32767;

// O Enter do teclado numérico conta como o Enter normal: são a mesma tecla
// para quem está a jogar.
int normalise(int key) { return key == Qt::Key_Enter ? Qt::Key_Return : key; }

} // namespace

const std::vector<KeyboardMap::Action> &KeyboardMap::actions()
{
	// A ordem é a da janela do mapa. As teclas por omissão são as do
	// chiaki-ng.
	static const std::vector<Action> lista = {
		{ "cross", CHIAKI_CONTROLLER_BUTTON_CROSS, Qt::Key_Return },
		{ "circle", CHIAKI_CONTROLLER_BUTTON_MOON, Qt::Key_Backspace },
		{ "square", CHIAKI_CONTROLLER_BUTTON_BOX, Qt::Key_C },
		{ "triangle", CHIAKI_CONTROLLER_BUTTON_PYRAMID, Qt::Key_V },
		{ "dpad_up", CHIAKI_CONTROLLER_BUTTON_DPAD_UP, Qt::Key_Up },
		{ "dpad_down", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, Qt::Key_Down },
		{ "dpad_left", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, Qt::Key_Left },
		{ "dpad_right", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, Qt::Key_Right },
		{ "l1", CHIAKI_CONTROLLER_BUTTON_L1, Qt::Key_1 },
		{ "l2", 0, Qt::Key_2 },
		{ "r1", CHIAKI_CONTROLLER_BUTTON_R1, Qt::Key_3 },
		{ "r2", 0, Qt::Key_5 },
		{ "l3", CHIAKI_CONTROLLER_BUTTON_L3, Qt::Key_4 },
		{ "r3", CHIAKI_CONTROLLER_BUTTON_R3, Qt::Key_6 },
		{ "lstick_up", 0, Qt::Key_W },
		{ "lstick_left", 0, Qt::Key_A },
		{ "lstick_down", 0, Qt::Key_S },
		{ "lstick_right", 0, Qt::Key_D },
		{ "rstick_up", 0, Qt::Key_I },
		{ "rstick_left", 0, Qt::Key_J },
		{ "rstick_down", 0, Qt::Key_K },
		{ "rstick_right", 0, Qt::Key_L },
		{ "options", CHIAKI_CONTROLLER_BUTTON_OPTIONS, Qt::Key_O },
		{ "share", CHIAKI_CONTROLLER_BUTTON_SHARE, Qt::Key_F },
		{ "touchpad", CHIAKI_CONTROLLER_BUTTON_TOUCHPAD, Qt::Key_T },
		{ "ps", CHIAKI_CONTROLLER_BUTTON_PS, Qt::Key_P },
	};
	return lista;
}

KeyboardMap::Bindings KeyboardMap::defaults()
{
	Bindings bindings;
	for(const Action &action : actions())
		bindings[action.id] = action.defaultKey;
	return bindings;
}

bool KeyboardMap::reserved(int key)
{
	return key == Qt::Key_Escape || key == Qt::Key_F11 || key == 0 || key == Qt::Key_unknown;
}

bool KeyboardMap::rebind(Bindings &bindings, const std::string &action, int key)
{
	key = normalise(key);
	if(reserved(key))
		return false;
	auto alvo = bindings.find(action);
	if(alvo == bindings.end())
		return false;
	const int antiga = alvo->second;
	for(auto &par : bindings)
	{
		if(par.first != action && par.second == key)
			par.second = antiga;
	}
	alvo->second = key;
	return true;
}

KeyboardMap::KeyboardMap() { setBindings({}); }

void KeyboardMap::setBindings(const Bindings &custom)
{
	// Parte-se sempre das teclas por omissão e aplica-se o que foi mudado,
	// uma acção de cada vez e pelas mesmas regras de troca: um ficheiro de
	// definições editado à mão não consegue deixar duas acções na mesma
	// tecla nem uma acção sem tecla.
	bindings_ = defaults();
	for(const auto &par : custom)
		rebind(bindings_, par.first, par.second);
	byKey_.clear();
	for(const auto &par : bindings_)
		byKey_.insert(par.second, par.first);
	pressed_.clear();
}

bool KeyboardMap::press(int key)
{
	key = normalise(key);
	if(!byKey_.contains(key))
		return false;
	pressed_.insert(key);
	return true;
}

bool KeyboardMap::release(int key)
{
	key = normalise(key);
	if(!byKey_.contains(key))
		return false;
	pressed_.remove(key);
	return true;
}

void KeyboardMap::clear() { pressed_.clear(); }

StreamSession::ControllerState KeyboardMap::state() const
{
	StreamSession::ControllerState state;
	auto carregada = [this](const char *acao) {
		auto tecla = bindings_.find(acao);
		return tecla != bindings_.end() && pressed_.contains(tecla->second);
	};
	for(const Action &action : actions())
	{
		if(action.button != 0 && carregada(action.id))
			state.buttons |= action.button;
	}
	if(carregada("l2"))
		state.l2 = 255;
	if(carregada("r2"))
		state.r2 = 255;
	if(carregada("lstick_left"))
		state.leftX = -kAxisMax;
	if(carregada("lstick_right"))
		state.leftX = kAxisMax;
	if(carregada("lstick_up"))
		state.leftY = -kAxisMax;
	if(carregada("lstick_down"))
		state.leftY = kAxisMax;
	if(carregada("rstick_left"))
		state.rightX = -kAxisMax;
	if(carregada("rstick_right"))
		state.rightX = kAxisMax;
	if(carregada("rstick_up"))
		state.rightY = -kAxisMax;
	if(carregada("rstick_down"))
		state.rightY = kAxisMax;
	return state;
}

} // namespace orbislink
