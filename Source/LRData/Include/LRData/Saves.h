#pragma once

// The original game's memory-card file (Save/<slot>/LEGORac1) and racer files (MENUDATA/DEFAULT.LRS, QBUILD.LRS): a
// header (the settings, unlocks, records, picks and controls) then the racer records, written in blocks of 127 data
// bytes and a checksum byte. Read only: our own saves are LRCore's.

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lr {

struct LRDATA_API OriginalSave {
	struct Piece {
		uint16_t type = 0;
		uint8_t x = 0, y = 0, rotation = 0, colour = 0;
		uint16_t part = 0;
		bool operator==(const Piece&) const = default;
	};
	struct Record {
		std::u16string name; // up to 14
		std::array<uint8_t, 4> looks = {}; // face, hat, legs, torso
		uint8_t expression = 0;
		bool stockCar = false;
		std::string chassis;
		std::vector<Piece> pieces;
		std::array<uint8_t, 7> trophies = {}; // per circuit: 0 none, 1 gold, 2 silver, 3 bronze
		bool operator==(const Record&) const = default;
	};
	struct Best {
		uint32_t lapMs = 0, raceMs = 0;
		std::u16string lapHolder, raceHolder;
		bool operator==(const Best&) const = default;
	};
	struct Pick {
		uint8_t record = 0, list = 0, slot = 0, layout = 0;
		bool operator==(const Pick&) const = default;
	};
	struct Layout {
		uint8_t kind = 0, subKind = 0, device = 0;
		std::array<uint32_t, 9> bindings = {};
		bool operator==(const Layout&) const = default;
	};
	uint8_t parts = 0, circuits = 0;
	uint16_t races = 0;
	std::array<Best, 13> best;
	uint8_t opponents = 0;
	std::array<uint8_t, 16> device = {};
	std::array<uint8_t, 2> kept = {};
	uint8_t music = 0, sound = 0, stereo = 0, language = 0, laps = 0;
	std::array<Pick, 2> picks;
	std::array<Layout, 5> layouts;
	std::vector<Record> records;

	static constexpr size_t HeaderSize = 0x477;
	static constexpr size_t RecordSize = 0x22d;

	// The data of a blocked file (checksums checked): false on a bad block.
	static bool unblock(std::span<const uint8_t> file, std::vector<uint8_t>& data);
	// From the unblocked data (padded: each record on whole blocks, as in a file; else back to back): false if it's
	// malformed. Bytes past the records are ignored.
	bool decode(std::span<const uint8_t> data, bool padded);
	bool decodeFile(std::span<const uint8_t> file);
	bool operator==(const OriginalSave&) const = default;
};

} // namespace lr
