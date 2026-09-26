// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/installer/error_codes.h"

#include "orbislink/common/tr.h"

#include <cstdio>
#include <map>

namespace orbislink {

namespace {

// Values confirmed in
// OpenOrbis-PS4-Toolchain/include/orbis/_types/errors.h (ORBIS_KERNEL_ERROR_*).
// TODO: the specific BGFT/AppInstUtil codes (families 0x8099xxxx and
// 0x8024xxxx) are not published in any citable source; until they are
// confirmed, those codes are shown in hexadecimal in the UI.
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

bool isOutOfSpaceError(uint32_t code) { return code == 0x8002001Cu; }

} // namespace orbislink
