// SPDX-License-Identifier: LGPL-3.0-only
//
// A PARAM.SFO that can be changed and written back. Ported from LibOrbisPkg
// (SFO/ParamSfo.cs, maxton, LGPL-3.0): keys sorted, each value in a slot of
// its maximum length.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink::fpkg {

class ParamSfo
{
public:
	enum class Type : uint16_t {
		Utf8Special = 0x0004,
		Utf8 = 0x0204,
		Integer = 0x0404,
	};

	struct Value
	{
		std::string name;
		Type type = Type::Utf8;
		std::string text;     // Utf8, Utf8Special
		int32_t number = 0;   // Integer
		int maxLength = 4;

		int length() const;
		// How the PKG's major-parameter digest spells it.
		std::string toString() const;
	};

	bool parse(const uint8_t *data, size_t size, std::string *error = nullptr);
	bool parse(const std::vector<uint8_t> &data, std::string *error = nullptr)
	{
		return parse(data.data(), data.size(), error);
	}

	const Value *find(const std::string &name) const;
	// Strings are cut to fit their slot (keeping whole UTF-8 characters).
	void setString(const std::string &name, const std::string &value, int maxLength);
	void setInteger(const std::string &name, int32_t value);

	std::vector<uint8_t> serialize() const;
	size_t fileSize() const;

private:
	void put(Value value);
	std::vector<Value> values_;
};

} // namespace orbislink::fpkg
