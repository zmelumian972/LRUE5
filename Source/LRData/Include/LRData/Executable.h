#pragma once

// The game's tables that live only in LEGORacers.exe: read from the player's executable when the
// data cache is built, never embedded in the source. Each is described here by its length and the SHA-256 of its bytes
// (a digest is not the data) and found at its known offset, else by sliding a window of its length over the
// executable's initialised data sections.

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace lr {

struct LRDATA_API ExecutableTables {
	std::array<uint16_t, 1024> random = {};          // the random table every stream reads (each < 1024)
	std::array<std::string, 13> cheats;              // the cheat codes by cheat number
	std::array<std::string, 9> languages;            // the languages' folders (lower case) by language number
	std::array<uint32_t, 5> frameColours = {};       // the setup screens' preview-frame colours, 0xAARRGGBB
	std::array<uint8_t, 25> frameMap = {};           // circuit/place to a frame colour
	std::array<int32_t, 6> placePoints = {};         // circuit points by place
	std::array<std::array<float, 3>, 3> curseDrop = {}; // where the curse hazard drops
	std::array<std::array<float, 3>, 3> lavaSpots = {};
	std::array<float, 3> lavaStarts = {}, lavaEnds = {}; // the lava hazard's frame windows
	std::array<std::array<float, 3>, 3> builderEyes = {}; // the car builder's camera places
	// The car builder's keypad steps: the grid step (x, y) of each direction 0..7 from each view slot.
	std::array<std::array<int32_t, 8>, 8> builderStepX = {}, builderStepY = {};

	bool operator==(const ExecutableTables&) const = default;

	// Finds and reads every table; false (with why in `error`) if one isn't there: not a supported version.
	bool locate(std::span<const uint8_t> exe, std::string& error);
};

// The executable this was made against.
inline constexpr size_t KnownExecutableSize = 802816;
inline constexpr const char* KnownExecutableDigest = "fe9e881cfa3ffc208db23ab8057ecc5697bad94d2752e8eccf6c79b10a5c1103";

} // namespace lr
