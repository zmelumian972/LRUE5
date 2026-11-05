// The red weapons (cannonball, grappling hook, lightning, homing missiles), the projectiles they and dynamite throw,
// the cone searches that aim them, and the explosions.
//
// A weapon fires in its first frame (aimed from the shooter's visible pose then), flies once per frame and is stopped
// by the track (whose surfaces may fire events and let it pass) or by a racer its touch entry met in a step. Its
// state timer carries: the time left past a state's end runs the next state in the same frame.

#include "LRCore/Race/World.h"
#include "Powers/PowerParts.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lr {

namespace {

constexpr float CannonRadius = 3.5355339f; // a 5 x 5 billboard
constexpr float ScarSlope = 0.70709997f;   // a burst on ground this flat leaves a scar
constexpr float MissileCone = 0.70709997f;

// A state's timer with the carry: true when it ran out, ms then what is left of the frame.
bool carriedTimeUp(Action& a, int32_t& ms)
{
	if (uint32_t(ms) >= a.ms) {
		ms -= int32_t(a.ms);
		a.ms = 0;
		return true;
	}
	a.ms -= uint32_t(ms);
	return false;
}

bool inCone(Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine, Vec3 p, float d2)
{
	if (!(dmin2 <= d2 && d2 <= dmax2)) {
		return false;
	}
	const Vec3 n = normalize(p - o);
	return ((d.z * n.z + d.y * n.y) + n.x * d.x) >= cosine;
}

float squaredXYZ(Vec3 a) { return a.x * a.x + a.y * a.y + a.z * a.z; }

// The lowest racer index in the cone (after `after`), -1 none.
int8_t firstInCone(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine, int after = -1)
{
	for (size_t r = size_t(after + 1); r < w.visible.size(); r++) {
		const Vec3 p = w.visible[r].position, delta = p - o;
		const float d2 = after < 0 ? dotZYX(delta, delta) : squaredXYZ(delta);
		if (inCone(o, d, dmin2, dmax2, cosine, p, d2)) {
			return int8_t(r);
		}
	}
	return -1;
}

int8_t farthestInCone(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine)
{
	int8_t best = -1;
	float most = 0;
	for (size_t r = 0; r < w.visible.size(); r++) {
		const Vec3 p = w.visible[r].position;
		const float d2 = squaredXYZ(p - o);
		if (inCone(o, d, dmin2, dmax2, cosine, p, d2) && d2 > most) {
			most = d2;
			best = int8_t(r);
		}
	}
	return best;
}

int16_t nearestTarget(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine)
{
	int16_t best = -1;
	float least = FLT_MAX;
	for (size_t t = 0; t < w.track.targets.size(); t++) {
		const Vec3 p = w.track.targets[t].position, delta = p - o;
		const float d2 = dotZYX(delta, delta);
		if (w.targetsOn[t] && inCone(o, d, dmin2, dmax2, cosine, p, d2) && d2 < least) {
			least = d2;
			best = int16_t(t);
		}
	}
	return best;
}

// The shooter's motion: its velocity's direction and speed (none for a hazard).
void shooterMotion(const World& w, int8_t shooter, Vec3& direction, float& speed)
{
	direction = {};
	speed = 0;
	if (shooter >= 0) {
		direction = w.karts[size_t(shooter)].velocityDirection;
		speed = w.karts[size_t(shooter)].speed;
	}
}

// The velocity that takes it from S to T in t seconds under its gravity.
void trajectory(Projectile& p, float t)
{
	const float i = 1 / t;
	const Vec3 d = p.target - p.start;
	const float dist = std::sqrt(d.y * d.y + d.x * d.x);
	p.speed = dist * i;
	p.velocity = {d.x * i, d.y * i, (d.z / dist) * p.speed - (p.gravity * dist) / (p.speed + p.speed)};
}

// A projectile that met a level 2 or 3 shield goes back at its owner.
void deflect(World& w, Projectile& p, size_t by)
{
	const int8_t old = p.owner;
	p.owner = int8_t(by);
	p.struck = -1;
	p.ageMs = 0;
	p.state = Projectile::Flying;
	Vec3 velocity;
	if (old >= 0) {
		p.target = w.visible[size_t(old)].position + Vec3{0, 0, 5};
		velocity = w.karts[size_t(old)].velocity;
	}
	else {
		p.target = p.start;
	}
	p.start = p.at;
	const Vec3 d = p.target - p.start;
	float dist = std::sqrt(d.y * d.y + d.x * d.x);
	dist = dist == 0 ? 1 : dist;
	const float t = dist / p.speed;
	p.target += velocity * t; // QUIRK: ft/ms times seconds
	trajectory(p, t);
}

bool shielded(const World& w, size_t r) { return w.racers[r].flags & RacerShielded; }
Owner ownerOf(const World& w, const Action& a) { return {Owner::Action, int16_t(&a - w.actions.data())}; }

// A hit's reactions (the show's voices): the owner's glad (if any), the struck racer's not.
void reactions(World& w, const Action& a, size_t struck)
{
	if (a.owner >= 0) {
		w.voice(size_t(a.owner), RaceEvent::Positive);
	}
	w.voice(struck, RaceEvent::Negative);
}

// A shield takes a hit: its racer is glad, the show plays the block.
void absorbed(World& w, size_t r)
{
	w.voice(r, RaceEvent::Positive);
	w.raise(RaceEvent::Absorb, {Owner::Racer, int16_t(r)}).place = w.visible[r].position;
}
uint8_t level(const World& w, size_t r) { return w.powers[r].shieldLevel; }

void keepMember(World& w, Action& a, Vec3 centre)
{
	if (a.member >= 0) {
		w.members[size_t(a.member)].centre = centre;
	}
}

void registerShot(World& w, Action& a, Vec3 centre, float radius)
{
	w.killMember(a.member);
	a.member = w.addMember({centre, radius, Member::Projectile, false, false, uint16_t(&a - w.actions.data())});
}

// --- Cannonball ---

void fireCannon(World& w, Action& a)
{
	Projectile& p = a.shot;
	if (a.emplacement >= 0) {
		const Hazard& h = w.hazards[size_t(a.emplacement)];
		launch(p, h.muzzle, -96.5279999f, 5000, -1);
		aimAtPoint(w, p, -1, h.point, 30);
	}
	else {
		const size_t r = size_t(a.owner);
		const Pose& v = w.visible[r];
		launch(p, v.position + Vec3{0, 0, 5}, -32.1759987f, 3000, a.owner);
		if (a.held >= 0) {
			aimAtRacer(w, p, r, size_t(a.held), 180, false);
		}
		else if (a.aimPoint >= 0) {
			aimAtPoint(w, p, a.owner, w.track.targets[size_t(a.aimPoint)].position, 180);
		}
		else {
			aimAtPoint(w, p, a.owner, v.position + v.rotation.f * 500 + Vec3{0, 0, 5}, 180);
		}
	}
	registerShot(w, a, p.at, CannonRadius);
	w.spawn("cannsmk", 0, ownerOf(w, a), p.at);
	w.playSound(6, ownerOf(w, a), &p.start);
}

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
				w.spin(r, 2, 0.007f, 0);
			}
			reactions(w, a, r);
			w.markHit(r);
			w.dropWhite(r);
		}
		if (!missile) {
			a.ms = 3000;
		}
	}
	if (burst) {
		// Its blast: at the struck racer (unless a shield took it), or where it stopped.
		if (p.state != Projectile::HitRacer || !shielded(w, size_t(p.struck))) {
			const Vec3 heard = p.state == Projectile::HitRacer ? w.visible[size_t(p.struck)].position : at;
			w.playSound(missile ? 50 : 5, ownerOf(w, a), &heard, 200, 600);
		}
		w.explode(false, at, p.state == Projectile::HitWorld && p.normal.z > ScarSlope, a.owner);
		w.raise(RaceEvent::Release, ownerOf(w, a)); // (its smoke, if still puffing, stops as it bursts)
		a.state = Action::Done;
		w.killMember(a.member);
	}
}

void cannonFrame(World& w, Action& a, int32_t ms)
{
	if (carriedTimeUp(a, ms)) {
		if (a.state == ShotArmed) {
			a.state = ShotFlying;
			a.ms = 3000;
			fireCannon(w, a);
		}
		else {
			a.state = Action::Done;
			w.killMember(a.member);
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
Vec3 hookCentre(const World& w) { return toWorld(w.grapple.rotation, w.track.hook.centre) + w.grapple.position; }

void letGo(World& w, Action& a, Vec3 at)
{
	if (a.held >= 0 || a.shot.state == Projectile::HitRacer) {
		if (a.held >= 0) {
			w.endPush(size_t(a.held), 0);
			w.endPush(size_t(a.owner), 1);
			a.held = -1;
		}
		w.playSound(20, ownerOf(w, a), &at); // (a racer's let go, or a shield threw it off)
	}
	else {
		w.playSound(22, ownerOf(w, a), &at); // (missed: the snap, then the reel)
		w.playSound(73, ownerOf(w, a), &at);
	}
	w.raise(RaceEvent::Release, ownerOf(w, a)); // (its smoke, if still puffing)
	a.state = HookReeling;
	a.ms = 500;
	a.letGo = at;
	a.shot.state = Projectile::Flying; // the rope reels in (its frame says when it is in)
	w.killMember(a.member);
}

// The rope's frame: reeling in, holding a racer, or the hook's flight.
Projectile::State ropeFrame(World& w, Action& a, int32_t ms)
{
	Projectile& p = a.shot;
	const Vec3 from = w.visible[size_t(a.owner)].position + Vec3{0, 0, 3};
	if (a.state == HookReeling) {
		// Reeled in at 250 ft per ms (QUIRK: per ms): in at once.
		const Vec3 left = a.letGo - from;
		if (dotZYX(left, left) < 36 || 250 * float(ms) >= lengthZYX(left)) {
			p.state = Projectile::Expired;
		}
		return p.state;
	}
	if (p.state == Projectile::HitRacer) {
		// The hook is on the racer (chased at 300 ft per ms: there at once), the rope taut: the track can cut it.
		p.at = w.visible[size_t(p.struck)].position + Vec3{0, 0, 2};
		Vec3 hit, normal;
		if (w.shotHits(from, p.at, hit, normal)) {
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
	const size_t r = size_t(a.owner);
	const Pose& v = w.visible[r];
	Projectile& p = a.shot;
	launch(p, v.position + Vec3{0, 0, 5}, -90.1760025f, 3000, a.owner);
	if (a.held >= 0) {
		aimAtRacer(w, p, r, size_t(a.held), 320, false);
	}
	else if (a.aimPoint >= 0) {
		aimAtPoint(w, p, a.owner, w.track.targets[size_t(a.aimPoint)].position, 320);
	}
	else {
		aimAtPoint(w, p, a.owner, v.position + v.rotation.f * 500 + Vec3{0, 0, 5}, 320);
	}
	a.held = -1; // (now the racer the rope holds)
	w.grapple = {p.at, basisFromForwardUp(p.target - v.position, {0, 0, 1})};
	registerShot(w, a, hookCentre(w), w.track.hook.radius);
	w.spawn("cannsmk", 0, ownerOf(w, a), p.at);
	w.playSound(21, ownerOf(w, a), &v.position);
}

void hookFrame(World& w, Action& a, int32_t ms)
{
	if (carriedTimeUp(a, ms)) {
		if (a.state == ShotArmed) {
			a.state = ShotFlying;
			a.ms = 3000;
			fireHook(w, a);
		}
		else if (a.state == ShotFlying || a.state == HookPulling) {
			letGo(w, a, w.grapple.position);
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
	p.at = w.grapple.position;
	const Projectile::State ps = ropeFrame(w, a, ms);
	w.grapple.position = p.at;
	if (ps == Projectile::HitRacer && a.state == ShotFlying) {
		// The hook hits: a shield throws it off, else the racer is pulled toward the shooter.
		const size_t r = size_t(p.struck);
		if (shielded(w, r)) {
			absorbed(w, r);
			letGo(w, a, p.at);
		}
		else {
			a.state = HookPulling;
			a.ms = 4000;
			reactions(w, a, r);
			a.held = int8_t(r);
			w.dropWhite(r);
			w.playSound(18, ownerOf(w, a), &w.visible[r].position);
			w.raise(RaceEvent::Release, ownerOf(w, a)); // (its smoke, if still puffing, stops as it holds)
		}
	}
	const size_t owner = size_t(a.owner);
	switch (a.state) {
	case ShotFlying:
		if (ps == Projectile::HitWorld) {
			letGo(w, a, p.hit);
		}
		else {
			w.grapple.rotation = basisFromForwardUp(p.target - w.visible[owner].position, {0, 0, 1});
		}
		break;
	case HookPulling: {
		if (ps == Projectile::HitWorld) {
			letGo(w, a, p.hit);
			break;
		}
		const size_t held = size_t(a.held);
		const Vec3 A = w.visible[owner].position, B = w.visible[held].position;
		if (shielded(w, held)) {
			letGo(w, a, B);
			break;
		}
		const Vec3 d = B - A;
		float q = d.y * d.y;
		q += d.z * d.z;
		q += d.x * d.x;
		const Vec3 f = w.visible[owner].rotation.f;
		float k = d.z * f.z;
		k += d.y * f.y;
		k += d.x * f.x;
		if (q < 144 || k < 0) {
			letGo(w, a, B);
			break;
		}
		const Vec3 pull = normalize(d) * 180;
		w.push(owner, 1, pull);
		w.push(held, 0, -pull);
		break;
	}
	case HookReeling:
		if (ps == Projectile::Expired) {
			a.state = Action::Done;
			a.ms = 0;
		}
		break;
	}
}

// --- Lightning ---

void boltFrame(World& w, Action& a, int32_t ms)
{
	if (carriedTimeUp(a, ms)) {
		a.state = a.state == BoltRamp ? uint8_t(BoltSustain) : a.state == BoltSustain ? uint8_t(BoltFade) : uint8_t(Action::Done);
		a.ms = a.state == BoltSustain ? 7000 : a.state == BoltFade ? 500 : 0;
		if (a.state == Action::Done) {
			// The thunder, its hum stopped.
			const Vec3 at = a.owner >= 0 ? w.powers[size_t(a.owner)].sourcePosition : a.point;
			w.playSound(66, ownerOf(w, a), &at);
			RaceEvent& loop = w.raise(RaceEvent::Sound, ownerOf(w, a));
			loop.sound = 68, loop.stop = true;
			return;
		}
	}
	if (a.held >= 0) {
		a.shockMs += ms;
		if (a.shockMs > 1000) {
			a.shockMs = 0;
			w.endSpinOut(size_t(a.held));
			a.held = -1;
			w.raise(RaceEvent::Finish, ownerOf(w, a)); // (the strike's sparks wind down)
		}
	}
	// The bolt from its source, as long as it has grown (or not yet faded), up to where it meets the track or its
	// victim's sphere.
	Vec3 origin = a.point, forward = a.direction;
	if (a.owner >= 0) {
		origin = w.powers[size_t(a.owner)].sourcePosition;
		forward = w.powers[size_t(a.owner)].sourceForward;
	}
	const Vec3 o = origin + Vec3{0, 0, 3};
	Vec3 d = forward;
	float length = a.state == BoltRamp ? (1 - float(int32_t(a.ms)) * 0.002f) * 50 : a.state == BoltFade ? (float(int32_t(a.ms)) * 0.002f) * 50 : 50;
	const Vec3 end = o + d * (length * 4);
	if (a.held >= 0) {
		const Vec3 c = w.visible[size_t(a.held)].position;
		d = normalize(c - o);
		const Vec3 oc = o - c;
		float dist = std::sqrt(oc.x * oc.x + oc.y * oc.y + oc.z * oc.z);
		const float r = w.groups[size_t(a.held)].radius;
		if (dist > r) {
			dist -= r;
		}
		length = dist * 0.25f;
	}
	else {
		Vec3 hit, normal;
		if (w.shotHits(o, end, hit, normal)) {
			const Vec3 oh = o - hit;
			length = std::sqrt(oh.x * oh.x + oh.y * oh.y + oh.z * oh.z) * 0.25f;
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
	for (int8_t q = firstInCone(w, o, forward, 9, 40000, 0.5f); q >= 0; q = firstInCone(w, o, forward, 9, 40000, 0.5f, q)) {
		if (q == a.owner) {
			continue;
		}
		const Sphere body = w.bodySphere(size_t(q));
		Vec3 from = o;
		for (const Vec3& to : points) {
			const Vec3 u = normalize(to - from), c = body.centre - from;
			const float along = c.z * u.z + c.y * u.y + c.x * u.x;
			const float disc = body.radius * body.radius - ((c.y * c.y + c.z * c.z + c.x * c.x) - along * along);
			const Vec3 span = from - to;
			const float t = along - std::sqrt(disc);
			if (disc > 0 && !(t * t > dotZYX(span, span))) {
				// Struck (only while it sustains): a shield takes it; a tumbling racer is left alone.
				const size_t r = size_t(q);
				if (a.state == BoltSustain && shielded(w, r)) {
					absorbed(w, r);
				}
				if (a.state == BoltSustain && !shielded(w, r) && !(w.karts[r].flags & KartSpinOut)) {
					w.spinOut(r);
					w.markHit(r);
					w.karts[r].velocity = {};
					const Vec3 h = w.karts[r].heading;
					w.impulse(r, {h.x * 80, h.y * 80, 200 + h.z * 80});
					reactions(w, a, r);
					w.dropWhite(r);
					w.spawn("lghthit", 0, ownerOf(w, a), w.visible[r].position);
					w.playSound(67, ownerOf(w, a), &w.visible[r].position);
					a.shockMs = 0;
					a.held = q;
				}
				break;
			}
			from = to;
		}
	}
}

// --- Homing missiles ---

void launchMissile(World& w, Action& a)
{
	const size_t r = size_t(a.owner);
	const Animation& anim = w.track.animations[size_t(w.track.missile.animation)];
	// From the launcher's "missile" node at the rise's last frame, carried by the car's pose (as last followed).
	const Pose node = w.nodeLocal(w.track.missile, &anim, 1, a.missile, float(anim.parts[a.missile].frames - 1), false);
	const Pose& v = w.visible[r];
	Projectile& p = a.shot;
	const Vec3 start = toWorld(a.ride.rotation, node.position * w.track.missile.scale) + a.ride.position;
	// (The missiles fly along the node's own forward, not its up.)
	const Basis rows = basisFromForwardUp(toWorld(a.ride.rotation, node.rotation.f), toWorld(a.ride.rotation, node.rotation.u));
	// Missile i takes the (i+1)-th racer in its cone, in racer order.
	int8_t q = firstInCone(w, v.position, v.rotation.f, 100, 160000, MissileCone);
	for (uint8_t n = 0; n < a.missile && q >= 0; n++) {
		q = firstInCone(w, v.position, v.rotation.f, 100, 160000, MissileCone, q);
	}
	launch(p, start, -32.1759987f, 5500, a.owner);
	if (q >= 0) {
		aimAtRacer(w, p, r, size_t(q), 170, false);
	}
	else {
		const Vec3 f = v.rotation.f;
		Vec3 ahead = {v.position.x + f.x * 500, v.position.y + f.y * 500, v.position.z + f.z * 500 + 5};
		if (a.missile == 0) {
			ahead.x += f.y * 150;
			ahead.y += -f.x * 150;
		}
		else if (a.missile == 2) {
			ahead.x += -f.y * 150;
			ahead.y += f.x * 150;
		}
		aimAtPoint(w, p, a.owner, ahead, 170);
	}
	a.held = q;
	a.centre = p.at;
	a.amplitude = 0;
	a.phase = 0;
	a.hasWaypoint = false;
	a.retargetMs = 0;
	a.heading = rows.f;
	a.checkpoint = w.racers[r].checkpoint;
	if (a.checkpoint >= 0) {
		const Vec3 n = w.track.checkpoints[size_t(a.checkpoint)].normal;
		if (a.heading.z * n.z + a.heading.y * n.y + n.x * a.heading.x > 0) {
			a.checkpoint = -1;
		}
	}
	registerShot(w, a, p.at, w.track.missile.radius);
}

void missileFrame(World& w, Action& a, int32_t ms)
{
	Projectile& p = a.shot;
	if (a.state == ShotArmed) {
		// Rising off the car: it launches once its part has played.
		const float last = float(w.track.animations[size_t(w.track.missile.animation)].parts[a.missile].frames - 1);
		a.riseFrames = std::min(a.riseFrames + 0.0299999993f * float(ms), last);
		if (a.riseFrames < last) {
			a.ride = w.visible[size_t(a.owner)];
			p.at = a.ride.position;
			return;
		}
		launchMissile(w, a);
		a.state = ShotFlying;
	}
	if (a.state != ShotFlying) {
		return;
	}
	if (p.state != Projectile::Flying) {
		stopped(w, a, true); // (a racer its touch entry met in a step)
		return;
	}
	// Targeting: the first racer in its cone it may hit, looked for again each second or when its target is spoiled.
	a.retargetMs += ms;
	auto spoiled = [&](int8_t q) {
		const size_t r = size_t(q);
		return (w.karts[r].flags & KartSpinOut) || shielded(w, r) || (w.racers[r].flags & RacerGhost);
	};
	if (a.held < 0 || a.retargetMs > 1000 || spoiled(a.held)) {
		a.retargetMs = 0;
		a.held = -1;
		for (int8_t q = firstInCone(w, a.centre, a.heading, 100, 160000, MissileCone); q >= 0;
			 q = firstInCone(w, a.centre, a.heading, 100, 160000, MissileCone, q)) {
			if (q != p.owner && !spoiled(q)) {
				a.held = q;
				break;
			}
		}
	}
	// The course: a waypoint over the next checkpoint ahead of it.
	if (!a.hasWaypoint && a.checkpoint >= 0) {
		int32_t k = a.checkpoint;
		while (k >= 0) {
			const Checkpoint& c = w.track.checkpoints[size_t(k)];
			if (dot(c.position - a.centre, a.heading) > 0 && dot(c.normal, a.centre) + c.offset >= 10) {
				break;
			}
			const uint8_t next = c.next[0];
			k = next == 255 ? -1 : next;
			if (k >= 0) {
				const Checkpoint& n = w.track.checkpoints[size_t(k)];
				if (lengthSquared(n.position - a.centre) > 90000 || dot(n.normal, a.heading) > 0) {
					k = -1;
				}
			}
		}
		if (k >= 0) {
			const Vec3 c = w.track.checkpoints[size_t(k)].position;
			Vec3 hit, normal;
			if (w.shotHits(c + Vec3{0, 0, 5}, c - Vec3{0, 0, 50}, hit, normal)) {
				a.waypoint = hit + Vec3{0, 0, 6};
				a.hasWaypoint = true;
			}
		}
	}
	// The flight: straight at a target within 60 ft (striking within 3), else along its heading low over the ground,
	// spiralling.
	const float e = float(ms) * 0.001f;
	p.ageMs += ms;
	if (p.ageMs >= p.lifeMs) {
		p.state = Projectile::Expired;
		p.hit = p.at;
	}
	else {
		Vec3 u = a.heading, goal, x;
		float q = FLT_MAX;
		if (a.held >= 0) {
			goal = w.visible[size_t(a.held)].position + Vec3{0, 0, 5};
			u = goal - a.centre;
			q = u.z * u.z + u.y * u.y + u.x * u.x;
		}
		bool struck = false;
		if (q < 3600) {
			u = normalize(u);
			const Vec3 v0 = p.velocity;
			const Vec3 wv = u * std::sqrt(v0.z * v0.z + v0.y * v0.y + v0.x * v0.x);
			x = a.centre + wv * e;
			const Vec3 g = goal - x;
			struck = q < 9 || g.z * u.z + g.y * u.y + g.x * u.x <= 0;
			if (struck) {
				p.hit = p.at = goal;
				p.state = Projectile::Expired; // (the explosion does the harm)
			}
		}
		else {
			if (a.hasWaypoint) {
				if (dot(a.waypoint - a.centre, u) > 0) {
					u = a.waypoint - a.centre;
				}
				else {
					a.hasWaypoint = false;
				}
			}
			u = normalize(u);
			x = a.centre + u * (p.speed * e);
			Vec3 hit, normal;
			if (w.shotHits(x + Vec3{0, 0, 2}, x - Vec3{0, 0, 6}, hit, normal)) {
				x.z = hit.z + 6;
				if (a.centre.z > x.z) {
					const float low = a.centre.z - 6 * e;
					x.z = low > x.z ? low : x.z;
				}
			}
		}
		if (!struck) {
			a.centre = x;
			a.amplitude = std::min(a.amplitude + 6 * e, 4.0f);
			a.phase += 10 * e;
			const float s = std::sin(a.phase), c = std::cos(a.phase), cc = 1 - c;
			x.x += (u.z * u.x * cc - u.y * s) * a.amplitude;
			x.y += (u.z * u.y * cc + s * u.x) * a.amplitude;
			x.z += ((u.z * u.z * cc) + c) * a.amplitude;
			Vec3 hit, normal;
			if (w.shotHits(p.at, x, hit, normal)) {
				p.state = Projectile::HitWorld;
				p.hit = p.at = hit;
				p.normal = normal;
			}
			else {
				p.at = x;
			}
			a.heading = u;
		}
	}
	keepMember(w, a, p.at);
	if (p.state != Projectile::Flying) {
		stopped(w, a, true);
	}
}

} // namespace

void launch(Projectile& p, Vec3 start, float gravity, int32_t lifeMs, int8_t owner)
{
	p.state = Projectile::Flying;
	p.start = p.at = start;
	p.gravity = gravity;
	p.lifeMs = lifeMs;
	p.ageMs = 0;
	p.owner = owner;
	p.struck = -1;
}

void aimAtPoint(const World& w, Projectile& p, int8_t shooter, Vec3 target, float speed)
{
	Vec3 dir;
	float moving;
	shooterMotion(w, shooter, dir, moving);
	p.target = target;
	float dx = target.x - p.start.x, dy = target.y - p.start.y;
	float dist = std::sqrt(dy * dy + dx * dx);
	const float inv = dist == 0 ? 1 : 1 / dist;
	dist = dist == 0 ? 1 : dist;
	dx *= inv;
	dy *= inv;
	const float k = dir.z * 0 + dir.y * dy + dir.x * dx;
	const float sigma = k > 0 ? speed + (k * moving) * 1000 : speed;
	trajectory(p, dist / sigma);
}

void aimAtRacer(const World& w, Projectile& p, size_t shooter, size_t target, float speed, bool predictive)
{
	Vec3 dir;
	float moving;
	shooterMotion(w, int8_t(shooter), dir, moving);
	p.target = w.visible[target].position + Vec3{0, 0, 5};
	const float dx = p.target.x - p.start.x, dy = p.target.y - p.start.y;
	float dist = std::sqrt(dy * dy + dx * dx);
	const float inv = dist == 0 ? 1 : 1 / dist;
	dist = dist == 0 ? 1 : dist;
	float k = dir.z * 0 + dir.y * (inv * dy) + dir.x * (inv * dx);
	k = k < 0 ? -k : k; // QUIRK: moving away speeds it up too
	const float sigma = std::max(speed + (k * moving) * 1000, 0.001f);
	const float t = dist / sigma, tms = t * 1000;
	const Vec3 lead = w.karts[target].velocity * tms;
	if (!predictive) {
		p.target += lead;
	}
	else if (dotZYX(w.karts[shooter].heading, (p.target + lead) - p.start) < 0) {
		p.target += lead;
	}
	trajectory(p, t);
}

Projectile::State flight(World& w, Projectile& p, int32_t ms)
{
	if (p.state != Projectile::Flying) {
		return p.state;
	}
	p.ageMs += ms;
	if (uint32_t(p.ageMs) >= uint32_t(p.lifeMs)) {
		p.state = Projectile::Expired;
		p.hit = p.at;
		return p.state;
	}
	const float e = float(p.ageMs) * 0.001f;
	const Vec3 x = {e * p.velocity.x + p.start.x, p.velocity.y * e + p.start.y, (p.velocity.z * e + p.start.z) + ((e * e) * 0.5f) * p.gravity};
	Vec3 hit, normal;
	if (w.shotHits(p.at, x, hit, normal)) {
		p.normal = normal;
		p.hit = p.at = hit;
		p.state = Projectile::HitWorld;
	}
	else {
		p.at = x;
	}
	return p.state;
}

int8_t nearestInCone(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine)
{
	int8_t best = -1;
	float least = FLT_MAX;
	for (size_t r = 0; r < w.visible.size(); r++) {
		const Vec3 p = w.visible[r].position;
		const float d2 = squaredXYZ(p - o);
		if (inCone(o, d, dmin2, dmax2, cosine, p, d2) && d2 < least) {
			least = d2;
			best = int8_t(r);
		}
	}
	return best;
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
		a.ms = 500;
		a.shockMs = 0;
		if (aim) {
			a.point = aim->position;
			a.direction = aim->direction;
		}
		RaceEvent& loop = w.raise(RaceEvent::Sound, ownerOf(w, a)); // its hum, at its source
		loop.sound = 68, loop.looping = true;
		loop.place = a.owner >= 0 ? w.visible[size_t(a.owner)].position : a.point;
		return;
	}
	if (a.kind == Action::Missile) {
		a.riseFrames = 0;
		a.ride = w.visible[size_t(a.owner)];
		w.playSound(51, ownerOf(w, a), &a.ride.position); // (as it rises off the car)
		return;
	}
	if (a.owner < 0) {
		return;
	}
	// Aimed now, from where the shooter's car is: a player prefers the points it can shoot.
	const size_t r = size_t(a.owner);
	const Pose& v = w.visible[r];
	const bool player = !w.racers[r].ai;
	if (a.kind == Action::Cannon) {
		a.aimPoint = player ? nearestTarget(w, v.position, v.rotation.f, 100, 160000, 0.94999999f) : -1;
		if (a.aimPoint < 0) {
			a.held = nearestInCone(w, v.position, v.rotation.f, 100, 160000, 0.89999998f);
		}
	}
	else {
		a.held = farthestInCone(w, v.position, v.rotation.f, 100, 62500, 0.93000001f);
		if (a.held < 0) {
			a.held = nearestInCone(w, v.position, v.rotation.f, 100, 160000, 0.60000002f);
		}
		if (a.held < 0 && player) {
			a.aimPoint = nearestTarget(w, v.position, v.rotation.f, 100, 160000, 0.94999999f);
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
		w.endSpinOut(size_t(a.held));
	}
	a.held = -1; // (a hook let go of while pulling leaves the pushes: QUIRK)
	a.shot.state = Projectile::Idle;
}

bool World::shotHits(Vec3 a, Vec3 b, Vec3& point, Vec3& normal)
{
	SegmentHit hit;
	if (!track.query(track.trackVolume, a, b, hit)) {
		return false;
	}
	if (hit.surface >= 0) {
		const Surface& s = track.surfaces[size_t(hit.surface)];
		if ((s.flags & SurfaceHasProjectileEvent) && s.projectileEvent != 0) {
			const int32_t event = s.projectileEvent;
			const Vec3 at = hit.point;
			startAt(event, &at);
			endAt(event, &at);
		}
		if (track.surfaces[size_t(hit.surface)].flags & SurfaceProjectilesPass) {
			return false; // QUIRK: and so does whatever is behind it on the segment
		}
	}
	point = hit.point;
	normal = hit.normal;
	return true;
}

void World::touchProjectile(size_t index, size_t i)
{
	Projectile& p = actions[index].shot;
	if (p.owner == int8_t(i) || p.state != Projectile::Flying) {
		return;
	}
	p.state = Projectile::HitRacer;
	p.struck = int8_t(i);
	p.hit = visible[i].position;
}

// --- Explosions ---

int8_t World::explode(bool spike, Vec3 at, bool scar, int8_t owner)
{
	std::vector<uint8_t>& free = freeExplosions[spike];
	std::vector<uint8_t>& list = explosionList[spike];
	int8_t e = -1;
	if (!free.empty()) {
		e = int8_t(free.front());
		free.erase(free.begin());
	}
	else {
		// Taken over: the scar with the least time left, else the flash, else the newest.
		for (Explosion::State state : {Explosion::Scar, Explosion::Exploding}) {
			int32_t least = INT32_MAX;
			for (uint8_t x : list) {
				if (explosions[x].state == state && explosions[x].ms < least) {
					least = explosions[x].ms;
					e = int8_t(x);
				}
			}
			if (e >= 0) {
				break;
			}
		}
		e = e >= 0 ? e : int8_t(list.front());
		list.erase(std::find(list.begin(), list.end(), uint8_t(e)));
	}
	list.insert(list.begin(), uint8_t(e));
	Explosion& x = explosions[size_t(e)];
	killMember(x.member);
	if (std::fabs(at.x) > 2048 || std::fabs(at.y) > 2048 || std::fabs(at.z) > 2048) {
		x.state = Explosion::Idle;
		return e;
	}
	x.state = Explosion::Exploding;
	x.ms = 1000;
	x.scar = scar;
	x.growth = 0.050000001f;
	x.rate = 2;
	x.owner = owner;
	x.centre = at;
	x.radius = 0.1f;
	x.member = addMember({at, 0.1f, Member::Explosion, false, false, uint16_t(e)});
	return e;
}

void hooksMoved(World& w)
{
	for (uint8_t index : w.actionList) {
		const Action& a = w.actions[index];
		if (a.kind == Action::Hook && a.member >= 0) {
			w.members[size_t(a.member)].centre = hookCentre(w);
		}
	}
}

void explosionFrame(World& w, Explosion& x, int32_t ms)
{
	if (x.state == Explosion::Exploding) {
		if (ms >= x.ms) {
			// Over: its scar fades from this frame on.
			w.killMember(x.member);
			x.state = x.scar ? Explosion::Scar : Explosion::Idle;
			x.ms = 5000;
		}
		else {
			// Growing from 0.1 ft to its blast radius over its flash, fast at first.
			constexpr float delta = (1 - 0.050000001f) - 2.0f, slowing = delta + delta;
			x.ms -= ms;
			const float e = float(ms) * 0.001f;
			x.rate = slowing * e + x.rate;
			x.growth = e * x.rate + x.growth;
			x.radius = (x.blast - 0.1f) * x.growth + 0.1f;
			if (x.member >= 0) {
				w.members[size_t(x.member)].radius = x.radius;
			}
			return;
		}
	}
	if (x.state == Explosion::Scar) {
		if (ms >= x.ms) {
			x.state = Explosion::Idle;
		}
		else {
			x.ms -= ms;
		}
	}
}

void World::blast(size_t index, size_t i)
{
	const Explosion& x = explosions[index];
	if (x.owner == int8_t(i) || (racers[i].flags & RacerShielded) || ((karts[i].flags & KartSpinOut) && !racers[i].ai)) {
		return;
	}
	float a = float(x.ms) / 1000.0f;
	a = std::clamp(a + a, 0.0f, 1.0f);
	spinOut(i);
	karts[i].velocity = {};
	const Vec3 h = karts[i].heading;
	Vec3 j = h * (a * 80);
	j.z += a * 200;
	impulse(i, j);
}

} // namespace lr
