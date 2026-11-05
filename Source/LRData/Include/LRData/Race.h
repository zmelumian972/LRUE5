#pragma once

// A track's race files, all token streams beside the race file:
//
// - The race file (.RAB): one keyword-tagged block, K(0x35) "display name" { entries }, each entry a keyword and its
//   values (texts, reals, integers). It names the track's other files by their source names ("collide.wdf"); the
//   archive holds them compiled, under binaryName(). Only the entries the race needs are kept.
// - The start grid (.SPB), the recorded racing lines the AI plays back (.RRB), the checkpoints (.CPB), the spheres
//   that start events when a racer touches them or are just places (.TRB), and the event file (.EVB): what an event id
//   starts (timers, lap marks, sounds and the rest of the presentation's).
// - The power-up bricks (.PWB), the points players aim at (.TGB), the hazards (.HZB) and the track timers (.TIB).

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

struct LRDATA_API RaceFile {
	std::string name;            // display name
	std::string collisionFile;   // the world database (.WDB) holding the collision volumes
	std::string trackVolume;     // the entity in it that is the track's collision
	std::string checkpointFile;  // checkpoints (.CPB); empty if none
	std::string checkpointVolume; // the entity holding the checkpoint gates; empty if none
	std::string surfaceFile;     // the surface table (.TMB); empty if none
	std::string startGridFile;   // .SPB
	std::string touchFile;       // the racers' touch spheres (.TRB)
	std::string eventFile;       // .EVB
	std::string introCamera;     // the animated camera the intro flies along; empty: no flyby
	std::array<float, 3> startCamera = {}, startLook = {1, 0, 0}, startDown = {0, 0, -1}; // the cameras before the race
	std::string scene;           // the track's world database of models (.WDB): animated scenery
	std::string sharedScene;     // the "blended" world database beside it
	std::string powerupFile;     // the bricks (.PWB)
	std::string powerupScene;    // the power-ups' models (.WDB)
	std::string positionalFile;  // spheres that start events at a place (.TRB)
	std::string timerFile;       // .TIB
	std::string hazardFile;      // .HZB
	std::string targetFile;      // .TGB; empty if none
	std::array<std::string, 2> emitterFiles; // the race's and the track's effect sets (.EMB)
	std::string effectsBank, voiceBank, ambientBank; // the race's sound banks (.SBK): effects, the second voice, the track's
	std::array<float, 4> mapBounds = {1023, 1023, -1023, -1023}; // the HUD map's: min x, max y, max x, min y
	// What the race draws besides the track's worlds: the sky (.SKB) and the sky's world of
	// backdrop models, the worlds of the effects' materials (the shared one and the track's: its second library the
	// map's texture).
	std::string skyFile, skyWorld, globalWorld, localWorld;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const RaceFile&) const = default;
};

// A source file name as the archive stores it compiled: upper case, the extension's last letter 'B'
// ("collide.wdf" -> "COLLIDE.WDB"). A name without an extension is returned upper-cased.
LRDATA_API std::string binaryName(std::string_view sourceName);

struct LRDATA_API StartGrid {
	struct Place {
		std::array<float, 3> position, forward, up;
		bool operator==(const Place&) const = default;
	};
	std::vector<Place> places; // by place index

	// False on a malformed file, or one missing a place below the highest.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const StartGrid&) const = default;
};

// A racing line as recorded: a start pose, then points each an offset from the previous one (fixed point), a
// rotation, the free width either side and a kind (ground contacts and the segment's length); and where a lap
// continues after the last point.
struct LRDATA_API RouteFile {
	struct Point {
		int16_t dx, dy;
		int8_t dz;
		int8_t rotation[4]; // x y z w, times 1/127
		int8_t left, right; // widths, eighths of a foot
		uint8_t kind;       // top 2 bits: ground contacts (3 means 4); the rest: length in 32 ms
		bool operator==(const Point&) const = default;
	};
	std::array<float, 3> start = {}, loop = {};
	std::array<float, 4> startRotation = {0, 0, 0, 1}, loopRotation = {0, 0, 0, 1};
	int32_t loopTime = 0;
	int32_t loopPoint = 0;
	std::vector<Point> points;

	// False on a malformed file, or one whose loop point is past its points.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const RouteFile&) const = default;
};

struct LRDATA_API CheckpointFile {
	struct Checkpoint {
		std::array<float, 3> normal = {}; // the gate's plane: dot(normal, p) + offset = 0
		float offset = 0;
		std::array<uint8_t, 4> next = {255, 255, 255, 255}; // [0] the main lap, [1..3] branches; 255 none
		std::array<float, 3> position = {};
		bool operator==(const Checkpoint&) const = default;
	};
	std::vector<Checkpoint> checkpoints;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const CheckpointFile&) const = default;
};

struct LRDATA_API TouchFile {
	struct Sphere {
		std::array<float, 3> centre = {};
		float radius = 0;
		int32_t event = 0;     // started for a racer that enters; -1 none
		bool playersOnly = false;
		bool projectilesOnly = false;
		std::string world;     // a collision entity the racer collides with while touching; empty none
		bool lapGated = false; // only a racer with `lap` completed laps touches it
		int32_t lap = 0;
		bool operator==(const Sphere&) const = default;
	};
	std::vector<Sphere> spheres;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const TouchFile&) const = default;
};

// The event file: sections of resources, each started, ended or fired by an event id. Every record keeps what the
// rules need (its id, flags and the three events it chains to as its phase changes) and its kind's fields.
struct LRDATA_API EventFile {
	enum Kind : uint8_t {
		Sounds,
		PartAnimations,
		MaterialAnimations,
		Particles,
		SkyStates,
		Timers,
		NodeTransforms,
		Colours,
		LapMarks,
		ModelDistances,
		LookTargets,
		Forces,
		Links,
	};
	enum Zone : uint8_t { Open, Line, Past };
	struct Resource {
		Kind kind = Sounds;
		int32_t id = 0;
		bool onEnd = false;  // reacts to its event's end instead of its start
		bool noEnd = false;
		std::array<int32_t, 3> chains = {-1, -1, -1}; // started as it starts, while active, as it ends
		// Timers.
		int32_t durationMs = 0, holdEvent = -1;
		// Lap marks.
		Zone zone = Line;
		// External forces.
		std::array<float, 3> force = {};
		int32_t channel = 0, armingEvent = -1;
		// Links: the event whose resources it force-ends.
		int32_t forced = -1;
		// Sounds and particles: where (or at the event's position), a tracked animated entity and its node (an empty
		// name: none; `shared`: in the shared scene); a particle's emitter and its direction and up; a sound's id,
		// volume, pitch, distances, whether it loops and its chance of playing on each retrigger.
		std::array<float, 3> position = {};
		bool atEvent = false, shared = false;
		std::string entity;
		int32_t node = 0;
		std::string emitter;
		std::array<float, 6> axes = {1, 0, 0, 0, 0, 1};
		int32_t sound = 0;
		float volume = 1, pitch = 1, nearDistance = 30, farDistance = 300, probability = 1;
		bool looping = false;
		// Sky states: the sky's transition and its ms, and the dome's and the sky world's hide flags (bits: hide dome,
		// show dome, hide sky world, show sky world).
		std::string sky;
		int32_t skyMs = 0;
		uint8_t skyFlags = 0;
		// Part animations: the entity's parts (active, idle, start, end; -1 none).
		std::array<int32_t, 4> parts = {0, 0, -1, -1};
		// Colour transforms: its start clears the tint instead of applying one (the entity: `entity`, none: the racer).
		bool clear = false;
		// Node transforms: the collision entity moved with node `node` of the animated entity `entity`. Model distances:
		// the model `entity` is hidden while the resource is active (else shown while active).
		std::string world;
		bool hide = false;
		bool operator==(const Resource&) const = default;
	};
	std::vector<Resource> resources; // by section, in file order within each

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const EventFile&) const = default;
};

// The power-up bricks: colour bricks (red 1, blue 2, green 3, yellow 4) and white ones.
struct LRDATA_API PowerupFile {
	struct Brick {
		std::array<float, 3> position = {};
		uint8_t colour = 3;
		int32_t respawnMs = 3000;
		bool operator==(const Brick&) const = default;
	};
	std::vector<Brick> colourBricks;
	std::vector<std::array<float, 3>> whiteBricks;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const PowerupFile&) const = default;
};

// Points a player's weapons aim at, each disabled for good by an event id.
struct LRDATA_API TargetFile {
	struct Point {
		std::array<float, 3> position = {};
		int32_t disabledBy = -1;
		bool operator==(const Point&) const = default;
	};
	std::vector<Point> points;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const TargetFile&) const = default;
};

// Timers that turn an event on and off, each phase's length fixed or random.
struct LRDATA_API TimerFile {
	struct Timer {
		int32_t onMs = 0, offMs = 0;
		bool randomOn = false, randomOff = false;
		int32_t event = -1;
		int32_t delayMs = 0;
		bool operator==(const Timer&) const = default;
	};
	std::vector<Timer> timers;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const TimerFile&) const = default;
};

// The hazards, in file order: each its type's keyword and what that type reads (most read nothing).
struct LRDATA_API HazardFile {
	struct Hazard {
		int32_t type = 0;
		int32_t trigger = -1;
		std::string entity;                 // the animated model it follows
		std::vector<std::string> entities;  // a triggered animation's
		std::string surface;                // the surface a triggered animation opens
		std::array<float, 3> source = {}, target = {}, triggerPoint = {};
		float triggerRadius = 0;
		std::vector<std::array<float, 4>> launches, targets; // a multi-launcher's: x y z and an event
		std::array<float, 2> scroll = {};   // an oscillator's
		float startFrame = 0;               // a rolling rock's animation phase
		std::array<float, 3> size = {};     // and box
		int32_t periodMs = 0;               // a sweep cannon's
		std::array<float, 3> sweep = {};
		int32_t intervalMs = 0;             // a cannonball rain's, and its balls' material
		std::string material;
		float strength = 0, low = 0, high = 0; // a grabber's pull and claw frames
		bool operator==(const Hazard&) const = default;
	};
	std::vector<Hazard> hazards;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const HazardFile&) const = default;
};

// A time race's record run (GHOST.GHB, Veronica Voltage's): its three lap times, its first pose unquantized and a
// sample every 250 ms: the position times 32 as 16 bits, the rotation (x y z w) times 127 as 8 bits, signed. Mirrored,
// every y and the rotations' y and w are negated.
struct LRDATA_API GhostFile {
	std::array<int32_t, 3> laps = {};
	std::array<float, 3> position = {};
	std::array<float, 4> rotation = {0, 0, 0, 1};
	std::vector<std::array<int16_t, 3>> positions;
	std::vector<std::array<int8_t, 4>> rotations;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes, bool mirrored);
	bool operator==(const GhostFile&) const = default;
};

// An effect set's emitters (.EMB): each its emission rate (as an interval in
// ms), its chance (a byte), its particles' acceleration, launch vectors, size and growth, life, the emitter's duration
// (-1: runs until stopped), visibility radius, material (a name, or a track of the set's material animation file) and
// priority.
struct LRDATA_API EmitterFile {
	struct Emitter {
		std::string name;
		int32_t intervalMs = 0;
		uint8_t chance = 0;
		std::array<float, 3> acceleration = {};
		std::vector<std::array<float, 3>> launches;
		float width = 1, height = 1;  // start size across and up the screen
		float growWidth = 0, growHeight = 0;
		int32_t track = -1;           // a material animation track, -1 none
		int32_t lifeMs = 0, durationMs = -1;
		float radius = 0;
		std::string material;         // a fixed material (no track)
		uint8_t priority = 0;
		bool operator==(const Emitter&) const = default;
	};
	std::vector<Emitter> emitters;

	int32_t find(std::string_view name) const; // exact on up to 8 characters; -1 none
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const EmitterFile&) const = default;
};

// A track's sky (BACKGRND.SKB): its states, each keyframes of a duration and three ring colours (the
// horizon's, the middle's, the high one's), looping; the state it starts in; the dome's height offset.
struct LRDATA_API SkyFile {
	struct Keyframe {
		int32_t durationMs = 1000;
		std::array<std::array<uint8_t, 3>, 3> colours = {{{255, 255, 255}, {255, 255, 255}, {255, 255, 255}}};
		bool operator==(const Keyframe&) const = default;
	};
	struct State {
		std::string name;
		std::vector<Keyframe> keyframes;
		bool operator==(const State&) const = default;
	};
	std::vector<State> states;
	std::string initial;
	float height = 0;

	int32_t find(std::string_view name) const; // -1 none
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const SkyFile&) const = default;
};

// The loading screen (LOADSCRN.LSB): the picture under it, the track's name (GAME.SRF's) and where its progress dots
// go, as fractions of the screen.
struct LRDATA_API LoadingScreen {
	std::string picture;
	int32_t name = 0;
	std::vector<std::array<float, 2>> dots;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const LoadingScreen&) const = default;
};

} // namespace lr
