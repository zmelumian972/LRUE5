// Route playback: the cursor along a recorded line, and a route kart's step (rate, cursor, motion, pose, sweep).

#include "LRCore/Race/Route.h"

#include "Kart/Body.h"
#include "LRCore/Race/Track.h"
#include "LRData/Cache.h"
#include "LRData/Race.h"

#include <algorithm>

namespace lr {

namespace {

constexpr float MaxRate = 2.75f, MinRate = -0.5f;
constexpr float BoostRate = 1.7f, SpinRate = 0.3f;

// Toward target by step, not past it.
float ease(float value, float target, float step)
{
	if (value < target) {
		return std::min(value + step, target);
	}
	if (value > target) {
		return std::max(value - step, target);
	}
	return value;
}

// The line's rotation turned by the bank, the tilt (about the forward row) and the spin (about the up row), with the
// tilt's and the spin's timers run by e ms.
void orient(Playback& pl, Kart& k, Pose& vis, const KartParams& p, int32_t e)
{
	Basis rows = basisOf(pl.cursor.rotation);
	if ((k.flags & KartHover) || k.bank != 0) {
		rows = basisFromForwardUp(rows.f, turnAbout(rows.u, rows.f, k.bank));
	}
	if (k.pitchMs != 0 || pl.tiltAngle != 0) {
		const Vec3 a = rows.f, b = rows.u;
		if (k.pitchMs != 0) {
			if (e >= k.pitchMs) {
				k.pitchMs = 0;
				pl.tiltRate = -(pl.tiltRate * 0.25f); // rebounds at a quarter
			}
			else {
				k.pitchMs -= e;
			}
		}
		const bool was = pl.tiltAngle >= 0;
		pl.tiltAngle += pl.tiltRate * float(e);
		if (k.pitchMs == 0 && (pl.tiltAngle >= 0) != was) {
			pl.tiltAngle = 0; // back through level: over
			pl.tiltHeight = 0;
		}
		else {
			// The anchor wheel stays on the ground: the kart rises by what tilting lifts it.
			const float before = (toWorld(rows, p.anchor) + vis.position).z;
			rows = basisFromForwardUp(a, turnAbout(b, a, pl.tiltAngle));
			pl.tiltHeight = (toWorld(rows, p.anchor) + vis.position).z - before;
		}
	}
	if (k.yawMs != 0) {
		if (e >= k.yawMs) {
			k.yawMs = 0;
			k.flags &= ~(KartSpinning | KartSpinFresh);
		}
		else {
			k.yawMs -= e;
			pl.spinAngle += pl.spinRate * float(e);
			rows = basisFromForwardUp(turnAbout(rows.f, rows.u, pl.spinAngle), rows.u);
		}
	}
	vis.rotation = rows;
}

// The line's position raised by the motion and moved sideways: the visible pose, and what follows from it.
void place(Playback& pl, Kart& k, Pose& vis, const KartParams& p)
{
	Vec3 at = pl.cursor.position;
	at.z = (((at.z - pl.sink) + pl.tiltHeight) + pl.hop) + k.lift;
	const Vec3 s = vis.rotation.s;
	at += Vec3{s.x * pl.side, s.y * pl.side, s.z * pl.side};
	vis.position = at;
	k.contacts = pl.cursor.contacts;
	k.centerOfMassWorld = toWorld(vis.rotation, p.centerOfMass) + at;
	k.heading = vis.rotation.f;
	k.invInertiaWorld = worldInverseInertia(k.rotation, p.invInertia); // from the body's rows: a step stale
}

// A hovering kart banks into the line's turn half a second ahead.
float hoverBank(const RoutePlace& c, const Route& route)
{
	const Vec3 s = basisOf(c.rotation).s;
	RoutePlace ahead = c;
	ahead.advance(route, 500);
	const Vec3 f = basisOf(ahead.rotation).f;
	const float d = std::clamp(((f.z * s.z + f.y * s.y) + f.x * s.x) * 8, -1.0f, 1.0f);
	return 0.78539819f * d;
}

// The visible pose at the attach point; the body keeps its own until the first advance.
void attached(Kart& k, Pose& vis, const KartParams& p, Vec3 position, Quat rotation)
{
	vis.position = position;
	vis.rotation = basisOf(rotation);
	k.centerOfMassWorld = toWorld(vis.rotation, p.centerOfMass) + position;
	for (int i = 0; i < 4; i++) {
		k.bodyPoints[i] = toWorld(vis.rotation, p.bodyPoints[i]) + position;
	}
}

} // namespace

bool Route::load(const Cache& cache, std::string_view path, bool mirrored)
{
	*this = {};
	RouteFile file;
	if (!cache.get(path, file) || size_t(file.loopPoint) + 1 >= file.points.size()) {
		return false;
	}
	// Mirrored: y negated (positions, and the rotations' y and w), the widths swapped.
	const float flip = mirrored ? -1.0f : 1.0f;
	auto quat = [&](const std::array<float, 4>& q) { return Quat{q[0], q[1] * flip, q[2], q[3] * flip}; };
	start = vec(file.start);
	start.y *= flip;
	loop = vec(file.loop);
	loop.y *= flip;
	startRotation = quat(file.startRotation);
	loopRotation = quat(file.loopRotation);
	loopTime = file.loopTime;
	loopPoint = file.loopPoint;
	for (const RouteFile::Point& f : file.points) {
		// Negated in their own widths.
		const int16_t dy = mirrored ? int16_t(-f.dy) : f.dy;
		const int8_t qy = mirrored ? int8_t(-f.rotation[1]) : f.rotation[1];
		const int8_t qw = mirrored ? int8_t(-f.rotation[3]) : f.rotation[3];
		const int8_t left = mirrored ? f.right : f.left, right = mirrored ? f.left : f.right;
		constexpr float Unit = 0.007874016f;
		const int32_t contacts = f.kind >> 6;
		points.push_back({{float(f.dx) * 0.00390625f, float(dy) * 0.00390625f, float(f.dz) * 0.0625f},
			{float(f.rotation[0]) * Unit, float(qy) * Unit, float(f.rotation[2]) * Unit, float(qw) * Unit}, float(left) * 0.125f,
			float(right) * 0.125f, (f.kind & 63) << 5, contacts == 3 ? 4 : contacts});
	}
	return true;
}

void RoutePlace::attachAtLoop(const Route& r)
{
	const Route::Point& at = r.points[size_t(r.loopPoint)];
	pa = position = r.loop;
	qa = rotation = r.loopRotation;
	ta = r.loopTime;
	time = float(r.loopTime);
	a = r.loopPoint;
	b = r.loopPoint + 1;
	contacts = at.contacts;
	left = at.left;
	right = at.right;
	pb = r.loop + r.points[size_t(b)].offset;
}

void RoutePlace::advance(const Route& r, float ms)
{
	const std::vector<Route::Point>& pts = r.points;
	time = (rate * ms) + time;
	int32_t target = int32_t(time);
	float startL, startR;
	if (a < 0) {
		// The first segment: from the start pose.
		startL = startR = 0;
		pa = r.start;
		qa = r.startRotation;
		ta = 0;
		pb = r.start + pts[size_t(b)].offset;
		qb = pts[size_t(b)].rotation;
		contacts = 4;
	}
	else {
		startL = pts[size_t(a)].left;
		startR = pts[size_t(a)].right;
	}
	float endL = pts[size_t(b)].left, endR = pts[size_t(b)].right;
	int32_t ts = ta, te = ts + pts[size_t(b)].length;
	if (target < ts) {
		// Back over earlier segments, but not before the first point. (A cursor on the first segment never goes back.)
		if (a <= 0) {
			target = 0;
			time = 0;
		}
		else {
			for (;;) {
				te = ts;
				b = a;
				pb = pa;
				qb = qa;
				endL = startL;
				endR = startR;
				const Route::Point& was = pts[size_t(a)];
				pa -= was.offset;
				ts = ta - was.length;
				a -= 1;
				ta = ts;
				const Route::Point& now = pts[size_t(a)];
				contacts = now.contacts;
				startL = now.left;
				startR = now.right;
				qa = now.rotation;
				if (target >= ts) {
					break;
				}
				if (a == 0) {
					target = 0;
					time = 0;
					break;
				}
			}
		}
	}
	else if (target >= te) {
		do {
			ta = te;
			a = b;
			pa = pb;
			qa = qb;
			startL = endL;
			startR = endR;
			b += 1;
			if (size_t(b) >= pts.size()) {
				// Past the last point: on from the loop. (te keeps counting from the old end: the first segment after
				// the wrap is timed wrong for this one advance.)
				pa = pb = r.loop;
				qa = qb = r.loopRotation;
				ta = r.loopTime;
				a = r.loopPoint;
				target += r.loopTime - te;
				b = r.loopPoint + 1;
				time = float(target);
			}
			const Route::Point& end = pts[size_t(b)];
			contacts = end.contacts;
			endL = end.left;
			endR = end.right;
			qb = end.rotation;
			pb += end.offset;
			te += end.length;
		} while (target >= te);
	}
	const float u = float(target - ta) / float(te - ta);
	left = (endL - startL) * u + startL;
	right = (endR - startR) * u + startR;
	position = (pb - pa) * u + pa;
	rotation = blend(qa, qb, u);
}

bool RoutePlace::seek(const Route& r, Vec3& d)
{
	const std::vector<Route::Point>& pts = r.points;
	const int32_t count = int32_t(pts.size());
	int32_t sa = a, sb = b, ts = ta, te = ts + pts[size_t(sb)].length;
	Vec3 spa = pa, spb = pb;
	auto isShort = [&] { return dotZYX(spb - spa, spb - spa) < 0.001f; };
	auto forward = [&] {
		spa = spb;
		ts = te;
		sa = sb;
		sb += 1;
		if (sb >= count) {
			return false;
		}
		te += pts[size_t(sb)].length;
		spb += pts[size_t(sb)].offset;
		return true;
	};
	auto back = [&] {
		spb = spa;
		sb = sa;
		te = ts;
		sa -= 1;
		if (sa < 0) {
			return false;
		}
		ts -= pts[size_t(sb)].length;
		spa -= pts[size_t(sb)].offset;
		return true;
	};
	while (isShort()) {
		if (!forward()) {
			return false;
		}
	}
	float len = lengthZYX(spb - spa);
	Vec3 u = (spb - spa) * (1 / len);
	auto along = [&] { return u.y * d.y + d.z * u.z + u.x * d.x; };
	float j = along(), amount;
	if (j < 0) {
		float behind = -j, rem = lengthZYX(position - spa);
		if (behind <= rem) {
			amount = rem - behind;
		}
		else {
			for (;;) {
				d = {d.x - (-rem) * u.x, d.y - (-rem) * u.y, d.z - (-rem) * u.z};
				do {
					if (!back()) {
						return false;
					}
				} while (isShort());
				len = lengthZYX(spb - spa);
				u = (spb - spa) * (1 / len);
				j = along();
				if (j > 0) {
					amount = len;
					break;
				}
				behind = -j;
				if (behind > len) {
					rem = len;
					continue;
				}
				amount = len - behind;
				break;
			}
		}
	}
	else {
		float rem = lengthZYX(position - spb);
		if (j <= rem) {
			amount = j + len - rem;
		}
		else {
			for (;;) {
				d = {d.x - rem * u.x, d.y - rem * u.y, d.z - rem * u.z};
				do {
					if (!forward()) {
						return false;
					}
				} while (isShort());
				len = lengthZYX(spb - spa);
				u = (spb - spa) * (1 / len);
				j = along();
				if (j < 0) {
					j = 0;
				}
				else if (j > len) {
					rem = len;
					continue;
				}
				rem = lengthZYX(position - spb);
				amount = j + len - rem;
				break;
			}
		}
	}
	ta = ts;
	pa = spa;
	pb = spb;
	time = float(te - ts) * (amount / len) + float(ts);
	a = sa;
	b = sb;
	qa = pts[size_t(sa)].rotation;
	qb = pts[size_t(sb)].rotation;
	advance(r, 0);
	return true;
}

void attachAtStart(Playback& pl, Kart& k, Pose& vis, const KartParams& p, const Route& r, int32_t slot)
{
	pl = {};
	pl.route = slot;
	pl.active = true;
	pl.paused = true;
	attached(k, vis, p, r.start, r.startRotation);
}

void attachAtLoop(Playback& pl, Kart& k, Pose& vis, const KartParams& p, const Route& r, int32_t slot)
{
	pl.route = slot;
	pl.active = true;
	pl.cursor.attachAtLoop(r);
	attached(k, vis, p, r.loop, r.loopRotation);
}

void stepPlayback(Playback& pl, Kart& k, Pose& vis, const KartParams& p, const Track& t, const Route& r, int32_t dt,
	std::vector<SweepHit>* hits)
{
	if (dt == 0 || pl.paused) {
		return;
	}
	RoutePlace& c = pl.cursor;
	if (k.flags & KartHover) {
		k.bankTarget = hoverBank(c, r);
	}
	if ((k.flags & KartBoost) && c.time < 2000) {
		pl.targetRate = (-((c.time - 2000) / 2000)) * (MaxRate - BoostRate) + BoostRate; // a start boost: 2.75 down to 1.7
	}
	saveBody(k, p, t);
	// One advance, then the sweep; on a wall the body is backed off, the cursor advances once more by the back-off
	// time (from where it already was), and the kart backs along its line.
	for (int32_t e = dt;;) {
		if (e != 0) {
			const Vec3 from = vis.position;
			const float ms = float(e);
			const uint32_t f = k.flags;
			if ((f & KartPushed) || !(f & (KartSpinning | KartBoost | KartCurseSlow)) || k.contacts == 0) {
				if (c.rate < pl.targetRate) {
					const float up = (f & KartPushed) ? 0.0020000001f : (f & KartBoost) ? 0.0099999998f : 0.00048828125f;
					c.rate = std::min(c.rate + up * ms, pl.targetRate);
				}
				else if (c.rate > pl.targetRate) {
					const float down = (f & KartPushed) ? 0.000732421875f : 0.0020000001f;
					c.rate = std::max(c.rate - down * ms, pl.targetRate);
				}
			}
			c.advance(r, ms);
			pl.side = ease(pl.side, 0, 0.001953125f * ms);
			if ((f & KartHover) || k.lift != 0) {
				k.lift = ease(k.lift, k.liftTarget, (k.liftRate * ms) * 0.001f);
				k.bank = ease(k.bank, k.bankTarget, ms * 0.005f);
			}
			if (pl.hopVelocity != 0 || pl.hop != 0) {
				const float s = ms * 0.001f;
				pl.hopVelocity += -100 * s;
				pl.hop += s * pl.hopVelocity;
				if (pl.hop <= 0) {
					pl.hop = 0;
					pl.hopVelocity = 0;
					k.flags &= ~KartSpinOut;
				}
			}
			orient(pl, k, vis, p, e);
			place(pl, k, vis, p);
			k.velocity = (vis.position - from) * (1 / ms);
			refreshStatistics(k);
			k.position = vis.position;
			k.rotation = vis.rotation;
		}
		if (k.wallHits != 0) {
			c.rate = -0.1f;
			k.wallHits = 0;
			saveBody(k, p, t);
			return;
		}
		e = sweepBody(k, p, t, e, false, hits);
		if (k.wallHits == 0) {
			return;
		}
	}
}

void routeImpulse(Playback& pl, const Kart& k, Vec3 dir, float magnitude)
{
	float d = dotZYX(k.heading, dir);
	if (magnitude < 0) {
		d = -d;
		magnitude = -magnitude;
	}
	const float f = std::min(magnitude, 240.0f) / 240;
	const bool spinning = k.flags & KartSpinning;
	float& rate = pl.cursor.rate;
	if (d >= 0) {
		rate = std::min((spinning ? SpinRate + 0.1f : rate + f) + (1 - d) * 0.050000001f, MaxRate);
	}
	else {
		rate = std::max((spinning ? SpinRate - 0.1f : rate - f) - (d + 1) * 0.25f, MinRate);
	}
}

void routeMove(Playback& pl, Kart& k, Pose& vis, const KartParams& p, const Route& r, Vec3 moved)
{
	pl.cursor.seek(r, moved);
	orient(pl, k, vis, p, 0);
	// What the line didn't take goes sideways, within the line's widths. (It replaces the offset, not adds to it.)
	const Vec3 s = vis.rotation.s;
	const float side = moved.z * s.z + moved.y * s.y + moved.x * s.x;
	pl.side = side > 0 ? std::min(side, pl.cursor.right) : std::max(side, -pl.cursor.left);
	place(pl, k, vis, p);
}

void routeSpin(Playback& pl, Kart& k, float turns, float rate)
{
	if (k.flags & KartSpinning) {
		return;
	}
	k.yawMs = int32_t((turns * 6.2831855f) / rate);
	pl.spinRate = rate;
	pl.spinAngle = 0;
	k.flags |= KartSpinning;
	if (k.contacts != 0) {
		pl.cursor.rate = SpinRate;
	}
}

void routeSpinOut(Playback& pl, Kart& k)
{
	k.flags |= KartSpinOut;
	k.spinOutMs = 600;
	pl.hopVelocity = 50;
	pl.cursor.rate = 0.1f;
}

void routeTilt(Playback& pl, Kart& k, float rate, int32_t ms)
{
	if (k.pitchMs == 0) {
		pl.tiltAngle = 0;
		pl.tiltRate = -rate;
		k.pitchMs = ms;
	}
}

void routePush(Playback& pl, Kart& k, Vec3 q)
{
	k.flags |= KartPushed;
	const float d = dotZYX(k.heading, q);
	const bool spinning = k.flags & KartSpinning;
	if (d >= 0) {
		pl.targetRate = std::min(spinning ? SpinRate + 0.1f : d / 300 + pl.cursor.rate, MaxRate);
	}
	else {
		pl.targetRate = std::max(spinning ? SpinRate - 0.1f : d / 300 + pl.cursor.rate, MinRate);
	}
}

void routeEndPush(Playback& pl, Kart& k)
{
	k.flags &= ~KartPushed;
	if (k.flags & KartSpinning) {
		pl.targetRate = SpinRate;
		return;
	}
	if (pl.baseRate < 0) {
		pl.baseRate = 1;
	}
	pl.targetRate = pl.baseRate;
}

namespace {

// Back to the usual rate once nothing else holds it.
void releaseRate(Playback& pl, const Kart& k)
{
	if (!(k.flags & (KartSpinning | KartBoost | KartPushed))) {
		pl.targetRate = 1;
	}
}

} // namespace

void routeBoost(Playback& pl, Kart& k, bool on)
{
	if (on) {
		pl.cursor.rate = BoostRate; // (the body has ignored a boost while cursed: so has this)
		return;
	}
	releaseRate(pl, k);
}

void routeCurse(Playback& pl, Kart& k, bool on)
{
	if (on) {
		k.flags |= KartCurseSlow;
		pl.cursor.rate = 0.5f;
		return;
	}
	k.flags &= ~KartCurseSlow;
	releaseRate(pl, k);
}

void routeGhost(Playback& pl, Kart& k, bool on)
{
	if (on) {
		k.flags |= KartHover | KartBoost | KartRouteGhost;
		pl.cursor.rate = 4.5f;
		k.liftTarget = 6;
		k.bankTarget = 0;
		k.liftRate = 3;
		return;
	}
	pl.cursor.rate = pl.baseRate;
	k.liftTarget = k.bankTarget = 0;
	k.liftRate = 15;
	k.flags &= ~(KartHover | KartBoost | KartRouteGhost);
}

void routeHover(Playback& pl, Kart& k, bool on)
{
	if (on) {
		if (!(k.flags & KartHover)) {
			routeTilt(pl, k, -0.003f, 250);
			k.lift = 0;
			k.flags |= KartHover;
			k.bank = k.bankTarget = 0;
			k.liftTarget = 6;
			k.liftRate = 3;
		}
		return;
	}
	k.liftTarget = k.bankTarget = 0;
	k.liftRate = 15;
	k.flags &= ~KartHover;
}

} // namespace lr
