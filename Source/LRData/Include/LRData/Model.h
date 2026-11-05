#pragma once

// The game's models (.GDB, a token stream): vertices, triangles in sections of one material and one part, material
// names (the textures and settings of the .MDB beside it) and a scale.
//
// On disk triangles index a 256-entry vertex cache: a "vertices" record loads a run of vertices into cache slots, a
// "triangles" record draws triangles whose three bytes are slots, and "material" and "part" records switch state for
// what follows. decode() resolves all that into sections of absolute vertex indices. A vertex rides the part (node)
// current when it was loaded, not when its triangle is drawn: one triangle may join two or three
// parts (the minifig bodies' joints, the turbo flames); no file loads a vertex under two parts.

#include "LRData/Api.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

struct LRDATA_API Model {
	struct Section {
		uint16_t material;
		uint16_t part; // the part current at its draw record (its vertices' own are vertexParts)
		std::vector<uint32_t> indices; // three per triangle
		bool operator==(const Section&) const = default;
	};
	enum class Vertices : uint8_t { PositionsOnly, Plain, Colored, Lit };

	Vertices kind = Vertices::Plain;
	std::vector<float> positions; // xyz per vertex
	std::vector<float> uvs;       // uv per vertex (not PositionsOnly)
	std::vector<uint8_t> colors;  // rgba per vertex (Colored; Plain and Lit: white)
	std::vector<float> normals;   // xyz per vertex (Lit)
	std::vector<Section> sections;
	std::vector<uint16_t> vertexParts; // per vertex: the part current when it was loaded (0 never loaded)
	std::vector<std::string> materials;
	float scale = 1.0f;
	uint32_t faceCount = 0;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const Model&) const = default;
};

// A head library (.GCB, beside a minifig body set's world: MENUPART/CBBODIES.GCB, GAMEPART/ICB_CHAR.GCB): the bare
// head and every hat model, sharing one vertex pool. The format, as read from the data:
//
//   K(0x28) [n] { x y z u v nx ny nz  × n }      the pool: integers; position in model units, uv × 1/4096, the normal as
//                                               signed bytes × 1/127
//   K(0x2c) [m] { K(0x2c) "name" { K(0x2d) scale  K(0x2b) [g] { K(0x2b) { K(0x27) "material"  K(0x2a) [t] { 3t pool
//                 indices } } × g } } × m }
//
// (Checked against the data: the uvs' 1/4096 by HEADS.GDB's (0.023, 0.28, 0.101 there, 94, 1146, 414 here), the normals
// are unit vectors.) Each
// entry is decoded as a model of its own: the pool's vertices it uses (Lit), a section per group (part 0).
struct LRDATA_API HeadLibrary {
	struct Head {
		std::string name;
		Model model;
		bool operator==(const Head&) const = default;
	};
	std::vector<Head> heads;

	const Head* find(std::string_view name) const; // case-insensitive
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const HeadLibrary&) const = default;
};

} // namespace lr
