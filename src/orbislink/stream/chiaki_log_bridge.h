// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <chiaki/log.h>

#include <cstdint>

namespace orbislink {

// Encaminha o registo do chiaki-lib para o registo do OrbisLink, para haver
// um único ficheiro a ler quando alguma coisa corre mal.
ChiakiLog *chiakiLog();

// Liga ou desliga as mensagens de depuração do chiaki (são muitas).
void setChiakiVerbose(bool verbose);

// O motivo que a consola deu da última vez que recusou um pedido
// ("RP-Application-Reason", ex.: 0x80108b02), ou 0 se não deu nenhum. O
// chiaki só o escreve no registo, por isso é daí que se tira. Ler apaga-o.
uint32_t takeApplicationReason();

} // namespace orbislink
