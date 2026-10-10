// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"

#include <atomic>
#include <string>

namespace orbislink {

// ShadowMountPlus on a PS5: what puts homebrew and game folders or images on
// the home screen. Both ask it to look again after something new was put
// where it looks.
namespace shadowmount {

// Its API (port 10101), when it lets the network in (api_bind_address).
bool ask(const std::string &address, const std::string &route, const std::string &body);
// Started again, which scans everything at once: a new copy asks the one
// running to stop and takes over ("[RESTART]" in its main.c). Its file from
// the console (etaHEN's payloads or plugins, the autoloader, PLDMGR), else
// from the payload library, to the ELF loader. Blocks: on a worker.
bool restart(const FtpClient::Config &config, const std::string &address, const std::atomic<bool> *cancel);
// Asks it to look now: its API, else a restart. "api", "restarted", or ""
// when neither worked.
std::string rescan(const FtpClient::Config &config, const std::string &address, const std::atomic<bool> *cancel);

} // namespace shadowmount
} // namespace orbislink
