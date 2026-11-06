// The red weapons: the cannonball, the grappling hook and lightning (the homing missiles are Missiles.cpp's, the
// projectiles' aiming and flight and the explosions Projectiles.cpp's).
//
// A weapon fires in its first frame (aimed from the shooter's visible pose then), flies once per frame and is stopped
// by the track (whose surfaces may fire events and let it pass) or by a racer its touch entry met in a step. Its
// state timer carries: the time left past a state's end runs the next state in the same frame.

#include "Powers/ActionParts.h"
#include "Common/Assert.h"
#include "Show/SoundIds.h"

#include <cmath>

namespace lr {

namespace {

constexpr float CannonRadius = 3.535534f; // a 5 x 5 billboard
constexpr float ScarSlope = 0.7071f; // a burst on ground this flat leaves a scar
constexpr float MissileSpinRate = 0.007f; // a racer a missile hits spins
// The aiming cones: 250 ft for the hook's farthest pick; their cosines.
constexpr float HookFar2 = 62500;
constexpr float TargetCone = 0.95f, CannonCone = 0.9f, HookNearCone = 0.93f, HookWideCone = 0.6f;

// A cannonball: a racer's (life, speed, its state's time from the shot and again from a hit) and an emplacement's.
constexpr int32_t CannonLifeMs = 3000, CannonFlightMs = 3000;
constexpr float CannonSpeed = 180;
constexpr float EmplacementGravity = -96.528f, EmplacementSpeed = 30;
constexpr int32_t EmplacementLifeMs = 5000;

// The grappling hook: its flight, the rope's reel, the hold and its pull (the racer let go nearer than 12 ft).
constexpr float HookGravity = -90.176f, HookSpeed = 320;
constexpr int32_t HookLifeMs = 3000, HookFlightMs = 3000, HookReelMs = 500, HookHoldMs = 4000;
constexpr float ReelSpeed = 250; // ft per ms
constexpr float ReeledIn2 = 36, LetGoNear2 = 144;
constexpr float HookPull = 180;
constexpr Vec3 RopeLift = {0, 0, 3}, HookOnRacer = {0, 0, 2};

// Lightning: its ramp and fade, its strike, the shock's hold, a span's full length, its cone.
constexpr int32_t BoltRampMs = 500, BoltSustainMs = 7000, BoltShockMs = 1000;
constexpr float BoltRampRate = 0.002f; // 1 / its ramp's ms
constexpr float BoltSpan = 50; // each of its four spans, grown
constexpr Vec3 BoltLift = {0, 0, 3};
constexpr float BoltNear2 = 9, BoltFar2 = 40000, BoltCone = 0.5f;

// A hit's reactions (the show's voices): the owner's glad (if any), the struck racer's not.
void reactions(World& w, const Action& a, size_t struck)
{
	if (a.owner >= 0) {
		w.raceView.voice(w, size_t(a.owner), RaceEvent::Positive);
	}
	w.raceView.voice(w, struck, RaceEvent::Negative);
}

// A shield takes a hit: its racer is glad, the show plays the block.
void absorbed(World& w, size_t r)
{
	w.raceView.voice(w, r, RaceEvent::Positive);
	w.raceView.raise(RaceEvent::Absorb, {Owner::Racer, int16_t(r)}).place = w.racers.visible[r].position;
}
uint8_t level(const World& w, size_t r) { return w.racers.powers[r].shieldLevel; }

// A racer's cannonball or hook, launched from over its car at what it picked as it was made: a racer, a target
// point, else straight ahead.
void fireAtPick(World& w, Action& a, float gravity, int32_t lifeMs, float speed)
{
	const size_t r = size_t(a.owner);
	const Pose& v = w.racers.visible[r];
	Projectile& p = a.shot;
	launch(p, v.position + ShotLift, gravity, lifeMs, a.owner);
	if (a.held >= 0) {
		aimAtRacer(w, p, r, size_t(a.held), speed, false);
	}
	else if (a.aimPoint >= 0) {
		aimAtPoint(w, p, a.owner, w.track.targets[a.aimPoint].position, speed);
	}
	else {
		aimAtPoint(w, p, a.owner, v.position + v.rotation.f * AimAhead + ShotLift, speed);
	}
}

// --- Cannonball ---

void fireCannon(World& w, Action& a)
{
	Projectile& p = a.shot;
	if (a.emplacement >= 0) {
		const Hazard& h = w.scenery.hazards[a.emplacement];
		launch(p, h.muzzle, EmplacementGravity, EmplacementLifeMs, -1);
		aimAtPoint(w, p, -1, h.point, EmplacementSpeed);
	}
	else {
		fireAtPick(w, a, ShotGravity, CannonLifeMs, CannonSpeed);
	}
	addMember(w, a, Member::Projectile, p.at, CannonRadius);
	w.raceView.spawn("cannsmk", 0, ownerOf(w, a), p.at);
	w.raceView.playSound(CannonFireSound, ownerOf(w, a), &p.start);
}

void cannonFrame(World& w, Action& a, int32_t ms)
{
	if (carriedTimeUp(a, ms)) {
		if (a.state == ShotArmed) {
			a.state = ShotFlying;
			a.ms = CannonFlightMs;
			fireCannon(w, a);
		}
		else {
			a.state = Action::Done;
			w.triggers.killMember(a.member);
			return;
		}
	}
	if (a.state != ShotFlying) {
		return;
	}
	Projectile& p = a.shot;
	if (p.state == Projectile::Flying) {
		flight(w, p, ms);
	}
	keepMember(w, a, p.at);
	if (p.state != Projectile::Flying) {
		stopped(w, a, false);
	}
}

// --- Grappling hook ---

// QUIRK: every hook moves the one grapple model, so two hooks out at once fly from (and touch with) one place.
Vec3 hookCentre(const World& w) { return toWorld(w.powerUps.grapple.rotation, w.track.hook.centre) + w.powerUps.grapple.position; }

void letGo(World& w, Action& a, Vec3 at)
{
	if (a.held >= 0 || a.shot.state == Projectile::HitRacer) {
		if (a.held >= 0) {
			w.powerUps.endPush(w, size_t(a.held), 0);
			w.powerUps.endPush(w, size_t(a.owner), 1);
			a.held = -1;
		}
		w.raceView.playSound(HookReleaseSound, ownerOf(w, a), &at); // (a racer's let go, or a shield threw it off)
	}
	else {
		w.raceView.playSound(HookSnapSound, ownerOf(w, a), &at); // (missed: the snap, then the reel)
		w.raceView.playSound(HookReelSound, ownerOf(w, a), &at);
	}
	w.raceView.raise(RaceEvent::Release, ownerOf(w, a)); // (its smoke, if still puffing)
	a.state = HookReeling;
	a.ms = HookReelMs;
	a.letGo = at;
	a.shot.state = Projectile::Flying; // the rope reels in (its frame says when it is in)
	w.triggers.killMember(a.member);
}

// The rope's frame: reeling in, holding a racer, or the hook's flight.
Projectile::State ropeFrame(World& w, Action& a, int32_t ms)
{
	Projectile& p = a.shot;
	const Vec3 from = w.racers.visible[a.owner].position + RopeLift;
	if (a.state == HookReeling) {
		// Reeled in at 250 ft per ms (QUIRK: per ms): in at once.
		const Vec3 left = a.letGo - from;
		if (dotZYX(left, left) < ReeledIn2 || ReelSpeed * float(ms) >= lengthZYX(left)) {
			p.state = Projectile::Expired;
		}
		return p.state;
	}
	if (p.state == Projectile::HitRacer) {
		// The hook is on the racer (chased at 300 ft per ms: there at once), the rope taut: the track can cut it.
		p.at = w.racers.visible[p.struck].position + HookOnRacer;
		Vec3 hit, normal;
		if (w.powerUps.shotHits(w, from, p.at, hit, normal)) {
			p.state = Projectile::HitWorld;
			p.hit = hit;
			p.normal = normal;
		}
		return p.state;
	}
	if (p.state == Projectile::Flying) {
		flight(w, p, ms);
	}
	return p.state;
}

void fireHook(World& w, Action& a)
{
	const Pose& v = w.racers.visible[a.owner];
	const Projectile& p = a.shot;
	fireAtPick(w, a, HookGravity, HookLifeMs, HookSpeed);
	a.held = -1; // (now the racer the rope holds)
	w.powerUps.grapple = {p.at, basisFromForwardUp(p.target - v.position, {0, 0, 1})};
	addMember(w, a, Member::Projectile, hookCentre(w), w.track.hook.radius);
	w.raceView.spawn("cannsmk", 0, ownerOf(w, a), p.at);
	w.raceView.playSound(HookFireSound, ownerOf(w, a), &v.position);
}

// The hook pulling its racer toward the shooter, until a shield throws it off or the racer is close or behind.
void hookPull(World& w, Action& a)
{
	const size_t owner = size_t(a.owner), held = size_t(a.held);
	LR_ASSERT(a.owner >= 0 && owner < w.racers.visible.size() && a.held >= 0 && held < w.racers.visible.size());
	const Vec3 A = w.racers.visible[owner].position, B = w.racers.visible[held].position;
	if (shielded(w, held)) {
		letGo(w, a, B);
		return;
	}
	const Vec3 d = B - A;
	float q = d.y * d.y;
	q += d.z * d.z;
	q += d.x * d.x;
	const float k = dotZYX(d, w.racers.visible[owner].rotation.f);
	if (q < LetGoNear2 || k < 0) {
		letGo(w, a, B);
		return;
	}
	const Vec3 pull = normalize(d) * HookPull;
	w.powerUps.push(w, owner, 1, pull);
	w.powerUps.push(w, held, 0, -pull);
}

void hookFrame(World& w, Action& a, int32_t ms)
{
	if (carriedTimeUp(a, ms)) {
		if (a.state == ShotArmed) {
			a.state = ShotFlying;
			a.ms = HookFlightMs;
			fireHook(w, a);
		}
		else if (a.state == ShotFlying || a.state == HookPulling) {
			letGo(w, a, w.powerUps.grapple.position);
		}
		else {
			a.state = Action::Done;
			return;
		}
	}
	if (a.state == ShotArmed) {
		return;
	}
	Projectile& p = a.shot;
	p.at = w.powerUps.grapple.position;
	const Projectile::State ps = ropeFrame(w, a, ms);
	w.powerUps.grapple.position = p.at;
	if (ps == Projectile::HitRacer && a.state == ShotFlying) {
		// The hook hits: a shield throws it off, else the racer is pulled toward the shooter.
		const size_t r = size_t(p.struck);
		if (shielded(w, r)) {
			absorbed(w, r);
			letGo(w, a, p.at);
		}
		else {
			a.state = HookPulling;
			a.ms = HookHoldMs;
			reactions(w, a, r);
			a.held = int8_t(r);
			w.powerUps.dropWhite(w, r);
			w.raceView.playSound(HookGrabSound, ownerOf(w, a), &w.racers.visible[r].position);
			w.raceView.raise(RaceEvent::Release, ownerOf(w, a)); // (its smoke, if still puffing, stops as it holds)
		}
	}
	const size_t owner = size_t(a.owner);
	switch (a.state) {
	case ShotFlying:
		if (ps == Projectile::HitWorld) {
			letGo(w, a, p.hit);
		}
		else {
			w.powerUps.grapple.rotation = basisFromForwardUp(p.target - w.racers.visible[owner].position, {0, 0, 1});
		}
		break;
	case HookPulling:
		if (ps == Projectile::HitWorld) {
			letGo(w, a, p.hit);
		}
		else {
			hookPull(w, a);
		}
		break;
	case HookReeling:
		if (ps == Projectile::Expired) {
			a.state = Action::Done;
			a.ms = 0;
		}
		break;
	}
}

// --- Lightning ---

// Whether the bolt's spans, from o through its points, pass through a body's sphere.
bool boltPasses(const Sphere& body, Vec3 o, const Vec3 (&points)[4])
{
	Vec3 from = o;
	for (const Vec3& to : points) {
		const Vec3 u = normalize(to - from), c = body.centre - from;
		const float along = dotZYX(c, u);
		const float disc = body.radius * body.radius - ((c.y * c.y + c.z * c.z + c.x * c.x) - along * along);
		const Vec3 span = from - to;
		const float t = along - std::sqrt(disc);
		if (disc > 0 && !(t * t > dotZYX(span, span))) {
			return true;
		}
		from = to;
	}
	return false;
}

// A racer the bolt passes through. Struck (only while it sustains): a shield takes it; a tumbling racer is left alone.
void boltStrike(World& w, Action& a, int8_t q)
{
	const size_t r = size_t(q);
	if (a.state == BoltSustain && shielded(w, r)) {
		absorbed(w, r);
	}
	if (a.state == BoltSustain && !shielded(w, r) && !(w.racers.karts[r].flags & KartSpinOut)) {
		w.powerUps.spinOut(w, r);
		w.raceView.markHit(r);
		w.racers.karts[r].velocity = {};
		const Vec3 h = w.racers.karts[r].heading;
		w.powerUps.impulse(w, r, {h.x * KnockForward, h.y * KnockForward, KnockUp + h.z * KnockForward});
		reactions(w, a, r);
		w.powerUps.dropWhite(w, r);
		w.raceView.spawn("lghthit", 0, ownerOf(w, a), w.racers.visible[r].position);
		w.raceView.playSound(BoltStrikeSound, ownerOf(w, a), &w.racers.visible[r].position);
		a.shockMs = 0;
		a.held = q;
	}
}

void boltFrame(World& w, Action& a, int32_t ms)
{
	if (carriedTimeUp(a, ms)) {
		a.state = a.state == BoltRamp ? uint8_t(BoltSustain) : a.state == BoltSustain ? uint8_t(BoltFade) : uint8_t(Action::Done);
		a.ms = a.state == BoltSustain ? BoltSustainMs : a.state == BoltFade ? BoltRampMs : 0;
		if (a.state == Action::Done) {
			// The thunder, its hum stopped.
			const Vec3 at = a.owner >= 0 ? w.racers.powers[a.owner].sourcePosition : a.point;
			w.raceView.playSound(ThunderSound, ownerOf(w, a), &at);
			w.raceView.stopSound(BoltHumSound, ownerOf(w, a));
			return;
		}
	}
	if (a.held >= 0) {
		a.shockMs += ms;
		if (a.shockMs > BoltShockMs) {
			a.shockMs = 0;
			w.powerUps.endSpinOut(w, size_t(a.held));
			a.held = -1;
			w.raceView.raise(RaceEvent::Finish, ownerOf(w, a)); // (the strike's sparks wind down)
		}
	}
	// The bolt from its source, as long as it has grown (or not yet faded), up to where it meets the track or its
	// victim's sphere.
	Vec3 origin = a.point, forward = a.direction;
	if (a.owner >= 0) {
		LR_ASSERT(inRange(w.racers.powers, a.owner));
		origin = w.racers.powers[a.owner].sourcePosition;
		forward = w.racers.powers[a.owner].sourceForward;
	}
	const Vec3 o = origin + BoltLift;
	Vec3 d = forward;
	float length = a.state == BoltRamp ? (1 - float(int32_t(a.ms)) * BoltRampRate) * BoltSpan
		: a.state == BoltFade		   ? (float(int32_t(a.ms)) * BoltRampRate) * BoltSpan
									   : BoltSpan;
	const Vec3 end = o + d * (length * 4);
	if (a.held >= 0) {
		LR_ASSERT(inRange(w.racers.visible, a.held) && inRange(w.racers.groups, a.held));
		const Vec3 c = w.racers.visible[a.held].position;
		d = normalize(c - o);
		const Vec3 oc = o - c;
		float dist = lr::length(oc);
		const float r = w.racers.groups[a.held].radius;
		if (dist > r) {
			dist -= r;
		}
		length = dist * 0.25f;
	}
	else {
		Vec3 hit, normal;
		if (w.powerUps.shotHits(w, o, end, hit, normal)) {
			const Vec3 oh = o - hit;
			length = lr::length(oh) * 0.25f;
		}
	}
	Vec3 points[4];
	Vec3 at = o;
	for (Vec3& p : points) {
		at += d * length;
		p = at;
	}
	if (a.held >= 0) {
		return;
	}
	// Victims: the racers in its cone whose body the bolt's spans pass through (every one, the last kept).
	for (int8_t q = firstInCone(w, o, forward, BoltNear2, BoltFar2, BoltCone); q >= 0;
		q = firstInCone(w, o, forward, BoltNear2, BoltFar2, BoltCone, q)) {
		if (q == a.owner) {
			continue;
		}
		if (boltPasses(w.racers.bodySphere(w, size_t(q)), o, points)) {
			boltStrike(w, a, q);
		}
	}
}

} // namespace

// What a cannonball or a missile does when it stops (with its hit point): a racer it hit is knocked about (unless a
// shield takes it, and a strong one sends it back), and it bursts.
void stopped(World& w, Action& a, bool missile)
{
	Projectile& p = a.shot;
	const Vec3 at = p.hit;
	bool burst = true;
	if (p.state == Projectile::HitRacer) {
		const size_t r = size_t(p.struck);
		burst = !(shielded(w, r) && level(w, r) >= 2);
		if (shielded(w, r)) {
			absorbed(w, r);
			if (level(w, r) >= 2) {
				deflect(w, p, r);
				a.owner = int8_t(r);
			}
		}
		else {
			if (missile) {
				w.powerUps.spin(w, r, 2, MissileSpinRate, 0);
			}
			reactions(w, a, r);
			w.raceView.markHit(r);
			w.powerUps.dropWhite(w, r);
		}
		if (!missile) {
			a.ms = CannonFlightMs;
		}
	}
	if (burst) {
		// Its blast: at the struck racer (unless a shield took it), or where it stopped.
		if (p.state != Projectile::HitRacer || !shielded(w, size_t(p.struck))) {
			const Vec3 heard = p.state == Projectile::HitRacer ? w.racers.visible[p.struck].position : at;
			w.raceView.playSound(missile ? MissileBlastSound : BlastSound, ownerOf(w, a), &heard, 200, 600);
		}
		w.powerUps.explode(w, false, at, p.state == Projectile::HitWorld && p.normal.z > ScarSlope, a.owner);
		w.raceView.raise(RaceEvent::Release, ownerOf(w, a)); // (its smoke, if still puffing, stops as it bursts)
		a.state = Action::Done;
		w.triggers.killMember(a.member);
	}
}

void startRed(World& w, Action& a, const Aim* aim)
{
	a.state = ShotArmed;
	a.ms = 0;
	a.held = -1;
	a.aimPoint = -1;
	a.shot = {};
	if (a.kind == Action::Bolt) {
		a.state = BoltRamp;
		a.ms = BoltRampMs;
		a.shockMs = 0;
		if (aim) {
			a.point = aim->position;
			a.direction = aim->direction;
		}
		w.raceView.loopSound(BoltHumSound, ownerOf(w, a),
			a.owner >= 0 ? w.racers.visible[a.owner].position : a.point); // its hum, at its source
		return;
	}
	if (a.kind == Action::Missile) {
		startMissile(w, a);
		return;
	}
	if (a.owner < 0) {
		return;
	}
	// Aimed now, from where the shooter's car is: a player prefers the points it can shoot.
	const size_t r = size_t(a.owner);
	const Pose& v = w.racers.visible[r];
	const bool player = !w.racers.race[r].ai;
	if (a.kind == Action::Cannon) {
		a.aimPoint = player ? nearestTarget(w, v.position, v.rotation.f, AimNear2, AimFar2, TargetCone) : -1;
		if (a.aimPoint < 0) {
			a.held = nearestInCone(w, v.position, v.rotation.f, AimNear2, AimFar2, CannonCone);
		}
	}
	else {
		a.held = nearestInCone(w, v.position, v.rotation.f, AimNear2, HookFar2, HookNearCone, true);
		if (a.held < 0) {
			a.held = nearestInCone(w, v.position, v.rotation.f, AimNear2, AimFar2, HookWideCone);
		}
		if (a.held < 0 && player) {
			a.aimPoint = nearestTarget(w, v.position, v.rotation.f, AimNear2, AimFar2, TargetCone);
		}
	}
}

void redFrame(World& w, Action& a, int32_t ms)
{
	if (a.state == Action::Done) {
		return;
	}
	switch (a.kind) {
	case Action::Cannon: cannonFrame(w, a, ms); break;
	case Action::Hook: hookFrame(w, a, ms); break;
	case Action::Bolt: boltFrame(w, a, ms); break;
	default: missileFrame(w, a, ms);
	}
}

void releaseRed(World& w, Action& a)
{
	if (a.kind == Action::Bolt && a.held >= 0) {
		w.powerUps.endSpinOut(w, size_t(a.held));
	}
	a.held = -1; // (a hook let go of while pulling leaves the pushes: QUIRK)
	a.shot.state = Projectile::Idle;
}

void hooksMoved(World& w)
{
	for (uint8_t index : w.powerUps.actionList) {
		const Action& a = w.powerUps.actions[index];
		if (a.kind == Action::Hook && a.member >= 0) {
			w.triggers.members[a.member].centre = hookCentre(w);
		}
	}
}

const ActionKind RedAction{
	.start = [](World& w, Action& a, int32_t, const Aim* aim) { startRed(w, a, aim); }, .frame = redFrame, .release = releaseRed};

} // namespace lr
