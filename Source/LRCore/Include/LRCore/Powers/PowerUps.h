#pragma once

// The power-ups in a race (Powers.cpp, Weapons.cpp, Effects.cpp): the bricks, the actions and explosions with their
// pools, the effect slots, and what they do to the racers. Its operations take the world they run in.

#include "LRCore/Common/Api.h"
#include "LRCore/Powers/Hazards.h"
#include "LRCore/Race/Track.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

struct World;

// What a fired weapon is aimed by, besides its racer: a hazard's place and direction.
struct Aim {
	Vec3 position, direction;
	int8_t emplacement = -1; // the hazard that fired it
};

// The power-ups: the bricks (Track::bricks), every pool's actions (kind by kind) with the active list (newest first)
// and each kind's free list (its head first), the two explosion pools likewise, the effect slots; the bricks' clocks
// (Track::brickModels': advanced in the power-ups' update).
struct LRCORE_API PowerUps {
	List<Brick> bricks;
	List<Action> actions;
	List<uint8_t> actionList;
	std::array<std::vector<uint8_t>, Action::Kinds> freeActions;
	Array<Explosion, 8> explosions; // 6, then 2 spike explosions
	std::array<std::vector<uint8_t>, 2> explosionList, freeExplosions;
	uint32_t effectMask = 0;
	int32_t effectsUsed = 0;
	std::vector<uint8_t> targetsOn; // Track::targets still enabled
	Pose grapple; // the one grapple model every hook moves
	std::array<Clock, 4> brickClocks;

	// The frame's part: the bricks, the actions newest first, the explosions, the done released.
	void powerFrame(World& w, int32_t frameMs);
	// A racer's held colour used, the AI's chance to use it; an action made (or the furthest-along active one taken
	// over); a white brick dropped; an action cancelled.
	void useHeld(World& w, size_t racer);
	void consider(World& w, size_t racer);
	int16_t make(World& w, int8_t racer, uint8_t colour, int32_t level, const Aim* aim = nullptr);
	void dropWhite(World& w, size_t racer);
	void cancelTurbo(size_t racer);
	void cancelShield(size_t racer);
	void cancelMagnetHold(size_t racer);
	// What touched a racer in a step: a brick, a trap, a projectile.
	void touchBrick(World& w, size_t brick, size_t racer);
	void touchTrap(World& w, size_t action, size_t racer);
	void touchProjectile(World& w, size_t action, size_t racer);
	void blast(World& w, size_t explosion, size_t racer);
	int8_t explode(World& w, bool spike, Vec3 at, bool scar, int8_t owner);
	// A projectile's segment against the track (its surfaces' projectile events fired): true and the hit if it stops.
	bool shotHits(World& w, Vec3 a, Vec3 b, Vec3& hit, Vec3& normal);
	// The bricks' entities posed: a brick's cube or streaks at a place and size, every node's
	// matrix in the world.
	void brickNodes(const World& w, Track::BrickModel model, Vec3 at, float size, std::vector<NodeMatrix>& out) const;

	// What happens to a racer (Effects.cpp), in either mode; a contact's shields and curses.
	void halt(World& w, size_t racer);
	void resume(World& w, size_t racer);
	void enterGhost(World& w, size_t racer);
	void leaveGhost(World& w, size_t racer);
	void turboOn(World& w, size_t racer, int32_t level);
	void turboOff(World& w, size_t racer);
	void shieldOn(World& w, size_t racer, int32_t level);
	void curseOff(World& w, size_t racer);
	void magnetHold(World& w, size_t racer, bool on);
	void spin(World& w, size_t racer, float turns, float rate, float align);
	void spinOut(World& w, size_t racer);
	void endSpinOut(World& w, size_t racer);
	void tilt(World& w, size_t racer, float rate, int32_t ms);
	void impulse(World& w, size_t racer, Vec3 j);
	void push(World& w, size_t racer, int32_t which, Vec3 q);
	void endPush(World& w, size_t racer, int32_t which);
	void endShove(World& w, size_t racer);
	void contactPowers(World& w, size_t first, size_t second, Vec3 point);

private:
	// The frame's parts: a brick's, the actions', the done released; a brick sent home.
	void brickFrame(World& w, size_t brick, int32_t frameMs);
	void actionsFrame(World& w, int32_t frameMs);
	void releaseDone(World& w);
	void sendHome(World& w, size_t brick);
	// What happens to a racer (Effects.cpp).
	void curseOn(World& w, size_t racer, int32_t ms);
	void shove(World& w, size_t racer, Vec3 push);
	void boost(World& w, size_t racer, bool on);
	void hover(World& w, size_t racer, bool on);
};

} // namespace lr
