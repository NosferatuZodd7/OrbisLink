// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <chiaki/log.h>

namespace orbislink {

// Encaminha o registo do chiaki-lib para o registo do OrbisLink, para haver
// um único ficheiro a ler quando alguma coisa corre mal.
ChiakiLog *chiakiLog();

// Liga ou desliga as mensagens de depuração do chiaki (são muitas).
void setChiakiVerbose(bool verbose);

} // namespace orbislink
