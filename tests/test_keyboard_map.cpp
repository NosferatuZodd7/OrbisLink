// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Keyboard as controller: the default keys, the swap when a key is
// already taken, and the keys that cannot be given to any action.
#include "orbislink/qt/input_map.h"
#include "test_support.h"

#include <chiaki/controller.h>

#include <Qt>

using namespace orbislink;

ORBISLINK_TEST(default_keys_are_chiakis)
{
	KeyboardMap keyMap;
	CHECK(keyMap.press(Qt::Key_Return));
	CHECK(keyMap.state().buttons & CHIAKI_CONTROLLER_BUTTON_CROSS);
	// The keypad Enter is the same Enter.
	CHECK(keyMap.release(Qt::Key_Enter));
	CHECK(keyMap.empty());
	CHECK(keyMap.press(Qt::Key_W));
	CHECK(keyMap.state().leftY < 0);
	CHECK(!keyMap.press(Qt::Key_Escape));
}

ORBISLINK_TEST(taken_key_swaps_with_the_other_action)
{
	KeyboardMap::Bindings keys = KeyboardMap::defaults();
	// Space becomes cross; Enter is freed.
	CHECK(KeyboardMap::rebind(keys, "cross", Qt::Key_Space));
	CHECK_EQ(keys["cross"], static_cast<int>(Qt::Key_Space));
	// P belongs to the PS button: giving it to cross swaps them.
	CHECK(KeyboardMap::rebind(keys, "cross", Qt::Key_P));
	CHECK_EQ(keys["cross"], static_cast<int>(Qt::Key_P));
	CHECK_EQ(keys["ps"], static_cast<int>(Qt::Key_Space));

	KeyboardMap keyMap;
	keyMap.setBindings(keys);
	CHECK(keyMap.press(Qt::Key_P));
	CHECK(keyMap.state().buttons & CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK(!(keyMap.state().buttons & CHIAKI_CONTROLLER_BUTTON_PS));
	// Enter no longer does anything.
	CHECK(!keyMap.press(Qt::Key_Return));
}

ORBISLINK_TEST(esc_and_f11_are_given_to_nobody)
{
	KeyboardMap::Bindings keys = KeyboardMap::defaults();
	CHECK(!KeyboardMap::rebind(keys, "cross", Qt::Key_Escape));
	CHECK(!KeyboardMap::rebind(keys, "cross", Qt::Key_F11));
	CHECK(!KeyboardMap::rebind(keys, "nao-existe", Qt::Key_Q));
	CHECK(keys == KeyboardMap::defaults());
}

ORBISLINK_TEST(hand_edited_settings_leave_no_repeated_keys)
{
	// Two actions on the same key, an unknown action and Esc: the result is a
	// valid map, with each key on a single action.
	KeyboardMap keyMap;
	keyMap.setBindings({ { "cross", Qt::Key_Q }, { "circle", Qt::Key_Q },
		{ "inventada", Qt::Key_Z }, { "square", Qt::Key_Escape } });
	std::map<int, int> uses;
	for(const auto &pair : keyMap.bindings())
		++uses[pair.second];
	for(const auto &inUse : uses)
		CHECK_EQ(inUse.second, 1);
	CHECK_EQ(keyMap.bindings().size(), KeyboardMap::actions().size());
	CHECK_EQ(keyMap.bindings().at("square"), static_cast<int>(Qt::Key_C));
}

TEST_MAIN()
