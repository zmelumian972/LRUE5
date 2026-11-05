#pragma once

// What a race shows besides its 3D, as far as the core keeps it: the
// heads-up display's state and clocks per player (its elements are drawn by the menus' 2D path, Menus::drawHud), and
// the events the rules raise each frame for the show (effects, sounds, voice lines, the sky), in the order raised.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

// One player's HUD: the banner's timer (0 none; > 0 the countdown's ms from 1; < 0 FINISH's,
// counting down), the real ms gathered since its last update, the place shown and its pulse, the final place once
// FINISH showed (-1), the best lap and the completed laps it has seen, the smoothed speed, whether a dialog hides
// part of it. The gadget's mode is RaceView::mapModes.
struct Hud {
	int32_t banner = 0;
	int32_t clockMs = 0;
	int32_t place = 8, pulseMs = 0, finalPlace = -1;
	int32_t bestMs = 0, laps = -1;
	float speed = 0;
	bool visible = true;
	bool standings = false; // the race's standings showed this frame (they clear the banner after it)
};

// What an effect or a sound belongs to: the show maps it to its handles.
struct Owner {
	enum Kind : uint8_t { None, Racer, Action, Explosion, Hazard, TrackEvent, Brick, World };
	Kind kind = None;
	int16_t index = -1;
	uint8_t role = 0; // which of the owner's (a racer's wheel, a hazard's spot...)
	bool operator==(const Owner&) const = default;
};

// An event of the frame, raised where the rules raise it, in order.
struct RaceEvent {
	enum Kind : uint8_t {
		Spawn, // an emitter (name) in a set (set: 0 the race's, 1 the track's, 2 either: the race's first)
		Finish, // the owner's emitter winds down
		Release, // the owner's emitter stops at once
		StopEffects, // every effect (a restart)
		DriftStart, // a racer starts drifting: dust, then the skid effects unless skidding (the show's handles)
		SkidStart, // reason: why
		SkidStop,
		DustStart,
		SmokeStart, // the car smoke
		Landed, // "carland" and the landing sound (value: how many more wheels touch)
		WheelParticle, // a wheel's surface particle (name; empty: none)
		Sparks, // kart against kart ("carsprk") at the place
		Sound, // a sound id: one-shot or looping, started or stopped
		Voice, // a racer's voice line (value: VoiceLine)
		Sky, // a sky state (name) over value ms
		// Sounds the show picks and paces (its cooldowns and draws): a racer against a wall,
		// two karts (value: the second racer), a shield taking a hit.
		Scrape,
		Bump,
		Absorb,
		// RACEC0R3's code display: value bit d set: light d (mmcode1..3) shows blue, else red;
		// flags 0: still (a read-out), 1: blinking (a success).
		CodeDisplay,
	};
	// The part of the frame it was raised in.
	enum Stage : uint8_t { Setup, Powers, Events, Hazards, Timers, Controls, Queue, Animation, Racers, Session, Hud };
	enum Reason : uint8_t { Drift, Spin, Turbo, Magnet, Halted, Ghost };
	enum VoiceLine : uint8_t { Negative, Positive, Taunt };
	Kind kind = Spawn;
	Stage stage = Setup;
	Owner owner;
	uint8_t set = 0;
	bool looping = false, stop = false, positional = true;
	int32_t sound = -1; // Sound
	int32_t value = 0; // Landed, Voice, Sky (its ms); a SkidStart/SkidStop's reason; a WheelParticle's wheel; a track
	// sound's retrigger chance (255: none)
	uint8_t flags = 0; // Sky: hide dome, show dome, hide sky world, show sky world (bits 0..3)
	float near = 30, far = 300, volume = 1, pitch = 1; // a sound's distances, volume and pitch
	Vec3 place; // where (a sound's or a spawn's), if positional
	std::array<char, 9> name = {}; // an emitter's or a sky state's
};

} // namespace lr
