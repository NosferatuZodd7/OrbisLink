// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/net/console_locator.h"

#include <cctype>
#include <set>

namespace orbislink {

std::string normaliseHardwareId(const std::string &text)
{
	std::string hex;
	for(const char c : text)
	{
		if(c == ':' || c == '-' || c == '.' || c == ' ')
			continue;
		if(!std::isxdigit(static_cast<unsigned char>(c)))
			return std::string();
		hex.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
	}
	if(hex.size() != 12 || hex == "000000000000")
		return std::string();
	return hex;
}

namespace {

std::set<std::string> idsOf(const SoughtConsole &console)
{
	std::set<std::string> ids;
	for(const std::string &id : console.ids)
	{
		const std::string normal = normaliseHardwareId(id);
		if(!normal.empty())
			ids.insert(normal);
	}
	return ids;
}

} // namespace

AddressCheck checkAddress(const SoughtConsole &console, const std::map<std::string, std::string> &neighbours)
{
	const auto entry = neighbours.find(console.address);
	if(entry == neighbours.end())
		return AddressCheck::Silent;
	const std::string mac = normaliseHardwareId(entry->second);
	if(mac.empty())
		return AddressCheck::Silent;
	return idsOf(console).count(mac) ? AddressCheck::Present : AddressCheck::Taken;
}

std::map<std::string, std::string> locateMoved(const std::vector<SoughtConsole> &sought,
	const std::vector<Sighting> &answered, const std::map<std::string, std::string> &neighbours)
{
	std::map<std::string, std::string> moved;
	std::map<std::string, int> claims;
	for(const SoughtConsole &console : sought)
	{
		const std::set<std::string> ids = idsOf(console);
		if(ids.empty())
			continue;
		std::set<std::string> places;
		for(const Sighting &sighting : answered)
			if(ids.count(normaliseHardwareId(sighting.id)))
				places.insert(sighting.address);
		for(const auto &neighbour : neighbours)
			if(ids.count(normaliseHardwareId(neighbour.second)))
				places.insert(neighbour.first);
		// Still where it was saved, or in two places at once: nothing to do.
		if(places.count(console.address) || places.size() != 1)
			continue;
		moved[console.address] = *places.begin();
		++claims[*places.begin()];
	}
	// Two saved consoles cannot both have gone to the same address.
	for(auto it = moved.begin(); it != moved.end();)
		it = claims[it->second] > 1 ? moved.erase(it) : std::next(it);
	return moved;
}

} // namespace orbislink
