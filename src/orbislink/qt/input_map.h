// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/session.h"

#include <QHash>
#include <QSet>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace orbislink {

// Keyboard → PS4 controller.
//
// It does not replace a real controller, but it is enough to confirm the
// stream responds and to navigate menus. The default map is chiaki-ng's,
// so people coming from there do not have to relearn it; each action can be
// moved to another key, and what changed is kept in the settings.
class KeyboardMap
{
public:
	// A controller action: a button (button != 0) or half of an axis.
	struct Action
	{
		const char *id;   // the name stored in the settings, e.g. "cross"
		uint32_t button;  // CHIAKI_CONTROLLER_BUTTON_*, or 0 for an axis
		int defaultKey;   // Qt::Key
	};
	static const std::vector<Action> &actions();

	// action → key. Actions not given keep their default key.
	using Bindings = std::map<std::string, int>;
	static Bindings defaults();

	// Keys that cannot be given to an action: Esc leaves the stream and F11
	// toggles full screen.
	static bool reserved(int key);

	// Gives `key` to `action`. If the key already belonged to another action,
	// the two swap — no action is left without a key. Returns false (touching
	// nothing) for an unknown action or a reserved key.
	static bool rebind(Bindings &bindings, const std::string &action, int key);

	KeyboardMap();
	void setBindings(const Bindings &custom);
	const Bindings &bindings() const { return bindings_; }

	// Returns false if the key is not mapped (so the event follows its
	// normal path, for example Esc closing the session).
	bool press(int key);
	bool release(int key);
	void clear();

	StreamSession::ControllerState state() const;
	bool empty() const { return pressed_.isEmpty(); }

private:
	Bindings bindings_;
	QHash<int, std::string> byKey_;
	QSet<int> pressed_;
};

// Physical controller → PS4 controller. Physical buttons go by SDL's
// names ("a", "b", "dpup", "leftshoulder"…), what they press by the key
// map's action ids ("cross", "l1"…). The triggers are axes and stay L2/R2.
struct PadMap
{
	using Map = std::map<std::string, std::string>;
	static const std::vector<std::pair<std::string, std::string>> &defaults();
	// Every physical button; those not in `overrides` keep their default.
	static Map effective(const Map &overrides);
	// `map` with `action` moved to `physical`; whatever `physical` did goes
	// to the button that had `action`, so the two swap.
	static Map remapped(Map map, const std::string &action, const std::string &physical);
	// Only what differs from the defaults (what the settings keep).
	static Map changes(const Map &map);
};

} // namespace orbislink
