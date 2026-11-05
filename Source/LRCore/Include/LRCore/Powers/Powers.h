#pragma once

// Power-ups: the bricks on the track, what a racer holds, and the actions a brick makes when it is used.
//
// A colour brick (red, blue, green, yellow) becomes the racer's held colour; white bricks (up to three) are the level
// it is used at. Using it makes an action of that colour and level: a weapon (red), a trap (yellow), a shield (blue)
// or a turbo or warp (green). Actions come from fixed pools per kind and live in one list, newest first, updated once
// per frame; what touches racers (bricks, traps, explosions, projectiles) joins the race's trigger list, swept every
// step. Explosions are their own two pools.
//
// Everything here is plain data owned by World: actions and explosions in flat arrays (their pools are index lists),
// the trigger list's members in stable slots with the list's order kept apart.

#include "LRCore/Common/Math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

enum Colour : uint8_t { NoColour, Red, Blue, Green, Yellow };

struct Brick {
	enum State : uint8_t { Idle, Wait, Growing, Shrinking };
	Vec3 position, home;
	State state = Wait, next = Wait;
	int32_t ms = 0;        // in its state, counting up
	int32_t respawnMs = 3000;
	uint8_t colour = 0, nextColour = 0, assigned = 0; // a colour brick's; 0: a white brick
	bool touched = false, wasTouched = false; // this frame, the frame before
	bool dropped = false, goHome = false;     // a white brick a hit racer lost; back home after shrinking
	int32_t droppedMs = 0;
	int16_t member = -1;
	// Its drawn size: 0.8 idle, growing to 1 and back to 0.8 over 500 ms, shrinking to nothing in 250
	// ms; 0 while waiting.
	float size() const
	{
		switch (state) {
		case Idle: return 0.8f;
		case Growing: return ms <= 400 ? float(ms) * 0.0025f : 1 - float(ms - 400) * 0.002f;
		case Shrinking: return float(250 - ms) * 0.4f * 0.008f;
		default: return 0;
		}
	}
};

// A racer's power-ups and what they did to it.
struct Power {
	uint8_t held = NoColour;
	int16_t whites[3] = {-1, -1, -1}; // bricks, by slot
	uint8_t whiteCount = 0;
	int32_t aiClockMs = 0, aiIntervalMs = 150;
	uint8_t chances[4] = {255, 255, 255, 255}; // of 256, by colour - 1: red, blue, green, yellow
	uint8_t chargeColour = 0, chargeTarget = 0; // held until it has that many white bricks
	uint8_t shieldLevel = 0, turboLevel = 0;
	int32_t curseMs = 0;
	bool cursePassed = false; // QUIRK: never cleared once cursed
	uint8_t shoveSlot = 0;    // the push a shove started: 1 A, 2 B
	float carScale = 1;       // the car's size (a warp shrinks it)
	// Where it was and faced at its last timers (a lightning bolt comes from here).
	Vec3 sourcePosition, sourceForward{1, 0, 0};
};

// A projectile's flight: thrown from S at T under gravity, or homing (missiles).
struct Projectile {
	enum State : uint8_t { Idle, Flying, HitRacer, HitWorld, Expired };
	State state = Idle;
	Vec3 start, target, at, hit, normal, velocity; // at: where its model is
	float gravity = 0, speed = 0;
	int32_t ageMs = 0, lifeMs = 0;
	int8_t owner = -1, struck = -1; // racers: its owner now, the one it hit
};

struct Action {
	enum Kind : uint8_t { Magnet, Oil, Dynamite, Curse, Cannon, Hook, Bolt, Missile, Shield, Turbo, Warp, Kinds };
	// States 1 and 6 are every kind's; the others are each kind's own (named in Powers.cpp).
	enum : uint8_t { Ready = 1, Done = 6 };
	Kind kind = Magnet;
	uint8_t state = Ready;
	uint32_t ms = 0;     // the state's timer, counting down
	uint8_t level = 0;
	int8_t owner = -1, held = -1, pulled = -1; // racers (held, pulled: a magnet's; held: a hook's or bolt's victim)
	int16_t member = -1; // its entry in the trigger list
	int8_t slots[3] = {-1, -1, -1}; // effect slots it holds
	Vec3 point, direction; // a trap's place (a magnet's, a curse's target), a warp's target and its direction
	bool stopped = false, lifted = false; // a magnet's hold
	bool hasTarget = false, followsPath = false, demo = false; // a warp's
	Projectile shot;
	// The red weapons.
	int16_t aimPoint = -1;    // a target point it was fired at
	int32_t shockMs = 0;      // lightning: how long its victim has been struck
	Vec3 letGo;               // where the reeling hook goes
	uint8_t missile = 0;      // which of three
	float riseFrames = 0;     // its launch animation's time, riding the car (as last followed)
	Pose ride;
	Vec3 centre, heading, waypoint; // homing: the path's centre, its direction, a point to fly over
	bool hasWaypoint = false;
	float amplitude = 0, phase = 0; // a missile's spiral; (phase) a dynamite stick's tumble
	int32_t retargetMs = 0;
	int16_t checkpoint = -1;
	int8_t emplacement = -1; // a hazard that fired it (cannonball rain)
	bool smoked = false;     // a level 2 turbo's smoke spawned
};

struct Explosion {
	enum State : uint8_t { Idle = 1, Exploding, Scar };
	State state = Idle;
	int32_t ms = 0;      // left in its state
	float growth = 0, rate = 0;
	Vec3 centre;
	float radius = 0;
	float blast = 5;     // its full radius
	int8_t owner = -1;
	bool scar = false;
	int16_t member = -1;
};

// The race's list of what racers touch each step: sorted by each member's lowest x, a new member at the front.
struct Member {
	enum Kind : uint8_t { TouchSphere, Positional, Brick, Trap, Explosion, Projectile, Hazard };
	Vec3 centre;
	float radius = 0;
	Kind kind = TouchSphere;
	bool playersOnly = false, dead = false;
	uint16_t ref = 0; // index of its owner of that kind
};

// A timed wait in the step queue: it fires once its time reaches its length.
struct Wait {
	enum Kind : uint8_t { TrackTimer, Shove };
	int32_t lengthMs = 0, elapsedMs = 0;
	Kind kind = TrackTimer;
	uint8_t ref = 0;
};

} // namespace lr
