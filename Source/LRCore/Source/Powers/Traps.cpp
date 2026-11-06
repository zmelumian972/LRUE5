// The yellow actions: the oil slick, the dynamite, the magnet and the curse, and what their traps do to the racers
// that meet them.

#include "Powers/ActionParts.h"
#include "Show/SoundIds.h"

#include <algorithm>

namespace lr {

namespace {

constexpr float SpinRate = 0.007f; // a racer the oil spins
constexpr float LayTilt = 0.0015f; // a racer laying a trap tilts, for
constexpr int32_t LayTiltMs = 150;
constexpr int32_t OilMs = 10000;
constexpr float OilRadius = 3;
// The dynamite: thrown back at 40 ft/s (90 ft behind with nothing to aim at), its fuse, the two blasts after a hit.
constexpr float DynamiteSpeed = 40, DynamiteBack = 90, DynamiteCone = 0.9f; // (aimed at the racers behind in its cone)
constexpr int32_t DynamiteLifeMs = 3000, DynamiteFuseMs = 5000, DynamiteBlastMs = 500;
constexpr float StickTumble = 0.012f; // rad per ms
// The magnet: armed until laid, hung 30 ft over the ground, a 10 ft trap; it catches a racer within 9 ft, which stops
// below this speed; a caught racer is held for at least a second.
constexpr int32_t MagnetArmedMs = 20000, MagnetCatchMs = 4000, MagnetHoldMs = 1000, TrapFadeMs = 1000;
constexpr float MagnetHeight = 30, MagnetCatch2 = 81, MagnetStopped = 0.002f, MagnetLifted2 = 100;
constexpr float TrapRadius = 10; // a magnet's and a curse's
constexpr int32_t CurseMs = 15000; // the curse's trap
constexpr Vec3 SkullLift = {0, 0, 13};

// Where a racer lays a trap: on the ground under its car (1 above it), else at the car.
Vec3 dropPoint(const World& w, size_t racer)
{
	const Vec3 at = w.racers.visible[racer].position;
	SegmentHit hit;
	return w.track.query(w.track.trackVolume, at, at - DropProbe, hit) ? hit.point + Vec3{0, 0, 1} : at;
}

void layTrap(World& w, Action& a, Vec3 at, float radius)
{
	a.point = at;
	addMember(w, a, Member::Trap, at, radius);
}

// --- The oil slick: laid under the car, it spins the first racer to meet it (unless halted) ---

void startOil(World&, Action& a, int32_t, const Aim*)
{
	a.state = TrapArmed;
	a.ms = 0;
}

void oilTouch(World& w, Action& a, size_t i)
{
	if (!(w.racers.race[i].flags & RacerHalted)) {
		w.powerUps.spin(w, i, 1, SpinRate, 1);
		w.raceView.markHit(i);
		w.raceView.playSound(TrapSpinSound, ownerOf(w, a), &w.racers.visible[i].position, 200, 600);
		a.state = Action::Done;
		a.ms = 0;
	}
}

void oilFrame(World& w, Action& a, int32_t ms)
{
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == TrapArmed) {
		a.state = TrapActive;
		a.ms = OilMs;
		layTrap(w, a, dropPoint(w, size_t(a.owner)), OilRadius);
		w.powerUps.tilt(w, size_t(a.owner), LayTilt, LayTiltMs);
		w.raceView.spawn("oilbub", 0, ownerOf(w, a), a.point);
		w.raceView.playSound(OilSplashSound, ownerOf(w, a), &a.point);
		w.raceView.loopSound(OilLoopSound, ownerOf(w, a), a.point);
	}
	else {
		a.state = Action::Done;
	}
}

// --- The dynamite: thrown back at a racer behind, it blows up where it stops (or as its fuse runs out), then twice
// more nearby ---

void startDynamite(World& w, Action& a, int32_t, const Aim*)
{
	const Pose& v = w.racers.visible[a.owner];
	a.held = nearestInCone(w, v.position, -v.rotation.f, AimNear2, AimFar2, DynamiteCone);
	a.shot.state = Projectile::Idle;
	a.state = DynamiteArmed;
	a.ms = 0;
}

void releaseDynamite(World&, Action& a) { a.shot.state = Projectile::Idle; }

// The stick tumbles about the world's y axis as it flies (12 rad/s, whichever way it was thrown); its sphere is its
// model's, so it swings with it.
void stickMoved(World& w, Action& a, int32_t ms)
{
	const Projectile& p = a.shot;
	a.phase += StickTumble * float(ms);
	const int32_t angle = int32_t(a.phase * 162.97466f); // (by the angle tables)
	const float c = cosEntry(angle), s = cosEntry(angle - 256);
	const Basis rows = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
	keepMember(w, a, toWorld(rows, w.track.stick.centre) + p.at);
}

void dynamiteFrame(World& w, Action& a, int32_t ms)
{
	const int8_t owner = a.owner;
	Projectile& p = a.shot;
	if (p.state != Projectile::Idle) {
		flight(w, p, ms);
		stickMoved(w, a, ms);
		if (p.state != Projectile::Flying) {
			w.powerUps.explode(w, true, p.at, true, owner);
			w.raceView.playSound(BlastSound, ownerOf(w, a), &p.at, 200, 600);
			w.raceView.raise(RaceEvent::Finish, ownerOf(w, a)); // (the spark)
			p.state = Projectile::Idle;
			w.triggers.killMember(a.member);
			a.state = DynamiteSecond;
			a.ms = DynamiteBlastMs;
		}
	}
	if (!timeUp(a, ms)) {
		return;
	}
	auto blastNearby = [&] {
		const int32_t x = w.draw(Draw::Dynamite), y = w.draw(Draw::Dynamite);
		w.powerUps.explode(w, true, p.at + Vec3{float(x % 13 - 6), float(y % 13 - 6), 0}, true, owner);
	};
	switch (a.state) {
	case DynamiteArmed: {
		a.state = DynamiteThrown;
		a.ms = DynamiteFuseMs;
		// The stick, thrown back at a racer behind (leading it only if that point is behind), else back along the road.
		const size_t r = size_t(owner);
		const Pose& v = w.racers.visible[r];
		launch(p, v.position + ShotLift, ShotGravity, DynamiteLifeMs, owner);
		if (a.held >= 0) {
			aimAtRacer(w, p, r, size_t(a.held), DynamiteSpeed, true);
		}
		else {
			const Vec3 f = v.rotation.f;
			aimAtPoint(w, p, owner,
				{v.position.x + f.x * -DynamiteBack, v.position.y + f.y * -DynamiteBack, v.position.z + f.z * -DynamiteBack + 5},
				DynamiteSpeed);
		}
		addMember(w, a, Member::Projectile, p.at, w.track.stick.radius);
		a.phase = 0;
		stickMoved(w, a, ms);
		w.raceView.spawn("dynsprk", 0, ownerOf(w, a), p.at);
		break;
	}
	case DynamiteThrown:
		a.state = Action::Done;
		w.powerUps.explode(w, true, p.at, true, owner);
		w.raceView.playSound(BlastSound, ownerOf(w, a), &p.at, 200, 600);
		break;
	case DynamiteSecond:
		a.state = DynamiteThird;
		a.ms = DynamiteBlastMs;
		blastNearby();
		break;
	case DynamiteThird:
		a.state = Action::Done;
		blastNearby();
		break;
	}
}

// --- The magnet: hung over the ground, it catches a racer within reach (pulling the others toward it) and holds it
// still ---

void startMagnet(World& w, Action& a, int32_t, const Aim*)
{
	if (!takeSlots(w, a, 3)) {
		a.state = Action::Done;
		return;
	}
	a.state = TrapArmed;
	a.ms = MagnetArmedMs;
	a.held = a.pulled = -1;
	a.stopped = a.lifted = false;
}

void releaseMagnet(World& w, Action& a)
{
	if (a.held >= 0) {
		w.powerUps.magnetHold(w, size_t(a.held), false);
	}
	a.held = -1;
}

void magnetTouch(World& w, Action& a, size_t i)
{
	if (a.state == TrapFading) {
		return;
	}
	if (a.state == TrapArmed) {
		a.state = TrapActive;
		a.ms = MagnetCatchMs;
	}
	if (lengthSquared((a.point - Vec3{0, 0, MagnetHeight}) - w.racers.visible[i].position) <= MagnetCatch2) {
		if (a.held < 0) {
			a.held = int8_t(i);
			w.powerUps.magnetHold(w, i, true);
			a.pulled = -1;
			w.raceView.playSound(MagnetCatchSound, ownerOf(w, a), &a.point);
			w.raceView.voice(w, i, RaceEvent::Negative);
		}
	}
	else if (a.pulled < 0) {
		a.pulled = int8_t(i);
	}
}

void magnetFrame(World& w, Action& a, int32_t ms)
{
	if (a.state == TrapArmed && a.ms == MagnetArmedMs) {
		// Deployed over the ground under the car, facing the course.
		const size_t r = size_t(a.owner);
		const Vec3 at = dropPoint(w, r);
		layTrap(w, a, at, TrapRadius);
		a.point = at + Vec3{0, 0, MagnetHeight};
		const int16_t c = w.racers.race[r].checkpoint;
		a.direction = c >= 0 ? w.track.checkpoints[c].normal : Vec3{1, 0, 0};
		w.powerUps.tilt(w, r, LayTilt, LayTiltMs);
		w.raceView.playSound(MagnetDropSound, ownerOf(w, a), &a.point);
		w.raceView.loopSound(MagnetLoopSound, ownerOf(w, a), a.point);
	}
	if (timeUp(a, ms)) {
		a.state = a.state == TrapFading ? uint8_t(Action::Done) : uint8_t(TrapFading);
		a.ms = a.state == TrapFading ? TrapFadeMs : 0;
		if (a.state == TrapFading) {
			w.raceView.playSound(MagnetFadeSound, ownerOf(w, a), &a.point);
		}
	}
	const Vec3 low = a.point - Vec3{0, 0, MagnetHeight};
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
		if (k.speed <= MagnetStopped) {
			a.stopped = true;
			if (!(w.racers.race[h].flags & RacerHalted)) {
				a.ms = std::min<uint32_t>(a.ms, MagnetHoldMs); // (halted however long the hold has left)
				w.powerUps.halt(w, h);
			}
		}
		if (!w.racers.playback[h].active && a.stopped && !a.lifted) {
			const Vec3 c = pull(h);
			if (k.speed <= MagnetStopped && lengthSquared(c - low) <= MagnetLifted2) {
				a.lifted = true;
			}
		}
		if (w.racers.drivers[h].flags & DriverTurbo) {
			w.powerUps.cancelTurbo(h);
			w.powerUps.turboOff(w, h);
		}
	}
	else if (a.pulled >= 0) {
		pull(size_t(a.pulled));
		a.pulled = -1;
	}
}

// --- The curse: a skull over the ground (or where a hazard drops it) that curses the racers it meets (the manager's
// touchTrap) ---

void startCurse(World& w, Action& a, int32_t, const Aim* aim)
{
	if (!takeSlots(w, a, 3)) {
		a.state = Action::Done;
		return;
	}
	if (a.owner < 0 && aim) {
		layTrap(w, a, aim->position, TrapRadius);
	}
	a.state = TrapArmed;
	a.ms = 0;
}

void curseFrame(World& w, Action& a, int32_t ms)
{
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == TrapArmed) {
		a.state = TrapActive;
		a.ms = CurseMs;
		if (a.owner >= 0) {
			layTrap(w, a, dropPoint(w, size_t(a.owner)), TrapRadius);
		}
		w.raceView.loopSound(CurseLoopSound, ownerOf(w, a), a.point + SkullLift);
	}
	else if (a.state == TrapActive) {
		a.state = TrapFading;
		a.ms = TrapFadeMs;
	}
	else {
		a.state = Action::Done;
	}
}

} // namespace

const ActionKind OilAction{.start = startOil, .frame = oilFrame, .touch = oilTouch, .loop = OilLoopSound};
const ActionKind DynamiteAction{.start = startDynamite, .frame = dynamiteFrame, .release = releaseDynamite};
const ActionKind MagnetAction{
	.start = startMagnet, .frame = magnetFrame, .release = releaseMagnet, .touch = magnetTouch, .loop = MagnetLoopSound};
const ActionKind CurseAction{.start = startCurse, .frame = curseFrame, .loop = CurseLoopSound};

} // namespace lr
