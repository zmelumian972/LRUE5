// The power-up manager: the bricks (taken, swapped, dropped, sent home), holding and using a colour (a player's key,
// the AI's chances), making an action from its kind's pool (or taking over the furthest-along active one), the
// effect slots some kinds need, the manager's frame (bricks, actions newest first, explosions, the done released),
// and the green (turbo, warp), blue (shield) and yellow (oil slick, dynamite, magnet, curse) actions.

#include "Common/Assert.h"
#include "LRCore/Race/World.h"
#include "Powers/PowerParts.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

constexpr int32_t Grows = 500, Shrinks = 250; // ms (strictly over)
// The power-ups' sound ids.
constexpr int32_t TurboSounds = 36; // + the turbo's level
constexpr int32_t BrickSounds = 11; // + the white bricks held: a brick picked up
constexpr int32_t BrickBackSound = 14; // a taken brick grows back
constexpr int32_t WhooshSound = 65; // a turbo's start, a warp's
constexpr int32_t WarpOutSound = 39, WarpInSound = 41; // the player's own warp
constexpr int32_t ShieldEndSound = 59;
constexpr int32_t OilSplashSound = 46, OilLoopSound = 48;
constexpr int32_t DynamiteBlastSound = 5; // the cannon's blast too
constexpr int32_t MagnetDropSound = 33, MagnetLoopSound = 34, MagnetCatchSound = 35, MagnetFadeSound = 74;
constexpr int32_t TrapSpinSound = 47;
constexpr int32_t CurseLoopSound = 9;
constexpr float RedConeMin = 100, RedConeMax = 62500, RedConeCos = 0.95999998f;

bool takeSlots(World& w, Action& a, int n)
{
	bool all = true;
	for (int s = 0; s < n; s++) {
		if (w.powerUps.effectsUsed >= EffectSlots) {
			all = false;
			continue;
		}
		int free = 0;
		while (w.powerUps.effectMask & (1u << free)) {
			free++;
		}
		w.powerUps.effectMask |= 1u << free;
		w.powerUps.effectsUsed++;
		a.slots[s] = int8_t(free);
	}
	return all;
}

void giveSlotsBack(World& w, Action& a)
{
	for (int8_t& s : a.slots) {
		if (s >= 0) {
			w.powerUps.effectMask &= ~(1u << s);
			w.powerUps.effectsUsed--;
			s = -1;
		}
	}
}

// Where a racer lays a trap: on the ground under its car (1 above it), else at the car.
Vec3 dropPoint(const World& w, size_t racer)
{
	const Vec3 at = w.racers.visible[racer].position;
	SegmentHit hit;
	return w.track.query(w.track.trackVolume, at, at - Vec3{0, 0, 50}, hit) ? hit.point + Vec3{0, 0, 1} : at;
}

void layTrap(World& w, Action& a, Vec3 at, float radius)
{
	a.point = at;
	w.killMember(a.member);
	a.member = w.addMember({at, radius, Member::Trap, false, false, uint16_t(&a - w.powerUps.actions.data())});
}

// The checkpoint a racer heads for: the one after its last (the first if it has none), -1 if the track has none.
int32_t nextCheckpoint(const World& w, size_t racer)
{
	const int16_t c = w.racers.race[racer].checkpoint;
	if (w.track.checkpoints.empty()) {
		return -1;
	}
	const uint8_t next = c < 0 ? 0 : w.track.checkpoints[size_t(c)].next[0];
	return next == 255 ? -1 : next;
}

// The release: the slots and the touch entry given back, and what the kind undoes.
Owner ownerOf(const World& w, const Action& a) { return {Owner::Action, int16_t(&a - w.powerUps.actions.data())}; }

void release(World& w, Action& a)
{
	w.raise(RaceEvent::Finish, ownerOf(w, a)); // (its particle, if it has one, winds down)
	if (a.kind == Action::Oil || a.kind == Action::Magnet || a.kind == Action::Curse) {
		RaceEvent& loop = w.raise(RaceEvent::Sound, ownerOf(w, a)); // (its loop stops)
		loop.sound = a.kind == Action::Oil ? 48 : a.kind == Action::Magnet ? 34 : 9;
		loop.stop = true;
	}
	giveSlotsBack(w, a);
	w.killMember(a.member);
	switch (a.kind) {
	case Action::Turbo:
		if (a.owner >= 0) {
			w.turboOff(size_t(a.owner));
			w.resume(size_t(a.owner));
		}
		break;
	case Action::Warp:
		if (a.owner >= 0) {
			w.racers.powers[size_t(a.owner)].carScale = 1;
		}
		a.followsPath = false;
		break;
	case Action::Magnet:
		if (a.held >= 0) {
			w.magnetHold(size_t(a.held), false);
		}
		a.held = -1;
		break;
	case Action::Dynamite: a.shot.state = Projectile::Idle; break;
	case Action::Cannon:
	case Action::Hook:
	case Action::Bolt:
	case Action::Missile: releaseRed(w, a); break;
	default: break;
	}
}

// A pool's action to reuse: the free list's head, else the furthest-along active one its kind may take over.
int16_t takeAction(World& w, Action::Kind kind)
{
	std::vector<uint8_t>& free = w.powerUps.freeActions[kind];
	if (!free.empty()) {
		const uint8_t a = free.front();
		free.erase(free.begin());
		return a;
	}
	// (A turbo takes over any green of level 0 to 2, a shield any blue, the others their own colour and level: each
	// its own kind.)
	int best = -1;
	uint8_t bestState = 0;
	uint32_t bestMs = UINT32_MAX;
	for (size_t i = 0; i < w.powerUps.actionList.size(); i++) {
		const Action& a = w.powerUps.actions[w.powerUps.actionList[i]];
		if (a.kind == kind && (a.state > bestState || (a.state == bestState && a.ms < bestMs))) {
			best = int(i);
			bestState = a.state;
			bestMs = a.ms;
		}
	}
	if (best < 0) {
		return -1;
	}
	const uint8_t a = w.powerUps.actionList[size_t(best)];
	w.powerUps.actionList.erase(w.powerUps.actionList.begin() + best);
	release(w, w.powerUps.actions[a]);
	return a;
}

// --- Green ---

void startTurbo(World& w, Action& a, int32_t level)
{
	if (!takeSlots(w, a, 3)) {
		a.state = Action::Done;
		return;
	}
	const size_t r = size_t(a.owner);
	if (w.racers.drivers[r].flags & DriverTurbo) {
		w.cancelTurbo(r);
		w.turboOff(r);
		w.resume(r);
	}
	a.ms = 400;
	a.smoked = false;
	w.tilt(r, -0.0025f, level == 0 ? 125 : 250);
	w.turboOn(r, level);
	if (level == 2) {
		w.halt(r);
	}
	w.playSound(TurboSounds + level, ownerOf(w, a), &w.racers.visible[r].position);
	w.playSound(WhooshSound, ownerOf(w, a), &w.racers.visible[r].position);
	a.state = TurboIgnite;
}

void turboFrame(World& w, Action& a, int32_t ms)
{
	auto advance = [&] {
		if (a.state == TurboIgnite) {
			static constexpr uint32_t boosts[] = {1000, 1500, 5000};
			a.state = TurboBoosting;
			a.ms = boosts[std::min<uint8_t>(a.level, 2)];
			if (a.owner >= 0) {
				w.voice(size_t(a.owner), RaceEvent::Positive);
			}
		}
		else if (a.state == TurboBoosting) {
			a.state = TurboFading;
			a.ms = 700;
		}
		else {
			a.state = Action::Done;
			if (a.owner >= 0) {
				// Its end: level 0 and 1 softly, 2 its own.
				w.playSound(a.level == 2 ? 75 : 40, ownerOf(w, a), &w.racers.visible[size_t(a.owner)].position);
				w.raceView.raceEvents.back().volume = a.level == 0 ? 0.6f : a.level == 1 ? 0.8f : 1.0f;
			}
		}
	};
	if (timeUp(a, ms)) {
		advance();
	}
	// A level 2 turbo's smoke, as it nears its end.
	if (a.level == 2 && a.state == TurboBoosting && a.ms < 800 && !a.smoked && a.owner >= 0) {
		a.smoked = true;
		w.spawn("trbsmke", 0, ownerOf(w, a), w.racers.visible[size_t(a.owner)].position);
	}
	// A hovering turbo that has stopped for good ends early.
	if (a.owner >= 0 && (w.racers.race[size_t(a.owner)].flags & RacerHalted) && a.state == TurboBoosting && a.ms < 4500) {
		const float speed = w.racers.karts[size_t(a.owner)].speed;
		if (speed > -0.04f && speed < 0.04f) {
			advance();
		}
	}
}

void startWarp(World& w, Action& a, const Aim* aim)
{
	const size_t r = size_t(a.owner);
	if (w.racers.race[r].flags & (RacerGhost | RacerWarping)) {
		a.state = Action::Done;
		return;
	}
	a.hasTarget = aim != nullptr;
	if (aim) {
		a.point = aim->position;
		a.direction = aim->direction;
	}
	a.ms = aim ? 0 : 500;
	a.state = WarpStarting;
	w.cancelMagnetHold(r);
	a.demo = w.racers.race[r].ai;
	w.racers.race[r].flags |= RacerWarping;
}

// Along the checkpoints' gates from x by d ft, heading for checkpoint c.
Vec3 alongPath(const World& w, Vec3 x, float d, int32_t c)
{
	while (d > 0 && c >= 0) {
		const Vec3 to = w.track.checkpoints[size_t(c)].position;
		const Vec3 delta = x - to;
		const float r = float(std::sqrt(double(dotZYX(delta, delta))));
		if (r < d) {
			d -= r;
			x = to;
		}
		else {
			x = {x.x + ((to.x - x.x) / r) * d, x.y + ((to.y - x.y) / r) * d, x.z + ((to.z - x.z) / r) * d};
			d = 0;
		}
		const uint8_t next = w.track.checkpoints[size_t(c)].next[0];
		c = next == 255 ? -1 : next;
	}
	return x;
}

void warpFrame(World& w, Action& a, int32_t ms)
{
	const size_t r = size_t(a.owner);
	if (a.state == WarpActive) {
		bool moved = false;
		Vec3 x;
		if (a.hasTarget) {
			const float f = float(int32_t(a.ms)) * 0.00066666666f;
			const Vec3 t = a.point, s = a.centre;
			x = {t.x - (t.x - s.x) * f, t.y - (t.y - s.y) * f, t.x - (t.z - s.z) * f}; // QUIRK: z from the target's x
			moved = true;
		}
		else if (a.followsPath) {
			x = alongPath(w, w.racers.visible[r].position, 0.6f * float(ms), nextCheckpoint(w, r));
			moved = true;
		}
		if (moved) {
			w.racers.visible[r].position = x;
			w.racers.karts[r].position = x;
		}
	}
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == WarpStarting) {
		a.state = WarpActive;
		a.ms = 1500;
		w.racers.race[r].flags &= ~RacerWarping;
		w.enterGhost(r);
		w.racers.powers[r].carScale = 1;
		w.endPush(r, 0);
		w.endPush(r, 1);
		a.centre = w.racers.visible[r].position; // where it starts from
		a.followsPath = !a.demo;
		if (!w.racers.race[r].ai) {
			w.playSound(WarpOutSound, ownerOf(w, a), nullptr);
		}
		w.playSound(WhooshSound, ownerOf(w, a), &a.centre, 200, 600);
		return;
	}
	// Arrive: on the ground, facing the course (or the target's way), launched forward.
	Vec3 at = a.hasTarget ? a.point : w.racers.visible[r].position;
	SegmentHit hit;
	if (w.track.query(w.track.trackVolume, at + Vec3{0, 0, 5}, at - Vec3{0, 0, 50}, hit)) {
		at = hit.point;
	}
	at.z += 5;
	Kart& k = w.racers.karts[r];
	w.racers.visible[r].position = k.position = at;
	if (!a.demo) {
		Vec3 dir{1, 0, 0};
		const int32_t c = nextCheckpoint(w, r);
		if (a.hasTarget) {
			dir = a.direction;
		}
		else if (c >= 0 && w.racers.race[r].checkpoint >= 0) {
			dir = -w.track.checkpoints[size_t(c)].normal;
		}
		w.racers.visible[r].rotation = k.rotation = basisFromForwardUp(dir, {0, 0, 1});
		k.velocity = {};
		if (w.racers.playback[r].active) {
			routeImpulse(w.racers.playback[r], k, dir, 700);
		}
		else {
			k.velocity += dir * (w.racers.params[r].invMass * 700);
			snapHeading(k);
		}
	}
	a.state = Action::Done;
	w.leaveGhost(r);
	if (!w.racers.race[r].ai) {
		w.playSound(WarpInSound, ownerOf(w, a), nullptr);
	}
}

// --- Blue ---

void startShield(World& w, Action& a, int32_t level)
{
	if (!takeSlots(w, a, 2)) {
		a.state = Action::Done;
		return;
	}
	const size_t r = size_t(a.owner);
	if (w.racers.race[r].flags & RacerShielded) {
		w.cancelShield(r);
	}
	static constexpr uint32_t lasts[] = {4000, 6000, 8000, 10000};
	a.ms = lasts[std::clamp(level, 0, 3)];
	a.state = ShieldActive;
	w.shieldOn(r, level);
	if (w.racers.race[r].flags & RacerCursed) {
		w.curseOff(r);
	}
}

void shieldFrame(World& w, Action& a, int32_t ms)
{
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == ShieldActive) {
		a.state = ShieldFading;
		a.ms = 1000;
	}
	else {
		a.state = Action::Done;
		w.racers.race[size_t(a.owner)].flags &= ~RacerShielded;
		const Vec3 at = w.racers.visible[size_t(a.owner)].position + Vec3{0, 0, 5};
		w.playSound(ShieldEndSound, ownerOf(w, a), &at);
	}
}

// --- Yellow ---

void oilFrame(World& w, Action& a, int32_t ms)
{
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == TrapArmed) {
		a.state = TrapActive;
		a.ms = 10000;
		layTrap(w, a, dropPoint(w, size_t(a.owner)), 3);
		w.tilt(size_t(a.owner), 0.0015f, 150);
		w.spawn("oilbub", 0, ownerOf(w, a), a.point);
		w.playSound(OilSplashSound, ownerOf(w, a), &a.point);
		RaceEvent& loop = w.raise(RaceEvent::Sound, ownerOf(w, a));
		loop.sound = OilLoopSound, loop.looping = true, loop.place = a.point;
	}
	else {
		a.state = Action::Done;
	}
}

// The stick tumbles about the world's y axis as it flies (12 rad/s, whichever way it was thrown); its sphere is its
// model's, so it swings with it.
void stickMoved(World& w, Action& a, int32_t ms)
{
	const Projectile& p = a.shot;
	a.phase += 0.012f * float(ms);
	const int32_t angle = int32_t(a.phase * 162.974655f); // (by the angle tables)
	const float c = cosEntry(angle), s = cosEntry(angle - 256);
	const Basis rows = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
	if (a.member >= 0) {
		w.triggers.members[size_t(a.member)].centre = toWorld(rows, w.track.stick.centre) + p.at;
	}
}

void dynamiteFrame(World& w, Action& a, int32_t ms)
{
	const int8_t owner = a.owner;
	Projectile& p = a.shot;
	if (p.state != Projectile::Idle) {
		flight(w, p, ms);
		stickMoved(w, a, ms);
		if (p.state != Projectile::Flying) {
			w.explode(true, p.at, true, owner);
			w.playSound(DynamiteBlastSound, ownerOf(w, a), &p.at, 200, 600);
			w.raise(RaceEvent::Finish, ownerOf(w, a)); // (the spark)
			p.state = Projectile::Idle;
			w.killMember(a.member);
			a.state = DynamiteSecond;
			a.ms = 500;
		}
	}
	if (!timeUp(a, ms)) {
		return;
	}
	auto blastNearby = [&] {
		const int32_t x = w.draw(Draw::Dynamite), y = w.draw(Draw::Dynamite);
		w.explode(true, p.at + Vec3{float(x % 13 - 6), float(y % 13 - 6), 0}, true, owner);
	};
	switch (a.state) {
	case DynamiteArmed: {
		a.state = DynamiteThrown;
		a.ms = 5000;
		// The stick, thrown back at a racer behind (leading it only if that point is behind), else back along the road.
		const size_t r = size_t(owner);
		const Pose& v = w.racers.visible[r];
		launch(p, v.position + Vec3{0, 0, 5}, -32.1759987f, 3000, owner);
		if (a.held >= 0) {
			aimAtRacer(w, p, r, size_t(a.held), 40, true);
		}
		else {
			aimAtPoint(w, p, owner, {v.position.x + v.rotation.f.x * -90, v.position.y + v.rotation.f.y * -90, v.position.z + v.rotation.f.z * -90 + 5}, 40);
		}
		w.killMember(a.member);
		a.member = w.addMember({p.at, w.track.stick.radius, Member::Projectile, false, false, uint16_t(&a - w.powerUps.actions.data())});
		a.phase = 0;
		stickMoved(w, a, ms);
		w.spawn("dynsprk", 0, ownerOf(w, a), p.at);
		break;
	}
	case DynamiteThrown:
		a.state = Action::Done;
		w.explode(true, p.at, true, owner);
		w.playSound(DynamiteBlastSound, ownerOf(w, a), &p.at, 200, 600);
		break;
	case DynamiteSecond:
		a.state = DynamiteThird;
		a.ms = 500;
		blastNearby();
		break;
	case DynamiteThird:
		a.state = Action::Done;
		blastNearby();
		break;
	}
}

void magnetFrame(World& w, Action& a, int32_t ms)
{
	if (a.state == TrapArmed && a.ms == 20000) {
		// Deployed over the ground under the car, facing the course.
		const size_t r = size_t(a.owner);
		const Vec3 at = dropPoint(w, r);
		layTrap(w, a, at, 10);
		a.point = at + Vec3{0, 0, 30};
		const int16_t c = w.racers.race[r].checkpoint;
		a.direction = c >= 0 ? w.track.checkpoints[size_t(c)].normal : Vec3{1, 0, 0};
		w.tilt(r, 0.0015f, 150);
		w.playSound(MagnetDropSound, ownerOf(w, a), &a.point);
		RaceEvent& loop = w.raise(RaceEvent::Sound, ownerOf(w, a));
		loop.sound = MagnetLoopSound, loop.looping = true, loop.place = a.point;
	}
	if (timeUp(a, ms)) {
		a.state = a.state == TrapFading ? uint8_t(Action::Done) : uint8_t(TrapFading);
		a.ms = a.state == TrapFading ? 1000 : 0;
		if (a.state == TrapFading) {
			w.playSound(MagnetFadeSound, ownerOf(w, a), &a.point);
		}
	}
	const Vec3 low = a.point - Vec3{0, 0, 30};
	auto pull = [&](size_t r) {
		const Vec3 c = w.racers.visible[r].position;
		const Vec3 dir = normalize(low - c);
		const float m = float(ms);
		if (w.racers.playback[r].active) {
			routeImpulse(w.racers.playback[r], w.racers.karts[r], dir, m);
		}
		else {
			w.racers.karts[r].velocity += dir * (w.racers.params[r].invMass * m);
		}
		return c;
	};
	if (a.held >= 0) {
		const size_t h = size_t(a.held);
		Kart& k = w.racers.karts[h];
		if (k.speed <= 0.002f) {
			a.stopped = true;
			if (!(w.racers.race[h].flags & RacerHalted)) {
				a.ms = std::min<uint32_t>(a.ms, 1000); // (halted however long the hold has left)
				w.halt(h);
			}
		}
		if (!w.racers.playback[h].active && a.stopped && !a.lifted) {
			const Vec3 c = pull(h);
			if (k.speed <= 0.002f && lengthSquared(c - low) <= 100) {
				a.lifted = true;
			}
		}
		if (w.racers.drivers[h].flags & DriverTurbo) {
			w.cancelTurbo(h);
			w.turboOff(h);
		}
	}
	else if (a.pulled >= 0) {
		pull(size_t(a.pulled));
		a.pulled = -1;
	}
}

void curseFrame(World& w, Action& a, int32_t ms)
{
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == TrapArmed) {
		a.state = TrapActive;
		a.ms = 15000;
		if (a.owner >= 0) {
			layTrap(w, a, dropPoint(w, size_t(a.owner)), 10);
		}
		RaceEvent& loop = w.raise(RaceEvent::Sound, ownerOf(w, a));
		loop.sound = CurseLoopSound, loop.looping = true, loop.place = a.point + Vec3{0, 0, 13};
	}
	else if (a.state == TrapActive) {
		a.state = TrapFading;
		a.ms = 1000;
	}
	else {
		a.state = Action::Done;
	}
}

void startAction(World& w, Action& a, int32_t level, const Aim* aim)
{
	switch (a.kind) {
	case Action::Turbo: startTurbo(w, a, level); break;
	case Action::Warp: startWarp(w, a, aim); break;
	case Action::Shield: startShield(w, a, level); break;
	case Action::Oil:
		a.state = TrapArmed;
		a.ms = 0;
		break;
	case Action::Dynamite: {
		const Pose& v = w.racers.visible[size_t(a.owner)];
		a.held = nearestInCone(w, v.position, -v.rotation.f, 100, 160000, 0.89999998f);
		a.shot.state = Projectile::Idle;
		a.state = DynamiteArmed;
		a.ms = 0;
		break;
	}
	case Action::Magnet:
		if (!takeSlots(w, a, 3)) {
			a.state = Action::Done;
			break;
		}
		a.state = TrapArmed;
		a.ms = 20000;
		a.held = a.pulled = -1;
		a.stopped = a.lifted = false;
		break;
	case Action::Curse:
		if (!takeSlots(w, a, 3)) {
			a.state = Action::Done;
			break;
		}
		if (a.owner < 0 && aim) {
			layTrap(w, a, aim->position, 10);
		}
		a.state = TrapArmed;
		a.ms = 0;
		break;
	default: startRed(w, a, aim);
	}
}

} // namespace

void World::touchBrick(size_t index, size_t i)
{
	LR_ASSERT(index < powerUps.bricks.size() && i < racers.powers.size());
	Brick& b = powerUps.bricks[index];
	b.touched = true;
	Power& pw = racers.powers[i];
	if (b.state != Brick::Idle || (racers.race[i].flags & RacerGhost) || (b.wasTouched && pw.held != NoColour)) {
		return;
	}
	if (b.colour != 0) {
		// Taken: a held colour is swapped in (it comes back at once), else the brick waits to respawn.
		const uint8_t had = pw.held;
		if (session.phase >= Session::Racing) {
			playSound(had ? 72 : 49, {Owner::Brick, int16_t(index)}, &b.position, 30, 150);
		}
		pw.held = b.colour;
		pw.aiClockMs = 0;
		pw.aiIntervalMs = 300;
		b.nextColour = had ? had : b.assigned;
		b.next = had ? Brick::Growing : Brick::Wait;
		b.state = Brick::Shrinking;
		b.ms = 0;
		return;
	}
	if (pw.whiteCount >= 3) {
		return;
	}
	playSound(BrickSounds + pw.whiteCount, {Owner::Brick, int16_t(index)}, nullptr); // (every racer's, the AI's too)
	if (pw.whiteCount == 2) {
		voice(i, RaceEvent::Positive);
	}
	for (int16_t& slot : pw.whites) {
		if (slot < 0) {
			slot = int16_t(index);
			break;
		}
	}
	pw.whiteCount++;
	b.state = Brick::Shrinking;
	b.next = Brick::Wait;
	b.ms = 0;
	b.dropped = false;
	b.droppedMs = 0;
}

void World::sendHome(size_t index)
{
	Brick& b = powerUps.bricks[index];
	b.dropped = false;
	b.droppedMs = 0;
	if (b.state == Brick::Idle) {
		b.state = Brick::Shrinking; // (a dropped brick nobody took: it goes home once it has shrunk)
		b.goHome = true;
		b.ms = 0;
		return;
	}
	b.position = b.home;
	triggers.members[size_t(b.member)].centre = b.home;
	b.state = Brick::Growing;
	b.ms = 0;
}

void World::dropWhite(size_t i)
{
	Power& pw = racers.powers[i];
	for (int16_t& slot : pw.whites) {
		if (slot < 0) {
			continue;
		}
		Brick& b = powerUps.bricks[size_t(slot)];
		const Vec3 at = racers.visible[i].position;
		SegmentHit hit;
		b.position = {at.x, at.y, (track.query(track.trackVolume, at + Vec3{0, 0, 25}, at - Vec3{0, 0, 25}, hit) ? hit.point.z : at.z) + 5};
		triggers.members[size_t(b.member)].centre = b.position;
		b.state = Brick::Growing;
		b.ms = 0;
		b.dropped = true;
		slot = -1;
		pw.whiteCount--;
		return;
	}
}

void World::useHeld(size_t i)
{
	Power& pw = racers.powers[i];
	if (pw.held == NoColour) {
		const Vec3 at = racers.visible[i].position; // a taunt
		fireAt(999, 999, &at);
		return;
	}
	if (pw.held == Yellow || pw.held == Blue) {
		voice(i, RaceEvent::Positive);
	}
	make(int8_t(i), pw.held, pw.whiteCount);
	pw.held = NoColour;
	for (int16_t& slot : pw.whites) {
		if (slot >= 0) {
			sendHome(size_t(slot));
			slot = -1;
		}
	}
	pw.whiteCount = 0;
}

void World::consider(size_t i)
{
	Power& pw = racers.powers[i];
	if (powerUps.effectsUsed >= 20 || (pw.chargeColour && pw.held == pw.chargeColour && pw.whiteCount < pw.chargeTarget)) {
		pw.aiIntervalMs = 1000;
		return;
	}
	const uint8_t chance = pw.chances[pw.held - 1];
	if (pw.held == Green) {
		if ((draw(Draw::UseGreen) & 255) >= chance) {
			pw.aiIntervalMs = 1000;
			return;
		}
		if (((racers.race[i].flags & RacerFinished) && pw.whiteCount == 3) || (racers.karts[i].flags & KartSpinning)) {
			return;
		}
		useHeld(i);
		return;
	}
	if (pw.held == Red) {
		const Pose& v = racers.visible[i];
		if (nearestInCone(*this, v.position, v.rotation.f, RedConeMin, RedConeMax, RedConeCos) < 0) {
			return;
		}
	}
	if ((draw(Draw::Use) & 255) >= chance) {
		pw.aiIntervalMs = 1000;
		return;
	}
	useHeld(i);
}

int16_t World::make(int8_t racer, uint8_t colour, int32_t level, const Aim* aim)
{
	const int32_t free = EffectSlots - powerUps.effectsUsed;
	Action::Kind kind;
	switch (colour) {
	case Red: kind = level == 0 ? Action::Cannon : level == 1 ? Action::Hook : level == 2 ? Action::Bolt : Action::Missile; break;
	case Yellow:
		kind = level == 0 ? Action::Oil : level == 1 ? Action::Dynamite : level == 2 ? Action::Magnet : Action::Curse;
		if (level >= 2 && free < 3) {
			return -1;
		}
		break;
	case Blue:
		kind = Action::Shield;
		if (free < 2) {
			return -1;
		}
		break;
	case Green:
		kind = level >= 3 ? Action::Warp : Action::Turbo;
		if (level < 3 && free < 3) {
			return -1;
		}
		break;
	default: return -1;
	}
	int16_t first = -1;
	for (uint8_t m = 0; m < (kind == Action::Missile ? 3 : 1); m++) {
		const int16_t index = takeAction(*this, kind);
		if (index < 0) {
			break;
		}
		first = first < 0 ? index : first;
		powerUps.actionList.insert(powerUps.actionList.begin(), uint8_t(index));
		Action& a = powerUps.actions[size_t(index)];
		a = {};
		a.kind = kind;
		a.owner = racer;
		a.missile = m;
		a.emplacement = aim ? aim->emplacement : -1;
		startAction(*this, a, level, aim);
		a.level = uint8_t(level);
	}
	return first;
}

void World::cancelTurbo(size_t i)
{
	for (uint8_t index : powerUps.actionList) {
		Action& a = powerUps.actions[index];
		if (a.kind == Action::Turbo && a.owner == int8_t(i) && a.state > Action::Ready) {
			a.state = Action::Done;
			a.owner = -1;
		}
	}
}

void World::cancelShield(size_t i)
{
	for (uint8_t index : powerUps.actionList) {
		Action& a = powerUps.actions[index];
		if (a.kind == Action::Shield && a.owner == int8_t(i) && a.state > Action::Ready) {
			a.state = Action::Done;
		}
	}
}

void World::cancelMagnetHold(size_t i)
{
	for (uint8_t index : powerUps.actionList) {
		Action& a = powerUps.actions[index];
		if (a.kind == Action::Magnet && a.held == int8_t(i) && a.state > Action::Ready) {
			a.state = Action::Done;
		}
	}
}

void World::touchTrap(size_t index, size_t i)
{
	LR_ASSERT(index < powerUps.actions.size());
	Action& a = powerUps.actions[index];
	if (a.owner == int8_t(i) || a.state == Action::Done) {
		return;
	}
	if (racers.race[i].flags & RacerShielded) {
		RaceEvent& absorb = raise(RaceEvent::Absorb, {Owner::Racer, int16_t(i)});
		absorb.place = racers.visible[i].position;
		if (a.level < 3) {
			a.state = Action::Done; // used up on the shield (a curse stays)
		}
		return;
	}
	switch (a.kind) {
	case Action::Oil:
		if (!(racers.race[i].flags & RacerHalted)) {
			spin(i, 1, 0.007f, 1);
			markHit(i);
			playSound(TrapSpinSound, {Owner::Action, int16_t(index)}, &racers.visible[i].position, 200, 600);
			a.state = Action::Done;
			a.ms = 0;
		}
		break;
	case Action::Magnet: {
		if (a.state == TrapFading) {
			break;
		}
		if (a.state == TrapArmed) {
			a.state = TrapActive;
			a.ms = 4000;
		}
		if (lengthSquared((a.point - Vec3{0, 0, 30}) - racers.visible[i].position) <= 81) {
			if (a.held < 0) {
				a.held = int8_t(i);
				magnetHold(i, true);
				a.pulled = -1;
				playSound(MagnetCatchSound, {Owner::Action, int16_t(index)}, &a.point);
				voice(i, RaceEvent::Negative);
			}
		}
		else if (a.pulled < 0) {
			a.pulled = int8_t(i);
		}
		break;
	}
	case Action::Curse:
		if (!(racers.race[i].flags & RacerCursed)) {
			curseOn(i, 10000);
		}
		break;
	default: break;
	}
}

// A brick's frame: its clock, a dropped white brick's way home, growing back, shrinking to its next state.
void World::brickFrame(size_t index, int32_t ms)
{
	Brick& b = powerUps.bricks[index];
	b.ms += ms;
	b.wasTouched = b.touched;
	b.touched = false;
	if (b.colour == 0 && b.dropped) {
		b.droppedMs += ms;
		if (b.droppedMs > 10000) {
			sendHome(index);
		}
	}
	switch (b.state) {
	case Brick::Wait:
		if (b.colour != 0 && b.respawnMs <= b.ms) {
			b.ms = 0;
			b.state = Brick::Growing;
			if (session.phase >= Session::Racing) {
				playSound(BrickBackSound, {Owner::Brick, int16_t(index)}, &b.position, 30, 150);
			}
		}
		break;
	case Brick::Growing:
		if (b.ms > Grows) {
			b.state = Brick::Idle;
			b.ms = 0;
		}
		break;
	case Brick::Shrinking:
		if (b.ms > Shrinks) {
			b.ms = 0;
			if (b.colour != 0) {
				b.state = b.next;
				b.colour = b.nextColour;
				if (b.state == Brick::Growing && session.phase >= Session::Racing) {
					playSound(BrickBackSound, {Owner::Brick, int16_t(index)}, &b.position, 30, 150);
				}
			}
			else if (b.goHome) {
				b.goHome = false;
				b.position = b.home;
				triggers.members[size_t(b.member)].centre = b.home;
				b.state = Brick::Growing;
			}
			else {
				b.state = Brick::Wait;
			}
		}
		break;
	case Brick::Idle: break;
	}
}

// Each action's frame by its kind, newest first.
void World::actionsFrame(int32_t ms)
{
	for (size_t n = 0; n < powerUps.actionList.size(); n++) {
		Action& a = powerUps.actions[powerUps.actionList[n]];
		switch (a.kind) {
		case Action::Turbo: turboFrame(*this, a, ms); break;
		case Action::Warp:
			if (a.state != Action::Done) {
				warpFrame(*this, a, ms);
			}
			break;
		case Action::Shield:
			if (a.state != Action::Done) {
				shieldFrame(*this, a, ms);
			}
			break;
		case Action::Oil:
			if (a.state != Action::Done) {
				oilFrame(*this, a, ms);
			}
			break;
		case Action::Dynamite:
			if (a.state != Action::Done) {
				dynamiteFrame(*this, a, ms);
			}
			break;
		case Action::Magnet:
			if (a.state != Action::Done) {
				magnetFrame(*this, a, ms);
			}
			break;
		case Action::Curse:
			if (a.state != Action::Done) {
				curseFrame(*this, a, ms);
			}
			break;
		default: redFrame(*this, a, ms);
		}
	}
}

// The done actions and the idle explosions back to their pools' free lists.
void World::releaseDone()
{
	for (size_t n = 0; n < powerUps.actionList.size();) {
		Action& a = powerUps.actions[powerUps.actionList[n]];
		if (a.state != Action::Done) {
			n++;
			continue;
		}
		const uint8_t index = powerUps.actionList[n];
		powerUps.actionList.erase(powerUps.actionList.begin() + long(n));
		release(*this, a);
		powerUps.freeActions[a.kind].insert(powerUps.freeActions[a.kind].begin(), index);
	}
	for (int pool = 0; pool < 2; pool++) {
		std::vector<uint8_t>& list = powerUps.explosionList[pool];
		for (size_t n = 0; n < list.size();) {
			if (powerUps.explosions[list[n]].state != Explosion::Idle) {
				n++;
				continue;
			}
			powerUps.freeExplosions[pool].insert(powerUps.freeExplosions[pool].begin(), list[n]);
			list.erase(list.begin() + long(n));
		}
	}
}

void World::powerFrame(int32_t ms)
{
	raceView.stage = RaceEvent::Powers;
	// The bricks: colour bricks then white bricks, in file order.
	for (size_t index = 0; index < powerUps.bricks.size(); index++) {
		brickFrame(index, ms);
	}

	// The actions, newest first, then the explosions; then the done are released back to their pools.
	actionsFrame(ms);
	hooksMoved(*this);
	for (int pool = 0; pool < 2; pool++) {
		for (uint8_t e : powerUps.explosionList[pool]) {
			explosionFrame(*this, powerUps.explosions[e], ms);
		}
	}
	releaseDone();
}

// The race's power-ups at load: every racer's chances (from its driver's habits, 255 for a built car), the pools,
// the bricks waiting to grow (white ones growing) in the trigger list.
void setupPowers(World& w, const std::vector<std::array<int32_t, 8>>& habits)
{
	w.racers.powers.assign(w.racers.race.size(), {});
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		Power& pw = w.racers.powers[i];
		const std::array<int32_t, 8>& h = habits[i];
		// The stats in the file's order: red, yellow, green, blue.
		static constexpr uint8_t colours[4] = {Red, Yellow, Green, Blue};
		for (int s = 0; s < 4; s++) {
			const float c = std::min(1.0f, ((float(h[size_t(s)]) * 0.888888896f) * 0.0099999998f) + 0.2f);
			pw.chances[colours[s] - 1] = uint8_t(int32_t(c * 255));
		}
		pw.chargeColour = h[6] >= 1 && h[6] <= 4 ? uint8_t(h[6]) : 0;
		pw.chargeTarget = uint8_t(h[7]);
	}
	w.powerUps.actions.clear();
	for (int k = 0; k < Action::Kinds; k++) {
		w.powerUps.freeActions[size_t(k)].clear();
		for (int32_t n = 0; n < PoolSizes[k]; n++) {
			w.powerUps.freeActions[size_t(k)].push_back(uint8_t(w.powerUps.actions.size()));
			w.powerUps.actions.emplace_back().kind = Action::Kind(k);
		}
	}
	for (int pool = 0; pool < 2; pool++) {
		w.powerUps.freeExplosions[size_t(pool)].clear();
		for (int n = 0; n < (pool ? 2 : 6); n++) {
			const uint8_t e = uint8_t(pool ? 6 + n : n);
			w.powerUps.freeExplosions[size_t(pool)].push_back(e);
			w.powerUps.explosions[e] = {};
			w.powerUps.explosions[e].blast = pool ? 10.0f : 5.0f;
		}
	}
	w.powerUps.targetsOn.assign(w.track.targets.size(), 1);
	for (size_t index = 0; index < w.track.bricks.size(); index++) {
		const BrickPlace& place = w.track.bricks[index];
		Brick& b = w.powerUps.bricks.emplace_back();
		b.position = b.home = place.position;
		b.colour = b.assigned = place.colour;
		b.nextColour = place.colour ? Green : 0; // (a brick's next colour is green until it is taken)
		b.respawnMs = place.respawnMs;
		b.state = place.colour ? Brick::Wait : Brick::Growing;
		b.ms = 3000;
		b.member = w.addMember({b.position, 0, Member::Brick, false, false, uint16_t(index)});
	}
}

} // namespace lr
