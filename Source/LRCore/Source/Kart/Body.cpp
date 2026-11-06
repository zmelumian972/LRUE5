// The kart body: a rigid box on four wheel rays.
//
// A step runs its timers, then substeps: forces, integration, a tilt limit, the wheel probes that snap the box onto
// the ground, the heading, the velocity statistics and the wheels' surfaces. After each substep the box's corners are
// swept from where they were at the last save to where they are now; on a wall the state goes back to the save and a
// shorter piece is run (5 ms short of the hit), then the bounce is applied and the rest of the time runs from there.

#include "Kart/Body.h"
#include "Kart/BodyParts.h"

#include "LRCore/Race/Track.h"
#include "Kart/Steering.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lr {

namespace {

constexpr Vec3 BoxSize = {8.0f, 5.0f, 6.2f};
constexpr int32_t YawKickMs = 200;
constexpr int32_t WallBackOffMs = 5;

// The forces' coefficients (per unit mass): the sideways slip damped by the wheels' grip, less while spinning or yaw
// locked; the engine brake with no thrust.
constexpr float LateralGrip = 0.01f, LockedLateralGrip = 0.002f;
constexpr float EngineBrake = 0.001f;
// Not steering, a kart creeping faster than this but slower than the slow yaw speed turns at the slow yaw speed's rate.
constexpr float CreepSpeed = 0.0005f, SlowYawSpeed = 0.03f;
constexpr float RestingTurn = 0.0006f; // a turn this small (squared) with no torque stops the spin
constexpr float MaxTiltCos = 0.70710677f, MaxTilt = 0.7853982f; // the tilt clamped to 45 degrees
// The hover banks into a turn, by up to 45 degrees for the tightest, none from this turn radius on.
constexpr float HoverBankRadius = 4096, MaxHoverBank = MaxTilt;
constexpr float HeadingSnap = 0.98f; // a heading this close to its target takes it
constexpr float HeadingTurnRate = 0.0025f; // radians per ms
constexpr float RollingSpeed = 0.008f; // the wheels' particles from this heading speed
// A wall hit: the yaw kick away from it (nose in), the rebound off it (less upward), the push out, the most upward speed.
constexpr float WallYawKick = 0.004f;
constexpr float WallRebound = 0.3f, WallReboundUp = 0.15f;
constexpr float WallPush = 0.004f, WallMaxRise = 0.3f;

// Out of the world below or above these heights, the kart goes back to its last good pose.
constexpr float WorldFloor = -250, WorldCeiling = 340;

// Surface defaults for a wheel off the ground or on a plain material.
constexpr float PlainSlopeHold = 0.5f, PlainFriction = 0.25f, PlainGrip = 3.0f, PlainRolling = 0.0f;

} // namespace

Basis worldInverseInertia(const Basis& r, Vec3 inv)
{
	// rotation^T * diag(inv) * rotation, rows.
	auto row = [&](float fi, float si, float ui) { return r.f * (inv.x * fi) + r.s * (inv.y * si) + r.u * (inv.z * ui); };
	return {row(r.f.x, r.s.x, r.u.x), row(r.f.y, r.s.y, r.u.y), row(r.f.z, r.s.z, r.u.z)};
}

namespace {

// The wheels from the kart's pose: the anchor's, the one across the track from it, those back along the wheelbase;
// with the axles' middles if asked.
void wheelsAt(Kart& k, const KartParams& p, bool axles)
{
	Wheel* w = k.wheels;
	w[1].position = toWorld(k.rotation, p.anchor) + k.position;
	w[0].position = w[1].position - k.rotation.s * p.trackWidth;
	w[3].position = w[1].position - k.rotation.f * p.wheelbase;
	w[2].position = w[0].position - k.rotation.f * p.wheelbase;
	if (axles) {
		k.axles[0] = (w[0].position + w[1].position) * 0.5f;
		k.axles[1] = (w[2].position + w[3].position) * 0.5f;
	}
}

// Momentum.
void cancelAlong(Kart& k, Vec3 axis) { k.angularMomentum -= axis * dot(k.angularMomentum, axis); }
// The momentum that spins the body at rate (rad/ms, world) about its principal axes.
void spinAt(Kart& k, const KartParams& p, Vec3 rate)
{
	const Vec3 local = toLocal(k.rotation, rate);
	k.angularMomentum += toWorld(k.rotation, {p.inertia.x * local.x, p.inertia.y * local.y, p.inertia.z * local.z});
}
// The four wheels' mean of a field, summed in wheel order.
float wheelMean(const Wheel* w, float Wheel::* field) { return (w[0].*field + w[1].*field + w[2].*field + w[3].*field) * 0.25f; }
// v a step toward target, not past it.

} // namespace

void yawKick(Kart& k, const KartParams& p, float rate, int32_t ms)
{
	k.yawMs = ms;
	cancelAlong(k, k.rotation.u);
	spinAt(k, p, k.rotation.u * rate);
}

void refreshStatistics(Kart& k)
{
	if (isZero(k.velocity)) {
		k.speed = 0;
		k.velocityDirection = k.rotation.f;
		k.flags = isZero(k.angularVelocity) ? k.flags | KartAtRest : k.flags & ~KartAtRest;
	}
	else {
		k.speed = length(k.velocity);
		k.velocityDirection = normalize(k.velocity);
		k.flags &= ~KartAtRest;
	}
	k.headingSpeed = dotZYX(k.velocity, k.heading);
	k.forwardVelocity = k.heading * k.headingSpeed;
	k.lateralVelocity = k.velocity - k.forwardVelocity;
}

void Body::accumulateForces()
{
	const float g = gravityForce();
	m_kart.yawRate = 0;
	float align = 0;
	if (m_kart.flags & KartSteering) {
		align = dot(m_kart.rotation.f, m_kart.velocityDirection);
		if (align <= 0) {
			stopSteering();
		}
	}

	if (m_kart.contacts == 0) {
		// Airborne: four times the gravity, the thrust along the heading but never lifting.
		addForce({0, 0, 4 * g});
		Vec3 thrust = m_kart.heading * (m_kart.thrust * m_params.massScale);
		thrust.z = std::min(thrust.z, -g);
		addForce(thrust);
		if (m_kart.turnRadius != 0 && !(m_kart.flags & KartYawLock)) {
			yawImpulse(m_kart.headingSpeed / m_kart.turnRadius, YawKickMs);
		}
	}
	else {
		groundForces(align);
	}

	const Wheel* w = m_kart.wheels;
	const bool hover = m_kart.flags & KartHover;
	if (!(m_kart.flags & (KartHover | KartSpinOut))) {
		addForce((((w[0].push + w[1].push) + w[2].push) + w[3].push) * (m_params.massScale * 0.25f));
	}
	const float rolling = hover ? 0.0f : wheelMean(w, &Wheel::rolling);
	addForce(m_kart.velocity * -((rolling + m_kart.dragCoefficient) * m_kart.speed));
	if (m_kart.flags & KartPushA) {
		addForce(m_kart.pushes[0]);
	}
	if (m_kart.flags & KartPushB) {
		addForce(m_kart.pushes[1]);
	}
}

// On the ground: the slope, the wheels' support (no tipping), the slide's friction, the grip, the thrust or the engine
// brake, and the turn's pull and yaw. (align: the heading against the velocity's direction, steering.)
void Body::groundForces(float align)
{
	const float gm = Gravity * m_params.massScale;
	const Basis& r = m_kart.rotation;
	const Wheel* w = m_kart.wheels;
	const float slopeHold = wheelMean(w, &Wheel::slopeHold);
	const float friction = wheelMean(w, &Wheel::friction);
	// Gravity along the ground, unless the surface holds the kart on the slope.
	if (length(m_kart.slopeForce) > gm * slopeHold) {
		addForce(m_kart.slopeForce);
	}
	if (m_kart.flags & KartHover) {
		// The two axle probes: no tipping over the other axle.
		for (int i = 0; i < 2; i++) {
			if (w[i].contact) {
				cancelPositiveAbout(m_kart.groundNormal, m_kart.axles[1 - i]);
			}
		}
	}
	else if (m_kart.pitchMs == 0) {
		for (int i = 0; i < 4; i++) {
			if (w[i].contact) {
				if (m_kart.contacts < 3) {
					addForceAt(w[i].force, w[i].position);
				}
				cancelPositiveAbout(m_kart.groundNormal, w[3 - i].position); // the diagonal wheel: no tipping over it
			}
		}
	}
	float grip = 1;
	if (m_kart.flags & (KartSteering | KartSpinning)) {
		addForce(m_kart.velocityDirection * -((gm * friction) * m_kart.slipRatio));
		grip = 1 - m_kart.slipRatio;
	}
	if (!(m_kart.flags & (KartSpinning | KartYawLock))) {
		addForce(m_kart.lateralVelocity * ((m_params.mass * -LateralGrip) * grip));
		if (m_kart.thrust != 0) {
			const Vec3 along = m_kart.contacts >= 3 && !(m_kart.flags & KartSteering) ? r.f : m_kart.heading;
			addForce(along * (m_kart.thrust * m_params.massScale));
		}
		else {
			addForce(m_kart.forwardVelocity * (m_params.mass * -EngineBrake)); // engine brake
		}
		if (m_kart.turnRadius != 0) {
			const Vec3 left = normalize(Vec3{-m_kart.heading.y, m_kart.heading.x, 0});
			const float fs = m_kart.headingSpeed;
			addForce(left * (((m_params.mass * fs) * fs) / m_kart.turnRadius));
			float yaw;
			if (m_kart.flags & KartSteering) {
				yaw = 0;
				if (align >= m_kart.steerAlignMin) {
					m_kart.yawRate = fs / m_kart.turnRadius;
					yaw = m_kart.steerGain * m_kart.yawRate;
				}
			}
			else {
				m_kart.yawRate = fs > CreepSpeed && fs < SlowYawSpeed ? SlowYawSpeed / m_kart.turnRadius : fs / m_kart.turnRadius;
				yaw = m_kart.yawRate;
			}
			yawImpulse(yaw, YawKickMs);
		}
	}
	else {
		addForce(m_kart.lateralVelocity * ((m_params.mass * -LockedLateralGrip) * grip));
	}
}

void Body::integrate(float dt)
{
	m_kart.angularVelocity = toLocal(m_kart.invInertiaWorld, m_kart.angularMomentum);
	const Vec3 dv = {m_forceSum.x * dt * m_params.invMass, m_forceSum.y * dt * m_params.invMass, m_forceSum.z * dt * m_params.invMass};
	m_kart.position += m_kart.velocity * dt + dv * (dt * 0.5f);
	if (isZero(dv)) {
		// Creeping to a halt: tiny velocities snap to zero.
		for (float* c : {&m_kart.velocity.x, &m_kart.velocity.y, &m_kart.velocity.z}) {
			if (*c > -1e-4f && *c < 1e-4f) {
				*c = 0;
			}
		}
	}
	m_kart.velocity += dv;
	const Vec3 turn = m_kart.angularVelocity * dt;
	const Vec3 f = m_kart.rotation.f + cross(turn, m_kart.rotation.f);
	const Vec3 u = m_kart.rotation.u + cross(turn, m_kart.rotation.u);
	m_kart.rotation = basisFromForwardUp(f, u);
	m_kart.angularMomentum += m_torqueSum * dt;
	if (isZero(m_torqueSum) && lengthSquared(turn) < RestingTurn) {
		m_kart.angularVelocity = {};
		m_kart.angularMomentum = {};
	}
	m_forceSum = {};
	m_torqueSum = {};
}

void Body::limitTilt()
{
	const Vec3 up = m_kart.rotation.u;
	if (up.z < MaxTiltCos) {
		// Keep the direction of the tilt, clamp it to 45 degrees.
		const Vec3 a = normalize(Vec3{up.y, -up.x, 0});
		const float s = sinPrecise(MaxTilt), c = cosPrecise(MaxTilt);
		m_kart.rotation = basisFromUpForward({-s * a.y, s * a.x, c}, m_kart.rotation.f);
	}
}

void Body::placeWheels() { wheelsAt(m_kart, m_params, true); }

void Body::turnHeadingToward(Vec3 target, float dt)
{
	const float d = dot(m_kart.heading, target);
	if (d > HeadingSnap) {
		m_kart.heading = target;
		return;
	}
	const float step = dt * HeadingTurnRate;
	if (step > tableAcos(d)) {
		m_kart.heading = target;
		return;
	}
	m_kart.heading = normalize(turnAbout(m_kart.heading, cross(target, m_kart.heading), step));
}

void Body::updateHeading(int32_t dt)
{
	const float fdt = float(dt);
	const Vec3 f = m_kart.rotation.f;
	if (m_kart.contacts == 0 || m_kart.headingSpeed < 0.03f) {
		turnHeadingToward(f, fdt);
	}
	else if (m_kart.contacts <= 2) {
		const Vec3 n = m_kart.groundNormal;
		turnHeadingToward(normalize(f - n * dot(f, n)), fdt);
	}
	else if ((m_kart.flags & KartSteering) && m_kart.thrust > 0) {
		// Steering under power: the heading lags the nose by the turn, up to the lag limit.
		const float yaw = m_kart.yawRate * fdt;
		const float behind = tableAcos(std::min(1.0f, dot(m_kart.heading, f))) + std::fabs(yaw);
		if (behind > m_kart.lagLimit) {
			m_kart.heading = normalize(turnAbout(f, cross(m_kart.heading, f), m_kart.lagLimit));
		}
		else {
			const float c = cosPrecise(yaw), s = sinPrecise(yaw);
			Vec3& h = m_kart.heading;
			h.x = h.x * c - h.y * s;
			h.y = h.y * c + h.x * s; // with the new x, as the game does
			h = normalize(h);
		}
	}
	else {
		turnHeadingToward(f, fdt);
	}
}

void Body::updateWheelSurfaces()
{
	// Each wheel's surface for its particle: in contact, the body neither hovering nor spun out,
	// going forward.
	if (m_surfaces) {
		std::array<int16_t, 4>& s = m_surfaces->emplace_back();
		const bool rolling = !(m_kart.flags & (KartHover | KartSpinOut)) && m_kart.headingSpeed > RollingSpeed;
		for (size_t i = 0; i < 4; i++) {
			s[i] = rolling && m_kart.wheels[i].contact && m_kart.wheels[i].surface >= 0 ? m_kart.wheels[i].surface : int16_t(-1);
		}
	}
	const bool ignore = m_kart.flags & KartIgnoreSurfaces;
	for (Wheel& w : m_kart.wheels) {
		const Surface* s = w.surface >= 0 && w.contact ? &m_track.surfaces[w.surface] : nullptr;
		auto pick = [&](uint32_t has, float value, float fallback) { return s && (s->flags & has) && !ignore ? value : fallback; };
		w.rolling = pick(SurfaceHasRolling, s ? s->rolling : 0, PlainRolling);
		w.slopeHold = pick(SurfaceHasSlopeHold, s ? s->slopeHold : 0, PlainSlopeHold);
		w.friction = pick(SurfaceHasFriction, s ? s->friction : 0, PlainFriction);
		w.grip = pick(SurfaceHasGrip, s ? s->grip : 0, PlainGrip);
		w.push = s && (s->flags & SurfaceHasPush) && !ignore ? s->push : Vec3{};
	}
}

void Body::substep(int32_t dt)
{
	accumulateForces();
	integrate(float(dt));
	limitTilt();
	if (m_kart.flags & KartHover) {
		probeHover(dt);
	}
	else {
		probeGround(dt);
	}
	updateHeading(dt);
	refreshStatistics(m_kart);
	updateWheelSurfaces();
	m_kart.centerOfMassWorld = toWorld(m_kart.rotation, m_params.centerOfMass) + m_kart.position;
	m_kart.invInertiaWorld = worldInverseInertia(m_kart.rotation, m_params.invInertia);
}

// What a save keeps, from the kart to its save or back (the two name it alike).
namespace {

template <class To, class From> void copySaved(To& to, const From& from)
{
	to.position = from.position;
	to.velocity = from.velocity;
	to.rotation = from.rotation;
	to.invInertiaWorld = from.invInertiaWorld;
	to.angularVelocity = from.angularVelocity;
	to.angularMomentum = from.angularMomentum;
	to.heading = from.heading;
	to.lift = from.lift;
	to.bank = from.bank;
	for (int i = 0; i < 4; i++) {
		auto& w = to.wheels[i];
		const auto& f = from.wheels[i];
		w.position = f.position;
		w.push = f.push;
		w.rolling = f.rolling;
		w.slopeHold = f.slopeHold;
		w.friction = f.friction;
		w.grip = f.grip;
		w.surface = f.surface;
		w.contact = f.contact;
		w.touched = f.touched;
		to.bodyPoints[i] = from.bodyPoints[i];
	}
}

} // namespace

void Body::save() { copySaved(m_kart.saved, m_kart); }

void Body::restore()
{
	copySaved(m_kart, m_kart.saved);
	m_kart.centerOfMassWorld = toWorld(m_kart.rotation, m_params.centerOfMass) + m_kart.position;
	refreshStatistics(m_kart);
}

// What the material hit is: a checkpoint's gate (the race counts it, the kart passes), a solid wall, or a surface,
// which can be the finish line or start a touch event for the race and which stops the kart unless non-solid.
bool Body::acceptsWall(const SegmentHit& hit, Vec3 worldNormal, Vec3 worldPoint) const
{
	if (hit.checkpoint >= 0) {
		if (m_hits) {
			const Vec3 n = m_track.checkpoints[hit.checkpoint].normal;
			m_hits->push_back({hit.checkpoint, !(dotZYX(worldNormal, n) < 0), false, -1, worldPoint});
		}
		return false;
	}
	if (hit.surface < 0) {
		return true;
	}
	const Surface& s = m_track.surfaces[hit.surface];
	const bool finish = s.flags & SurfaceFinishLine, touch = s.flags & SurfaceHasTouchEvent;
	if (m_hits && (finish || touch)) {
		m_hits->push_back({-1, false, finish, touch ? s.touchEvent : -1, worldPoint});
	}
	return !(s.flags & SurfaceNonSolid);
}

int32_t Body::sweep(int32_t dt, bool queryTrack)
{
	m_kart.wallHits = 0;
	float best = -FLT_MAX, fraction = 0;
	bool marked[4] = {};
	for (int i = 0; i < 4; i++) {
		m_kart.bodyPoints[i] = toWorld(m_kart.rotation, m_params.bodyPoints[i]) + m_kart.position;
	}
	// Each corner from where it was at the save to where it is now. Only a hit deeper than the best so far counts.
	auto consider = [&](int i, Vec3 end, const SegmentHit& hit, Vec3 worldNormal) {
		const float depth2 = lengthSquared(end - hit.point);
		if (depth2 > best) {
			best = depth2;
			m_kart.wallNormal = worldNormal;
			marked[i] = true;
			fraction = hit.fraction;
			m_kart.wallHits++;
		}
	};
	for (int32_t slot = 1; slot < m_kart.worldCount; slot++) {
		const uint32_t volume = uint32_t(m_kart.worlds[slot]);
		const Track::Volume& v = m_track.volumes[volume];
		for (int i = 0; i < 4; i++) {
			if (marked[i]) {
				continue;
			}
			const Vec3 a = toLocal(v.rotation, m_kart.saved.bodyPoints[i] - v.position);
			const Vec3 b = toLocal(v.rotation, m_kart.bodyPoints[i] - v.position);
			SegmentHit hit;
			if (m_track.query(volume, a, b, hit) && dot(hit.point - b, hit.normal) > 0) {
				const Vec3 normal = toWorld(v.rotation, hit.normal);
				if (acceptsWall(hit, normal, toWorld(v.rotation, hit.point) + v.position)) {
					consider(i, b, hit, normal);
				}
			}
		}
	}
	if (queryTrack) {
		for (int i = 0; i < 4; i++) {
			if (marked[i]) {
				continue;
			}
			const Vec3 a = m_kart.saved.bodyPoints[i], b = m_kart.bodyPoints[i];
			SegmentHit hit;
			if (m_track.query(m_track.trackVolume, a, b, hit) && dot(hit.point - b, hit.normal) >= 0 &&
				acceptsWall(hit, hit.normal, hit.point)) {
				consider(i, b, hit, hit.normal);
			}
		}
	}
	if (m_kart.wallHits == 0) {
		return 0;
	}
	const int32_t rerun = int32_t(float(dt) * fraction);
	restore();
	return rerun < WallBackOffMs ? 0 : rerun - WallBackOffMs;
}

void Body::bounceOffWall()
{
	const Vec3 n = m_kart.wallNormal;
	if (m_kart.flags & KartSteering) {
		stopSteering();
	}
	const float into = dotZYX(m_kart.velocity, n);
	if (into < 0) {
		m_kart.velocity -= n * into;
	}
	if (!(m_kart.flags & KartSpinning) && dot(m_kart.rotation.f, n) < 0) {
		// Nose into the wall: a yaw kick away from it, harder the more head-on.
		const float q = dot(m_kart.rotation.s, n);
		const float rate = q < 0 ? -WallYawKick * (((q + 1) * 0.5f) + 0.5f) : (((1 - q) * 0.5f) + 0.5f) * WallYawKick;
		yawImpulse(rate, YawKickMs);
		m_kart.flags |= KartYawLock;
	}
	if (into < 0) {
		m_kart.velocity.x -= n.x * (WallRebound * into);
		m_kart.velocity.y -= n.y * (WallRebound * into);
		m_kart.velocity.z -= (WallReboundUp * into) * n.z;
	}
	m_kart.velocity += n * WallPush;
	if (!(m_kart.velocity.z < WallMaxRise)) {
		m_kart.velocity.z = WallMaxRise;
	}
}

void Body::runPieces(int32_t time)
{
	int32_t e = time, remainder = time;
	bool recovered = false;
	save();
	for (;;) {
		if (m_kart.wallHits == 0 && e == 0) {
			break;
		}
		if (e != 0) {
			substep(e);
		}
		if (m_kart.wallHits != 0) {
			if (e != 0) {
				const int32_t rerun = sweep(e, !(m_kart.flags & KartGhost));
				if (m_kart.wallHits != 0) {
					e = rerun; // still hitting: back up further
					continue;
				}
			}
			// The piece ran clean (or there was nothing left to run): bounce here and run the rest.
			bounceOffWall();
			refreshStatistics(m_kart);
			recovered = true;
			m_kart.wallHits = 0;
			save();
			remainder -= e;
			e = remainder;
			continue;
		}
		e = sweep(e, !(m_kart.flags & KartGhost));
		if (e != 0 || !recovered) {
			continue;
		}
		break; // a hit right at the restored pose after a bounce: the rest of the time is lost
	}
	if (recovered) {
		m_kart.wallHits = 1;
	}
}

// A step's timers: the spin-out, the spin (its last piece cuts the step short: the ms the step runs, returned) or
// the yaw lock, the spin alignment and the pitch lock.
namespace {

int32_t stepTimers(Kart& k, int32_t dt)
{
	if (k.flags & KartSpinOut) {
		if (dt >= k.spinOutMs) {
			k.flags &= ~KartSpinOut;
			k.spinOutMs = 0;
		}
		else {
			k.spinOutMs -= dt;
		}
	}
	int32_t e = dt;
	if (k.flags & KartSpinning) {
		if (k.flags & KartSpinFresh) {
			k.flags &= ~KartSpinFresh;
		}
		else if (dt >= k.yawMs) {
			if (k.yawMs != 0) {
				e = k.yawMs; // the spin's last piece: the rest of the step isn't run
				k.yawMs = 0;
			}
			else {
				endSpin(k);
			}
		}
		else {
			k.yawMs -= dt;
		}
	}
	else if (dt >= k.yawMs) {
		k.yawMs = 0;
		cancelAlong(k, k.rotation.u);
		k.flags &= ~KartYawLock;
	}
	else {
		k.yawMs -= dt;
	}
	k.spinAlignMs = e > k.spinAlignMs ? 0 : k.spinAlignMs - e;
	if (k.pitchMs != 0) {
		if (e >= k.pitchMs) {
			k.pitchMs = 0;
			cancelAlong(k, k.rotation.s);
		}
		else {
			k.pitchMs -= e;
		}
	}
	return e;
}

} // namespace

void Body::step(int32_t dt, Pose* drawn)
{
	m_kart.wallHits = 0;
	const int32_t e = stepTimers(m_kart, dt);
	if (m_kart.flags & KartHover) {
		const float r = m_kart.turnRadius;
		// Leaning into the turn, less the wider it is; none going straight.
		const float lean = (1 - std::min(1.0f, std::fabs(r) / HoverBankRadius)) * MaxHoverBank;
		m_kart.bankTarget = r == 0 ? 0 : std::copysign(lean, -r);
	}
	runPieces(e);
	m_kart.airborneMs = m_kart.contacts == 0 ? m_kart.airborneMs + dt : 0;

	// The drawn car leans into a powerslide (or with a hover's bank).
	float lean = 0;
	if (m_kart.flags & KartHover) {
		lean = -m_kart.bank;
	}
	else if ((m_kart.flags & KartPowerslide) && std::fabs(m_kart.turnRadius) >= 40) {
		lean = std::clamp(((((1 / m_kart.turnRadius) * -1) * 40) * m_kart.slideFactor) * ((m_kart.speed * 1000) / m_kart.maxSpeed), -0.5f,
			0.5f);
	}
	const float easing = float(dt) * 0.002f;
	m_kart.visualBank = easeToward(m_kart.visualBank, lean, easing);
	if (drawn) {
		*drawn = drawnPose(m_kart, m_params);
	}

	// Race checks: remember a good pose; go back to it out of the world, or while held on the grid.
	const bool held = m_kart.preStart && m_kart.engineStarted;
	if (m_kart.contacts == 4 && m_kart.wallHits == 0 && !held) {
		m_kart.resetPosition = m_kart.position;
		m_kart.resetRotation = m_kart.rotation;
	}
	if (m_kart.position.z < WorldFloor || m_kart.position.z > WorldCeiling || held) {
		m_kart.position = m_kart.resetPosition;
		m_kart.rotation = m_kart.resetRotation;
		m_kart.velocity = {};
		m_kart.angularMomentum = {};
	}
}

KartParams makeKartParams(const KartSource& src)
{
	KartParams p;
	p.mass = src.mass;
	p.invMass = 1 / p.mass;
	p.massScale = (p.mass * 0.001f) * 0.001f;
	p.inertia = boxInertia(p.mass, BoxSize);
	p.invInertia = {1 / p.inertia.x, 1 / p.inertia.y, 1 / p.inertia.z};
	p.centerOfMass = src.centerOfMass;
	p.anchor = src.frontLeftWheel;
	p.trackWidth = src.leftY - src.rightY;
	p.wheelbase = src.frontLeftWheel.x - src.rearX;
	const float hl = src.shadowLength * 0.5f, hw = src.shadowWidth * 0.5f;
	p.bodyPoints[0] = {hl, -hw, 3.5f};
	p.bodyPoints[1] = {hl, hw, 3.5f};
	p.bodyPoints[2] = {-hl, -hw, 3.5f};
	p.bodyPoints[3] = {-hl, hw, 3.5f};
	const int32_t weight = int32_t(float(src.pieceWeight) * 0.05f);
	p.handling = std::min(100, src.handling + weight);
	p.topSpeed = std::min(100, src.topSpeed + weight);
	p.acceleration = std::max(0, src.acceleration - weight);
	p.handlingScale = float(p.handling) * 0.003f + 0.7f;
	p.accelerationScale = 1 - float(50 - p.acceleration) * 0.001f;
	p.topSpeedScale = 1 - float(50 - p.topSpeed) * 0.001f;
	return p;
}

void placeKart(Kart& k, const KartParams& p, Vec3 position, Basis rotation)
{
	k = {};
	k.position = position;
	k.rotation = rotation;
	k.invInertiaWorld = worldInverseInertia(rotation, p.invInertia);
	k.heading = rotation.f;
	k.gripScale = 1;
	k.maxSpeed = p.topSpeedScale * 120.0f;
	k.centerOfMassWorld = toWorld(rotation, p.centerOfMass) + position;
	wheelsAt(k, p, false);
	for (int i = 0; i < 4; i++) {
		k.wheels[i].surface = -1;
		k.wheels[i].cacheSlot = -1;
		k.bodyPoints[i] = toWorld(rotation, p.bodyPoints[i]) + position;
	}
	k.steerGain = 1;
	k.steerAlignMin = -1;
	k.lagLimit = Pi;
	k.resetPosition = position;
	k.resetRotation = rotation;
	k.liftTarget = 6;
	k.liftRate = 3;
	k.slideFactor = 0.2f;
}

void stepBody(Kart& kart, const KartParams& params, const Track& track, int32_t dt, std::vector<SweepHit>* hits, Pose* drawn,
	std::vector<std::array<int16_t, 4>>* surfaces)
{
	Body(kart, params, track, hits, surfaces).step(dt, drawn);
}

Pose drawnPose(const Kart& k, const KartParams& p)
{
	if (k.visualBank == 0) {
		return {k.position, k.rotation};
	}
	const Basis& r = k.rotation;
	const float rise = std::tan(std::fabs(k.visualBank)) * (p.trackWidth * 0.5f);
	return {k.position + Vec3{0, 0, rise}, basisFromForwardUp(r.f, turnAbout(r.u, r.f, k.visualBank))};
}

void saveBody(Kart& kart, const KartParams& params, const Track& track) { Body(kart, params, track).save(); }

void startSpin(Kart& k, const KartParams& p, float turns, float rate, float align)
{
	if (k.flags & KartSpinning) {
		return;
	}
	cancelAlong(k, k.rotation.u);
	const int32_t period = int32_t(Tau / rate);
	yawKick(k, p, rate, int32_t(float(period) * turns));
	k.flags = (k.flags & ~KartSteering) | KartSpinning | KartSpinFresh; // (no steering stop: its limits are kept)
	if (align > 0) {
		k.spinAlignMs = int32_t(float(period) * std::min(align, turns));
	}
}

void endSpin(Kart& k)
{
	if (!(k.flags & KartSpinning)) {
		return;
	}
	k.yawMs = 0;
	cancelAlong(k, k.rotation.u);
	k.flags &= ~(KartSpinning | KartSpinFresh | KartYawLock);
}

void pitchKick(Kart& k, const KartParams& p, float rate, int32_t ms)
{
	k.pitchMs = ms;
	cancelAlong(k, k.rotation.s);
	spinAt(k, p, k.rotation.s * rate);
}

void enterHover(Kart& k, const KartParams& p)
{
	k.flags |= KartHover;
	pitchKick(k, p, -0.003f, 150);
	k.lift = k.bank = k.bankTarget = 0;
	wheelsAt(k, p, true);
}

void leaveHover(Kart& k)
{
	k.flags &= ~KartHover;
	k.lift = k.bank = k.bankTarget = 0;
}

void startBoost(Kart& k, const KartParams& p, bool boosted)
{
	if (boosted) {
		if (k.flags & KartCurseSlow) {
			return;
		}
		k.flags |= KartBoost;
	}
	else {
		k.flags &= ~KartBoost;
	}
	k.gripScale = boosted ? 1.5f : 1.0f;
	k.maxSpeed = p.topSpeedScale * (boosted ? 176.0f : 120.0f);
	k.dragCoefficient = (p.mass * std::fabs(k.thrust)) / (k.maxSpeed * k.maxSpeed);
}

void snapHeading(Kart& k)
{
	k.heading = k.rotation.f;
	refreshStatistics(k);
}

int32_t sweepBody(Kart& kart, const KartParams& params, const Track& track, int32_t dt, bool queryTrack, std::vector<SweepHit>* hits)
{
	return Body(kart, params, track, hits).sweep(dt, queryTrack);
}

} // namespace lr
