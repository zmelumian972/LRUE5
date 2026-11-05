#pragma once

// What a race shows, kept by the core because the rules read it: the players'
// cameras, the kart's look (the body's lean, the chassis' and the driver's parts, the group sphere the touch spheres
// test), which animated scenery each camera draws, and the skeletons' poses. The presentation reads these after a
// frame and poses its meshes from them; it never computes a pose itself.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

struct Animation;
struct Model;

// A skeleton's node at rest: its pose under its parent (node 0 the root).
struct SkeletonNode {
	Vec3 position;
	Basis rotation;
	int16_t parent = -1;
};

// A node's matrix in the world: rows scaled (the model's scale) and the position. A point p of the node's frame is at
// p.x·rows.f + p.y·rows.s + p.z·rows.u + position.
struct NodeMatrix {
	Basis rows;
	Vec3 position;
};

// The quaternion of a rotation's rows (the trace method: w from the trace when it is > 0, else the largest diagonal
// leads), x y z w.
LRCORE_API Quat quaternionOf(const Basis& rows);

// Skeleton sampling, shared by the rules and the drawing: node `node`'s pose in its parent's
// frame at `time` (frames) of `part`; a channel without keys keeps the rest value; a time below 0 is the rest pose.
LRCORE_API Pose sampleNode(const std::vector<SkeletonNode>& nodes, const Animation* animation, size_t node, int32_t part, float time, bool loop);
// Every node's matrix in the world: sampled at the time (a blend of two parts' poses while `blend` < 1: `from` at
// `fromTime` toward the part by `blend`), through its parents, scaled and placed.
struct PartPose {
	int32_t part = -1;
	float time = 0;
	bool loop = true;
	int32_t fromPart = -1; // a transition's old part (-1 none)
	float fromTime = 0, blend = 1;
};
LRCORE_API void poseSkeleton(const std::vector<SkeletonNode>& nodes, const Animation* animation, const PartPose& pose, const Pose& place, float scale,
	std::vector<NodeMatrix>& out);
// The same in two halves, for a skeleton drawn frame after frame: each node's pose under its parent sampled into
// `locals` (a channel without keys keeps the node's value from its last sampling: at first its rest pose), then
// placed.
LRCORE_API void sampleLocals(const std::vector<SkeletonNode>& nodes, const Animation* animation, const PartPose& pose, std::vector<Pose>& locals);
LRCORE_API void placeNodes(const std::vector<SkeletonNode>& nodes, const std::vector<Pose>& locals, const Pose& place, float scale,
	std::vector<NodeMatrix>& out);

// A model's vertices posed on its nodes: each vertex in the frame of the node current when it was
// loaded (Model::vertexParts), placed by that node's matrix (a vertex on a node past the list stays where it is).
LRCORE_API void skinVertices(const Model& model, const std::vector<NodeMatrix>& nodes, std::vector<Vec3>& out);
// Whether a triangle has a vertex on another node than its section's: no rigid mesh per node can draw that model.
LRCORE_API bool joinsNodes(const Model& model);

// The players' race keys besides driving (Key in Kart.h): the camera, the map gadget, looking back.
// One player's camera.
struct LRCORE_API PlayerCamera {
	enum Kind : uint8_t { Chase, Cockpit, Finish, Transition, Flyby };
	int8_t racer = -1;
	uint8_t view = 0;     // the chosen view, 0..3
	uint8_t profile = 0;  // the table's row: 0..4, or 5..7 in split screen
	Kind kind = Chase;
	bool split = false;
	bool dirty = true;     // re-snapped at its next update
	bool lookBack = false; // held
	bool finishing = false; // the finish view is wanted (re-checked until it holds)
	bool turbo = false;     // the drive's turbo as last seen (its rise pulls the camera in)
	int32_t clockMs = 0;    // the transition's or the finish swing's
	// The chase: the view direction (2D), the turn lead, the smoothed target, the turbo pull-in.
	float vx = 0, vy = 0, lead = 0, pull = 1;
	Vec3 target;
	// The pose: eye and rows (right, down, look), smoothed; the raw pose of this update; a transition's start.
	Vec3 eye, rawEye, fromEye;
	Vec3 right{0, -1, 0}, down{0, 0, -1}, look{1, 0, 0};
	Quat rotation, rawRotation, fromRotation, toRotation;
	Vec3 toEye; // a transition's end
	Vec3 viewEye;      // the chosen view's own smoothed pose (what a transition heads for is its raw one)
	Quat viewRotation;
	// The lens: the vertical FOV (degrees, before the shake), the clip planes, the viewport (left, top, right, bottom
	// of the 640x480 screen).
	float fov = 65, nearPlane = 5, farPlane = 800;
	std::array<int32_t, 4> viewport = {0, 0, 640, 480};
	float aspect = 4.0f / 3; // the frustum's width over height (the viewport's; a capture made in a wider window, wider)
	bool hidden = false; // its racer is warping (GHOST): the world isn't drawn

	// What it shows: the eye and rows with looking back applied.
	Vec3 shownEye() const;
	void shownRows(Vec3& r, Vec3& d, Vec3& l) const;
	// A sphere outside the view's frustum.
	bool outside(Vec3 centre, float radius) const;
};

// A part playing on a model: the chassis' (forward or reverse) or the driver's.
struct PartClock {
	int32_t part = -1;
	float time = 0;
	bool loop = true;
	int32_t next = -1; // a one-shot's follow-up
	// A transition (the driver's 300 ms blend): the old part keeps playing, progress 0..1.
	int32_t fromPart = -1;
	float fromTime = 0, progress = 1;
	PartPose pose() const { return {part, time, loop, progress < 1 ? fromPart : -1, fromTime, progress}; }
};

// A racer's look: the body's springs and pose, the chassis' and the driver's parts, the driver
// model's own state.
struct RacerLook {
	float pitch = 0, roll = 0; // the lean springs' velocities
	Pose body;                 // the body (and the second variant), leaning
	Pose driver;               // at the driver mount through the body
	PartClock car, variant, figure; // the chassis car's, the second variant's, the driver's
	std::vector<Pose> carNodes, variantNodes, figureNodes; // their skeletons as last drawn (under their parents)
	int32_t lookMs = 0;  // the driver's look cooldown
	float lastSpeed = 0; // the forward speed of the last update
	uint8_t marks = 0;   // reactions: bit 1 hit
	bool stoppedHard = false; // this update (the negative reaction's sound is the presentation's)
	// The marks the show's events need: skidding, the spin's skid, airborne and the ground count
	// when it last was; each wheel's surface particle (its emitter's name; empty: none).
	bool skidding = false, spinSkid = false, airborne = false, drifting = false;
	int32_t grounded = 4;
	std::array<std::array<char, 9>, 4> wheelParticles = {};
};

// What a racer is drawn with, fixed at setup.
struct RacerShape {
	enum Member : uint8_t { Body, Variant, Car, Driver };
	bool present[4] = {true, false, true, true}; // the cheats take members away
	Sphere body;       // the body model's bounds (model frame, scaled)
	float carScale = 1, variantScale = 1, driverScale = 1; // the models' scales
	int16_t carAnimation = -1, variantAnimation = -1; // World::shapes' animations
	Vec3 mount;        // the driver's seat in the car's frame
};

} // namespace lr
