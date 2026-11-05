#pragma once

// A track, loaded once at race start: its collision (bounding volumes, triangles under a BSP tree, placed in the world,
// and the surface table that gives each triangle's material its physics), and what the race runs on it: the start
// grid, the checkpoints and their lap fractions, the spheres racers touch, the lap marks and the intro's length; the
// power-up bricks, the points players aim at, the hazards, the event table's timers, forces and links, the track
// timers, and the animated scenery the hazards follow (its skeletons and animations).
//
// Queries are segments against one placed volume, in that volume's own frame, answered the way the original game
// answers them: a depth-first walk of the BSP, near side first, taking the first triangle that the segment crosses in
// a node's plane, not the nearest one.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Show/View.h"
#include "LRData/Scene.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

class Cache;
struct CollisionDatabase;
struct Model;
struct RaceFile;

// Surface::flags: which properties the table sets, and what the surface is.
enum SurfaceFlag : uint32_t {
	SurfaceHasPush = 1u << 6,
	SurfaceHasSlopeHold = 1u << 11,
	SurfaceHasFriction = 1u << 12,
	SurfaceHasGrip = 1u << 13,
	SurfaceHasRolling = 1u << 15,
	SurfaceHasTouchEvent = 1u << 3,
	SurfaceHasProjectileEvent = 1u << 4,
	SurfaceNonSolid = 1u << 16, // walls of it don't stop a kart
	SurfaceProjectilesPass = 1u << 17,
	SurfaceFinishLine = 1u << 18, // a kart touching it has done the race's laps
};

struct Surface {
	std::string name;
	uint32_t flags = 0; // SurfaceFlag (the bits of LRData's SurfaceFile)
	Vec3 push;
	float slopeHold = 0, friction = 0, grip = 0, rolling = 0;
	int32_t touchEvent = 0; // started (and ended) for a racer whose body hits it
	int32_t projectileEvent = 0; // started and ended where a projectile hits it
	std::array<char, 9> particle = {}; // the wheels' emitter on it
	int32_t sound = -1; // the wheels' loop on it (key 0x2e: a race sound id; -1 none)
};

struct SegmentHit {
	Vec3 point;
	Vec3 normal; // unit, of the hit triangle
	float offset = 0; // plane: dot(normal, p) + offset = 0
	float fraction = 0; // along the segment
	Vec3 corners[3]; // the hit triangle
	int16_t surface = -1; // index into Track::surfaces, -1 none
	int16_t checkpoint = -1; // a checkpoint's gate (the checkpoint volume's digit-named materials), -1 none
};

struct CollisionMesh {
	struct Triangle {
		Vec3 v[3];
		int16_t surface; // -1 none
		int16_t checkpoint; // -1 none
	};
	struct Node {
		Vec3 normal;
		Vec3 point; // on the plane: vertex 0 of the node's first triangle (the file's plane offset is ignored)
		int32_t child[2]; // -1 none; child 0 is the positive side
		uint32_t first, count;
	};
	std::vector<Triangle> triangles;
	std::vector<Node> nodes;
	std::vector<std::string> materials;
	std::vector<int16_t> materialSurfaces;
	std::vector<int16_t> materialCheckpoints; // -1 none
};

// A gate across the track, crossed against its normal when going the right way.
struct Checkpoint {
	Vec3 normal;
	float offset; // the gate's plane: dot(normal, p) + offset = 0
	uint8_t next[4]; // [0] along the main lap, [1..3] branches that rejoin it; 255 none
	float fraction; // of a lap at the gate; -1 unset
	Vec3 position; // the gate's place (homing missiles fly the course by them)
};

// A sphere that starts an event for a racer entering it (the lap marks, the start line's world).
struct TouchSphere {
	Vec3 centre;
	float radius, xlo, xhi; // xlo and xhi: the centre's x -+ radius
	int32_t index; // in the file
	int32_t event; // -1 none
	int32_t world; // a volume the racer collides with while touching; -1 none
	int32_t lap; // only a racer with this many laps done touches it; -1 any
	bool playersOnly;
};

struct LapMark {
	enum Zone : uint8_t { Open, Line, Past };
	int32_t event;
	Zone zone;
	int32_t chains[3] = {-1, -1, -1}; // events it starts (and ends) as a racer reaches it: start, active, end
};

// The event table's resources the core runs: timers that run an event's chain for a while, held by another event;
// forces that push racers; links that force-end another event's resources; part animations that play an animated
// entity's parts, node transforms that move a collision entity with one's node, model distances that hide and show a
// model; and, for the show's events, the sounds, particles and sky states (when they start and end;
// how they sound and look is the show's). (The tints are the presentation's.)
struct EventResource {
	enum Kind : uint8_t { Timer, Force, Link, Sound, Particle, Sky, Colour, PartAnimation, NodeTransform, ModelDistance };
	Kind kind;
	int32_t id;
	bool onEnd, noEnd;
	int32_t chains[3]; // start, active, end
	int32_t durationMs, holdEvent; // a timer's
	Vec3 force; // a force's (mirrored), its push channel and the event that arms it
	int32_t channel, armingEvent;
	int32_t forced; // a link's
	// A sound's or a particle's place (mirrored; or the event's, if `atEvent`); a particle's emitter or a sky's
	// transition; a sound's id, volume, pitch, distances, whether it loops and its retrigger chance (255: none); a sky's
	// ms and hide flags.
	Vec3 position = {};
	bool atEvent = false;
	std::array<char, 9> name = {};
	int32_t sound = 0;
	float volume = 1, pitch = 1, nearDistance = 30, farDistance = 300;
	bool looping = false;
	int32_t chance = 255;
	int32_t skyMs = 0;
	uint8_t skyFlags = 0;
	bool clear = false, onEntity = false; // a colour transform's: clears the tint; tints an entity, not the racer
	// A sound's tracked animated entity (an index into Track::animated, -1 none) and its node: the sound rides it. A part
	// animation's and a node transform's animated entity; a model distance's model (a plain one if `plain`).
	std::string entityName = {};
	int16_t entity = -1;
	int32_t node = 0;
	// A part animation's parts: active, idle, start, end (-1 none); `looping` its flag. A node transform's collision
	// entity (Track::volumes, -1 none). A model distance's "hide while active".
	std::array<int32_t, 4> parts = {0, 0, -1, -1};
	int32_t collision = -1;
	bool plain = false, hide = false;
};

// A track timer: an event on for a while, then off for a while (each length fixed or drawn).
struct TrackTimer {
	int32_t onMs, offMs;
	bool randomOn, randomOff;
	int32_t event;
	int32_t delayMs;
};

struct BrickPlace {
	Vec3 position;
	uint8_t colour; // Colour; 0: a white brick
	int32_t respawnMs;
};

struct TargetPoint {
	Vec3 position;
	int32_t disabledBy; // the event that disables it for good, -1 none
};

// A model of the scenery that plays its animation's parts: its pose, its size, its skeleton's nodes (a pose under
// the parent's; node 0 the root) and the animation that moves them.
struct AnimatedModel {
	using Node = SkeletonNode;
	std::string name;
	std::string model; // its model's name (none: a camera's dummy)
	Pose pose;
	float scale = 1; // the model's times the entity's
	// Its levels of detail's draw distances, squared: a level the file doesn't give -1, one not below
	// FLT_MAX FLT_MAX. (The world keeps them as they change: Scenery::animatedDistances.)
	std::array<float, 3> distances = {-1, -1, -1};
	float radius = 0; // the model's (the hazards' spheres)
	Sphere bounds; // the model's (model frame, unscaled)
	int16_t animation = -1; // Track::animations, -1 none
	int16_t part = -1; // played looping from load, -1 none
	std::vector<Node> nodes;
	// Material animation bindings (Track::materialTracks).
	std::vector<std::array<int32_t, 3>> bindings; // track, material slot, level of detail
};

struct Animation {
	struct Part {
		int32_t frames, loopFrames;
		float rate; // frames per ms
		int32_t track;
		Sphere bounds; // model units
	};
	struct Track {
		int32_t rotationFrame, rotationKey, rotationKeys, positionFrame, positionKey, positionKeys;
	};
	std::vector<Part> parts;
	std::vector<Vec3> positions;
	std::vector<Quat> rotations;
	std::vector<int32_t> keys;
	std::vector<Track> tracks;
};

// A hazard as the file gives it (positions mirrored, names resolved).
struct HazardSpec {
	int32_t type; // the file's keyword
	int32_t trigger;
	int16_t model = -1; // Track::animated, -1 none
	int16_t models[4] = {-1, -1, -1, -1};
	int16_t surface = -1;
	Vec3 source, target, triggerPoint;
	float triggerRadius;
	std::vector<std::array<float, 4>> launches, targets; // x y z, event
	float startFrame;
	Vec3 size;
	int32_t periodMs, intervalMs;
	Vec3 sweep;
	float strength, low, high;
};

// A world's sprite as the core keeps it. `reach` is its distance squared; no axis: (0, 0, 0). A
// material animation shows `track` instead of `material`, -1 none.
struct LRCORE_API Sprite {
	Vec3 position = {};
	Vec3 axis = {};
	float width = 0, height = 0, reach = 0;
	std::string material;
	int32_t track = -1;

	// A place in the world (mirrored: y negated).
	static Sprite of(const SceneFile::Sprite& s, bool mirrored);
	bool inReach(Vec3 eye) const { return lengthSquared(position - eye) <= reach; }
	// Its corners seen by a camera, clockwise from the top left, from the camera's rows: turned about the axis until its
	// side lies square to the view's direction, or wholly square to it with no axis.
	std::array<Vec3, 4> corners(Vec3 right, Vec3 down, Vec3 look) const;
};

class LRCORE_API Track {
public:
	struct Volume {
		std::string name;
		uint32_t mesh;
		Vec3 position;
		Basis rotation;
	};

	// The race file's track: "GAMEDATA/RACEC0R1/RACEC0R1.RAB". Its other files are beside it. Mirrored tracks are
	// flipped in y at load (positions, directions and normals). A time race has its own bricks (the power-up file's
	// name with a 2 before its extension). False if a file is missing or malformed.
	[[nodiscard]] bool load(const Cache& cache, std::string_view raceFile, bool mirrored, bool timeRace = false);

	// The segment a->b, in volume's frame.
	bool query(uint32_t volume, Vec3 a, Vec3 b, SegmentHit& hit) const;

	std::vector<Volume> volumes;
	std::vector<CollisionMesh> meshes;
	std::vector<Surface> surfaces;
	uint32_t trackVolume = 0;
	int32_t checkpointVolume = -1;

	std::vector<Pose> grid; // by start place
	std::vector<Checkpoint> checkpoints;
	std::vector<TouchSphere> touchSpheres; // the racers' (not the projectiles-only ones), by xlo
	std::vector<LapMark> lapMarks;
	int32_t introFrames = 0, introRate = 0; // the flyby camera's animation (frames per second); 0 frames: no flyby
	int16_t introEntity = -1, introNode = -1; // the animated entity and node the flyby's camera follows
	Vec3 startCamera, startLook{1, 0, 0}, startDown{0, 0, -1}; // the cameras before the race
	bool mirrored = false;

	std::vector<BrickPlace> bricks; // the colour bricks, then the white ones
	std::vector<TargetPoint> targets;
	std::vector<TouchSphere> places; // the positional spheres, in file order
	std::vector<EventResource> events;
	std::vector<TrackTimer> timers;
	std::vector<HazardSpec> hazards;
	std::vector<AnimatedModel> animated; // the track's, then the shared database's (from sharedAnimated on)
	size_t sharedAnimated = 0;
	std::vector<Animation> animations;
	std::vector<std::pair<std::string, Pose>> models; // the plain models' places, by name
	std::vector<std::array<float, 3>> modelDistances; // and their draw distances (as AnimatedModel::distances)
	// Material animation: the worlds' tracks, each its frames (a material and the frame it shows
	// from), its length in frames and rate (frames per ms); the bindings of tracks to entities' material slots.
	struct MaterialTrack {
		std::vector<std::pair<std::string, int32_t>> frames;
		int32_t duration = 1;
		float rate = 0.03f;
	};
	struct MaterialBinding {
		bool animated; // an animated entity's (else a plain model's)
		int32_t entity; // its index in animated or models
		int32_t track, slot, level;
	};
	// A texture scroll, in 1/65536 UV per ms.
	struct Scroll {
		bool animated;
		int32_t entity;
		int32_t u, v;
	};
	std::vector<MaterialTrack> materialTracks;
	std::vector<MaterialBinding> materialBindings;
	std::vector<Scroll> scrolls;
	std::vector<Sprite> sprites; // the track's (mirrored)
	int32_t blowupDuration = 0, blowupRate = 0; // the shared database's "blowup" material track (a sphinx's)
	Sphere hook, stick; // the grapple's and the dynamite stick's bounds (model frame, scaled)
	AnimatedModel missile; // the homing missile, with its animation
	// The power-up bricks' four shared entities: the colour brick's cube and streak, the white
	// brick's cube and streaks, each with its animation (after the track's), at the origin, entity scale 1.
	enum BrickModel : uint8_t { BrickCube, BrickStreak, WhiteCube, WhiteStreaks };
	std::array<AnimatedModel, 4> brickModels;

	// An animated model by name (first 8 characters), -1 none; in one database only (the shared one, or the track's).
	int16_t animatedOf(std::string_view name) const;
	int16_t animatedIn(std::string_view name, bool shared) const;
	// A plain model by name, -1 none.
	int32_t modelOf(std::string_view name) const;
	// A plain model's place, null if none.
	const Pose* modelPlace(std::string_view name) const;

	// The surface of a material name (first 8 characters), -1 none.
	int16_t surfaceOf(std::string_view material) const;

private:
	[[nodiscard]] bool loadMeshes(const Cache& cache, const std::string& dir, const CollisionDatabase& db, bool mirror);
	void findEventEntities();
	[[nodiscard]] bool loadRace(const Cache& cache, const std::string& dir, const RaceFile& race, const CollisionDatabase& db, bool mirrored);
	[[nodiscard]] bool loadPowerups(const Cache& cache, const std::string& dir, const RaceFile& race, bool timeRace);
	[[nodiscard]] bool loadScenery(const Cache& cache, const std::string& dir, const RaceFile& race);
};

// X inside triangle (a, b, c), projected on the two axes picked by normal n. On an edge counts as inside.
bool insideTriangle(Vec3 a, Vec3 b, Vec3 c, Vec3 n, Vec3 x);

// A model's bounds: the middle of its vertices' box and the farthest vertex from it, scaled.
LRCORE_API Sphere modelBounds(const Model& model);

// An animation file as the core plays it (mirrored: positions' y negated, rotations' y and w).
struct AnimationFile;
LRCORE_API Animation animationOf(const AnimationFile& file, bool mirrored);

} // namespace lr
