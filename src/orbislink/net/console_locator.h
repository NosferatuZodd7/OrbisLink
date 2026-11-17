// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// A saved console looked for by who it is, not by where it was.
//
// A router hands addresses out again — after a power cut, a while away, a
// new router — and a console that was at .20 comes back at .45. Each saved
// console carries what identifies it on the network: the host-id Remote
// Play's discovery and registration report (its network adapter's address)
// and the hardware address this PC saw it answer from (its MAC, from the
// neighbour table). Those are compared, never a name or a guess: a console
// is only said to have moved when exactly one other address answers as it.

#include <map>
#include <string>
#include <vector>

namespace orbislink {

// "a2:4f:10:7c:3e:91", "A2-4F-10-7C-3E-91" or "A24F107C3E91" → "A24F107C3E91";
// empty when it is not a 48-bit hardware address (or is all zeros).
std::string normaliseHardwareId(const std::string &text);

struct SoughtConsole
{
	std::string address;          // where it is saved
	std::vector<std::string> ids; // its host-id and/or MAC, in any of the forms above
};

// A console that answered Remote Play's search: where, and its host-id.
struct Sighting
{
	std::string address;
	std::string id;
};

// Whether a console is at its saved address, by this PC's neighbour table
// (address → MAC, see neighbourMacs()):
// * Present — the address answers with one of its ids;
// * Taken   — the address answers, with another machine's hardware address;
// * Silent  — nothing answers there (off, or gone elsewhere).
enum class AddressCheck { Present, Taken, Silent };
AddressCheck checkAddress(const SoughtConsole &console, const std::map<std::string, std::string> &neighbours);

// Where each sought console is now, for those that are somewhere else:
// saved address → new address. A console seen at its saved address, seen at
// two other addresses, or whose new address another sought console also
// claims, is left out — moving it would be a guess.
std::map<std::string, std::string> locateMoved(const std::vector<SoughtConsole> &sought,
	const std::vector<Sighting> &answered, const std::map<std::string, std::string> &neighbours);

} // namespace orbislink
