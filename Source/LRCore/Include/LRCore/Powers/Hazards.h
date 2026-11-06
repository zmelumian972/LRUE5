#pragma once

// A track's moving parts besides the karts: its animated scenery's clocks, the hazards, the event table's timers and
// forces, and the track timers. Their definitions are the Track's; this is their state in one race (World's), and the scenery's own operations
// (Hazards.cpp) take the world they run in.
//
// Events are ids started, ended or "fired" (both at once), at a place or for a racer, by touch spheres, positional
// spheres, surfaces, projectiles, timers and the hazards themselves; each reaches the event table's resources (timers
// that hold and chain events, the lap marks, forces, links) and then every hazard, whose trigger id activates or
// deactivates it.

#include "LRCore/Common/Api.h"
#include "LRCore/Powers/Powers.h"
#include "LRCore/Race/Track.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

struct World;

// An animated model's part playback, in frames: `loop` is the current part's loop-current mark;
// a part animation queues a part that takes over when the current one reaches its last frame.
struct Clock {
	float time = 0;
	float drawn = -1; // when it was last drawn (what its skeleton shows; -1: never, its rest pose)
	float rate = 0; // frames per ms
	int16_t part = -1;
	int16_t queued = -1; // waiting to take over (-1 none)
	bool loop = false, animating = false, done = false;
	bool queueFree() const { return queued < 0; }
};

struct Hazard {
	enum Type : uint8_t {
		Pillar,
		Sphinx,
		Hammer,
		Ghost,
		Lava,
		Code,
		Rocket,
		Snow,
		Smoke,
		Crane,
		Launcher,
		Opener, // a triggered animation that opens a wall
		Oscillator,
		Launchers, // a multi-launcher
		Rock,
		CurseDrop,
		Sweep,
		Rain,
		Grabber,
		WarpPad,
	};
	enum State : uint8_t { Idle, Active, Ending };
	Type type = Pillar;
	State state = Idle;
	int32_t trigger = -1;
	int16_t member = -1; // its sphere in the trigger list, while it has one
	Vec3 centre; // its sphere's
	float radius = 0;
	bool armed = false, landed = false, lit = false, on = false; // per type
	// Per type (ms). The cycle: the code display's blink, the ghost's search, the snow's restart, the sweep's period,
	// the rain's interval, the grabber's hold. The cooldown: the lava's after an eruption, the sweep's between shots,
	// the grabber's grip and rest.
	int32_t cycleMs = 0, cooldownMs = 0;
	uint8_t right[3] = {}, reset[3] = {}; // the code puzzle's switches
	uint8_t progress = 0;
	Projectile ball; // a launcher's
	int8_t launch = 0, aimed = 0; // a multi-launcher's choice
	int8_t held = -1; // the grabber's racer
	uint8_t grab = 0; // the grabber: 0 open, 1 holding, 2 cooling
	Vec3 point, muzzle; // the rain's drop point and muzzle
	Basis rows; // a rolling rock's box
};

// An event table resource's state (timers and forces; lap marks keep none).
struct EventState {
	enum Phase : uint8_t { Idle, Starting, Active, Ended, EndPending };
	Phase phase = Idle;
	int32_t remainingMs = 0;
	bool held = false, armed = false;
	Vec3 at; // a sound's or a particle's place now (from the event's, if it takes it)
};

struct TrackTimerState {
	bool on = false;
	int32_t delayMs = 0;
};

// A sphere that starts events at its centre: running from its first touch to a frame with no touch.
struct PlaceState {
	bool active = false, touched = false;
};

// The track's moving parts: the animated scenery's clocks (Track::animated's), the hazards (Track::hazards'), the event
// table's states (Track::events') and timers, a sphinx's blow-up track's cursor while it plays (else -1). The draw
// distances as they are now, squared, per level of detail: the animated entities' and the plain
// models' (Track::models); hazards and the event table's model distances hide a model (-1) and show it.
struct LRCORE_API Scenery {
	List<Clock> clocks;
	List<std::array<float, 3>> animatedDistances, modelDistances;
	List<Hazard> hazards;
	List<EventState> events;
	std::vector<TrackTimerState> timers;
	float blowup = -1;

	// The animated scenery's clocks advanced (the frame's); a node's pose in its parent's frame, sampled at a time of a
	// part (looping or not), a time below 0 (never drawn) its rest pose.
	void advanceClocks(World& w, int32_t frameMs);
	Pose nodeLocal(const AnimatedModel& model, const Animation* animation, size_t node, int32_t part, float time, bool loop) const;
	// The hazards: set up as the race starts, their frame, what events do to them, what their spheres do to racers.
	void setupHazards(World& w);
	void hazardFrame(World& w, int32_t frameMs);
	void hazardEvent(World& w, int32_t start, int32_t end, const Vec3* at, int32_t racer, bool force);
	void touchHazard(World& w, size_t hazard, size_t racer);
};

} // namespace lr
