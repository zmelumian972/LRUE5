#pragma once

// SHA-256 (FIPS 180-4): the digests that key the data cache and locate the executable's tables.

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace lr {

using Digest = std::array<uint8_t, 32>;

LRDATA_API Digest sha256(std::span<const uint8_t> bytes);
LRDATA_API std::string hex(const Digest& digest);
LRDATA_API bool parseHex(std::string_view text, Digest& digest); // 64 hex digits

} // namespace lr
