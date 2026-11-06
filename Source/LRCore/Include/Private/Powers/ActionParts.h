#pragma once

// What the power-ups' files share besides PowerParts.h. Each action kind is one ActionKind (Powers.cpp's table runs
// them), defined with its colour: Boosts.cpp (green and blue), Traps.cpp (yellow) and Weapons.cpp (red, with
// Missiles.cpp's homing missiles); Projectiles.cpp aims and flies the shots and runs the explosions, Bricks.cpp the
// bricks.

#include "Powers/PowerParts.h"

namespace lr {

// An action kind: how it starts (made at a level, perhaps aimed), its frame, what it undoes as it is released, what
// it does to a racer that meets its trap, the loop it stops as it is released (-1 none).
struct ActionKind {
	void (*start)(World&, Action&, int32_t level, const Aim* aim) = nullptr;
	void (*frame)(World&, Action&, int32_t ms) = nullptr;
	void (*release)(World&, Action&) = nullptr;
	void (*touch)(World&, Action&, size_t racer) = nullptr;
	int32_t loop = -1;
	bool framesWhenDone = false; // its frame runs once it is done too (a turbo's)
};

extern const ActionKind TurboAction, WarpAction, ShieldAction, OilAction, DynamiteAction, MagnetAction, CurseAction, RedAction;

constexpr Vec3 DropProbe = {0, 0, 50}; // a trap's ground (or a warp's landing) is looked for this far under the car
constexpr Vec3 ShotLift = {0, 0, 5}; // shots leave from, and aim at, this far above a car's place
constexpr float AimAhead = 500; // with nothing to aim at, a shot aims this far along the car
// The aiming cones' reach, squared: 10 to 400 ft.
constexpr float AimNear2 = 100, AimFar2 = 160000;
// A struck racer's knock (a blast's scaled by its flash left): along its heading and up.
constexpr float KnockForward = 80, KnockUp = 200;

inline uint16_t indexOf(const World& w, const Action& a) { return uint16_t(&a - w.powerUps.actions.data()); }
inline Owner ownerOf(const World& w, const Action& a) { return {Owner::Action, int16_t(indexOf(w, a))}; }
inline bool shielded(const World& w, size_t r) { return w.racers.race[r].flags & RacerShielded; }
// The checkpoint after one along the course, -1 none.
inline int32_t nextCheckpoint(const Checkpoint& c) { return c.next[0] == 255 ? -1 : c.next[0]; }

// Its touch entry in the trigger list (any old one killed): a sphere of a kind at centre.
inline void addMember(World& w, Action& a, Member::Kind kind, Vec3 centre, float radius)
{
	w.triggers.killMember(a.member);
	a.member = w.triggers.addMember(w, {centre, radius, kind, false, false, indexOf(w, a)});
}
inline void keepMember(World& w, Action& a, Vec3 centre)
{
	if (a.member >= 0) {
		w.triggers.members[a.member].centre = centre;
	}
}

// Effect slots taken (Powers.cpp): false if not all n could be.
bool takeSlots(World& w, Action& a, int n);
// The cone searches (Projectiles.cpp): the lowest racer index in a cone (after `after`), the nearest target point
// switched on in it, -1 none.
int8_t firstInCone(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine, int after = -1);
int16_t nearestTarget(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine);
// A projectile that met a level 2 or 3 shield goes back at its owner (Projectiles.cpp).
void deflect(World& w, Projectile& p, size_t by);
// What a cannonball or a missile does when it stops (Weapons.cpp).
void stopped(World& w, Action& a, bool missile);
// The homing missiles (Missiles.cpp).
void startMissile(World& w, Action& a);
void missileFrame(World& w, Action& a, int32_t ms);

} // namespace lr
