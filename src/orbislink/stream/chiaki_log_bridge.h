// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <chiaki/log.h>

#include <cstdint>

namespace orbislink {

// Forwards chiaki-lib's log to the OrbisLink log, so there is a single
// file to read when something goes wrong.
ChiakiLog *chiakiLog();

// Turns chiaki's debug messages on or off (there are many).
void setChiakiVerbose(bool verbose);

// The reason the console gave the last time it refused a request
// ("RP-Application-Reason", e.g. 0x80108b02), or 0 if it gave none.
// chiaki only writes it to the log, so that is where it is taken from. Reading clears it.
uint32_t takeApplicationReason();

} // namespace orbislink
