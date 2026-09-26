// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace orbislink {

// JSON mínimo, sem dependências externas.
//
// Tolerância deliberada: o Remote Package Installer devolve números em
// hexadecimal sem aspas (ex.: { "error_code": 0x80990015, "size": 0x1A2B }),
// o que NÃO é JSON válido. O parser aceita literais 0x... como inteiros para
// conseguir ler as respostas reais da consola.
class Json
{
public:
	enum class Type { Null, Bool, Int, Double, String, Array, Object };

	Json() = default;
	static Json makeObject();
	static Json makeArray();
	static Json fromBool(bool v);
	static Json fromInt(int64_t v);
	static Json fromDouble(double v);
	static Json fromString(std::string v);

	Type type() const { return type_; }
	bool isNull() const { return type_ == Type::Null; }
	bool isObject() const { return type_ == Type::Object; }
	bool isArray() const { return type_ == Type::Array; }
	bool isString() const { return type_ == Type::String; }
	bool isNumber() const { return type_ == Type::Int || type_ == Type::Double; }
	bool isBool() const { return type_ == Type::Bool; }

	// Acessos com valor por omissão: nunca lançam exceções.
	bool toBool(bool def = false) const;
	int64_t toInt(int64_t def = 0) const;
	double toDouble(double def = 0.0) const;
	std::string toString(const std::string &def = std::string()) const;

	// `"true"`/`"false"` (o is_exists da consola devolve o booleano como texto)
	// e também true/false reais.
	bool toLooseBool(bool def = false) const;

	const Json &operator[](const std::string &key) const;
	const Json &at(size_t index) const;
	size_t size() const;
	bool contains(const std::string &key) const;
	const std::vector<Json> &items() const { return array_; }
	const std::map<std::string, Json> &members() const { return object_; }

	void set(const std::string &key, Json value);
	void push(Json value);

	std::string dump() const;

	// Devolve Json nulo em caso de erro; `error` recebe a descrição.
	static Json parse(const std::string &text, std::string *error = nullptr);

	static std::string escape(const std::string &s);

private:
	Type type_ = Type::Null;
	bool bool_ = false;
	int64_t int_ = 0;
	double double_ = 0.0;
	std::string string_;
	std::vector<Json> array_;
	std::map<std::string, Json> object_;
};

} // namespace orbislink
