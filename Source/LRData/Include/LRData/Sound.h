#pragma once

// The game's sounds (.PCM in LEGO.JAM) and music (.tun beside it): 4-bit ADPCM, mono or stereo (bytes alternating
// left and right), after an "ALP " header (data offset, channel count, and for sounds the sample rate).

#include "LRData/Api.h"

#include <cstdint>
#include <span>
#include <vector>

namespace lr {

struct LRDATA_API Sound {
	uint32_t channels = 0;
	uint32_t rate = 0;
	std::vector<int16_t> samples; // interleaved

	// False on a malformed file. Music has no rate in its header: it plays at 22050 Hz.
	bool decode(std::span<const uint8_t> bytes, bool music);
	bool operator==(const Sound&) const = default;
};

} // namespace lr
