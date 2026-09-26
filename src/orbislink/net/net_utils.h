// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

struct LocalInterface
{
	std::string name;
	std::string address; // IPv4 em texto
	std::string netmask;
	bool loopback = false;
};

// Interfaces IPv4 ativas da máquina.
std::vector<LocalInterface> localInterfaces();

bool parseIPv4(const std::string &text, uint32_t *outHostOrder);
bool sameSubnet(const std::string &a, const std::string &b, const std::string &netmask);

// Escolhe o IP local cuja sub-rede contém `consoleIp` (§5.3). Se nenhuma
// interface corresponder devolve a primeira não-loopback; string vazia se não
// houver nenhuma.
std::string localAddressForConsole(const std::string &consoleIp);

// Ligação TCP com timeout: base da verificação de serviços (§5.1).
// `banner`, se não for nulo, recebe a primeira linha enviada pelo servidor
// (usado para confirmar o "220" do FTP).
bool tcpProbe(const std::string &host, uint16_t port, int timeoutMs, std::string *banner = nullptr);

// Porta TCP livre a partir de `preferred` (§5.3: se ocupada, escolher outra).
bool findFreePort(const std::string &bindAddress, uint16_t preferred, uint16_t *outPort,
	int maxAttempts = 64);

} // namespace orbislink
