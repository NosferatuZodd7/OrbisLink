// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/stream_types.h"

#include <functional>
#include <memory>
#include <string>

namespace orbislink {

// Registo do PC na consola (§5.2 do chiaki: "Add Device").
//
// Na consola: Definições → Definições de Ligação do Remote Play →
// Adicionar Dispositivo. Aparece um PIN de 8 dígitos. Aqui é preciso esse
// PIN e o Account ID da PSN em base64 — a consola só aceita o registo se
// os dois baterem certo.
//
// O que sai é a chave de registo e a rp_key, que ficam guardadas: a partir
// daí liga-se sem PIN nenhum.
class StreamRegistration
{
public:
	struct Request
	{
		std::string address;
		std::string accountIdBase64; // Account ID da PSN, 8 bytes em base64
		uint32_t pin = 0;            // os 8 dígitos que a consola mostra
		int target = 0;              // ChiakiTarget, vindo da descoberta
		bool ps5 = false;
	};

	using Finished = std::function<void(bool ok, StreamCredentials credentials, std::string error)>;

	StreamRegistration();
	~StreamRegistration();

	// Visível porque o callback em C do chiaki precisa de lhe chegar.
	struct Impl;

	// Arranca o registo. `finished` é chamado numa thread do chiaki.
	bool start(const Request &request, Finished finished, std::string *error = nullptr);
	void cancel();
	bool running() const;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace orbislink
