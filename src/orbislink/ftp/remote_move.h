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

// A file or folder of the console with everything in it: what is in a
// folder goes first (a console's server refuses RMD for a folder that is not
// empty), then the folder. `progress` gets the entries deleted and how many
// there are; returning false stops, and what is left stays.
FtpResult removeOnConsole(FtpClient &ftp, const std::string &path, bool isDirectory,
	const std::function<bool(int done, int total)> &progress = nullptr);

// Everything under a folder of the console: its files (path and size, and
// where each goes under `localRoot` on this PC) and its folders, the top
// one included.
struct RemoteTreeFile
{
	std::string remote;
	std::string relative; // "sub/file.bin"
	int64_t size = 0;
};
FtpResult listTreeOnConsole(FtpClient &ftp, const std::string &root, std::vector<RemoteTreeFile> *files,
	std::vector<std::string> *folders = nullptr);

} // namespace orbislink
