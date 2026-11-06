// The homing missiles: three rise off the car on its launcher, then each flies at the next racer in its cone (looked
// for again each second), or low over the ground along the course, spiralling, until it strikes or hits the track.

#include "Powers/ActionParts.h"
#include "Common/Assert.h"
#include "Show/SoundIds.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lr {

namespace {

constexpr float MissileCone = 0.7071f;
// The rise off the car (frames per ms), the flight, the spread of the three with no target, the
// retargeting, the homing (within 60 ft, striking within 3), the flight over the ground and its spiral.
constexpr float MissileRiseRate = 0.03f;
constexpr int32_t MissileLifeMs = 5500, RetargetMs = 1000;
constexpr float MissileSpeed = 170, MissileSpread = 150;
constexpr float HomingRange2 = 3600, StrikeRange2 = 9;
constexpr float CruiseHeight = 6, CruiseProbe = 2, SinkRate = 6; // ft over the ground; sinking ft/s
constexpr float SpiralGrowth = 6, SpiralRadius = 4.0f, SpiralRate = 10; // ft/s, ft, rad/s
// A waypoint: over a checkpoint at least 10 ft past it and within 300 ft, found down from 5 ft above, 6 ft over it.
constexpr float WaypointClear = 10, WaypointReach2 = 90000, WaypointLift = 6;
constexpr Vec3 WaypointProbeUp = {0, 0, 5}, WaypointProbeDown = {0, 0, 50};

void launchMissile(World& w, Action& a)
{
	const size_t r = size_t(a.owner);
	const Animation& anim = w.track.animations[w.track.missile.animation];
	// From the launcher's "missile" node at the rise's last frame, carried by the car's pose (as last followed).
	const Pose node = w.scenery.nodeLocal(w.track.missile, &anim, 1, a.missile, float(anim.parts[a.missile].frames - 1), false);
	const Pose& v = w.racers.visible[r];
	Projectile& p = a.shot;
	const Vec3 start = toWorld(a.ride.rotation, node.position * w.track.missile.scale) + a.ride.position;
	// (The missiles fly along the node's own forward, not its up.)
	const Basis rows = basisFromForwardUp(toWorld(a.ride.rotation, node.rotation.f), toWorld(a.ride.rotation, node.rotation.u));
	// Missile i takes the (i+1)-th racer in its cone, in racer order.
	int8_t q = firstInCone(w, v.position, v.rotation.f, AimNear2, AimFar2, MissileCone);
	for (uint8_t n = 0; n < a.missile && q >= 0; n++) {
		q = firstInCone(w, v.position, v.rotation.f, AimNear2, AimFar2, MissileCone, q);
	}
	launch(p, start, ShotGravity, MissileLifeMs, a.owner);
	if (q >= 0) {
		aimAtRacer(w, p, r, size_t(q), MissileSpeed, false);
	}
	else {
		const Vec3 f = v.rotation.f;
		Vec3 ahead = {v.position.x + f.x * AimAhead, v.position.y + f.y * AimAhead, v.position.z + f.z * AimAhead + ShotLift.z};
		if (a.missile == 0) {
			ahead.x += f.y * MissileSpread;
			ahead.y += -f.x * MissileSpread;
		}
		else if (a.missile == 2) {
			ahead.x += -f.y * MissileSpread;
			ahead.y += f.x * MissileSpread;
		}
		aimAtPoint(w, p, a.owner, ahead, MissileSpeed);
	}
	a.held = q;
	a.centre = p.at;
	a.amplitude = 0;
	a.phase = 0;
	a.hasWaypoint = false;
	a.retargetMs = 0;
	a.heading = rows.f;
	a.checkpoint = w.racers.race[r].checkpoint;
	if (a.checkpoint >= 0) {
		const Vec3 n = w.track.checkpoints[a.checkpoint].normal;
		if (dotZYX(a.heading, n) > 0) {
			a.checkpoint = -1;
		}
	}
	addMember(w, a, Member::Projectile, p.at, w.track.missile.radius);
}

// Targeting: the first racer in its cone it may hit, looked for again each second or when its target is spoiled.
void retargetMissile(World& w, Action& a, int32_t ms)
{
	const Projectile& p = a.shot;
	a.retargetMs += ms;
	auto spoiled = [&](int8_t q) {
		const size_t r = size_t(q);
		return (w.racers.karts[r].flags & KartSpinOut) || shielded(w, r) || (w.racers.race[r].flags & RacerGhost);
	};
	if (a.held < 0 || a.retargetMs > RetargetMs || spoiled(a.held)) {
		a.retargetMs = 0;
		a.held = -1;
		for (int8_t q = firstInCone(w, a.centre, a.heading, AimNear2, AimFar2, MissileCone); q >= 0;
			q = firstInCone(w, a.centre, a.heading, AimNear2, AimFar2, MissileCone, q)) {
			if (q != p.owner && !spoiled(q)) {
				a.held = q;
				break;
			}
		}
	}
}

// The course: a waypoint over the next checkpoint ahead of it.
void missileWaypoint(World& w, Action& a)
{
	if (a.hasWaypoint || a.checkpoint < 0) {
		return;
	}
	int32_t k = a.checkpoint;
	while (k >= 0) {
		const Checkpoint& c = w.track.checkpoints[k];
		if (dot(c.position - a.centre, a.heading) > 0 && dot(c.normal, a.centre) + c.offset >= WaypointClear) {
			break;
		}
		k = nextCheckpoint(c);
		if (k >= 0) {
			const Checkpoint& n = w.track.checkpoints[k];
			if (lengthSquared(n.position - a.centre) > WaypointReach2 || dot(n.normal, a.heading) > 0) {
				k = -1;
			}
		}
	}
	if (k >= 0) {
		const Vec3 c = w.track.checkpoints[k].position;
		Vec3 hit, normal;
		if (w.powerUps.shotHits(w, c + WaypointProbeUp, c - WaypointProbeDown, hit, normal)) {
			a.waypoint = hit + Vec3{0, 0, WaypointLift};
			a.hasWaypoint = true;
		}
	}
}

// The flight: straight at a target within 60 ft (striking within 3), else along its heading low over the ground,
// spiralling.
void flyMissile(World& w, Action& a, int32_t ms)
{
	Projectile& p = a.shot;
	const float e = float(ms) * 0.001f;
	p.ageMs += ms;
	if (p.ageMs >= p.lifeMs) {
		p.state = Projectile::Expired;
		p.hit = p.at;
		return;
	}
	Vec3 u = a.heading, goal, x;
	float q = FLT_MAX;
	if (a.held >= 0) {
		LR_ASSERT(inRange(w.racers.visible, a.held));
		goal = w.racers.visible[a.held].position + ShotLift;
		u = goal - a.centre;
		q = dotZYX(u, u);
	}
	bool struck = false;
	if (q < HomingRange2) {
		u = normalize(u);
		const Vec3 v0 = p.velocity;
		const Vec3 wv = u * lengthZYX(v0);
		x = a.centre + wv * e;
		const Vec3 g = goal - x;
		struck = q < StrikeRange2 || dotZYX(g, u) <= 0;
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
		if (w.powerUps.shotHits(w, x + Vec3{0, 0, CruiseProbe}, x - Vec3{0, 0, CruiseHeight}, hit, normal)) {
			x.z = hit.z + CruiseHeight;
			if (a.centre.z > x.z) {
				const float low = a.centre.z - SinkRate * e;
				x.z = low > x.z ? low : x.z;
			}
		}
	}
	if (!struck) {
		a.centre = x;
		a.amplitude = std::min(a.amplitude + SpiralGrowth * e, SpiralRadius);
		a.phase += SpiralRate * e;
		const float s = std::sin(a.phase), c = std::cos(a.phase), cc = 1 - c;
		x.x += (u.z * u.x * cc - u.y * s) * a.amplitude;
		x.y += (u.z * u.y * cc + s * u.x) * a.amplitude;
		x.z += ((u.z * u.z * cc) + c) * a.amplitude;
		Vec3 hit, normal;
		if (w.powerUps.shotHits(w, p.at, x, hit, normal)) {
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

} // namespace

void startMissile(World& w, Action& a)
{
	a.riseFrames = 0;
	a.ride = w.racers.visible[a.owner];
	w.raceView.playSound(MissileLaunchSound, ownerOf(w, a), &a.ride.position); // (as it rises off the car)
}

void missileFrame(World& w, Action& a, int32_t ms)
{
	Projectile& p = a.shot;
	if (a.state == ShotArmed) {
		// Rising off the car: it launches once its part has played.
		const float last = float(w.track.animations[w.track.missile.animation].parts[a.missile].frames - 1);
		a.riseFrames = std::min(a.riseFrames + MissileRiseRate * float(ms), last);
		if (a.riseFrames < last) {
			a.ride = w.racers.visible[a.owner];
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
	retargetMissile(w, a, ms);
	missileWaypoint(w, a);
	flyMissile(w, a, ms);
	keepMember(w, a, p.at);
	if (p.state != Projectile::Flying) {
		stopped(w, a, true);
	}
}

} // namespace lr
