// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

std::string trim(const std::string &s);
bool iequals(const std::string &a, const std::string &b);
bool startsWith(const std::string &s, const std::string &prefix);
bool endsWith(const std::string &s, const std::string &suffix);
std::string toLower(const std::string &s);
std::vector<std::string> split(const std::string &s, char sep, bool keepEmpty = true);
std::string join(const std::vector<std::string> &parts, const std::string &sep);

// Unguessable hexadecimal token for the local HTTP server URLs (§5.3).
std::string randomToken(size_t bytes = 16);

// Keeps only safe characters in the name exposed in the URL/FTP.
std::string sanitizeFileName(const std::string &name);
std::string urlEncodePath(const std::string &path);
std::string urlDecode(const std::string &text);

std::string baseName(const std::string &path);
std::string fileExtensionLower(const std::string &path);
std::string joinPath(const std::string &a, const std::string &b);
// Normalises an absolute FTP path (resolves "." and "..", removes duplicate slashes).
std::string normalizeRemotePath(const std::string &path);

// Size of a local file in bytes; -1 if it does not exist or cannot be read.
int64_t fileSize(const std::string &path);
bool fileExists(const std::string &path);
bool directoryExists(const std::string &path);

std::string humanBytes(int64_t bytes);
std::string humanDuration(int64_t seconds);

int64_t nowUnixSeconds();
int64_t monotonicMillis();

} // namespace orbislink
