// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace orbislink {

// Estado da consola tal como responde ao pedido de descoberta (porta
// 987/UDP no PS4 — ver docs/validacao.md).
enum class HostState { Unknown, Ready, Standby };

const char *hostStateName(HostState state);

// O que a consola diz sobre si própria quando responde à descoberta.
struct HostInfo
{
	bool found = false;
	HostState state = HostState::Unknown;
	bool ps5 = false;
	std::string address;
	std::string name;           // nome que o utilizador deu à consola
	std::string id;             // host-id, o MAC sem separadores
	std::string systemVersion;  // ex.: "09000000"
	std::string runningAppName;
	std::string runningAppTitleId;
	uint16_t requestPort = 0;
	// Valor de ChiakiTarget correspondente à versão de sistema: é o que
	// diz ao chiaki que protocolo falar.
	int target = 0;
};

// O que fica guardado depois de registar a consola. Sem isto não há
// sessão: a chave de registo e a "morning" são o que autentica o PC.
struct StreamCredentials
{
	bool valid = false;
	std::string nickname;
	std::string hostId;       // MAC em hexadecimal, para casar com a descoberta
	std::string registKey;    // rp_regist_key, texto
	std::string rpKeyHex;     // rp_key (16 bytes) em hexadecimal
	uint32_t rpKeyType = 0;
	int target = 0;
	bool ps5 = false;
	// Credencial de 64 bits usada para acordar a consola em repouso: é a
	// própria chave de registo lida como hexadecimal.
	uint64_t wakeupCredential() const;
};

} // namespace orbislink
