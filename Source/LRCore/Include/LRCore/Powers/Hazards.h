#pragma once

// A track's moving parts besides the karts: its animated scenery's clocks, the hazards, the event table's timers and
// forces, and the track timers. Their definitions are the Track's; this is their state in one race (World's).
//
// Events are ids started, ended or "fired" (both at once), at a place or for a racer, by touch spheres, positional
// spheres, surfaces, projectiles, timers and the hazards themselves; each reaches the event table's resources (timers
// that hold and chain events, the lap marks, forces, links) and then every hazard, whose trigger id activates or
// deactivates it.

#include "LRCore/Powers/Powers.h"

#include <cstdint>

namespace lr {

// An animated model's part playback, in frames.
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
	int32_t timer = 0, timer2 = 0; // per type (ms)
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

} // namespace lr
