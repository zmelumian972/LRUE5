#pragma once

// What Powers.cpp (the manager, bricks, green, blue and yellow) and Weapons.cpp (red, explosions, projectiles) share:
// each kind's states, the pools, and the weapons' entry points.

#include "LRCore/Race/World.h"

namespace lr {

// Each kind's states after Ready (1), before Done (6).
enum : uint8_t {
	TurboIgnite = 2, TurboBoosting, TurboFading,
	WarpStarting = 2, WarpActive,
	ShieldActive = 3, ShieldFading,
	TrapArmed = 2, TrapActive, TrapFading, // oil slick, magnet (its active is its hold), curse
	DynamiteArmed = 2, DynamiteThrown, DynamiteSecond, DynamiteThird,
	ShotArmed = 2, ShotFlying, HookPulling, HookReeling, // cannonball, hook, missile (rising: armed)
	BoltRamp = 2, BoltSustain, BoltFade,
};

constexpr int32_t PoolSizes[Action::Kinds] = {3, 4, 3, 2, 4, 3, 3, 9, 5, 6, 3};
constexpr int32_t EffectSlots = 25;

// A state's timer: true when it ran out (the action then advances).
inline bool timeUp(Action& a, int32_t ms)
{
	if (uint32_t(ms) >= a.ms) {
		a.ms = 0;
		return true;
	}
	a.ms -= uint32_t(ms);
	return false;
}

// The red weapons and the projectiles (Weapons.cpp).
void startRed(World& w, Action& a, const Aim* aim);
void redFrame(World& w, Action& a, int32_t ms);
void releaseRed(World& w, Action& a);
void hooksMoved(World& w); // (after the actions' frames: the grapple's sphere for every hook out)
void explosionFrame(World& w, Explosion& e, int32_t ms);
// A projectile launched from start (flying, its age 0), then aimed at a point (still) or at a racer (led by its
// velocity; predictive: only if that point is behind the shooter), its speed raised by the shooter's motion.
void launch(Projectile& p, Vec3 start, float gravity, int32_t lifeMs, int8_t owner);
void aimAtPoint(const World& w, Projectile& p, int8_t shooter, Vec3 target, float speed);
void aimAtRacer(const World& w, Projectile& p, size_t shooter, size_t target, float speed, bool predictive);
// A frame of its flight: the ballistic path, stopped by the track (its surfaces' projectile events fired).
Projectile::State flight(World& w, Projectile& p, int32_t ms);

// The race's power-ups at load (Powers.cpp): each racer's habits (its driver's red, yellow, green and blue stats,
// two unused, its charge colour and target), the pools and the bricks.
void setupPowers(World& w, const std::vector<std::array<int32_t, 8>>& habits);
// The racer whose visible car is nearest in a cone (dmin2 <= d2 <= dmax2, angle cosine >= cosine), -1 none.
int8_t nearestInCone(const World& w, Vec3 origin, Vec3 direction, float dmin2, float dmax2, float cosine);

} // namespace lr
