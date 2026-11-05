#pragma once

// What a track shows, as token streams beside its race file (the parts the game's rules read):
//
// - The world database of models (.WDB, the race file's "scene"): the model, skeleton, animation and material
//   animation names it uses, and its entities, each a model placed in the world (position, forward and up, a scale);
//   an animated entity also has a skeleton and an animation and may play one of its parts from the start.
// - A skeleton (.SDB): named nodes, each a local position and rotation under its parent.
// - A material animation file (.MAB): tracks of texture frames played at a rate for a duration.

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lr {

struct LRDATA_API SceneFile {
	struct Entity {
		std::string name;
		int32_t model = -1, skeleton = -1, animation = -1; // indices into the name lists; -1 none
		float drawDistance = 0; // the first level of detail's
		std::vector<float> distances; // every level of detail's draw distance, in order (negative: no limit)
		std::array<float, 3> position = {0, 0, 0};
		std::array<float, 3> forward = {1, 0, 0};
		std::array<float, 3> up = {0, 0, 1};
		float scale = 1;
		int32_t part = -1; // the animation part it plays from the start; -1 none
		std::string partName; // or the part by name
		std::array<float, 2> scroll = {}; // texture scroll, u and v per second (key 0x3f)
		// Material animation bindings (key 0x3e): the scene's material animation file, its track, the material slot
		// and the level of detail.
		std::vector<std::array<int32_t, 4>> materialBindings;
		bool operator==(const Entity&) const = default;
	};
	std::vector<std::string> models, skeletons, animations, materialAnimations;
	// The material libraries (.MDB) and texture lists (.TDB) its models are drawn with, in order; the
	// models no entity places (drawn where they are: the track itself).
	std::vector<std::string> materialLibraries, textureLists, unplaced;
	std::vector<Entity> entities; // the plain models
	std::vector<Entity> animated;
	// Its cameras: a place, a view direction and up, the clip planes and the vertical field of
	// view; a camera may track a node of an animated entity.
	struct Camera {
		std::string name;
		int32_t entity = -1, node = -1; // the tracked animated entity and its node; -1 none
		std::array<float, 3> position = {0, 0, 0};
		std::array<float, 3> forward = {1, 0, 0};
		std::array<float, 3> up = {0, 0, 1};
		float nearPlane = 1, farPlane = 100, fov = 65; // degrees
		int32_t part = -1; // the part the tracked entity plays from the load (K(0x35)); -1 none
		std::string partName; // or by name
		bool operator==(const Camera&) const = default;
	};
	std::vector<Camera> cameras;
	// Its lights: named ambient records (K(0x48), a colour) and directional ones (K(0x49), a colour
	// and the direction the light travels), in file order.
	struct Light {
		std::string name;
		std::array<float, 3> colour = {};
		std::array<float, 3> direction = {};
		bool operator==(const Light&) const = default;
	};
	std::vector<Light> ambients, lights;
	// Its sprites: textured quads turned to each camera. No axis (0, 0, 0): they face it wholly. A
	// sprite with no material takes its material animation's: a file of materialAnimations and its track.
	struct Sprite {
		std::array<float, 3> position = {};
		std::array<float, 3> axis = {};
		float width = 0, height = 0, distance = 0;
		std::string material;
		int32_t animationFile = -1, animationTrack = -1;
		bool operator==(const Sprite&) const = default;
	};
	std::vector<Sprite> sprites;

	// False on a malformed file.
	[[nodiscard]] bool decode(std::span<const uint8_t> bytes);
	bool operator==(const SceneFile&) const = default;
};

struct LRDATA_API SkeletonFile {
	struct Node {
		std::string name;
		std::array<float, 3> position = {};
		std::array<float, 4> rotation = {0, 0, 0, 1}; // x y z w
		std::string parent; // empty: the root
		bool operator==(const Node&) const = default;
	};
	std::vector<Node> nodes;

	// False on a malformed file.
	[[nodiscard]] bool decode(std::span<const uint8_t> bytes);
	bool operator==(const SkeletonFile&) const = default;
};

struct LRDATA_API MaterialAnimationFile {
	// The frames: each a material and the frame it shows from.
	struct Frame {
		std::string material;
		int32_t start = 0;
		bool operator==(const Frame&) const = default;
	};
	std::vector<Frame> frames;
	struct Track {
		int32_t firstFrame = 0, frames = 1; // its range of the frame list
		int32_t duration = 1; // in frames
		int32_t rate = 30; // frames per second
		bool operator==(const Track&) const = default;
	};
	std::vector<Track> tracks;

	// False on a malformed file.
	[[nodiscard]] bool decode(std::span<const uint8_t> bytes);
	bool operator==(const MaterialAnimationFile&) const = default;
};

} // namespace lr
