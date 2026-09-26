// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/stream_types.h"

#include <string>
#include <vector>

namespace orbislink {

// Descoberta de consolas, em cima do chiaki-lib.
//
// Duas utilizações: perguntar a um endereço concreto (é o caso normal, o
// utilizador já sabe o IP da consola) e varrer a rede local à procura de
// consolas (para o assistente de primeira utilização).
class StreamDiscovery
{
public:
	// Pergunta directamente a `address`. Devolve found=false se ninguém
	// responder dentro de `timeoutMs`.
	//
	// `ps4Port` existe só para os testes poderem pôr uma consola falsa numa
	// porta alta: a 987 é privilegiada e o CI não corre como root. Em uso
	// normal fica a 0, que significa a porta do protocolo.
	static HostInfo probe(const std::string &address, int timeoutMs = 2000, uint16_t ps4Port = 0);

	// O mesmo que probe(), mas sem ficar no diário da tentativa nem no
	// registo: é o que a verificação periódica das consolas usa, que corre
	// de poucos em poucos segundos, também a meio de uma sessão.
	static HostInfo peek(const std::string &address, int timeoutMs = 2000, uint16_t ps4Port = 0);

	// Varre a rede local. Devolve todas as consolas que responderem.
	static std::vector<HostInfo> scan(int timeoutMs = 3000);

	// Acorda uma consola em repouso. Precisa da credencial que vem do
	// registo; sem ela a consola ignora o pacote.
	static bool wakeup(const std::string &address, uint64_t credential, bool ps5,
		std::string *error = nullptr);
};

} // namespace orbislink
