// The shots' aiming and flight: the cone searches that pick a target, a projectile launched and aimed (at a point or
// leading a racer), its ballistic flight stopped by the track, sent back by a strong shield; and the explosions, whose
// spheres grow over their flash and knock about the racers they meet.

#include "Powers/ActionParts.h"
#include "Common/Assert.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lr {

namespace {

// An explosion: outside this cube it never goes off; its flash (from 0.1 ft, growing), the scar it leaves.
constexpr float WorldBound = 2048;
constexpr int32_t FlashMs = 1000, ScarMs = 5000;
constexpr float FlashGrowth = 0.05f, FlashRate = 2, FlashRadius = 0.1f;

bool inCone(Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine, Vec3 p, float d2)
{
	if (!(dmin2 <= d2 && d2 <= dmax2)) {
		return false;
	}
	const Vec3 n = normalize(p - o);
	return ((d.z * n.z + d.y * n.y) + n.x * d.x) >= cosine;
}

// The shooter's motion: its velocity's direction and speed (none for a hazard).
void shooterMotion(const World& w, int8_t shooter, Vec3& direction, float& speed)
{
	direction = {};
	speed = 0;
	if (shooter >= 0) {
		direction = w.racers.karts[shooter].velocityDirection;
		speed = w.racers.karts[shooter].speed;
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

} // namespace

// The lowest racer index in the cone (after `after`), -1 none.
int8_t firstInCone(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine, int after)
{
	for (size_t r = size_t(after + 1); r < w.racers.visible.size(); r++) {
		const Vec3 p = w.racers.visible[r].position, delta = p - o;
		const float d2 = after < 0 ? dotZYX(delta, delta) : lengthSquared(delta);
		if (inCone(o, d, dmin2, dmax2, cosine, p, d2)) {
			return int8_t(r);
		}
	}
	return -1;
}

int16_t nearestTarget(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine)
{
	int16_t best = -1;
	float least = FLT_MAX;
	for (size_t t = 0; t < w.track.targets.size(); t++) {
		const Vec3 p = w.track.targets[t].position, delta = p - o;
		const float d2 = dotZYX(delta, delta);
		if (w.powerUps.targetsOn[t] && inCone(o, d, dmin2, dmax2, cosine, p, d2) && d2 < least) {
			least = d2;
			best = int16_t(t);
		}
	}
	return best;
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
		p.target = w.racers.visible[old].position + ShotLift;
		velocity = w.racers.karts[old].velocity;
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
	p.target = w.racers.visible[target].position + Vec3{0, 0, 5};
	const float dx = p.target.x - p.start.x, dy = p.target.y - p.start.y;
	float dist = std::sqrt(dy * dy + dx * dx);
	const float inv = dist == 0 ? 1 : 1 / dist;
	dist = dist == 0 ? 1 : dist;
	float k = dir.z * 0 + dir.y * (inv * dy) + dir.x * (inv * dx);
	k = k < 0 ? -k : k; // QUIRK: moving away speeds it up too
	const float sigma = std::max(speed + (k * moving) * 1000, 0.001f);
	const float t = dist / sigma, tms = t * 1000;
	const Vec3 lead = w.racers.karts[target].velocity * tms;
	if (!predictive) {
		p.target += lead;
	}
	else if (dotZYX(w.racers.karts[shooter].heading, (p.target + lead) - p.start) < 0) {
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
	const Vec3 x = {e * p.velocity.x + p.start.x, p.velocity.y * e + p.start.y,
		(p.velocity.z * e + p.start.z) + ((e * e) * 0.5f) * p.gravity};
	Vec3 hit, normal;
	if (w.powerUps.shotHits(w, p.at, x, hit, normal)) {
		p.normal = normal;
		p.hit = p.at = hit;
		p.state = Projectile::HitWorld;
	}
	else {
		p.at = x;
	}
	return p.state;
}

int8_t nearestInCone(const World& w, Vec3 o, Vec3 d, float dmin2, float dmax2, float cosine, bool farthest)
{
	int8_t best = -1;
	float bestD2 = farthest ? 0 : FLT_MAX;
	for (size_t r = 0; r < w.racers.visible.size(); r++) {
		const Vec3 p = w.racers.visible[r].position;
		const float d2 = lengthSquared(p - o);
		if (inCone(o, d, dmin2, dmax2, cosine, p, d2) && (farthest ? d2 > bestD2 : d2 < bestD2)) {
			bestD2 = d2;
			best = int8_t(r);
		}
	}
	return best;
}

bool PowerUps::shotHits(World& w, Vec3 a, Vec3 b, Vec3& point, Vec3& normal)
{
	SegmentHit hit;
	if (!w.track.query(w.track.trackVolume, a, b, hit)) {
		return false;
	}
	if (hit.surface >= 0) {
		const Surface& s = w.track.surfaces[hit.surface];
		if ((s.flags & SurfaceHasProjectileEvent) && s.projectileEvent != 0) {
			const int32_t event = s.projectileEvent;
			const Vec3 at = hit.point;
			w.triggers.startAt(w, event, &at);
			w.triggers.endAt(w, event, &at);
		}
		if (w.track.surfaces[hit.surface].flags & SurfaceProjectilesPass) {
			return false; // QUIRK: and so does whatever is behind it on the segment
		}
	}
	point = hit.point;
	normal = hit.normal;
	return true;
}

void PowerUps::touchProjectile(World& w, size_t index, size_t i)
{
	LR_ASSERT(index < actions.size());
	Projectile& p = actions[index].shot;
	if (p.owner == int8_t(i) || p.state != Projectile::Flying) {
		return;
	}
	p.state = Projectile::HitRacer;
	p.struck = int8_t(i);
	p.hit = w.racers.visible[i].position;
}

// --- Explosions ---

int8_t PowerUps::explode(World& w, bool spike, Vec3 at, bool scar, int8_t owner)
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
	Explosion& x = explosions[e];
	w.triggers.killMember(x.member);
	if (std::fabs(at.x) > WorldBound || std::fabs(at.y) > WorldBound || std::fabs(at.z) > WorldBound) {
		x.state = Explosion::Idle;
		return e;
	}
	x.state = Explosion::Exploding;
	x.ms = FlashMs;
	x.scar = scar;
	x.growth = FlashGrowth;
	x.rate = FlashRate;
	x.owner = owner;
	x.centre = at;
	x.radius = FlashRadius;
	x.member = w.triggers.addMember(w, {at, FlashRadius, Member::Explosion, false, false, uint16_t(e)});
	return e;
}

void explosionFrame(World& w, Explosion& x, int32_t ms)
{
	if (x.state == Explosion::Exploding) {
		if (ms >= x.ms) {
			// Over: its scar fades from this frame on.
			w.triggers.killMember(x.member);
			x.state = x.scar ? Explosion::Scar : Explosion::Idle;
			x.ms = ScarMs;
		}
		else {
			// Growing from 0.1 ft to its blast radius over its flash, fast at first.
			constexpr float delta = (1 - FlashGrowth) - FlashRate, slowing = delta + delta;
			x.ms -= ms;
			const float e = float(ms) * 0.001f;
			x.rate = slowing * e + x.rate;
			x.growth = e * x.rate + x.growth;
			x.radius = (x.blast - FlashRadius) * x.growth + FlashRadius;
			if (x.member >= 0) {
				w.triggers.members[x.member].radius = x.radius;
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

void PowerUps::blast(World& w, size_t index, size_t i)
{
	LR_ASSERT(index < explosions.size() && i < w.racers.race.size());
	const Explosion& x = explosions[index];
	if (x.owner == int8_t(i) || (w.racers.race[i].flags & RacerShielded) ||
		((w.racers.karts[i].flags & KartSpinOut) && !w.racers.race[i].ai)) {
		return;
	}
	float a = float(x.ms) / float(FlashMs);
	a = std::clamp(a + a, 0.0f, 1.0f);
	spinOut(w, i);
	w.racers.karts[i].velocity = {};
	const Vec3 h = w.racers.karts[i].heading;
	Vec3 j = h * (a * KnockForward);
	j.z += a * KnockUp;
	impulse(w, i, j);
}

} // namespace lr
