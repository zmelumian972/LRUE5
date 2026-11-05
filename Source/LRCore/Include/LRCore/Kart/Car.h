#pragma once

// A built car: up to 64 pieces from the piece library on a 10x6 stud grid, the chassis first. The grid keeps, per
// cell, the top in plates of whatever stands highest there, whether that top is studded and which piece made it; a
// piece goes where its resting cells meet studded tops (a chassis may also stand on the floor) without passing
// through anything, and no top may pass 15 plates.
//
// The mesh (for the presentation: the menus' HI build and the race's LO build) is every piece's primitives placed and
// welded, the studs that show, then the faces that press together cut away. Its primitive count is the builder's
// building limit.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Common/Math.h"
#include "LRData/Pieces.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace lr {

struct LRCORE_API CarMesh {
	struct Vertex {
		std::array<float, 3> position; // the chassis model's units
		std::array<int8_t, 3> normal; // x 1/127
		std::array<float, 2> uv;
		bool operator==(const Vertex&) const = default;
	};
	struct Triangle {
		std::array<uint32_t, 3> vertices;
		int32_t material; // the library's material list
	};
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles; // by material
	int32_t primitives = 0;
	bool overflowed = false;
	size_t materialCount() const;
};

// A mesh's sphere: its vertices' box's middle and the farthest of them from it.
LRCORE_API Sphere meshBounds(const CarMesh& mesh);

struct LRCORE_API Car {
	static constexpr int32_t Width = 10;
	static constexpr int32_t Depth = 6;
	static constexpr int32_t MostPieces = 64;
	static constexpr int32_t HighestTop = 15;
	static constexpr int32_t PrimitiveBudget = 2500;
	// The placing test's refusals.
	enum : int32_t { NothingBelow = -1, PastLeft = -2, PastRight = -3, PastFar = -4, Collides = -5, TooHigh = -6, TooMany = -7 };

	struct Entry {
		CarPiece piece;
		int32_t height = -1; // plates; -1 not yet known
	};
	struct Cell {
		int8_t height = 0, owner = -1;
		bool studded = false;
		bool operator==(const Cell&) const = default;
	};
	std::vector<Entry> entries;
	std::array<Cell, Width * Depth> grid; // x * Depth + y
	std::array<float, 3> offset = {-5, -3, 0};
	int32_t status = 0; // the last menu build's: 0 it fit, else how often detail had to go

	const Cell& cell(int32_t x, int32_t y) const { return grid[size_t(x * Depth + y)]; }

	// Where a piece at (x, y, rotation) rests, in plates, or a refusal (NothingBelow, Past*); the floor for chassis.
	int32_t rest(const PieceLibrary& lib, const PieceLibrary::Piece& p, int32_t x, int32_t y, int32_t rotation, bool floor) const;
	bool collides(const PieceLibrary& lib, const PieceLibrary::Piece& p, int32_t x, int32_t y, int32_t rotation, int32_t height) const;
	// The builder's test: the resting height, or a refusal.
	int32_t test(const PieceLibrary& lib, const PieceLibrary::Piece& p, int32_t x, int32_t y, int32_t rotation) const;
	bool add(const PieceLibrary& lib, const CarPiece& piece, bool floor);
	// Adding as the builder does: a chassis takes a random colour (one draw) and may stand on the floor.
	bool place(const PieceLibrary& lib, CarPiece piece, Game& game);
	bool remove(const PieceLibrary& lib, size_t index);
	bool rebuild(const PieceLibrary& lib);
	void clear();
	// A record's pieces (or a quick build's), as stored: unknown pieces and ones past the height limit are left out.
	void load(const PieceLibrary& lib, std::span<const CarPiece> pieces);
	std::vector<CarPiece> pieces() const;
	// The chassis' origin becomes the car's (when it is opened).
	void chooseOffset(const PieceLibrary& lib);

	// What the race takes from it (RaceBrief::Slot): the high pieces' volume in plates and its centre on the grid.
	struct Numbers {
		int32_t volume = 0;
		float centroidX = 0, centroidY = 0;
	};
	Numbers numbers(const PieceLibrary& lib) const;

	struct MeshOptions {
		bool realStuds = true, planarStuds = false, clip = true, cull = true;
	};
	// The first pieceCount pieces' mesh; colourMaterials: each colour's material in the library (CarSets::materials).
	CarMesh mesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials, MeshOptions options, size_t pieceCount) const;
	// The builder's held piece: its primitives and its own studs at a height, with the car's offset.
	CarMesh heldMesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials, const CarPiece& piece, int32_t height) const;
	// A piece carousel's item: the piece at rotation 0 with its studs, nothing clipped or culled,
	// centred on the bounding box of its own positions (z's centre × 0.4).
	static CarMesh itemMesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials, int32_t type, int32_t colour);
	// The menus' build: detail given up until it fits the budget (status says how far it went).
	CarMesh menuMesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials);

private:
	// A stud that shows: x, y, height, the piece under it.
	struct Stud {
		int32_t x, y, height, owner;
	};
	void stamp(const PieceLibrary& lib, size_t index, int32_t height, std::vector<Stud>* studs);
	bool rebuildGrid(const PieceLibrary& lib, std::vector<Stud>* studs);
};

} // namespace lr
