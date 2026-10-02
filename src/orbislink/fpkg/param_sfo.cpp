// SPDX-License-Identifier: LGPL-3.0-only
#include "orbislink/fpkg/param_sfo.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orbislink::fpkg {

namespace {

uint32_t le32(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
void put32(uint8_t *p, uint32_t v)
{
	for(int i = 0; i < 4; ++i)
		p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void put16(uint8_t *p, uint16_t v)
{
	p[0] = static_cast<uint8_t>(v);
	p[1] = static_cast<uint8_t>(v >> 8);
}

// Cuts at a character boundary, so a title never ends half a character.
std::string fitUtf8(const std::string &text, size_t maxBytes)
{
	if(text.size() <= maxBytes)
		return text;
	size_t cut = maxBytes;
	while(cut > 0 && (static_cast<uint8_t>(text[cut]) & 0xC0) == 0x80)
		--cut;
	return text.substr(0, cut);
}

} // namespace

int ParamSfo::Value::length() const
{
	switch(type)
	{
		case Type::Integer: return 4;
		case Type::Utf8: return static_cast<int>(text.size()) + 1;
		case Type::Utf8Special: return static_cast<int>(text.size());
	}
	return 0;
}

std::string ParamSfo::Value::toString() const
{
	if(type == Type::Integer)
	{
		char buffer[16];
		std::snprintf(buffer, sizeof buffer, "0x%08x", static_cast<uint32_t>(number));
		return buffer;
	}
	return text;
}

bool ParamSfo::parse(const uint8_t *data, size_t size, std::string *error)
{
	auto fail = [&](const char *message) {
		if(error)
			*error = message;
		return false;
	};
	values_.clear();
	size_t start = 0;
	if(size >= 4 && data[0] == 'S' && data[1] == 'C' && data[2] == 'E' && data[3] == 'C')
		start = 0x800;
	if(size < start + 0x14)
		return fail("param.sfo is too short");
	const uint8_t *base = data + start;
	const size_t avail = size - start;
	if(!(base[0] == 0 && base[1] == 'P' && base[2] == 'S' && base[3] == 'F'))
		return fail("param.sfo has no PSF magic");
	const uint32_t keyTable = le32(base + 8);
	const uint32_t dataTable = le32(base + 12);
	const uint32_t count = le32(base + 16);
	if(0x14 + size_t(count) * 0x10 > avail)
		return fail("param.sfo index runs past the end");
	for(uint32_t i = 0; i < count; ++i)
	{
		const uint8_t *entry = base + 0x14 + i * 0x10;
		const size_t keyAt = size_t(keyTable) + le16(entry);
		const Type type = static_cast<Type>(le16(entry + 2));
		const uint32_t len = le32(entry + 4);
		const uint32_t maxLen = le32(entry + 8);
		const size_t dataAt = size_t(dataTable) + le32(entry + 12);
		if(keyAt >= avail || dataAt > avail || dataAt + len > avail)
			return fail("param.sfo entry runs past the end");
		Value v;
		const char *key = reinterpret_cast<const char *>(base + keyAt);
		v.name.assign(key, strnlen(key, avail - keyAt));
		v.type = type;
		v.maxLength = static_cast<int>(maxLen);
		switch(type)
		{
			case Type::Integer:
				if(dataAt + 4 > avail)
					return fail("param.sfo integer runs past the end");
				v.number = static_cast<int32_t>(le32(base + dataAt));
				v.maxLength = 4;
				break;
			case Type::Utf8:
				v.text.assign(reinterpret_cast<const char *>(base + dataAt), len > 0 ? len - 1 : 0);
				break;
			case Type::Utf8Special:
				v.text.assign(reinterpret_cast<const char *>(base + dataAt), len);
				break;
			default:
				return fail("param.sfo has an unknown value type");
		}
		values_.push_back(std::move(v));
	}
	return true;
}

const ParamSfo::Value *ParamSfo::find(const std::string &name) const
{
	for(const Value &v : values_)
		if(v.name == name)
			return &v;
	return nullptr;
}

void ParamSfo::put(Value value)
{
	for(Value &v : values_)
	{
		if(v.name == value.name)
		{
			v = std::move(value);
			return;
		}
	}
	values_.push_back(std::move(value));
}

void ParamSfo::setString(const std::string &name, const std::string &value, int maxLength)
{
	Value v;
	v.name = name;
	v.type = Type::Utf8;
	v.maxLength = maxLength;
	v.text = fitUtf8(value, static_cast<size_t>(std::max(0, maxLength - 1)));
	put(std::move(v));
}

void ParamSfo::setInteger(const std::string &name, int32_t value)
{
	Value v;
	v.name = name;
	v.type = Type::Integer;
	v.number = value;
	v.maxLength = 4;
	put(std::move(v));
}

size_t ParamSfo::fileSize() const
{
	size_t keys = 0, data = 0;
	for(const Value &v : values_)
	{
		keys += v.name.size() + 1;
		data += static_cast<size_t>(v.maxLength);
	}
	size_t dataTable = 0x14 + values_.size() * 0x10 + keys;
	dataTable = (dataTable + 3) & ~size_t(3);
	return dataTable + data;
}

std::vector<uint8_t> ParamSfo::serialize() const
{
	std::vector<Value> sorted = values_;
	std::sort(sorted.begin(), sorted.end(),
		[](const Value &a, const Value &b) { return a.name < b.name; });

	size_t keys = 0;
	for(const Value &v : sorted)
		keys += v.name.size() + 1;
	const size_t keyTable = 0x14 + sorted.size() * 0x10;
	const size_t dataTable = (keyTable + keys + 3) & ~size_t(3);

	std::vector<uint8_t> out(fileSize(), 0);
	out[1] = 'P';
	out[2] = 'S';
	out[3] = 'F';
	put32(out.data() + 4, 0x101);
	put32(out.data() + 8, static_cast<uint32_t>(keyTable));
	put32(out.data() + 12, static_cast<uint32_t>(dataTable));
	put32(out.data() + 16, static_cast<uint32_t>(sorted.size()));
	size_t keyOffset = 0, dataOffset = 0;
	for(size_t i = 0; i < sorted.size(); ++i)
	{
		const Value &v = sorted[i];
		uint8_t *entry = out.data() + 0x14 + i * 0x10;
		put16(entry, static_cast<uint16_t>(keyOffset));
		put16(entry + 2, static_cast<uint16_t>(v.type));
		put32(entry + 4, static_cast<uint32_t>(v.length()));
		put32(entry + 8, static_cast<uint32_t>(v.maxLength));
		put32(entry + 12, static_cast<uint32_t>(dataOffset));
		std::memcpy(out.data() + keyTable + keyOffset, v.name.data(), v.name.size());
		uint8_t *slot = out.data() + dataTable + dataOffset;
		if(v.type == Type::Integer)
			put32(slot, static_cast<uint32_t>(v.number));
		else
			std::memcpy(slot, v.text.data(),
				std::min(v.text.size(), static_cast<size_t>(v.maxLength)));
		keyOffset += v.name.size() + 1;
		dataOffset += static_cast<size_t>(v.maxLength);
	}
	return out;
}

} // namespace orbislink::fpkg
