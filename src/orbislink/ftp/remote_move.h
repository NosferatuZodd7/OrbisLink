// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"

#include <string>

namespace orbislink {

// Moves a file or folder on the console to `to` (its full new path).
//
// A rename first: on one disk it is instant. Between disks (/data and a USB
// drive, say) a console's server cannot rename — rename() fails there and the
// server answers 550 — and none has a copy command either. Then it is copied
// through this PC: each file downloaded into `scratchDir`, sent to its new
// place and its size checked there; only when all of it is in place is the
// original deleted. Nothing is overwritten: when `to` is taken, the move is
// refused. `progress` counts each byte twice (down, then up); returning
// false cancels, and the original stays as it was. `throughPc` says which
// way it went.
FtpResult moveOnConsole(FtpClient &ftp, const std::string &from, const std::string &to,
	const std::string &scratchDir, const FtpProgressCallback &progress = nullptr, bool *throughPc = nullptr);

} // namespace orbislink
