// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <cstdint>

namespace orbislink::fpkg {

// Encrypts the IMAGE_KEY entry (EKPFS).
extern const uint8_t kFakeKeysetModulus[256];
// Signs license.dat (PKCS#1 v1.5, SHA-256).
extern const uint8_t kDebugRifModulus[256];
extern const uint8_t kDebugRifPrivateExponent[256];
extern const uint8_t kRifDebugKey[16];
extern const uint8_t kKeystoneHmacKey[32];
extern const uint8_t kKeystoneMacData[32];
// Encrypt the passcode and derived keys in ENTRY_KEYS; index 3 also signs
// the PKG header.
extern const uint8_t kPkgPublicKeys[7][256];

} // namespace orbislink::fpkg
