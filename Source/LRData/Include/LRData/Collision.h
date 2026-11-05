#pragma once

// What a track collides with, as token streams:
//
// - The world database (.WDB): names of bounding-volume files and a list of entities, each one of those volumes placed
//   in the world (position, forward and up vectors). The race file names which entity is the track and which holds
//   the checkpoint gates.
// - The bounding volume (.BVB): a triangle soup with material indices, and a BSP tree over it whose nodes each own a
//   contiguous run of triangles lying in the node's plane.
// - The surface table (.TMB): per material name, the physical properties of what a wheel rolls on or a kart hits.

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lr {

struct LRDATA_API CollisionDatabase {
	struct Entity {
		std::string name;
		uint32_t volume = 0; // index into volumes
		std::array<float, 3> position = {0, 0, 0};
		std::array<float, 3> forward = {1, 0, 0};
		std::array<float, 3> up = {0, 0, 1};
		bool operator==(const Entity&) const = default;
	};
	std::vector<std::string> volumes; // .BVB names, without extension
	std::vector<Entity> entities;

	// False on a malformed file, or one using what no track does (material-table overrides).
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const CollisionDatabase&) const = default;
};

struct LRDATA_API BoundingVolume {
	struct Triangle {
		uint16_t v[3];
		uint16_t material;
		bool operator==(const Triangle&) const = default;
	};
	// Two on-disk forms: reals with a plane offset (unused by the game, not kept), or fixed point (normal * 2^30).
	struct Node {
		int32_t child[2]; // -1: none
		std::array<float, 3> normal;
		uint32_t first, count; // its triangles
		bool operator==(const Node&) const = default;
	};
	std::vector<std::string> materials;
	std::vector<std::array<float, 3>> vertices;
	std::vector<Triangle> triangles;
	std::vector<Node> nodes; // the root is node 0

	// False on a malformed file (including indices out of range).
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const BoundingVolume&) const = default;
};

struct LRDATA_API SurfaceFile {
	// Which properties a surface sets (the file's keywords 0x28.. map to bits 1..; bit 0 is always set).
	enum : uint32_t {
		HasTouchEvent = 1u << 3,
		HasProjectileEvent = 1u << 4,
		HasPush = 1u << 6,
		HasSlopeHold = 1u << 11,
		HasFriction = 1u << 12,
		HasGrip = 1u << 13,
		HasRolling = 1u << 15,
		NonSolid = 1u << 16,
		ProjectilesPass = 1u << 17,
		FinishLine = 1u << 18,
	};
	struct Surface {
		std::string name;
		uint32_t flags = 1;
		int32_t touchEvent = 0, projectileEvent = 0;
		std::array<float, 3> push = {0, 0, 0};
		float slopeHold = 0, friction = 0, grip = 0, rolling = 0;
		std::string particle; // the wheels' particle emitter on it (empty: none)
		int32_t sound = -1;   // the wheels' sound on it (the track's bank; -1: none)
		bool operator==(const Surface&) const = default;
	};
	std::vector<Surface> surfaces;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const SurfaceFile&) const = default;
};

} // namespace lr
