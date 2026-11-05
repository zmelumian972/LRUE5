// The race's rules: who drives (the players, the AI returning to a route or braking), what moves (bodies, routes),
// karts against karts (and what shields and curses do there), the checkpoint gates and lap marks, the racers' timers
// (the AI's use of its brick, taunts, curses), the standings and the rubber band.

#include "Common/Assert.h"
#include "Kart/Body.h"
#include "LRCore/Race/World.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lr {

namespace {

constexpr Vec3 ContactBox = {8.0f * 0.5f, 5.0f * 0.5f, 6.2f * 0.5f}; // half sizes

// A contact box: its rows, centre and half sizes.
struct Box {
	Basis rows;
	Vec3 centre, half;
};
bool boxesTouch(const Box& a, const Box& b, Vec3& normal, float& penetration, Vec3& point);

// Ahead of the kart on the route: the preview cursor attached at the loop and advanced by ms.
void aimAlong(RoutePlace& c, const Route& route, int32_t ms, bool resetRate)
{
	c.attachAtLoop(route);
	if (resetRate) {
		c.rate = 1;
	}
	c.advance(route, float(ms));
}

// How a body at `centre` (world inverse inertia w) turns for a push along n at point.
Vec3 turningOf(Vec3 centre, const Basis& w, Vec3 point, Vec3 n)
{
	const Vec3 r = normalize(centre - point);
	const Vec3 g = cross(r, n);
	return cross(toWorld(w, g), r);
}

// A returning driver's stuck clock: a spin or 2 s reversing ends the reversing; 1 s without moving starts it.
void updateStuck(Driver& d, const Kart& k, int32_t dt)
{
	d.stuckMs += dt;
	if (k.flags & KartSpinning) {
		d.stuckMs = 0;
		d.flags &= ~DriverReversing;
	}
	else if (d.flags & DriverReversing) {
		if (d.stuckMs >= 2000) {
			d.stuckMs = 0;
			d.flags &= ~DriverReversing;
		}
	}
	else if (std::fabs(k.headingSpeed) > 0.0089999996f) {
		d.stuckMs = 0;
	}
	else if (d.stuckMs >= 1000) {
		d.stuckMs = 0;
		d.flags |= DriverReversing;
	}
}

// A returning driver there: into route playback, a little along from the loop (by its preview's ms).
void rejoinRoute(World& w, size_t i, const Route& route)
{
	Driver& d = w.racers.drivers[i];
	Kart& k = w.racers.karts[i];
	Playback& pl = w.racers.playback[i];
	attachAtLoop(pl, k, w.racers.visible[i], w.racers.params[i], route, pl.route);
	RoutePlace& c = pl.cursor;
	c.rate = 1;
	Vec3 none{};
	c.seek(route, none);
	pl.baseRate = 1;
	pl.sink = pl.hop = pl.side = pl.hopVelocity = 0;
	pl.targetRate = 1;
	c.advance(route, float(d.previewMs));
	d.flags &= ~DriverReturning;
	d.previewMs = 0;
	d.radius = 0;
	d.thrust = 0;
	k.turnRadius = 0;
	k.thrust = 0;
}

} // namespace

float World::progress(size_t i) const
{
	const Racer& r = racers.race[i];
	return r.checkpoint < 0 ? 0 : float(r.checkpointCount) + track.checkpoints[size_t(r.checkpoint)].fraction;
}

// (A warp shrinks the car, and its spheres with it.)
Sphere World::carSphere(size_t i) const
{
	const float g = racers.powers.empty() ? 1.0f : racers.powers[i].carScale;
	return {toWorld(racers.visible[i].rotation, racers.carBounds[i].centre * g) + racers.visible[i].position, racers.carBounds[i].radius * g};
}

Sphere World::bodySphere(size_t i) const
{
	// The body as it leans.
	const float g = racers.powers.empty() ? 1.0f : racers.powers[i].carScale;
	const Pose& body = raceView.looks.empty() ? racers.visible[i] : raceView.looks[i].body;
	return {toWorld(body.rotation, racers.bodyBounds[i].centre * g) + body.position, racers.bodyBounds[i].radius * g};
}

Sphere World::groupSphere(size_t i) const { return racers.groups[i]; }

void World::handToAi(size_t i)
{
	racers.race[i].ai = true;
	if (racers.playback[i].route >= 0) {
		return;
	}
	// The nearest route's loop point.
	int32_t nearest = -1;
	float best = FLT_MAX;
	for (size_t slot = 0; slot < routes.size(); slot++) {
		if (routes[slot].points.empty()) {
			continue;
		}
		const Vec3 to = routes[slot].loop - racers.visible[i].position;
		const float d = dotZYX(to, to);
		if (d < best) {
			best = d;
			nearest = int32_t(slot);
		}
	}
	Driver& d = racers.drivers[i];
	if (nearest < 0) {
		d.flags |= DriverBrakeToStop;
		return;
	}
	racers.playback[i].route = nearest;
	d.previewMs = 1000;
	d.flags |= DriverReturning;
	endPowerslide(d, racers.karts[i]);
	d.radius = 0;
	aimAlong(preview, routes[size_t(nearest)], d.previewMs, true);
}

void World::driveRacer(size_t i, int32_t dt)
{
	raceView.stage = RaceEvent::Controls;
	Driver& d = racers.drivers[i];
	Kart& k = racers.karts[i];
	const KartParams& p = racers.params[i];
	if (d.flags & DriverBrakeToStop) {
		d.thrust = k.headingSpeed > 0 ? -108.0f : 0.0f;
		applyDriver(d, k, p); // with the radius before (not refreshed)
		return;
	}
	if (!racers.race[i].ai) {
		if (d.flags & DriverCursed) {
			// A curse jitters the throttle: a new offset (and steering's, unused) at random times.
			if (d.jitterMs == 0) {
				d.jitterMs = draw(Draw::CurseJitter) % 500;
				draw(Draw::CurseJitter);
				d.curseThrottle = float(draw(Draw::CurseJitter) % 200) * 0.01f - 1;
			}
			else {
				d.jitterMs = std::max(0, d.jitterMs - dt);
			}
		}
		stepDriver(d, k, p, dt);
		return;
	}
	if (!(d.flags & DriverReturning)) {
		return;
	}
	// Back to the route: aim at a point a little way along it, from the loop.
	Playback& pl = racers.playback[i];
	LR_ASSERT(pl.route >= 0 && size_t(pl.route) < routes.size());
	const Route& route = routes[size_t(pl.route)];
	const Pose& vis = racers.visible[i];
	Vec3 delta = preview.position - vis.position;
	const float dist = lengthZYX(delta);
	if (dist < 3) {
		rejoinRoute(*this, i, route);
		return;
	}
	d.prevRadius = d.radius;
	updateStuck(d, k, dt);
	const Vec3 along = k.contacts >= 3 ? vis.rotation.f : k.heading;
	const bool left = dot(vis.rotation.s, delta) >= 0;
	const Vec3 side = left ? vis.rotation.s : -vis.rotation.s;
	delta = delta * (1 / dist);
	const float closing = dot(side, delta);
	d.radius = closing < 0.00050000002f ? 4096.0f : dist / (closing + closing);
	if (!left) {
		d.radius = -d.radius;
	}
	d.thrust = 18;
	if (d.flags & DriverReversing) {
		d.radius = -d.radius;
		d.thrust = -54;
	}
	if ((dist < 30 || dot(basisOf(preview.rotation).f, along) < 0.5f) && dist <= 80) {
		d.previewMs += 250;
		aimAlong(preview, route, d.previewMs, false);
	}
	applyDriver(d, k, p);
}

void World::moveRacer(size_t i, int32_t dt)
{
	raceView.stage = RaceEvent::Controls;
	Kart& k = racers.karts[i];
	hits.clear();
	if (racers.playback[i].active) {
		const Playback& pl = racers.playback[i];
		LR_ASSERT(pl.route >= 0 && size_t(pl.route) < routes.size());
		stepPlayback(racers.playback[i], k, racers.visible[i], racers.params[i], track, routes[size_t(pl.route)], dt, &hits);
	}
	else {
		k.preStart = racers.race[i].flags & RacerOnGrid;
		k.engineStarted = racers.race[i].flags & RacerEngine;
		wheelSurfaces.clear();
		stepBody(k, racers.params[i], track, dt, &hits, &racers.visible[i], &wheelSurfaces);
		// The wheels' surface particles (a player's), substep by substep: a change is an event.
		RacerLook& look = raceView.looks[i];
		for (const std::array<int16_t, 4>& on : wheelSurfaces) {
			for (size_t w = 0; w < 4; w++) {
				std::array<char, 9> name = {};
				if (!racers.race[i].ai && on[w] >= 0) {
					name = track.surfaces[size_t(on[w])].particle;
				}
				if (name != look.wheelParticles[w]) {
					look.wheelParticles[w] = name;
					RaceEvent& e = raise(RaceEvent::WheelParticle, {Owner::Racer, int16_t(i), uint8_t(w)});
					e.name = name;
					e.set = 2;
					e.value = int32_t(w);
					e.place = k.wheels[w].position;
				}
			}
		}
	}
	if (k.wallHits > 0) {
		raise(RaceEvent::Scrape, {Owner::Racer, int16_t(i)}).place = racers.visible[i].position; // (its cooldown is the show's)
	}
	applyHits(i);
}

void World::applyHits(size_t i)
{
	for (const SweepHit& h : hits) {
		if (h.checkpoint >= 0) {
			crossGate(i, h.checkpoint, h.forward);
			continue;
		}
		if (h.finishLine) {
			racers.race[i].laps = session.raceLaps;
		}
		if (h.event >= 0) {
			// Started and ended at once, at the hit and for the racer.
			fireAt(h.event, h.event, &h.point);
			fireFor(h.event, h.event, i);
		}
	}
}

void World::crossGate(size_t i, int16_t g, bool forward)
{
	Racer& r = racers.race[i];
	if (g == r.checkpoint && forward == r.forward) {
		return; // the same gate again (another body point, or after a back-off)
	}
	if (track.checkpoints[size_t(g)].fraction == 0) {
		// The line's gate.
		if (!forward) {
			r.flags |= RacerCrossedBackward;
			r.forward = false;
			r.zones[0] = LapMark::Open;
			r.zones[1] = LapMark::Past;
			r.zones[2] = LapMark::Past; // so the next line mark counts no lap
		}
		else {
			if (!(r.flags & RacerCrossedBackward)) {
				r.checkpointCount++;
			}
			r.flags &= ~RacerCrossedBackward;
			r.forward = true;
		}
	}
	else {
		if (r.flags & RacerCrossedBackward) {
			r.checkpointCount--;
			r.flags &= ~RacerCrossedBackward;
		}
		r.forward = forward;
	}
	r.checkpoint = g;
}

void World::lapMark(size_t i, LapMark::Zone zone)
{
	Racer& r = racers.race[i];
	if (zone == r.zones[0]) {
		return;
	}
	// A lap: to the line from the open track, reached from just past the line, reached from the line.
	if (zone == LapMark::Line && r.zones[0] == LapMark::Open && r.zones[1] == LapMark::Past && r.zones[2] == LapMark::Line) {
		r.lineCrossings++;
		if (r.lineCrossings > 0 && r.lineCrossings > r.laps) {
			r.laps++;
			r.timeBehind = r.standing == 1 ? 0 : timeBehind(i);
			if (r.timeBehind != 0 && r.laps < session.raceLaps) {
				r.timeBehindMs = 2000;
			}
		}
	}
	r.zones[2] = r.zones[1];
	r.zones[1] = r.zones[0];
	r.zones[0] = zone;
}

int32_t World::timeBehind(size_t i) const
{
	// Behind the slowest of those who have done as many laps, over those laps (and the laps they did beyond).
	const Racer& r = racers.race[i];
	const int32_t laps = std::min(r.laps, session.raceLaps);
	int32_t most = 0;
	for (size_t o = 0; o < racers.race.size(); o++) {
		const int32_t other = std::min(racers.race[o].laps, session.raceLaps);
		if (o == i || other < laps) {
			continue;
		}
		int32_t behind = 0;
		for (int32_t l = 0; l < laps; l++) {
			behind += r.lapMs[l] - racers.race[o].lapMs[l];
		}
		for (int32_t l = laps; l < other; l++) {
			behind += racers.race[o].lapMs[l];
		}
		most = std::max(most, behind);
	}
	return most;
}

void World::finish(size_t i)
{
	Racer& r = racers.race[i];
	r.flags |= RacerFinished;
	r.place = session.finishers++;
	setStanding(i, session.finishers);
	finished(i);
}

void World::setStanding(size_t i, int32_t standing)
{
	// A better place is cheered, a worse one regretted (the show's voices).
	const int32_t before = racers.race[i].standing;
	if (before != 0 && standing != before) {
		voice(i, standing < before ? RaceEvent::Positive : RaceEvent::Negative);
	}
	racers.race[i].standing = standing;
}

// The boxes of two karts (centred on the centre of mass, oriented by the body), by separating axes: the three faces
// of each, then the nine edge pairs, which only reject. The normal is the face axis of least overlap.
bool World::boxContact(size_t first, size_t second, Vec3& normal, float& penetration, Vec3& point) const
{
	const float sa = racers.race[first].boxScale, sb = racers.race[second].boxScale;
	if (sa == 0 || sb == 0) {
		return false;
	}
	return boxesTouch({racers.karts[first].rotation, racers.karts[first].centerOfMassWorld, sa != 1 ? ContactBox * sa : ContactBox},
		{racers.karts[second].rotation, racers.karts[second].centerOfMassWorld, sb != 1 ? ContactBox * sb : ContactBox}, normal, penetration, point);
}

namespace {

// The corners of each box at least that deep along the normal (the first's along it, the second's against it); the
// point is their mean.
Vec3 contactPoint(const Box& boxA, const Box& boxB, Vec3 normal, float depthA, float depthB)
{
	Vec3 sum{};
	int count = 0;
	auto corners = [&](const Box& box, float depth, float sign) {
		const Basis& o = box.rows;
		const Vec3 h = box.half;
		for (int c = 0; c < 8; c++) {
			const Vec3 off = toWorld(o, {c < 4 ? h.x : -h.x, (c & 2) ? -h.y : h.y, (c & 1) ? -h.z : h.z});
			if ((normal.y * off.y + normal.z * off.z + normal.x * off.x) * sign >= depth) {
				sum += box.centre + off;
				count++;
			}
		}
	};
	corners(boxA, depthA, 1);
	corners(boxB, depthB, -1);
	return count ? sum * (1 / float(count)) : sum;
}

bool boxesTouch(const Box& boxA, const Box& boxB, Vec3& normal, float& penetration, Vec3& point)
{
	const Vec3 a = boxA.half, b = boxB.half;
	const Basis& A = boxA.rows;
	const Basis& B = boxB.rows;
	const Vec3 dc = boxB.centre - boxA.centre;
	const Vec3 t = {dotZYX(A.f, dc), dotZYX(A.s, dc), dotZYX(A.u, dc)};
	const Vec3* ar = &A.f;
	const Vec3* br = &B.f;
	float R[3][3], Q[3][3];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			R[i][j] = dot(br[i], ar[j]);
			Q[i][j] = std::fabs(R[i][j]);
		}
	}
	struct Axis {
		float distance, projection, half;
	};
	const Axis faces[6] = {
		{std::fabs(t.x), b.z * Q[2][0] + b.y * Q[1][0] + b.x * Q[0][0], a.x},
		{std::fabs(t.z * R[0][2] + t.y * R[0][1] + t.x * R[0][0]), a.z * Q[0][2] + a.y * Q[0][1] + a.x * Q[0][0], b.x},
		{std::fabs(t.y), b.z * Q[2][1] + b.y * Q[1][1] + b.x * Q[0][1], a.y},
		{std::fabs(t.z), b.z * Q[2][2] + b.y * Q[1][2] + b.x * Q[0][2], a.z},
		{std::fabs(t.z * R[1][2] + t.y * R[1][1] + t.x * R[1][0]), a.z * Q[1][2] + a.y * Q[1][1] + a.x * Q[1][0], b.y},
		{std::fabs(t.z * R[2][2] + t.y * R[2][1] + t.x * R[2][0]), a.z * Q[2][2] + a.y * Q[2][1] + a.x * Q[2][0], b.z},
	};
	int best = -1;
	float least = FLT_MAX;
	for (int i = 0; i < 6; i++) {
		const float extent = faces[i].projection + faces[i].half;
		if (faces[i].distance > extent) {
			return false;
		}
		if (extent - faces[i].distance < least) {
			least = extent - faces[i].distance;
			best = i;
		}
	}
	const float edges[9][2] = {
		{t.z * R[0][1] - t.y * R[0][2], b.y * Q[2][0] + b.z * Q[1][0] + a.y * Q[0][2] + a.z * Q[0][1]},
		{t.z * R[1][1] - t.y * R[1][2], b.x * Q[2][0] + b.z * Q[0][0] + a.y * Q[1][2] + a.z * Q[1][1]},
		{t.z * R[2][1] - t.y * R[2][2], a.y * Q[2][2] + a.z * Q[2][1] + b.x * Q[1][0] + b.y * Q[0][0]},
		{t.x * R[0][2] - t.z * R[0][0], b.y * Q[2][1] + b.z * Q[1][1] + a.x * Q[0][2] + a.z * Q[0][0]},
		{t.x * R[1][2] - t.z * R[1][0], b.x * Q[2][1] + b.z * Q[0][1] + a.x * Q[1][2] + a.z * Q[1][0]},
		{t.x * R[2][2] - t.z * R[2][0], a.x * Q[2][2] + a.z * Q[2][0] + b.x * Q[1][1] + b.y * Q[0][1]},
		{t.y * R[0][0] - t.x * R[0][1], b.y * Q[2][2] + b.z * Q[1][2] + a.x * Q[0][1] + a.y * Q[0][0]},
		{t.y * R[1][0] - t.x * R[1][1], b.x * Q[2][2] + b.z * Q[0][2] + a.x * Q[1][1] + a.y * Q[1][0]},
		{t.y * R[2][0] - t.x * R[2][1], a.x * Q[2][1] + a.y * Q[2][0] + b.x * Q[1][2] + b.y * Q[0][2]},
	};
	for (const auto& [projected, extent] : edges) {
		if (std::fabs(extent) >= 0.001f && std::fabs(projected) > extent) {
			return false;
		}
	}
	const Vec3 rows[6] = {A.f, B.f, A.s, A.u, B.s, B.u};
	normal = rows[best];
	const Vec3 ba = boxA.centre - boxB.centre;
	if (normal.y * ba.y + normal.z * ba.z + normal.x * ba.x < 0) {
		normal = -normal;
	}
	penetration = least;
	point = contactPoint(boxA, boxB, normal, faces[best].half - least, faces[best].projection - least);
	return true;
}

} // namespace

void World::contactSweep()
{
	raceView.stage = RaceEvent::Queue;
	contacts.clear();
	const size_t n = racers.karts.size();
	std::vector<Sphere> spheres(n);
	for (size_t i = 0; i < n; i++) {
		spheres[i] = carSphere(i);
	}
	auto sphere = [&](uint8_t i) -> Sphere { return i < RockEntry ? spheres[i] : Sphere{scenery.hazards[i - RockEntry].centre, scenery.hazards[i - RockEntry].radius}; };
	auto xlo = [&](uint8_t i) { return sphere(i).centre.x - sphere(i).radius; };
	// Insertion sort by xlo, equals keeping their order.
	for (size_t i = 1; i < contactOrder.size(); i++) {
		const uint8_t e = contactOrder[i];
		size_t j = i;
		while (j > 0 && xlo(e) < xlo(contactOrder[j - 1])) {
			contactOrder[j] = contactOrder[j - 1];
			j--;
		}
		contactOrder[j] = e;
	}
	for (size_t ei = 0; ei < contactOrder.size(); ei++) {
		const uint8_t e = contactOrder[ei];
		const Sphere se = sphere(e);
		const float xhi = se.radius + se.centre.x;
		for (size_t oi = ei + 1; oi < contactOrder.size(); oi++) {
			const uint8_t o = contactOrder[oi];
			if (xlo(o) > xhi) {
				break;
			}
			const Sphere so = sphere(o);
			const Vec3 d = so.centre - se.centre;
			const float r = so.radius + se.radius;
			if (!(lengthSquared(d) < r * r)) {
				continue;
			}
			if (e >= RockEntry || o >= RockEntry) {
				// A rock: the kart bounces off it (the rock first does nothing; two rocks don't meet).
				if ((e >= RockEntry) != (o >= RockEntry)) {
					hitRock(e < RockEntry ? e : o, size_t((e >= RockEntry ? e : o) - RockEntry));
				}
				continue;
			}
			Contact c{e, o, {}, 0, {}, 0};
			if (boxContact(e, o, c.normal, c.penetration, c.point) && c.penetration != 0 && contact(c)) {
				contacts.push_back(c);
			}
		}
	}
}

// A racer moved out of a contact; a physics kart only if its body can go there.
void World::moveOut(size_t i, Vec3 move)
{
	Kart& k = racers.karts[i];
	if (racers.playback[i].active) {
		Playback& pl = racers.playback[i];
		LR_ASSERT(pl.route >= 0 && size_t(pl.route) < routes.size());
		routeMove(pl, k, racers.visible[i], racers.params[i], routes[size_t(pl.route)], move);
		return;
	}
	k.wallHits = 0;
	saveBody(k, racers.params[i], track);
	const Vec3 from = racers.visible[i].position;
	racers.visible[i].position = from + move;
	k.position = racers.visible[i].position;
	hits.clear();
	sweepBody(k, racers.params[i], track, 0, !(k.flags & KartGhost), &hits);
	if (k.wallHits != 0) {
		racers.visible[i].position = k.position = from;
	}
	applyHits(i);
}

void World::hitRock(size_t i, size_t hazard)
{
	const Hazard& h = scenery.hazards[hazard];
	const HazardSpec& spec = track.hazards[hazard];
	Racer& r = racers.race[i];
	Kart& k = racers.karts[i];
	Vec3 n, point;
	float depth;
	const float scale = r.boxScale;
	if (scale == 0 || !boxesTouch({k.rotation, k.centerOfMassWorld, scale != 1 ? ContactBox * scale : ContactBox}, {h.rows, h.centre, spec.size * 0.5f}, n, depth, point) ||
		depth == 0 || (r.flags & RacerGhost) || (k.flags & KartSpinOut)) {
		return;
	}
	moveOut(i, n * depth);
	// An impulse against a body that doesn't move (its inverse inertia as at load, QUIRK).
	constexpr float RockInvMass = 4.99999987e-05f;
	const Vec3 inertia = boxInertia(20000, spec.size);
	const Basis rockW = {{1 / inertia.x, 0, 0}, {0, 1 / inertia.y, 0}, {0, 0, 1 / inertia.z}};
	const float num = -dot(k.velocity, n) * (0.75f + 1);
	const float m = (racers.params[i].invMass + RockInvMass) * dot(n, n);
	const float j = num / (dot(n, turningOf(k.centerOfMassWorld, k.invInertiaWorld, point, n) + turningOf(h.centre, rockW, point, n)) + m);
	if (racers.playback[i].active) {
		routeImpulse(racers.playback[i], k, n, j);
	}
	else {
		k.velocity += n * (racers.params[i].invMass * j);
	}
}

bool World::contact(Contact& c)
{
	const size_t first = c.first, second = c.second;
	const Vec3 n = c.normal, point = c.point;
	const float depth = c.penetration;
	Racer& ra = racers.race[first];
	Racer& rb = racers.race[second];
	Kart& ka = racers.karts[first];
	Kart& kb = racers.karts[second];
	if (((ra.flags | rb.flags) & RacerGhost) || ((ka.flags | kb.flags) & KartSpinOut)) {
		return false;
	}
	// The first is moved out; an impulse along the normal, restitution 0.75.
	moveOut(first, n * depth);
	const Vec3 vrel = ka.velocity - kb.velocity;
	const float num = -dot(vrel, n) * (0.75f + 1);
	const float m = (racers.params[first].invMass + racers.params[second].invMass) * dot(n, n);
	const float j = num / (dot(n, turningOf(ka.centerOfMassWorld, ka.invInertiaWorld, point, n) + turningOf(kb.centerOfMassWorld, kb.invInertiaWorld, point, n)) + m);
	c.impulse = j;
	auto push = [&](size_t i, float magnitude) {
		if (racers.playback[i].active) {
			routeImpulse(racers.playback[i], racers.karts[i], n, magnitude);
		}
		else {
			racers.karts[i].velocity += n * (racers.params[i].invMass * magnitude);
		}
	};
	push(first, j);
	push(second, -j);

	if (ra.ai && rb.ai) {
		for (Racer* r : {&ra, &rb}) {
			if (r->boxScale == 1) {
				r->boxMs = 750;
			}
		}
	}
	// Two racers not both AI: the faster first scrapes the other (the sound and its cooldowns are the show's), sparks
	// fly, and one reacts.
	if (!(ra.ai && rb.ai)) {
		if (ka.speed > kb.speed) {
			RaceEvent& bump = raise(RaceEvent::Bump, {Owner::Racer, int16_t(first)});
			bump.value = int32_t(second);
			bump.place = point;
			raise(RaceEvent::Sparks, {Owner::Racer, int16_t(first)}).place = point;
			voice(ra.flags & RacerShielded ? second : first, RaceEvent::Negative);
		}
		else {
			voice(first, RaceEvent::Negative);
		}
	}

	contactPowers(first, second, point);
	ra.flags |= RacerBoxRamp;
	rb.flags |= RacerBoxRamp;
	return true;
}

// A contact's power-ups: the shields' shoves and spins, a curse passed on.
void World::contactPowers(size_t first, size_t second, Vec3 point)
{
	// A shield: at level 1 it shoves the other racer when that one is hit from the side, at 2 and 3 spins it.
	const size_t pair[2] = {first, second};
	const Vec3 offsets[2] = {normalize(racers.karts[first].centerOfMassWorld - point), normalize(racers.karts[second].centerOfMassWorld - point)};
	if (((racers.race[first].flags ^ racers.race[second].flags) & RacerShielded) != 0) {
		for (int s = 0; s < 2; s++) {
			const size_t own = pair[s], other = pair[1 - s];
			if (!(racers.race[own].flags & RacerShielded)) {
				continue;
			}
			const Vec3 f = racers.visible[other].rotation.f, d = offsets[1 - s];
			const float side = dot(f, d);
			switch (racers.powers[own].shieldLevel) {
			case 1:
				if (side > -0.69999999f && side < 0.69999999f) {
					shove(other, d * 200);
				}
				break;
			case 2: spin(other, 1, 0.0089999996f, 0); break;
			case 3: spin(other, 2, 0.0089999996f, 0); break;
			default: break;
			}
		}
	}
	// A curse passes to the other racer (once a racer has had one).
	for (int s = 0; s < 2; s++) {
		const size_t from = pair[s], to = pair[1 - s];
		if ((racers.race[from].flags & RacerCursed) && !(racers.race[to].flags & RacerCursed) && racers.powers[from].cursePassed && !(racers.race[to].flags & RacerShielded)) {
			curseOn(to, racers.powers[from].curseMs);
			curseOff(from);
			break;
		}
	}
}

// An AI taunts a racer close in front (an event at its car); its next taunt's delay.
void World::tauntNearby(size_t i)
{
	Racer& r = racers.race[i];
	bool near = false;
	for (size_t o = 0; o < racers.race.size() && !near; o++) {
		const Vec3 d = racers.visible[o].position - racers.visible[i].position;
		const float d2 = lengthSquared(d);
		near = o != i && d2 < 169 && d2 > 0 && dot(racers.visible[i].rotation.f, d) * (1 / std::sqrt(d2)) > 0.30000001f;
	}
	r.tauntMs = near ? 2000 + 8 * draw(Draw::Taunt) : 2000;
	if (near) {
		const Vec3 at = racers.visible[i].position;
		voice(i, RaceEvent::Taunt);
		fireAt(999, 999, &at);
	}
}

// Wrong way (players): shown after half a second facing back along the course.
void World::updateWrongWay(size_t i, int32_t dt)
{
	Racer& r = racers.race[i];
	const Kart& k = racers.karts[i];
	if (r.ai || r.checkpoint < 0) {
		if (r.ai) {
			r.wrongWayMs = 0;
			r.flags &= ~(RacerWrongWayPending | RacerWrongWayShown);
		}
	}
	else if (r.flags & RacerGhost) {
		r.wrongWayMs = 0;
		r.flags &= ~(RacerWrongWayPending | RacerWrongWayShown);
	}
	else {
		if (r.wrongWayMs != 0) {
			if (dt >= r.wrongWayMs) {
				r.wrongWayMs = 0;
				r.flags &= ~RacerWrongWayShown;
			}
			else {
				r.wrongWayMs -= dt;
			}
		}
		const Checkpoint& c = track.checkpoints[size_t(r.checkpoint)];
		const bool hasNext = c.next[0] != 255 || c.next[1] != 255 || c.next[2] != 255 || c.next[3] != 255;
		const bool fine = hasNext && dotZYX(k.heading, c.normal) <= 0.2f; // the normals point back along the course
		if (r.flags & RacerWrongWayPending) {
			if (fine) {
				r.wrongWayMs = 0;
				r.flags &= ~RacerWrongWayPending;
			}
			else if (r.wrongWayMs == 0) {
				r.flags = (r.flags & ~RacerWrongWayPending) | RacerWrongWayShown;
			}
		}
		else if (!(r.flags & RacerWrongWayShown)) {
			if (!fine) {
				r.wrongWayMs = 500;
				r.flags |= RacerWrongWayPending;
			}
		}
		else if (!fine) {
			r.wrongWayMs = 500;
		}
	}
}

void World::racerTimers(int32_t dt)
{
	raceView.stage = RaceEvent::Timers;
	auto countDown = [dt](int32_t& ms) { ms = std::max(0, ms - dt); };
	for (size_t i = 0; i < racers.race.size(); i++) {
		Racer& r = racers.race[i];
		Kart& k = racers.karts[i];
		Power& pw = racers.powers[i];
		if (r.flags & RacerOnGrid) {
			continue;
		}
		if (r.laps < session.raceLaps) {
			r.lapMs[r.laps] += dt;
		}
		// The AI looks at its brick every 150 ms or so.
		pw.aiClockMs += dt;
		if (pw.aiClockMs > pw.aiIntervalMs) {
			pw.aiClockMs = 0;
			pw.aiIntervalMs = 150;
			if (r.ai && pw.held != NoColour) {
				consider(i);
			}
		}
		if (!racers.playback[i].active && (k.flags & KartSpinOut)) {
			k.flags &= ~KartSpinOut;
			k.spinOutMs = 0;
		}
		countDown(r.tauntMs);
		// A curse runs out.
		if (dt > pw.curseMs) {
			if (r.flags & RacerCursed) {
				curseOff(i);
			}
		}
		else {
			pw.curseMs -= dt;
		}
		countDown(r.timeBehindMs);

		// The contact box: two AI karts in contact shrink each other's to nothing over 250 ms.
		if (dt < r.boxMs && (r.flags & RacerBoxRamp)) {
			r.boxMs -= dt;
			r.boxScale = r.boxMs > 500 ? float(r.boxMs - 500) * 0.004f : 0;
		}
		else {
			r.boxMs = 0;
			r.boxScale = 1;
		}
		r.flags &= ~RacerBoxRamp;
		pw.sourcePosition = racers.visible[i].position;
		pw.sourceForward = racers.visible[i].rotation.f;

		if (r.ai && r.tauntMs == 0) {
			tauntNearby(i);
		}
		updateWrongWay(i, dt);
	}
}

void World::updateStandings()
{
	const size_t n = racers.race.size();
	std::vector<uint8_t> order(n);
	std::vector<float> prog(n), tie(n);
	for (size_t i = 0; i < n; i++) {
		order[i] = uint8_t(i);
		prog[i] = progress(i);
	}
	// Least progress first: each place takes the least of the rest (a swap, so not stable).
	for (size_t s = 0; s < n; s++) {
		size_t best = s;
		for (size_t j = s + 1; j < n; j++) {
			if (prog[order[j]] < prog[order[best]]) {
				best = j;
			}
		}
		std::swap(order[s], order[best]);
	}
	// Ties: by how far past the gate's next planes they are (the greatest, the furthest along, last).
	for (size_t g = 0; g + 1 < n; g++) {
		size_t end = g + 1;
		while (end < n && prog[order[end]] == prog[order[g]]) {
			end++;
		}
		const int16_t cp = racers.race[order[g]].checkpoint;
		if (end - g < 2 || cp < 0) {
			continue;
		}
		const Checkpoint& c = track.checkpoints[size_t(cp)];
		for (size_t k = g; k < end; k++) {
			const Vec3 p = racers.visible[order[k]].position;
			float least = FLT_MAX;
			for (uint8_t next : c.next) {
				if (next != 255) {
					const Checkpoint& nc = track.checkpoints[next];
					least = std::min(least, dot(nc.normal, p) + nc.offset);
				}
			}
			tie[order[k]] = least;
		}
		for (size_t s = g; s < end; s++) {
			size_t best = s;
			for (size_t j = s + 1; j < end; j++) {
				if (tie[order[j]] > tie[order[best]]) {
					best = j;
				}
			}
			std::swap(order[s], order[best]);
		}
	}
	for (size_t i = 0; i < n; i++) {
		if (!(racers.race[order[i]].flags & RacerFinished)) {
			setStanding(order[i], int32_t(n - i));
		}
	}
}

void World::pace(int32_t dt)
{
	if (dt <= session.paceDelay) {
		session.paceDelay -= dt;
		return;
	}
	session.paceDelay = 0;
	float best = -1;
	for (size_t i = 0; i < racers.race.size(); i++) {
		if (!racers.race[i].ai && progress(i) > best) {
			best = progress(i);
		}
	}
	for (size_t i = 0; i < racers.race.size(); i++) {
		// With no player every racer plays at 1; else racers 1 and up (never racer 0) ahead of the best player go 5%
		// slow, those behind 5% fast.
		if (racers.race[i].flags & (RacerMagnetHeld | RacerGrabbed)) {
			continue;
		}
		if (best == -1) {
			routePace(racers.playback[i], racers.karts[i], 1);
		}
		else if (i > 0) {
			const float p = progress(i);
			if (p > best) {
				routePace(racers.playback[i], racers.karts[i], (1 - 0.050000001f) + session.paceBonus);
			}
			else if (p < best) {
				routePace(racers.playback[i], racers.karts[i], (0.050000001f + session.paceBonus) + 1);
			}
		}
	}
}

} // namespace lr
