// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Arnês de testes mínimo: sem dependências externas, para o CI ser trivial.

#include "orbislink/common/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace orbislink_test {

struct TestCase
{
	std::string name;
	std::function<void()> body;
};

inline std::vector<TestCase> &registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

struct Registrar
{
	Registrar(const std::string &name, std::function<void()> body)
	{
		registry().push_back({ name, std::move(body) });
	}
};

struct Failure
{
	std::string message;
};

inline void failCheck(const std::string &expression, const char *file, int line,
	const std::string &detail)
{
	throw Failure { std::string(file) + ":" + std::to_string(line) + ": " + expression
		+ (detail.empty() ? "" : " — " + detail) };
}

inline int runAll()
{
	// Os testes silenciam o log por omissão; ORBISLINK_TEST_VERBOSE=1 volta a ligá-lo.
	if(std::getenv("ORBISLINK_TEST_VERBOSE") == nullptr)
		orbislink::Logger::instance().setLevel(orbislink::LogLevel::Off);

	int failures = 0;
	for(const auto &test : registry())
	{
		try
		{
			test.body();
			std::cout << "[ok]   " << test.name << std::endl;
		}
		catch(const Failure &failure)
		{
			std::cout << "[FALHA] " << test.name << "\n        " << failure.message << std::endl;
			++failures;
		}
		catch(const std::exception &error)
		{
			std::cout << "[FALHA] " << test.name << "\n        exceção: " << error.what() << std::endl;
			++failures;
		}
	}
	std::cout << (failures == 0 ? "Todos os testes passaram" : std::to_string(failures) + " teste(s) falharam")
			  << " (" << registry().size() << " no total)" << std::endl;
	return failures == 0 ? 0 : 1;
}

} // namespace orbislink_test

#define ORBISLINK_TEST(name)                                                                      \
	static void name();                                                                           \
	static orbislink_test::Registrar registrar_##name(#name, name);                               \
	static void name()

#define CHECK(expression)                                                                         \
	do                                                                                            \
	{                                                                                             \
		if(!(expression))                                                                         \
			orbislink_test::failCheck(#expression, __FILE__, __LINE__, "");                       \
	} while(false)

#define CHECK_EQ(actual, expected)                                                                \
	do                                                                                            \
	{                                                                                             \
		const auto &actualValue = (actual);                                                       \
		const auto &expectedValue = (expected);                                                   \
		if(!(actualValue == expectedValue))                                                       \
		{                                                                                         \
			std::ostringstream detail;                                                            \
			detail << "obtido=" << actualValue << " esperado=" << expectedValue;                  \
			orbislink_test::failCheck(#actual " == " #expected, __FILE__, __LINE__, detail.str());\
		}                                                                                         \
	} while(false)

#define TEST_MAIN()                                                                               \
	int main() { return orbislink_test::runAll(); }
