#pragma once

// What a racer's car is made of, as token streams in GAMEDATA/COMMON: the chassis table (.CMB: wheels, mass, stats,
// the models that draw it), the champions (.CCB: the preset cars, each a body on a chassis) and the drivers (.DDB:
// who races as which champion, and how its AI uses power-ups). And the animation file (.ADB) beside a model: its parts
// (bounds, frame counts, rates, first track), the key frames and the tracks that sample them per scene node.

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lr {

struct LRDATA_API ChassisTable {
	struct Chassis {
		std::string name;
		std::string model;     // the first variant's model (.GDB)
		std::string animation; // the first variant's part file (.ADB)
		std::string skeleton, materials, textures; // the first variant's scene nodes (.SDB), material library, texture list
		// The second variant (key 0x29), drawn with the car when it names a model.
		struct Variant {
			std::string materials, textures, model, skeleton, animation;
			bool operator==(const Variant&) const = default;
		} second;
		std::array<float, 3> driverMount = {}; // where the driver sits, in the car's frame (ft)
		std::array<float, 3> centerOfMass = {};
		float mass = 0;
		float shadowWidth = 0, shadowLength = 0;
		float enginePitch = 0; // the engine sounds' pitch factor
		// The skid marks: their widths (front, rear) and where each wheel lays them, car space.
		std::array<float, 2> skidWidths = {};
		std::array<std::array<float, 3>, 4> skidPoints = {};
		std::array<std::array<float, 3>, 4> wheels = {}; // front right, front left, rear right, rear left
		int32_t handling = 50, topSpeed = 50, acceleration = 80;
		bool operator==(const Chassis&) const = default;
	};
	std::vector<Chassis> chassis;

	const Chassis* find(std::string_view name) const;
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const ChassisTable&) const = default;
};

struct LRDATA_API ChampionTable {
	struct Champion {
		std::string name;
		std::string model; // the body (.GDB)
		std::string materials, textures;
		std::string chassis;
		float mass = 0;
		std::array<float, 3> centerOfMass = {};
		bool operator==(const Champion&) const = default;
	};
	std::vector<Champion> champions;

	const Champion* find(std::string_view name) const;
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const ChampionTable&) const = default;
};

struct LRDATA_API DriverTable {
	struct Driver {
		std::string name;
		std::string champion;
		std::array<int32_t, 6> stats = {}; // power-up use: red, yellow, green, blue, and two unused
		int32_t chargeColour = 0, chargeTarget = 0; // a colour it holds until it has that many white bricks
		int32_t nameIndex = 0;              // its name in DRIVERS.SRF
		std::array<int32_t, 5> looks = {};  // its minifig: K(0x35)..K(0x39)
		int32_t voice = 0;                  // its voice in the race's second voice bank
		std::string materials, textures, model; // its driver model (on the shared PELVIS skeleton)
		bool operator==(const Driver&) const = default;
	};
	std::vector<Driver> drivers;

	const Driver* find(std::string_view name) const;
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const DriverTable&) const = default;
};

struct LRDATA_API AnimationFile {
	struct Part {
		std::string name;
		int32_t frames = 0, loopFrames = 0, rate = 0; // frames per second
		std::array<float, 4> bounds = {}; // centre x y z, radius (model units)
		int32_t track = 0; // the first track: node k of the skeleton uses track + k
		bool operator==(const Part&) const = default;
	};
	// Where a node's rotation and position keys are: the first frame of each in rotations / positions, the first key
	// time in keys, and how many.
	struct Track {
		int32_t rotationFrame, rotationKey, rotationKeys, positionFrame, positionKey, positionKeys;
		bool operator==(const Track&) const = default;
	};
	std::vector<Part> parts;
	std::vector<std::array<float, 3>> positions;
	std::vector<std::array<float, 4>> rotations; // x y z w
	std::vector<int32_t> keys; // frame numbers
	std::vector<Track> tracks;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const AnimationFile&) const = default;
};

} // namespace lr
