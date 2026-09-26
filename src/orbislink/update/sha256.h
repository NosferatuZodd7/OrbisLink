// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace orbislink {

// Self-contained SHA-256, with no dependencies.
//
// It deliberately does not use OpenSSL: the OrbisLink core builds without
// Remote Play, and in that case there is no OpenSSL in the build. It is a
// hundred lines and the algorithm has public test vectors — it is covered
// in tests/test_update.cpp.
class Sha256
{
public:
	Sha256();
	void update(const void *data, size_t size);
	void update(const std::string &data);
	// Returns the 64 characters in lowercase. After this the object must
	// not be reused.
	std::string hex();

private:
	void compress(const uint8_t block[64]);

	uint32_t state_[8];
	uint8_t buffer_[64];
	size_t buffered_ = 0;
	uint64_t total_ = 0;
};

std::string sha256Hex(const std::string &data);
// Empty if the file cannot be read.
std::string sha256File(const std::string &path);

} // namespace orbislink
