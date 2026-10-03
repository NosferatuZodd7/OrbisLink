// SPDX-License-Identifier: LGPL-3.0-only
#include "orbislink/fpkg/fpkg_crypto.h"

#include "orbislink/fpkg/fpkg_keys.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace orbislink::fpkg {

// ───────────────────────────────── SHA-256

struct Sha256::Impl
{
	EVP_MD_CTX *ctx = nullptr;
};

Sha256::Sha256() : impl_(std::make_unique<Impl>())
{
	impl_->ctx = EVP_MD_CTX_new();
	if(!impl_->ctx || EVP_DigestInit_ex(impl_->ctx, EVP_sha256(), nullptr) != 1)
		throw std::runtime_error("SHA-256 is not available");
}

Sha256::~Sha256() { EVP_MD_CTX_free(impl_->ctx); }

void Sha256::update(const void *data, size_t size)
{
	if(size > 0)
		EVP_DigestUpdate(impl_->ctx, data, size);
}

Digest Sha256::finish()
{
	Digest out {};
	unsigned int len = 0;
	EVP_DigestFinal_ex(impl_->ctx, out.data(), &len);
	EVP_DigestInit_ex(impl_->ctx, EVP_sha256(), nullptr);
	return out;
}

Digest sha256(const void *data, size_t size)
{
	Sha256 hash;
	hash.update(data, size);
	return hash.finish();
}

// ───────────────────────────────── HMAC

struct HmacSha256::Impl
{
	Bytes key;
};

HmacSha256::HmacSha256(const uint8_t *key, size_t keySize) : impl_(std::make_unique<Impl>())
{
	impl_->key.assign(key, key + keySize);
}

HmacSha256::~HmacSha256() = default;

Digest HmacSha256::compute(const void *data, size_t size)
{
	return hmacSha256(impl_->key.data(), impl_->key.size(), data, size);
}

Digest hmacSha256(const uint8_t *key, size_t keySize, const void *data, size_t size)
{
	Digest out {};
	unsigned int len = 0;
	static const uint8_t empty = 0;
	if(!HMAC(EVP_sha256(), key, static_cast<int>(keySize),
		   size > 0 ? static_cast<const unsigned char *>(data) : &empty, size, out.data(), &len))
		throw std::runtime_error("HMAC-SHA-256 failed");
	return out;
}

// ───────────────────────────────── AES

namespace {

void aesCbc(uint8_t *data, size_t size, const uint8_t key[16], const uint8_t iv[16], bool encrypt)
{
	if(size % 16 != 0)
		throw std::runtime_error("AES-CBC needs whole blocks");
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	int len = 0;
	const bool ok = ctx
		&& EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key, iv, encrypt ? 1 : 0) == 1
		&& EVP_CIPHER_CTX_set_padding(ctx, 0) == 1
		&& EVP_CipherUpdate(ctx, data, &len, data, static_cast<int>(size)) == 1;
	EVP_CIPHER_CTX_free(ctx);
	if(!ok)
		throw std::runtime_error("AES-CBC failed");
}

} // namespace

void aes128CbcEncrypt(uint8_t *data, size_t size, const uint8_t key[16], const uint8_t iv[16])
{
	aesCbc(data, size, key, iv, true);
}

void aes128CbcDecrypt(uint8_t *data, size_t size, const uint8_t key[16], const uint8_t iv[16])
{
	aesCbc(data, size, key, iv, false);
}

struct XtsEncryptor::Impl
{
	EVP_CIPHER_CTX *enc = nullptr;
	EVP_CIPHER_CTX *dec = nullptr;
	uint8_t key[32];
};

XtsEncryptor::XtsEncryptor(const uint8_t dataKey[16], const uint8_t tweakKey[16])
	: impl_(std::make_unique<Impl>())
{
	// OpenSSL takes the data key first and the tweak key second.
	std::memcpy(impl_->key, dataKey, 16);
	std::memcpy(impl_->key + 16, tweakKey, 16);
	impl_->enc = EVP_CIPHER_CTX_new();
	impl_->dec = EVP_CIPHER_CTX_new();
	if(!impl_->enc || !impl_->dec
		|| EVP_EncryptInit_ex(impl_->enc, EVP_aes_128_xts(), nullptr, impl_->key, nullptr) != 1
		|| EVP_DecryptInit_ex(impl_->dec, EVP_aes_128_xts(), nullptr, impl_->key, nullptr) != 1)
		throw std::runtime_error("AES-XTS is not available");
}

XtsEncryptor::~XtsEncryptor()
{
	EVP_CIPHER_CTX_free(impl_->enc);
	EVP_CIPHER_CTX_free(impl_->dec);
}

namespace {

void xtsRun(EVP_CIPHER_CTX *ctx, bool encrypt, uint8_t *sector, size_t size, uint64_t number)
{
	uint8_t tweak[16] = {};
	for(int i = 0; i < 8; ++i)
		tweak[i] = static_cast<uint8_t>(number >> (8 * i));
	int len = 0;
	const bool ok = encrypt
		? EVP_EncryptInit_ex(ctx, nullptr, nullptr, nullptr, tweak) == 1
			&& EVP_EncryptUpdate(ctx, sector, &len, sector, static_cast<int>(size)) == 1
		: EVP_DecryptInit_ex(ctx, nullptr, nullptr, nullptr, tweak) == 1
			&& EVP_DecryptUpdate(ctx, sector, &len, sector, static_cast<int>(size)) == 1;
	if(!ok)
		throw std::runtime_error("AES-XTS failed");
}

} // namespace

void XtsEncryptor::encryptSector(uint8_t *sector, size_t size, uint64_t sectorNumber)
{
	xtsRun(impl_->enc, true, sector, size, sectorNumber);
}

void XtsEncryptor::decryptSector(uint8_t *sector, size_t size, uint64_t sectorNumber)
{
	xtsRun(impl_->dec, false, sector, size, sectorNumber);
}

// ───────────────────────────────── RSA

Bytes rsa2048ModExp(const uint8_t *value, size_t valueSize, const uint8_t *exponent,
	size_t exponentSize, const uint8_t modulus[256])
{
	BN_CTX *ctx = BN_CTX_new();
	BIGNUM *m = BN_bin2bn(value, static_cast<int>(valueSize), nullptr);
	BIGNUM *e = BN_bin2bn(exponent, static_cast<int>(exponentSize), nullptr);
	BIGNUM *n = BN_bin2bn(modulus, 256, nullptr);
	BIGNUM *r = BN_new();
	Bytes out(256);
	const bool ok = ctx && m && e && n && r && BN_mod_exp(r, m, e, n, ctx) == 1
		&& BN_bn2binpad(r, out.data(), 256) == 256;
	BN_free(m);
	BN_free(e);
	BN_free(n);
	BN_free(r);
	BN_CTX_free(ctx);
	if(!ok)
		throw std::runtime_error("RSA failed");
	return out;
}

Bytes rsa2048EncryptKey(const uint8_t modulus[256], const uint8_t *hash32)
{
	// 1. The PRNG is seeded with SHA-256(SHA-256(modulus || input)).
	uint8_t buffer[256 + 32];
	std::memcpy(buffer, modulus, 256);
	std::memcpy(buffer + 256, hash32, 32);
	const Digest inner = sha256(buffer, sizeof buffer);
	const Digest seedHash = sha256(inner.data(), inner.size());
	uint32_t seed[8];
	for(int i = 0; i < 8; ++i)
		seed[i] = (uint32_t(seedHash[4 * i]) << 24) | (uint32_t(seedHash[4 * i + 1]) << 16)
			| (uint32_t(seedHash[4 * i + 2]) << 8) | uint32_t(seedHash[4 * i + 3]);
	MersenneTwister mt(seed, 8);

	// 2. PKCS#1 type 2 padding, its random bytes drawn from that PRNG.
	uint8_t padded[256] = {};
	padded[0] = 0;
	padded[1] = 2;
	padded[223] = 0;
	std::memcpy(padded + 224, hash32, 32);
	for(int k = 2; k < 223;)
	{
		uint8_t source[48];
		for(int i = 0; i < 12; ++i)
		{
			const uint32_t v = mt.next();
			source[4 * i] = static_cast<uint8_t>(v >> 24);
			source[4 * i + 1] = static_cast<uint8_t>(v >> 16);
			source[4 * i + 2] = static_cast<uint8_t>(v >> 8);
			source[4 * i + 3] = static_cast<uint8_t>(v);
		}
		const Digest random = sha256(source, sizeof source);
		for(uint8_t r : random)
		{
			if(k >= 223)
				break;
			if(r != 0)
				padded[k++] = r;
		}
	}

	// 3. RSA with the public exponent.
	static const uint8_t e[3] = {0x01, 0x00, 0x01};
	return rsa2048ModExp(padded, sizeof padded, e, sizeof e, modulus);
}

Bytes rsa2048SignSha256(const Digest &hash, const uint8_t modulus[256],
	const uint8_t privateExponent[256])
{
	static const uint8_t digestInfo[19] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48,
		0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
	uint8_t em[256];
	em[0] = 0;
	em[1] = 1;
	const size_t padEnd = 256 - 32 - sizeof digestInfo - 1;
	std::memset(em + 2, 0xFF, padEnd - 2);
	em[padEnd] = 0;
	std::memcpy(em + padEnd + 1, digestInfo, sizeof digestInfo);
	std::memcpy(em + 256 - 32, hash.data(), 32);
	return rsa2048ModExp(em, sizeof em, privateExponent, 256, modulus);
}

// ───────────────────────────────── keys

Digest computeKeys(const std::string &contentId, const std::string &passcode, uint32_t index)
{
	if(contentId.size() != 36)
		throw std::runtime_error("the content ID must be 36 characters long");
	if(passcode.size() != 32)
		throw std::runtime_error("the passcode must be 32 characters long");
	uint8_t indexBytes[4] = {static_cast<uint8_t>(index >> 24), static_cast<uint8_t>(index >> 16),
		static_cast<uint8_t>(index >> 8), static_cast<uint8_t>(index)};
	uint8_t id[48] = {};
	std::memcpy(id, contentId.data(), 36);

	uint8_t data[96];
	const Digest a = sha256(indexBytes, 4);
	const Digest b = sha256(id, 48);
	std::memcpy(data, a.data(), 32);
	std::memcpy(data + 32, b.data(), 32);
	std::memcpy(data + 64, passcode.data(), 32);
	return sha256(data, sizeof data);
}

Bytes createKeystone(const std::string &passcode)
{
	static const uint8_t header[32] = {0x6b, 0x65, 0x79, 0x73, 0x74, 0x6f, 0x6e, 0x65, 0x02, 0x00,
		0x01, 0x00};
	const Digest fingerprint = hmacSha256(kKeystoneHmacKey, 32, passcode.data(), passcode.size());
	Bytes out(header, header + 32);
	out.insert(out.end(), fingerprint.begin(), fingerprint.end());
	const Digest final = hmacSha256(kKeystoneMacData, 32, out.data(), out.size());
	out.insert(out.end(), final.begin(), final.end());
	return out;
}

Digest pfsGenCryptoKey(const uint8_t *ekpfs, size_t ekpfsSize, const uint8_t *seed,
	size_t seedSize, uint32_t index)
{
	Bytes data(4 + seedSize);
	data[0] = static_cast<uint8_t>(index);
	data[1] = static_cast<uint8_t>(index >> 8);
	data[2] = static_cast<uint8_t>(index >> 16);
	data[3] = static_cast<uint8_t>(index >> 24);
	if(seedSize > 0)
		std::memcpy(data.data() + 4, seed, seedSize);
	return hmacSha256(ekpfs, ekpfsSize, data.data(), data.size());
}

// ───────────────────────────────── Mersenne Twister

MersenneTwister::MersenneTwister(const uint32_t *seed, size_t count)
{
	mt_[0] = 0x12BD6AA;
	for(int i = 1; i < N; ++i)
		mt_[i] = uint32_t(i) + 0x6C078965u * (mt_[i - 1] ^ (mt_[i - 1] >> 30));

	uint32_t stateIdx = 1, seedIdx = 0;
	for(size_t length = std::max<size_t>(N, count); length > 0; --length)
	{
		mt_[stateIdx] = (mt_[stateIdx] ^ ((mt_[stateIdx - 1] ^ (mt_[stateIdx - 1] >> 30)) * 0x19660Du))
			+ seed[seedIdx] + seedIdx;
		++stateIdx;
		++seedIdx;
		if(stateIdx >= uint32_t(N))
		{
			mt_[0] = mt_[N - 1];
			stateIdx = 1;
		}
		if(seedIdx >= count)
			seedIdx = 0;
	}
	for(int length = 0; length < N - 1; ++length)
	{
		mt_[stateIdx] = (mt_[stateIdx] ^ ((mt_[stateIdx - 1] ^ (mt_[stateIdx - 1] >> 30)) * 0x5D588B65u))
			- stateIdx;
		++stateIdx;
		if(stateIdx >= uint32_t(N))
		{
			mt_[0] = mt_[N - 1];
			stateIdx = 1;
		}
	}
	mt_[0] = 1u << 31;
	index_ = N;
}

uint32_t MersenneTwister::next()
{
	static const uint32_t mag01[2] = {0, 0x9908b0dfu};
	constexpr int M = 397;
	uint32_t y;
	if(index_ >= N)
	{
		int kk;
		for(kk = 0; kk < N - M; ++kk)
		{
			y = (mt_[kk] & 0x80000000u) | (mt_[kk + 1] & 0x7fffffffu);
			mt_[kk] = mt_[kk + M] ^ (y >> 1) ^ mag01[y & 1];
		}
		for(; kk < N - 1; ++kk)
		{
			y = (mt_[kk] & 0x80000000u) | (mt_[kk + 1] & 0x7fffffffu);
			mt_[kk] = mt_[kk + M - N] ^ (y >> 1) ^ mag01[y & 1];
		}
		y = (mt_[N - 1] & 0x80000000u) | (mt_[0] & 0x7fffffffu);
		mt_[N - 1] = mt_[M - 1] ^ (y >> 1) ^ mag01[y & 1];
		index_ = 0;
	}
	y = mt_[index_++];
	y ^= y >> 11;
	y ^= (y << 7) & 0x9d2c5680u;
	y ^= (y << 15) & 0xefc60000u;
	y ^= y >> 18;
	return y;
}

} // namespace orbislink::fpkg
