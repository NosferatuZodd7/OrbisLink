// SPDX-License-Identifier: GPL-3.0-only
//
// Fake-signed SELF ("fSELF") from a plain PS4 ELF.
//
// A dump of a game (GoldHEN's, for one) holds its executables decrypted, as
// plain ELF files; the console only starts them from a fake package once they
// are wrapped again as fake-signed SELFs. This is that wrapping, after flatz'
// make_fself.py as ported in OpenOrbis' create-fself (GPL-3.0).
#pragma once

#include "orbislink/fpkg/fpkg_crypto.h"

#include <string>

namespace orbislink::fpkg {

// True for a 64-bit little-endian ELF ("\x7FELF"), false for a SELF or
// anything else.
bool isPlainElf(const uint8_t *data, size_t size);

// The fSELF for `elf`. Empty, with `error` set, when it is not an ELF that
// can be wrapped.
Bytes makeFself(const Bytes &elf, std::string *error);

} // namespace orbislink::fpkg
