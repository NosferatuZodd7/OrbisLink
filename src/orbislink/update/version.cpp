// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/update/version.h"

#include "orbislink/common/util.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace orbislink {

namespace {

// Compara dois pedaços de um identificador de pré-lançamento. Pelo semver,
// um pedaço numérico vale menos que um alfanumérico, e dois numéricos
// comparam-se por valor e não por texto ("10" > "9").
bool allDigits(const std::string &s)
{
	if(s.empty())
		return false;
	for(char c : s)
	{
		if(!std::isdigit(static_cast<unsigned char>(c)))
			return false;
	}
	return true;
}

int comparePreRelease(const std::string &a, const std::string &b)
{
	// Sem pré-lançamento é sempre a versão mais recente das duas.
	if(a.empty() && b.empty())
		return 0;
	if(a.empty())
		return 1;
	if(b.empty())
		return -1;

	const std::vector<std::string> left = split(a, '.', false);
	const std::vector<std::string> right = split(b, '.', false);
	const size_t count = left.size() < right.size() ? left.size() : right.size();
	for(size_t i = 0; i < count; ++i)
	{
		const bool leftNumeric = allDigits(left[i]);
		const bool rightNumeric = allDigits(right[i]);
		if(leftNumeric && rightNumeric)
		{
			const long l = std::strtol(left[i].c_str(), nullptr, 10);
			const long r = std::strtol(right[i].c_str(), nullptr, 10);
			if(l != r)
				return l < r ? -1 : 1;
			continue;
		}
		if(leftNumeric != rightNumeric)
			return leftNumeric ? -1 : 1;
		const std::string l = toLower(left[i]);
		const std::string r = toLower(right[i]);
		if(l != r)
			return l < r ? -1 : 1;
	}
	if(left.size() == right.size())
		return 0;
	return left.size() < right.size() ? -1 : 1;
}

} // namespace

std::string Version::toString() const
{
	std::string text = std::to_string(major) + "." + std::to_string(minor) + "."
		+ std::to_string(patch);
	if(!pre.empty())
		text += "-" + pre;
	return text;
}

Version parseVersion(const std::string &text)
{
	Version version;
	std::string value = trim(text);
	if(value.empty())
		return version;
	// As tags vêm com "v" à frente; o nome que a app tem de si própria não.
	if(value[0] == 'v' || value[0] == 'V')
		value.erase(0, 1);

	// Tudo o que venha depois de um "+" é metadados de build e não conta
	// para a ordem (semver).
	const size_t plus = value.find('+');
	if(plus != std::string::npos)
		value.erase(plus);

	const size_t dash = value.find('-');
	std::string core = dash == std::string::npos ? value : value.substr(0, dash);
	if(dash != std::string::npos)
		version.pre = value.substr(dash + 1);

	const std::vector<std::string> parts = split(core, '.', false);
	if(parts.empty() || !allDigits(parts[0]))
		return version;
	version.major = static_cast<int>(std::strtol(parts[0].c_str(), nullptr, 10));
	if(parts.size() > 1 && allDigits(parts[1]))
		version.minor = static_cast<int>(std::strtol(parts[1].c_str(), nullptr, 10));
	if(parts.size() > 2 && allDigits(parts[2]))
		version.patch = static_cast<int>(std::strtol(parts[2].c_str(), nullptr, 10));
	version.valid = true;
	return version;
}

int compareVersions(const Version &a, const Version &b)
{
	if(!a.valid || !b.valid)
	{
		if(a.valid == b.valid)
			return 0;
		return a.valid ? 1 : -1;
	}
	if(a.major != b.major)
		return a.major < b.major ? -1 : 1;
	if(a.minor != b.minor)
		return a.minor < b.minor ? -1 : 1;
	if(a.patch != b.patch)
		return a.patch < b.patch ? -1 : 1;
	return comparePreRelease(a.pre, b.pre);
}

int compareVersions(const std::string &a, const std::string &b)
{
	return compareVersions(parseVersion(a), parseVersion(b));
}

} // namespace orbislink
