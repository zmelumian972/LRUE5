#pragma once

// What cars and drivers are built from, in MENUDATA/PIECEDB and MENUDATA/PARTDB:
//
// - The piece library (LPIECEHI.LEB, LPIECELO.LEB: the same pieces, finer and coarser): each piece's shape on the build
//   grid (its cells' tops and bottoms in plates, which can carry or rest on studs) and its geometry (commands into
//   shared position, normal and texture coordinate lists). Types below 2048 are the chassis, 2048 the stud.
// - The car sets (CRSTMGR.LEB and its twelve set files, L_COLORS.LEB): each set's chassis, part number and choices of
//   piece and colour.
// - The driver part catalog (BODYPART.PCB): the hats, faces, torsos and legs and what unlocks each.
//
// All token streams.

#include "LRData/Api.h"
#include "LRData/Index.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

class Archive;

struct LRDATA_API PieceLibrary {
	static constexpr int32_t StudType = 2048; // and the first type that isn't a chassis

	struct Cell {
		uint8_t top = 0, bottom = 0; // plates above the piece's base
		bool studded = false; // something can stand on its top
		bool rests = false; // it can sit on a studded top
		bool flush = false; // it may sit flush on a studded top without colliding
		bool solid() const { return top || bottom; }
		// The bottom, or 0 when it's above the top.
		uint8_t base() const { return bottom > top ? 0 : bottom; }
		bool operator==(const Cell&) const = default;
	};
	struct LRDATA_API Piece {
		std::string name; // 8 characters
		int32_t type = 0;
		uint8_t width = 0, height = 0; // studs along x and y, unrotated
		uint32_t cells = 0; // its first in `cells`: column by column
		uint32_t first = 0, count = 0; // its commands in `commands`
		int32_t anchor = -1; // a chassis': the position that is its origin
		uint8_t tallest = 0; // the highest top
		bool chassis() const { return type < StudType; }
		// The extent at a rotation (0..3 quarter turns).
		uint8_t extentX(int32_t rotation) const { return rotation & 1 ? height : width; }
		uint8_t extentY(int32_t rotation) const { return rotation & 1 ? width : height; }
		// The cell index (into `cells`) at (x, y) of the rotated piece.
		uint32_t cellAt(int32_t x, int32_t y, int32_t rotation) const;
		bool operator==(const Piece&) const = default;
	};
	std::vector<Piece> pieces; // by type
	std::vector<Cell> cells;
	std::vector<uint16_t> commands;
	List<std::array<int16_t, 3>> positions; // 1/256 units
	List<std::array<int8_t, 3>> normals; // 1/127
	List<std::array<int16_t, 2>> uvs; // 1/1024
	std::vector<std::string> materials; // its material list's names (the .MDB beside it), in order

	const Piece* find(int32_t type) const;
	const Piece* find(std::string_view name) const; // case-insensitive
	int32_t material(std::string_view name) const; // -1 none
	// The .LEB and the .MDB; false on a malformed file.
	[[nodiscard]] bool decode(std::span<const uint8_t> pieceFile, std::span<const uint8_t> materialFile);
	bool operator==(const PieceLibrary&) const = default;
};

struct LRDATA_API CarSets {
	struct Choice {
		int32_t type = 0; // the piece (0: not in the library)
		int32_t colour = -1; // -1: not a colour
		bool operator==(const Choice&) const = default;
	};
	struct Set {
		int32_t part = 0; // what its pieces store, and its chassis' type
		std::string chassis; // 16 characters
		List<Choice> choices;
		bool operator==(const Set&) const = default;
	};
	std::array<std::string, 10> colours; // black white biege ltgray dkgray red brown yellow green blue
	std::array<int32_t, 10> materials = {}; // each colour's in the library's material list (-1 none; LO's are HI's)
	List<Set> sets; // 12

	int32_t setOfPart(int32_t part) const; // -1 none
	// CRSTMGR.LEB, the set files it names and L_COLORS.LEB; false if one is missing or malformed.
	[[nodiscard]] bool load(const Archive& archive, const PieceLibrary& library);
	bool operator==(const CarSets&) const = default;
};

struct LRDATA_API PartCatalog {
	// Unlock values: 0..2 always; 3..10 part-unlock bit (value - 3); 128 every race unlocked.
	struct Part {
		std::string name; // a hat's, torso's or legs' model; a face's texture prefix ("lh_")
		uint8_t variant = 0; // a torso's (hooked chest) or legs' (pegs)
		uint8_t unlock = 0;
		bool operator==(const Part&) const = default;
	};
	// Hat, face, torso, legs.
	std::array<List<Part>, 4> rings;
	std::vector<std::string> hatModels; // each hat's model in the head library
	std::array<std::string, 5> bodies; // the plain/hooked x plain/pegs bodies, then the seated one

	[[nodiscard]] bool decode(std::span<const uint8_t> bytes);
	bool operator==(const PartCatalog&) const = default;
};

} // namespace lr
