#pragma once

// One kart: its fixed parameters, its state, and the three things that move it each step.
//
// - The player's controls (once per frame) turn keys into a steering value and a throttle, sent to the driver.
// - The driver (each step) turns those into what the body uses: a turn radius, a thrust, and steering parameters
//   (gain, slip, how far the heading may lag the nose), with powerslides as a variant.
// - The body (each step) is a rigid box on four wheel rays: forces, integration, snapping to the ground, a heading that
//   lags the nose, and walls handled by backing up to just before the hit and bouncing off.
//
// Units: feet, milliseconds (speeds in ft/ms), radians. Everything is plain data (no pointers), so a kart's state can
// be copied, compared and stepped for many karts at once.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"

#include <cstdint>
#include <vector>

namespace lr {

class Track;

// What a kart is built from (the chassis, the built car or the champion's preset).
struct KartSource {
	float mass;          // chassis mass plus the built car's piece count, or the champion's
	int pieceWeight = 0; // the built car's piece count; 0 for champions
	Vec3 centerOfMass;   // local
	Vec3 frontLeftWheel; // local; the anchor the other wheels hang from
	float rearX;         // rear wheels' x
	float rightY, leftY; // the rear wheels' y (right negative)
	int handling = 50, acceleration = 80, topSpeed = 50; // chassis stats 0..100
	float shadowWidth, shadowLength;
};

struct KartParams {
	float mass, invMass, massScale;
	Vec3 inertia, invInertia; // diagonal, body frame
	Vec3 centerOfMass;
	Vec3 anchor;
	float trackWidth, wheelbase;
	Vec3 bodyPoints[4]; // the box corners walls are swept with
	int handling, acceleration, topSpeed;
	float handlingScale, accelerationScale, topSpeedScale;
};

LRCORE_API KartParams makeKartParams(const KartSource& source);

enum KartFlag : uint32_t {
	KartSteering = 1u << 0,
	KartSpinning = 1u << 1,
	KartSpinFresh = 1u << 2,  // the spin started this step: its timer starts next step
	KartHover = 1u << 3,      // held up off the ground (magnets, ghosts)
	KartPushA = 1u << 5,      // external pushes (shoves, grapples, force zones)
	KartPushB = 1u << 6,
	KartSpinOut = 1u << 7,    // tumbling: no ground probe depth, no surface pushes
	KartAtRest = 1u << 8,
	KartSteeringAux = 1u << 9, // cleared with KartSteering
	KartPowerslide = 1u << 10,
	KartYawLock = 1u << 11,   // kicked off a wall: weak grip, no thrust or turning until the yaw timer ends
	KartGhost = 1u << 12,     // passes the track's walls
	KartBoost = 1u << 16,
	KartRouteGhost = 1u << 17, // route playback only
	KartPushed = 1u << 18,    // route playback only: a push sets its rate
	KartCurseSlow = 1u << 19,
	KartIgnoreSurfaces = 1u << 20,
};

struct Wheel {
	Vec3 position; // world
	Vec3 hit;      // ground point
	Vec3 force;    // contact force (a normal while probing)
	Vec3 push;     // the surface's push
	float depth2;  // squared distance of the hit above the ray's end
	float rolling, slopeHold, friction, grip; // the surface's, or defaults
	int16_t surface; // the last surface touched, -1 none
	int8_t cacheSlot; // -1 none
	bool contact, touched;
};

// Ground planes recently under the wheels: tried before a full query, and they win over it.
struct RecentPlanes {
	struct Slot {
		Vec3 corners[3];
		Vec3 normal;
		float offset;
		int32_t age;
		int16_t surface;
	};
	Slot slots[8];
	int32_t count;
};

// What a wall hit backs up to (saved at the start of each step and after each wall bounce).
struct KartSnapshot {
	Vec3 position, velocity, angularMomentum, angularVelocity, heading;
	Basis rotation, invInertiaWorld;
	float lift, bank;
	struct WheelPart {
		Vec3 position, push;
		float rolling, slopeHold, friction, grip;
		int16_t surface;
		bool contact, touched;
	} wheels[4];
	Vec3 bodyPoints[4];
};

struct Kart {
	// Rigid body. invInertiaWorld is a matrix by rows.
	Vec3 position, velocity, angularMomentum, angularVelocity, centerOfMassWorld;
	Basis rotation, invInertiaWorld;

	// Ground, heading and walls.
	Vec3 heading, groundNormal, slopeForce, wallNormal;
	int32_t contacts; // wheels on the ground: 0, 1, 2 or 4
	uint32_t flags;   // KartFlag
	int32_t wallHits;

	// Set by the driver.
	float thrust, turnRadius, maxSpeed, dragCoefficient, gripScale;
	float steerGain, steerAlignMin, lagLimit, slipRatio;
	float yawRate;

	// Velocity statistics, along and across the heading (refreshed at fixed points of the step only).
	float speed, headingSpeed;
	Vec3 velocityDirection, forwardVelocity, lateralVelocity;

	Vec3 pushes[2]; // KartPushA, KartPushB
	int32_t airborneMs, spinOutMs, pitchMs, yawMs, spinAlignMs;
	float lift, liftTarget, liftRate, bank, bankTarget; // hover: height (ft) eased at liftRate (ft/s), and bank (rad)
	Vec3 axles[2];      // hover: the front and rear axles' middles (the two probes)
	float slideFactor;  // how far a powerslide leans the drawn car
	float visualBank;   // the drawn car's lean (rad)
	Vec3 bodyPoints[4]; // world, as last swept
	Vec3 resetPosition; // where the kart goes back to when it leaves the world
	Basis resetRotation;
	Wheel wheels[4];
	RecentPlanes cache;
	KartSnapshot saved;

	// Inputs from the rest of the game, current before each step.
	float boundsRadius; // the visible model's (it sets the probe rays' height)
	bool preStart, engineStarted; // held on the grid while both
	int32_t worldCount;  // collision volumes: [0] the track, [1] the checkpoints, then dynamic ones
	int32_t worlds[5];
};

// The kart at its start pose, at rest.
LRCORE_API void placeKart(Kart& kart, const KartParams& params, Vec3 position, Basis rotation);

// What a body's wall sweeps met that the race counts, in order: a checkpoint's gate (crossed forward or backward),
// or a surface that is the finish line or has a touch event.
struct SweepHit {
	int16_t checkpoint; // -1: not a gate
	bool forward;
	bool finishLine;
	int32_t event;      // the touch event, -1 none (event 0 is one: C1R3's "in" surface)
	Vec3 point;         // where (world)
};

// One body step of dt ms (1..40), with its race checks. What the sweeps met is appended to hits; the car as drawn
// (leaning, and before the race checks put the body back) goes to drawn.
// Each substep's wheel surfaces (in contact, rolling forward) go to surfaces.
LRCORE_API void stepBody(Kart& kart, const KartParams& params, const Track& track, int32_t dt, std::vector<SweepHit>* hits = nullptr,
	Pose* drawn = nullptr, std::vector<std::array<int16_t, 4>>* surfaces = nullptr);

// The drawn car's pose: the body's, leaned by the visual bank (raised so the low side stays down).
LRCORE_API Pose drawnPose(const Kart& kart, const KartParams& params);

// The velocity statistics (speed, direction, along and across the heading) from the velocity and heading.
LRCORE_API void refreshStatistics(Kart& kart);

// What power-ups and hazards do to a body.
// A spin: `turns` turns at `rate` rad/ms (ignored while spinning); with align > 0 a brake can end it after that many.
LRCORE_API void startSpin(Kart& kart, const KartParams& params, float turns, float rate, float align);
LRCORE_API void endSpin(Kart& kart);
// A pitch for ms at rate rad/ms (a tilt back, a nod).
LRCORE_API void pitchKick(Kart& kart, const KartParams& params, float rate, int32_t ms);
// Held up off the ground on two probes, the lift eased to its target.
LRCORE_API void enterHover(Kart& kart, const KartParams& params);
LRCORE_API void leaveHover(Kart& kart);
// A turbo's boost (more top speed and grip; not while cursed) or its end.
LRCORE_API void startBoost(Kart& kart, const KartParams& params, bool boosted);
// The heading put on the nose.
LRCORE_API void snapHeading(Kart& kart);
// A push (0: A, 1: B) of q (a force, times the mass scale) until ended.
inline void startPush(Kart& k, const KartParams& p, int which, Vec3 q)
{
	k.pushes[which] = q * p.massScale;
	k.flags |= which ? KartPushB : KartPushA;
}

// What the player's keys (or the AI) ask of the kart.
enum DriverFlag : uint32_t {
	DriverTurbo = 1u << 0,
	DriverSliding = 1u << 2, // powerslide
	DriverCursed = 1u << 4,  // steering inverted, the throttle jittered
	DriverReversing = 1u << 5, // returning to the path, stuck: backing up
	DriverReturning = 1u << 6, // an AI driving back to a route
	DriverBrakeToStop = 1u << 7,
	DriverTurboWeak = 1u << 8, // a turbo that hit a wall
};

struct Driver {
	float radius;     // the turn radius asked for, eased each step
	float prevRadius;
	float thrust;     // the thrust asked for
	uint32_t flags;   // DriverFlag
	int32_t slideMs;
	bool tightSlide;  // the brake was held when the powerslide began
	bool drifting;    // the racer's: set with a powerslide, cleared after the step that ended it
	int32_t previewMs; // returning: how far along the route the point aimed at is
	int32_t stuckMs;  // returning: time spent too slow (or reversing)
	int32_t jitterMs; // cursed: until the throttle's offset changes
	float curseThrottle;
};

// The driver's inputs. steer in [-1, 1], positive left; throttle in [-1, 1].
LRCORE_API void steerInput(Driver& driver, const KartParams& params, float steer);
LRCORE_API void throttleInput(Driver& driver, const Kart& kart, float throttle);
LRCORE_API void startPowerslide(Driver& driver, Kart& kart, bool tight);
LRCORE_API void endPowerslide(Driver& driver, Kart& kart);
// Each step, before the body.
LRCORE_API void stepDriver(Driver& driver, Kart& kart, const KartParams& params, int32_t dt);
// The end of a step of the driver: the radius eased to what the grip allows into the body's turn radius and
// steering, and the thrust. The AI's drivers (returning to a route, braking to a stop) set radius and thrust, then this.
LRCORE_API void applyDriver(Driver& driver, Kart& kart, const KartParams& params);
// Each step, after the body.
inline void afterBody(Driver& driver) { driver.drifting = driver.drifting && (driver.flags & DriverSliding); }

// The player's keys.
enum Key : uint16_t {
	KeyLeft = 1u << 0,
	KeyRight = 1u << 1,
	KeyThrottle = 1u << 2,
	KeyBrake = 1u << 3,
	KeyUse = 1u << 4, // the held power-up
	KeyCamera = 1u << 5, // the next camera view
	KeyMap = 1u << 6,    // the next HUD gadget
	KeyDrift = 1u << 7,
	KeyLookBack = 1u << 8, // held
};

struct Controls {
	float steering; // eases toward the keys' lock, and back to the centre
	// What the last update sent (for checking).
	float sentSteering, sentThrottle;
	int8_t startedDrift; // -1 none, 0 normal, 1 tight
	int8_t endedDrift;   // calls
	// The start boost: how long since the throttle was let go (or the brake held), and its window.
	int32_t throttleHeldMs, boostWindowMs;
	bool throttleDown, useDown;
};

// Once per frame, before the steps. While blocked (before the start) nothing is sent.
LRCORE_API void updateControls(Controls& controls, uint16_t keys, bool blocked, int32_t frameMs, Driver& driver, Kart& kart,
	const KartParams& params);

} // namespace lr
