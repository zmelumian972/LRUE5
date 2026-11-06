#pragma once

// The show's decals (Decals.cpp): the cars' shadows, the skid trails, explosion scars and oil slicks,
// projected each frame onto the track's collision triangles. Its frame takes the show (the car visuals' skids, the
// chassis' sizes).

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lr {

class Show;
struct World;

// A decal projected on the track: triangles in world space with their box's uvs.
struct Decal {
	enum Kind : uint8_t { Shadow, Skid, Scar, Slick };
	Kind kind = Shadow;
	int16_t owner = -1; // racer (shadow), trail (skid), explosion (scar), action (slick)
	std::string material; // RACERFX's (carshadN, skid, trbskid), EMITTER's (exscar, oilslck)
	uint8_t alpha = 255; // the vertex alpha (a shadow's per camera: Decals::shadowAlpha)
	std::vector<Vec3> points; // three per triangle
	std::vector<std::array<float, 2>> uvs;
};

// This frame's decals: each racer's shadow (alpha by camera, shadowAlpha), the skid trails' pieces, scars and slicks.
struct LRCORE_API Decals {
	std::vector<Decal> shown;
	std::vector<std::array<uint8_t, 2>> shadowAlpha; // by racer, per player camera (0: not drawn)
	struct Trail {
		int16_t racer = -1;
		uint8_t wheel = 0;
		bool turbo = false, spinning = false, fading = false;
		int32_t fadeMs = 0;
		Vec3 anchor; // where the live piece starts (world)
		struct Piece {
			Vec3 from, to;
			int32_t ageMs = 0;
		};
		std::vector<Piece> pieces; // baked, oldest first (at most 3), then the live one
		bool live = false;
		Piece current;
	};
	std::array<Trail, 4> trails;
	std::vector<float> scarMs; // by explosion: ms since its scar began (-1 none)

	// The ground under the world's track, and a shadow and scar per racer and explosion.
	void load(const World& world);
	// The frame's decals: the shadows, the skid trails (laid from the car visuals' skids), scars and slicks.
	void update(Show& show, const World& world, int32_t ms);

private:
	void groundCells(const World& world);
	void carShadows(const Show& show, const World& world);
	void skidMarks(Show& show, const World& world, int32_t ms);
	void layTrail(const Show& show, const World& world, size_t i, uint8_t w, int32_t ms);
	void trailDecals(const Show& show, int32_t ms);
	void groundMarks(const World& world);
	void project(Decal& d, Vec3 centre, Vec3 along, float width, float length) const;

	// The track's collision triangles under a box, found through a grid of cells.
	struct Triangle {
		Vec3 v[3];
	};
	std::vector<Triangle> m_ground;
	std::vector<std::vector<uint32_t>> m_cells;
	mutable std::vector<uint32_t> m_taken; // by ground triangle: the projection that last took it (project's)
	mutable uint32_t m_projection = 0;
	float m_minX = 0, m_minY = 0, m_cell = 16;
	int32_t m_columns = 0, m_rows = 0;
};

} // namespace lr
