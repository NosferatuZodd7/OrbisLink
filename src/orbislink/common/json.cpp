// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace orbislink {

namespace {
const Json &nullJson()
{
	static const Json instance;
	return instance;
}
} // namespace

Json Json::makeObject()
{
	Json j;
	j.type_ = Type::Object;
	return j;
}

Json Json::makeArray()
{
	Json j;
	j.type_ = Type::Array;
	return j;
}

Json Json::fromBool(bool v)
{
	Json j;
	j.type_ = Type::Bool;
	j.bool_ = v;
	return j;
}

Json Json::fromInt(int64_t v)
{
	Json j;
	j.type_ = Type::Int;
	j.int_ = v;
	return j;
}

Json Json::fromDouble(double v)
{
	Json j;
	j.type_ = Type::Double;
	j.double_ = v;
	return j;
}

Json Json::fromString(std::string v)
{
	Json j;
	j.type_ = Type::String;
	j.string_ = std::move(v);
	return j;
}

bool Json::toBool(bool def) const { return type_ == Type::Bool ? bool_ : def; }

int64_t Json::toInt(int64_t def) const
{
	switch(type_)
	{
		case Type::Int: return int_;
		case Type::Double: return static_cast<int64_t>(double_);
		case Type::Bool: return bool_ ? 1 : 0;
		case Type::String: {
			try { return static_cast<int64_t>(std::stoll(string_, nullptr, 0)); }
			catch(...) { return def; }
		}
		default: return def;
	}
}

double Json::toDouble(double def) const
{
	switch(type_)
	{
		case Type::Double: return double_;
		case Type::Int: return static_cast<double>(int_);
		default: return def;
	}
}

std::string Json::toString(const std::string &def) const
{
	return type_ == Type::String ? string_ : def;
}

bool Json::toLooseBool(bool def) const
{
	if(type_ == Type::Bool)
		return bool_;
	if(type_ == Type::Int)
		return int_ != 0;
	if(type_ == Type::String)
	{
		std::string lowered;
		lowered.reserve(string_.size());
		for(char c : string_)
			lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		if(lowered == "true" || lowered == "1" || lowered == "yes")
			return true;
		if(lowered == "false" || lowered == "0" || lowered == "no")
			return false;
	}
	return def;
}

const Json &Json::operator[](const std::string &key) const
{
	if(type_ != Type::Object)
		return nullJson();
	auto it = object_.find(key);
	return it == object_.end() ? nullJson() : it->second;
}

const Json &Json::at(size_t index) const
{
	if(type_ != Type::Array || index >= array_.size())
		return nullJson();
	return array_[index];
}

size_t Json::size() const
{
	if(type_ == Type::Array)
		return array_.size();
	if(type_ == Type::Object)
		return object_.size();
	return 0;
}

bool Json::contains(const std::string &key) const
{
	return type_ == Type::Object && object_.find(key) != object_.end();
}

void Json::set(const std::string &key, Json value)
{
	type_ = Type::Object;
	object_[key] = std::move(value);
}

void Json::push(Json value)
{
	type_ = Type::Array;
	array_.push_back(std::move(value));
}

std::string Json::escape(const std::string &s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for(unsigned char c : s)
	{
		switch(c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if(c < 0x20)
				{
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else
					out.push_back(static_cast<char>(c));
				break;
		}
	}
	return out;
}

std::string Json::dump() const
{
	std::ostringstream os;
	switch(type_)
	{
		case Type::Null: os << "null"; break;
		case Type::Bool: os << (bool_ ? "true" : "false"); break;
		case Type::Int: os << int_; break;
		case Type::Double: {
			char buf[40];
			std::snprintf(buf, sizeof(buf), "%.17g", double_);
			os << buf;
			break;
		}
		case Type::String: os << '"' << escape(string_) << '"'; break;
		case Type::Array: {
			os << '[';
			for(size_t i = 0; i < array_.size(); ++i)
			{
				if(i)
					os << ',';
				os << array_[i].dump();
			}
			os << ']';
			break;
		}
		case Type::Object: {
			os << '{';
			bool first = true;
			for(const auto &kv : object_)
			{
				if(!first)
					os << ',';
				first = false;
				os << '"' << escape(kv.first) << "\":" << kv.second.dump();
			}
			os << '}';
			break;
		}
	}
	return os.str();
}

namespace {

class Parser
{
public:
	Parser(const std::string &text) : text_(text) {}

	bool parseValue(Json &out)
	{
		skipWhitespace();
		if(pos_ >= text_.size())
			return fail("fim inesperado do documento");
		char c = text_[pos_];
		switch(c)
		{
			case '{': return parseObject(out);
			case '[': return parseArray(out);
			case '"': {
				std::string s;
				if(!parseString(s))
					return false;
				out = Json::fromString(std::move(s));
				return true;
			}
			case 't':
				if(text_.compare(pos_, 4, "true") == 0)
				{
					pos_ += 4;
					out = Json::fromBool(true);
					return true;
				}
				return fail("literal inválido");
			case 'f':
				if(text_.compare(pos_, 5, "false") == 0)
				{
					pos_ += 5;
					out = Json::fromBool(false);
					return true;
				}
				return fail("literal inválido");
			case 'n':
				if(text_.compare(pos_, 4, "null") == 0)
				{
					pos_ += 4;
					out = Json();
					return true;
				}
				return fail("literal inválido");
			default: return parseNumber(out);
		}
	}

	void skipWhitespace()
	{
		while(pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])))
			++pos_;
	}

	bool atEndIgnoringWhitespace()
	{
		skipWhitespace();
		return pos_ >= text_.size();
	}

	std::string error() const { return error_; }

private:
	bool fail(const std::string &msg)
	{
		if(error_.empty())
			error_ = msg + " (posição " + std::to_string(pos_) + ")";
		return false;
	}

	bool parseObject(Json &out)
	{
		out = Json::makeObject();
		++pos_; // '{'
		skipWhitespace();
		if(pos_ < text_.size() && text_[pos_] == '}')
		{
			++pos_;
			return true;
		}
		for(;;)
		{
			skipWhitespace();
			std::string key;
			if(pos_ >= text_.size() || text_[pos_] != '"')
				return fail("esperava-se uma chave entre aspas");
			if(!parseString(key))
				return false;
			skipWhitespace();
			if(pos_ >= text_.size() || text_[pos_] != ':')
				return fail("esperava-se ':'");
			++pos_;
			Json value;
			if(!parseValue(value))
				return false;
			out.set(key, std::move(value));
			skipWhitespace();
			if(pos_ >= text_.size())
				return fail("objeto não terminado");
			if(text_[pos_] == ',')
			{
				++pos_;
				continue;
			}
			if(text_[pos_] == '}')
			{
				++pos_;
				return true;
			}
			return fail("esperava-se ',' ou '}'");
		}
	}

	bool parseArray(Json &out)
	{
		out = Json::makeArray();
		++pos_; // '['
		skipWhitespace();
		if(pos_ < text_.size() && text_[pos_] == ']')
		{
			++pos_;
			return true;
		}
		for(;;)
		{
			Json value;
			if(!parseValue(value))
				return false;
			out.push(std::move(value));
			skipWhitespace();
			if(pos_ >= text_.size())
				return fail("lista não terminada");
			if(text_[pos_] == ',')
			{
				++pos_;
				continue;
			}
			if(text_[pos_] == ']')
			{
				++pos_;
				return true;
			}
			return fail("esperava-se ',' ou ']'");
		}
	}

	bool parseString(std::string &out)
	{
		++pos_; // '"'
		out.clear();
		while(pos_ < text_.size())
		{
			char c = text_[pos_++];
			if(c == '"')
				return true;
			if(c != '\\')
			{
				out.push_back(c);
				continue;
			}
			if(pos_ >= text_.size())
				return fail("escape truncado");
			char e = text_[pos_++];
			switch(e)
			{
				case '"': out.push_back('"'); break;
				case '\\': out.push_back('\\'); break;
				case '/': out.push_back('/'); break;
				case 'b': out.push_back('\b'); break;
				case 'f': out.push_back('\f'); break;
				case 'n': out.push_back('\n'); break;
				case 'r': out.push_back('\r'); break;
				case 't': out.push_back('\t'); break;
				case 'u': {
					if(pos_ + 4 > text_.size())
						return fail("escape \\u truncado");
					unsigned code = 0;
					for(int i = 0; i < 4; ++i)
					{
						char h = text_[pos_ + i];
						code <<= 4;
						if(h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
						else if(h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
						else if(h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
						else return fail("escape \\u inválido");
					}
					pos_ += 4;
					appendUtf8(out, code);
					break;
				}
				default: return fail("escape desconhecido");
			}
		}
		return fail("string não terminada");
	}

	static void appendUtf8(std::string &out, unsigned code)
	{
		if(code < 0x80)
			out.push_back(static_cast<char>(code));
		else if(code < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (code >> 6)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xE0 | (code >> 12)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
	}

	bool parseNumber(Json &out)
	{
		size_t start = pos_;
		bool negative = false;
		if(pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+'))
		{
			negative = text_[pos_] == '-';
			++pos_;
		}
		// Extensão: hexadecimal (o instalador da consola devolve 0x...).
		if(pos_ + 1 < text_.size() && text_[pos_] == '0'
			&& (text_[pos_ + 1] == 'x' || text_[pos_ + 1] == 'X'))
		{
			pos_ += 2;
			size_t digitsStart = pos_;
			uint64_t value = 0;
			while(pos_ < text_.size() && std::isxdigit(static_cast<unsigned char>(text_[pos_])))
			{
				char h = text_[pos_++];
				value <<= 4;
				if(h >= '0' && h <= '9') value |= static_cast<uint64_t>(h - '0');
				else if(h >= 'a' && h <= 'f') value |= static_cast<uint64_t>(h - 'a' + 10);
				else value |= static_cast<uint64_t>(h - 'A' + 10);
			}
			if(pos_ == digitsStart)
				return fail("número hexadecimal sem dígitos");
			int64_t signedValue = static_cast<int64_t>(value);
			out = Json::fromInt(negative ? -signedValue : signedValue);
			return true;
		}
		bool isDouble = false;
		while(pos_ < text_.size())
		{
			char c = text_[pos_];
			if(std::isdigit(static_cast<unsigned char>(c)))
				++pos_;
			else if(c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
			{
				isDouble = isDouble || c == '.' || c == 'e' || c == 'E';
				++pos_;
			}
			else
				break;
		}
		if(pos_ == start)
			return fail("valor inesperado");
		std::string token = text_.substr(start, pos_ - start);
		try
		{
			if(isDouble)
				out = Json::fromDouble(std::stod(token));
			else
				out = Json::fromInt(static_cast<int64_t>(std::stoll(token)));
		}
		catch(...)
		{
			return fail("número inválido");
		}
		return true;
	}

	const std::string &text_;
	size_t pos_ = 0;
	std::string error_;
};

} // namespace

Json Json::parse(const std::string &text, std::string *error)
{
	Parser parser(text);
	Json value;
	if(!parser.parseValue(value))
	{
		if(error)
			*error = parser.error();
		return Json();
	}
	if(error)
		error->clear();
	return value;
}

} // namespace orbislink
