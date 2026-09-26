// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>

namespace orbislink {

// Uma versão do OrbisLink: "0.1.8", "v0.1.8", "0.1.8-dev.42".
//
// A ordem das pré-lançamentos é a do semver: 0.1.8-dev.2 vem ANTES de
// 0.1.8. É o que faz o canal de testes funcionar — quem está na 0.1.8-dev.2
// tem de receber a 0.1.8-dev.3 e depois a 0.1.8 final, e quem está na 0.1.8
// final não pode ser empurrado para trás por uma dev.
struct Version
{
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::string pre;      // "dev.42", vazio no lançamento final
	bool valid = false;

	std::string toString() const;
};

Version parseVersion(const std::string &text);

// <0 se a for anterior a b, 0 se iguais, >0 se a for posterior.
// Uma versão inválida conta como a mais antiga possível.
int compareVersions(const Version &a, const Version &b);
int compareVersions(const std::string &a, const std::string &b);

} // namespace orbislink
