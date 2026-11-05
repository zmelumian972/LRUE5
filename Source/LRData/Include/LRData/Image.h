#pragma once

// The game's .BMP textures: its own format (not Windows'), 4- or 8-bit paletted or 24-bit, compressed in sections.

#include "LRData/Api.h"

#include <cstdint>
#include <span>
#include <vector>

namespace lr {

struct LRDATA_API Image {
	uint32_t width = 0;
	uint32_t height = 0;
	uint8_t bits = 0;          // as stored: 4 or 8 (paletted) or 24
	std::vector<uint8_t> rgba; // width * height * 4, rows top first

	// False on a malformed file.
	bool decodeBmp(std::span<const uint8_t> bytes);
	bool operator==(const Image&) const = default;
};

} // namespace lr
