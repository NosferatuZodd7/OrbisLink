// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/input_map.h"

#include <chiaki/controller.h>

#include <Qt>

namespace orbislink {

namespace {

// PS4 axes range from -32768 to 32767; with a keyboard it is all or nothing.
constexpr int16_t kAxisMax = 32767;

// The keypad Enter counts as the normal Enter: they are the same key
// to someone playing.
int normalise(int key) { return key == Qt::Key_Enter ? Qt::Key_Return : key; }

} // namespace

const std::vector<KeyboardMap::Action> &KeyboardMap::actions()
{
	// The order is the map window's. The default keys are
	// chiaki-ng's.
	static const std::vector<Action> items = {
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
	return items;
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
	auto target = bindings.find(action);
	if(target == bindings.end())
		return false;
	const int old = target->second;
	for(auto &pair : bindings)
	{
		if(pair.first != action && pair.second == key)
			pair.second = old;
	}
	target->second = key;
	return true;
}

KeyboardMap::KeyboardMap() { setBindings({}); }

void KeyboardMap::setBindings(const Bindings &custom)
{
	// Always start from the default keys and apply what was changed, one
	// action at a time and by the same swap rules: a hand-edited settings
	// file cannot leave two actions on the same key or an action
	// without a key.
	bindings_ = defaults();
	for(const auto &pair : custom)
		rebind(bindings_, pair.first, pair.second);
	byKey_.clear();
	for(const auto &pair : bindings_)
		byKey_.insert(pair.second, pair.first);
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
	auto pressed = [this](const char *action) {
		auto keyCap = bindings_.find(action);
		return keyCap != bindings_.end() && pressed_.contains(keyCap->second);
	};
	for(const Action &action : actions())
	{
		if(action.button != 0 && pressed(action.id))
			state.buttons |= action.button;
	}
	if(pressed("l2"))
		state.l2 = 255;
	if(pressed("r2"))
		state.r2 = 255;
	if(pressed("lstick_left"))
		state.leftX = -kAxisMax;
	if(pressed("lstick_right"))
		state.leftX = kAxisMax;
	if(pressed("lstick_up"))
		state.leftY = -kAxisMax;
	if(pressed("lstick_down"))
		state.leftY = kAxisMax;
	if(pressed("rstick_left"))
		state.rightX = -kAxisMax;
	if(pressed("rstick_right"))
		state.rightX = kAxisMax;
	if(pressed("rstick_up"))
		state.rightY = -kAxisMax;
	if(pressed("rstick_down"))
		state.rightY = kAxisMax;
	return state;
}

const std::vector<std::pair<std::string, std::string>> &PadMap::defaults()
{
	static const std::vector<std::pair<std::string, std::string>> map = {
		{ "a", "cross" }, { "b", "circle" }, { "x", "square" }, { "y", "triangle" },
		{ "dpup", "dpad_up" }, { "dpdown", "dpad_down" }, { "dpleft", "dpad_left" },
		{ "dpright", "dpad_right" }, { "leftshoulder", "l1" }, { "rightshoulder", "r1" },
		{ "leftstick", "l3" }, { "rightstick", "r3" }, { "start", "options" }, { "back", "share" },
		{ "guide", "ps" }, { "touchpad", "touchpad" },
	};
	return map;
}

PadMap::Map PadMap::effective(const Map &overrides)
{
	Map map;
	for(const auto &pair : defaults())
	{
		const auto given = overrides.find(pair.first);
		map[pair.first] = given != overrides.end() ? given->second : pair.second;
	}
	return map;
}

PadMap::Map PadMap::remapped(Map map, const std::string &action, const std::string &physical)
{
	if(!map.count(physical))
		return map;
	const std::string before = map[physical];
	for(auto &pair : map)
		if(pair.second == action)
			pair.second = before;
	map[physical] = action;
	return map;
}

PadMap::Map PadMap::changes(const Map &map)
{
	Map changed;
	for(const auto &pair : defaults())
	{
		const auto now = map.find(pair.first);
		if(now != map.end() && now->second != pair.second)
			changed[pair.first] = now->second;
	}
	return changed;
}

} // namespace orbislink
