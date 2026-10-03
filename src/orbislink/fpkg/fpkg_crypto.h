// SPDX-License-Identifier: LGPL-3.0-only
//
// The cryptography of a fake PKG, on OpenSSL. Ported from LibOrbisPkg
// (Util/Crypto.cs, Util/MersenneTwister.cs, maxton, LGPL-3.0): the same
// inputs give the same bytes, which is what lets the tests compare a package
// built here with one built by PkgTool.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orbislink::fpkg {

using Bytes = std::vector<uint8_t>;
using Digest = std::array<uint8_t, 32>;

// SHA-256, in one go or fed in pieces.
class Sha256
{
public:
	Sha256();
	~Sha256();
	Sha256(const Sha256 &) = delete;
	Sha256 &operator=(const Sha256 &) = delete;
	void update(const void *data, size_t size);
	Digest finish();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

Digest sha256(const void *data, size_t size);
inline Digest sha256(const Bytes &data) { return sha256(data.data(), data.size()); }

// HMAC-SHA-256 with a key that is reused many times (the PFS block
// signatures): the key is set up once.
class HmacSha256
{
public:
	HmacSha256(const uint8_t *key, size_t keySize);
	~HmacSha256();
	HmacSha256(const HmacSha256 &) = delete;
	HmacSha256 &operator=(const HmacSha256 &) = delete;
	Digest compute(const void *data, size_t size);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

Digest hmacSha256(const uint8_t *key, size_t keySize, const void *data, size_t size);

// AES-128-CBC without padding; the size must be a multiple of 16.
void aes128CbcEncrypt(uint8_t *data, size_t size, const uint8_t key[16], const uint8_t iv[16]);
void aes128CbcDecrypt(uint8_t *data, size_t size, const uint8_t key[16], const uint8_t iv[16]);

// AES-128-XTS over 0x1000-byte sectors, the tweak being the sector number.
class XtsEncryptor
{
public:
	XtsEncryptor(const uint8_t dataKey[16], const uint8_t tweakKey[16]);
	~XtsEncryptor();
	XtsEncryptor(const XtsEncryptor &) = delete;
	XtsEncryptor &operator=(const XtsEncryptor &) = delete;
	void encryptSector(uint8_t *sector, size_t size, uint64_t sectorNumber);
	void decryptSector(uint8_t *sector, size_t size, uint64_t sectorNumber);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// value^exponent mod modulus, everything big-endian, 256-byte result.
Bytes rsa2048ModExp(const uint8_t *value, size_t valueSize, const uint8_t *exponent,
	size_t exponentSize, const uint8_t modulus[256]);
// The "encryption" the PKG uses for its keys: deterministic padding drawn
// from a Mersenne Twister seeded with the input, then RSA with e = 65537.
Bytes rsa2048EncryptKey(const uint8_t modulus[256], const uint8_t *hash32);
// PKCS#1 v1.5 signature of a SHA-256 digest.
Bytes rsa2048SignSha256(const Digest &hash, const uint8_t modulus[256],
	const uint8_t privateExponent[256]);

// The PKG's derived keys: SHA-256(SHA-256(index) || SHA-256(content id
// padded to 48) || passcode). Index 1 is the EKPFS.
Digest computeKeys(const std::string &contentId, const std::string &passcode, uint32_t index);
// The sce_sys/keystone file of an application PKG.
Bytes createKeystone(const std::string &passcode);

// PFS keys from the EKPFS and the image seed.
Digest pfsGenCryptoKey(const uint8_t *ekpfs, size_t ekpfsSize, const uint8_t *seed,
	size_t seedSize, uint32_t index);

// The Mersenne Twister of the padding above, initialised by array.
class MersenneTwister
{
public:
	explicit MersenneTwister(const uint32_t *seed, size_t count);
	uint32_t next();

private:
	static constexpr int N = 624;
	uint32_t mt_[N];
	int index_ = N;
};

} // namespace orbislink::fpkg
