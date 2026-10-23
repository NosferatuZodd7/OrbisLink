// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/installer/error_codes.h"

#include "orbislink/common/tr.h"

#include <cstdio>
#include <map>

namespace orbislink {

namespace {

// libkernel: values confirmed in
// OpenOrbis-PS4-Toolchain/include/orbis/_types/errors.h (ORBIS_KERNEL_ERROR_*).
// BGFT (the console's download and install tasks): the names in etaHEN's
// error table (Source Code/util/include/error_translator.hpp, the
// SCE_BGFT_ERROR_* list), the ones an install from OrbisLink can meet.
// TODO: AppInstUtil (0x8024xxxx) and libhttp still have no citable source;
// those codes are shown in hexadecimal in the UI.
const std::map<uint32_t, const char *> &knownErrors()
{
	static const std::map<uint32_t, const char *> table = {
		{ 0x80020001u, QT_TRANSLATE_NOOP("Messages", "Operation not permitted on the console (EPERM).") },
		{ 0x80020002u, QT_TRANSLATE_NOOP("Messages", "The console could not find the file or path "
			"(ENOENT).") },
		{ 0x80020005u, QT_TRANSLATE_NOOP("Messages", "Input/output error on the console (EIO).") },
		{ 0x8002000Cu, QT_TRANSLATE_NOOP("Messages", "The console ran out of memory (ENOMEM).") },
		{ 0x8002000Du, QT_TRANSLATE_NOOP("Messages", "Access denied on the console (EACCES).") },
		{ 0x80020010u, QT_TRANSLATE_NOOP("Messages", "The resource is busy on the console (EBUSY).") },
		{ 0x80020011u, QT_TRANSLATE_NOOP("Messages", "Already exists (EEXIST).") },
		{ 0x80020016u, QT_TRANSLATE_NOOP("Messages", "Invalid request for the console (EINVAL).") },
		{ 0x8002001Bu, QT_TRANSLATE_NOOP("Messages", "File too large for the console (EFBIG).") },
		{ 0x8002001Cu, QT_TRANSLATE_NOOP("Messages", "Not enough space on the console (ENOSPC).") },
		{ 0x8002001Eu, QT_TRANSLATE_NOOP("Messages", "Read-only file system (EROFS).") },
		{ 0x80020023u, QT_TRANSLATE_NOOP("Messages", "The console asked to try again (EAGAIN).") },
		{ 0x80020033u, QT_TRANSLATE_NOOP("Messages", "The console could not reach the network "
			"(ENETUNREACH).") },
		{ 0x80020035u, QT_TRANSLATE_NOOP("Messages", "Connection aborted (ECONNABORTED).") },
		{ 0x80020036u, QT_TRANSLATE_NOOP("Messages", "The connection was reset by the other side "
			"(ECONNRESET).") },
		{ 0x8002003Cu, QT_TRANSLATE_NOOP("Messages", "The console timed out (ETIMEDOUT).") },
		{ 0x8002003Du, QT_TRANSLATE_NOOP("Messages", "The console could not connect to the PC "
			"(ECONNREFUSED). Check the Windows firewall and "
			"that both are on the same network.") },
		{ 0x80020041u, QT_TRANSLATE_NOOP("Messages", "The console cannot reach the PC (EHOSTUNREACH).") },
		{ 0x80020055u, QT_TRANSLATE_NOOP("Messages", "The operation was cancelled (ECANCELED).") },

		{ 0x80990004u, QT_TRANSLATE_NOOP("Messages", "The console's installer refused the request as "
			"invalid (BGFT INVALID_ARGUMENT).") },
		{ 0x80990015u, QT_TRANSLATE_NOOP("Messages", "The console already has an install task for this "
			"package, left by an earlier try (BGFT TASK_DUPLICATED). Delete it from the console's "
			"Downloads list, or restart the console, then install again.") },
		{ 0x80990018u, QT_TRANSLATE_NOOP("Messages", "The console's download list is full: delete the "
			"finished or failed entries in its Downloads list (BGFT TASK_ENTRY_NOSPC).") },
		{ 0x8099008Cu, QT_TRANSLATE_NOOP("Messages", "The console's download list is full: delete the "
			"finished or failed entries in its Downloads list (BGFT DOWNLOAD_TASK_ENTRY_NOSPC).") },
		{ 0x80990027u, QT_TRANSLATE_NOOP("Messages", "The console could not download the package from "
			"the PC: the PC's server answered with an error (BGFT HTTP_STATUS).") },
		{ 0x8099002Cu, QT_TRANSLATE_NOOP("Messages", "The download from the PC broke off (BGFT "
			"HTTP_RECV_IO). Check the network and try again.") },
		{ 0x80990045u, QT_TRANSLATE_NOOP("Messages", "The console could not connect to the PC to download "
			"the package (BGFT HTTP_NOT_CONNECTED). Check the Windows firewall.") },
		{ 0x80990038u, QT_TRANSLATE_NOOP("Messages", "The package does not match what the console "
			"expected (BGFT CONTENTID_UNMATCH).") },
		{ 0x80990053u, QT_TRANSLATE_NOOP("Messages", "The package is damaged (BGFT FILE_BROKEN). Make it "
			"or copy it again.") },
		{ 0x80990079u, QT_TRANSLATE_NOOP("Messages", "The console does not take this kind of package "
			"(BGFT UNSUPPORTED_PACKAGE).") },
		{ 0x80990082u, QT_TRANSLATE_NOOP("Messages", "The package needs a newer system software (BGFT "
			"NEED_SYSTEM_UPDATE).") },
		{ 0x80990086u, QT_TRANSLATE_NOOP("Messages", "The console is already downloading this content "
			"(BGFT CONTENT_ALREADY_DOWNLOADING).") },
		{ 0x80990087u, QT_TRANSLATE_NOOP("Messages", "The disc version of this game is installed (BGFT "
			"DISC_APPLICATION_ALREADY_INSTALLED).") },
		{ 0x80990088u, QT_TRANSLATE_NOOP("Messages", "This same game is already installed (BGFT "
			"SAME_APPLICATION_ALREADY_INSTALLED). Delete it on the console to install it again.") },
		{ 0x8099008Bu, QT_TRANSLATE_NOOP("Messages", "The game is running on the console: close it "
			"first (BGFT APPLICATION_IS_RUNNING).") },
		{ 0x80990039u, QT_TRANSLATE_NOOP("Messages", "Not enough space on the console (BGFT "
			"DEVICE_NOSPC).") },
		{ 0x80990085u, QT_TRANSLATE_NOOP("Messages", "Not enough space on the console (BGFT "
			"DEVICE_NOSPC_KERNEL).") },
		{ 0x8099008Du, QT_TRANSLATE_NOOP("Messages", "Not enough space on the console (BGFT "
			"DOWNLOAD_DEVICE_NOSPC).") },
		{ 0x80990106u, QT_TRANSLATE_NOOP("Messages", "Not enough space on the console's extended "
			"storage (BGFT DOWNLOAD_DEVICE_EXT_NOSPC).") },
		{ 0x80990107u, QT_TRANSLATE_NOOP("Messages", "Not enough space on the console's extended "
			"storage (BGFT DEVICE_EXT_NOSPC).") },
	};
	return table;
}

} // namespace

std::string formatErrorCode(uint32_t code)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "0x%08X", code);
	return buffer;
}

std::string describeConsoleError(uint32_t code)
{
	if(code == 0)
		return std::string();
	const auto &table = knownErrors();
	auto it = table.find(code);
	if(it != table.end())
		return std::string(it->second) + " (" + formatErrorCode(code) + ")";
	return std::string(QT_TRANSLATE_NOOP("Messages", "The console returned an error")) + ": " + formatErrorCode(code);
}

bool isOutOfSpaceError(uint32_t code)
{
	switch(code)
	{
		case 0x8002001Cu: // ENOSPC
		case 0x80990039u: // BGFT DEVICE_NOSPC
		case 0x80990085u: // BGFT DEVICE_NOSPC_KERNEL
		case 0x8099008Du: // BGFT DOWNLOAD_DEVICE_NOSPC
		case 0x80990106u: // BGFT DOWNLOAD_DEVICE_EXT_NOSPC
		case 0x80990107u: // BGFT DEVICE_EXT_NOSPC
			return true;
		default: return false;
	}
}

} // namespace orbislink
