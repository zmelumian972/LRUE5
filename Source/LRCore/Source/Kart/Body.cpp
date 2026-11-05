// The kart body: a rigid box on four wheel rays.
//
// A step runs its timers, then substeps: forces, integration, a tilt limit, the wheel probes that snap the box onto
// the ground, the heading, the velocity statistics and the wheels' surfaces. After each substep the box's corners are
// swept from where they were at the last save to where they are now; on a wall the state goes back to the save and a
// shorter piece is run (5 ms short of the hit), then the bounce is applied and the rest of the time runs from there.

#include "Kart/Body.h"

#include "LRCore/Race/Track.h"
#include "Kart/Steering.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lr {

namespace {

constexpr Vec3 BoxSize = {8.0f, 5.0f, 6.2f};
constexpr float GroundClearance = 0.2f;
constexpr float ProbeDepthPerMs = 0.04f;
constexpr float ContactBand = 0.4f; // wheels this far below the highest hit still touch
constexpr float DropOnLoss = 0.008f; // the instant fall when the ground is lost
constexpr int32_t YawKickMs = 200;
constexpr int32_t WallBackOffMs = 5;

// The forces' coefficients (per unit mass): the sideways slip damped by the wheels' grip, less while spinning or yaw
// locked; the engine brake with no thrust.
constexpr float LateralGrip = 0.0099999998f, LockedLateralGrip = 0.0020000001f;
constexpr float EngineBrake = 0.001f;
// Not steering, a kart creeping faster than this but slower than the slow yaw speed turns at the slow yaw speed's rate.
constexpr float CreepSpeed = 0.00050000002f, SlowYawSpeed = 0.03f;
constexpr float RestingTurn = 0.00060000003f; // a turn this small (squared) with no torque stops the spin
constexpr float MaxTiltCos = 0.70710677f, MaxTilt = 0.78539819f; // the tilt clamped to 45 degrees
constexpr float HeadingSnap = 0.98000002f; // a heading this close to its target takes it
constexpr float HeadingTurnRate = 0.0025f; // radians per ms
constexpr float RollingSpeed = 0.0080000004f; // the wheels' particles from this heading speed
// A wall hit: the yaw kick away from it (nose in), the rebound off it (less upward), the push out, the most upward speed.
constexpr float WallYawKick = 0.0040000002f;
constexpr float WallRebound = 0.30000001f, WallReboundUp = 0.15000001f;
constexpr float WallPush = 0.0040000002f, WallMaxRise = 0.30000001f;

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

// Momentum.
void cancelAlong(Kart& k, Vec3 axis) { k.angularMomentum -= axis * dot(k.angularMomentum, axis); }
// The momentum that spins the body at rate (rad/ms, world) about its principal axes.
void spinAt(Kart& k, const KartParams& p, Vec3 rate)
{
	const Vec3 local = toLocal(k.rotation, rate);
	k.angularMomentum += toWorld(k.rotation, {p.inertia.x * local.x, p.inertia.y * local.y, p.inertia.z * local.z});
}
void yawKick(Kart& k, const KartParams& p, float rate, int32_t ms)
{
	k.yawMs = ms;
	cancelAlong(k, k.rotation.u);
	spinAt(k, p, k.rotation.u * rate);
}

class Body {
public:
	Body(Kart& kart, const KartParams& params, const Track& track, std::vector<SweepHit>* hits = nullptr, std::vector<std::array<int16_t, 4>>* surfaces = nullptr)
		: k(kart)
		, p(params)
		, t(track)
		, m_hits(hits)
		, m_surfaces(surfaces)
	{
	}

	void step(int32_t dt, Pose* drawn);
	void save();
	void restore();
	int32_t sweep(int32_t dt, bool queryTrack);

private:
	float gravityForce() const { return -(Gravity * p.massScale); } // negative: down

	void cancelPositiveAbout(Vec3 direction, Vec3 point)
	{
		const Vec3 axis = normalize(cross(point - k.centerOfMassWorld, direction));
		const float along = dot(k.angularMomentum, axis);
		if (along >= 0) {
			k.angularMomentum -= axis * along;
		}
	}
	void yawImpulse(float rate, int32_t ms) { yawKick(k, p, rate, ms); }
	void stopSteering() { lr::stopSteering(k); }

	void addForce(Vec3 f) { m_forceSum += f; }
	void addForceAt(Vec3 f, Vec3 at) { m_torqueSum += cross(at - k.centerOfMassWorld, f); }

	void accumulateForces();
	void groundForces(float align);
	void integrate(float dt);
	void limitTilt();
	void placeWheels();
	void probeGround(int32_t dt);
	void consider(int i, int& best, float& bestDepth2) const;
	void probeVolumes(float above, float below, int& best, float& bestDepth2);
	void probeTrack(float above, float below, int& best, float& bestDepth2);
	void settle(int best, float bestDepth2, bool landing);
	void probeHover(int32_t dt);
	void probeSurface(int probe, int16_t surface);
	void hoverVolumes(float above, float below);
	void hoverTrack(float above, float below);
	void settleHover(int best, float bestDepth2, bool landing);
	bool cachedGround(int wheel, Vec3 a, Vec3 b);
	void insertPlane(int wheel, const SegmentHit& hit);
	void snapToGround(int best);
	void turnHeadingToward(Vec3 target, float dt);
	void updateHeading(int32_t dt);
	void updateWheelSurfaces();
	void substep(int32_t dt);

	bool acceptsWall(const SegmentHit& hit, Vec3 worldNormal, Vec3 worldPoint) const;
	void bounceOffWall();
	void runPieces(int32_t time);

	Kart& k;
	const KartParams& p;
	const Track& t;
	std::vector<SweepHit>* m_hits;
	std::vector<std::array<int16_t, 4>>* m_surfaces;
	Vec3 m_forceSum, m_torqueSum;
};

} // namespace

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

namespace {

void Body::accumulateForces()
{
	const float g = gravityForce();
	k.yawRate = 0;
	float align = 0;
	if (k.flags & KartSteering) {
		align = dot(k.rotation.f, k.velocityDirection);
		if (align <= 0) {
			stopSteering();
		}
	}

	if (k.contacts == 0) {
		// Airborne: four times the gravity, the thrust along the heading but never lifting.
		addForce({0, 0, 4 * g});
		Vec3 thrust = k.heading * (k.thrust * p.massScale);
		thrust.z = std::min(thrust.z, -g);
		addForce(thrust);
		if (k.turnRadius != 0 && !(k.flags & KartYawLock)) {
			yawImpulse(k.headingSpeed / k.turnRadius, YawKickMs);
		}
	}
	else {
		groundForces(align);
	}

	const Wheel* w = k.wheels;
	const bool hover = k.flags & KartHover;
	if (!(k.flags & (KartHover | KartSpinOut))) {
		addForce((((w[0].push + w[1].push) + w[2].push) + w[3].push) * (p.massScale * 0.25f));
	}
	const float rolling = hover ? 0.0f : (w[0].rolling + w[1].rolling + w[2].rolling + w[3].rolling) * 0.25f;
	addForce(k.velocity * -((rolling + k.dragCoefficient) * k.speed));
	if (k.flags & KartPushA) {
		addForce(k.pushes[0]);
	}
	if (k.flags & KartPushB) {
		addForce(k.pushes[1]);
	}
}

// On the ground: the slope, the wheels' support (no tipping), the slide's friction, the grip, the thrust or the engine
// brake, and the turn's pull and yaw. (align: the heading against the velocity's direction, steering.)
void Body::groundForces(float align)
{
	const float gm = Gravity * p.massScale;
	const Basis& r = k.rotation;
	const Wheel* w = k.wheels;
	const float slopeHold = (w[0].slopeHold + w[1].slopeHold + w[2].slopeHold + w[3].slopeHold) * 0.25f;
	const float friction = (w[0].friction + w[1].friction + w[2].friction + w[3].friction) * 0.25f;
	// Gravity along the ground, unless the surface holds the kart on the slope.
	if (length(k.slopeForce) > gm * slopeHold) {
		addForce(k.slopeForce);
	}
	if (k.flags & KartHover) {
		// The two axle probes: no tipping over the other axle.
		for (int i = 0; i < 2; i++) {
			if (w[i].contact) {
				cancelPositiveAbout(k.groundNormal, k.axles[1 - i]);
			}
		}
	}
	else if (k.pitchMs == 0) {
		for (int i = 0; i < 4; i++) {
			if (w[i].contact) {
				if (k.contacts < 3) {
					addForceAt(w[i].force, w[i].position);
				}
				cancelPositiveAbout(k.groundNormal, w[3 - i].position); // the diagonal wheel: no tipping over it
			}
		}
	}
	float grip = 1;
	if (k.flags & (KartSteering | KartSpinning)) {
		addForce(k.velocityDirection * -((gm * friction) * k.slipRatio));
		grip = 1 - k.slipRatio;
	}
	if (!(k.flags & (KartSpinning | KartYawLock))) {
		addForce(k.lateralVelocity * ((p.mass * -LateralGrip) * grip));
		if (k.thrust != 0) {
			const Vec3 along = k.contacts >= 3 && !(k.flags & KartSteering) ? r.f : k.heading;
			addForce(along * (k.thrust * p.massScale));
		}
		else {
			addForce(k.forwardVelocity * (p.mass * -EngineBrake)); // engine brake
		}
		if (k.turnRadius != 0) {
			const Vec3 left = normalize(Vec3{-k.heading.y, k.heading.x, 0});
			const float fs = k.headingSpeed;
			addForce(left * (((p.mass * fs) * fs) / k.turnRadius));
			float yaw;
			if (k.flags & KartSteering) {
				yaw = 0;
				if (align >= k.steerAlignMin) {
					k.yawRate = fs / k.turnRadius;
					yaw = k.steerGain * k.yawRate;
				}
			}
			else {
				k.yawRate = fs > CreepSpeed && fs < SlowYawSpeed ? SlowYawSpeed / k.turnRadius : fs / k.turnRadius;
				yaw = k.yawRate;
			}
			yawImpulse(yaw, YawKickMs);
		}
	}
	else {
		addForce(k.lateralVelocity * ((p.mass * -LockedLateralGrip) * grip));
	}
}

void Body::integrate(float dt)
{
	k.angularVelocity = toLocal(k.invInertiaWorld, k.angularMomentum);
	const Vec3 dv = {m_forceSum.x * dt * p.invMass, m_forceSum.y * dt * p.invMass, m_forceSum.z * dt * p.invMass};
	k.position += k.velocity * dt + dv * (dt * 0.5f);
	if (isZero(dv)) {
		// Creeping to a halt: tiny velocities snap to zero.
		for (float* c : {&k.velocity.x, &k.velocity.y, &k.velocity.z}) {
			if (*c > -9.9999997e-05f && *c < 9.9999997e-05f) {
				*c = 0;
			}
		}
	}
	k.velocity += dv;
	const Vec3 turn = k.angularVelocity * dt;
	const Vec3 f = k.rotation.f + cross(turn, k.rotation.f);
	const Vec3 u = k.rotation.u + cross(turn, k.rotation.u);
	k.rotation = basisFromForwardUp(f, u);
	k.angularMomentum += m_torqueSum * dt;
	if (isZero(m_torqueSum) && lengthSquared(turn) < RestingTurn) {
		k.angularVelocity = {};
		k.angularMomentum = {};
	}
	m_forceSum = {};
	m_torqueSum = {};
}

void Body::limitTilt()
{
	const Vec3 up = k.rotation.u;
	if (up.z < MaxTiltCos) {
		// Keep the direction of the tilt, clamp it to 45 degrees.
		const Vec3 a = normalize(Vec3{up.y, -up.x, 0});
		const float s = sinf64(MaxTilt), c = cosf64(MaxTilt);
		k.rotation = basisFromUpForward({-s * a.y, s * a.x, c}, k.rotation.f);
	}
}

void Body::placeWheels()
{
	const Basis& r = k.rotation;
	Wheel* w = k.wheels;
	w[1].position = toWorld(r, p.anchor) + k.position;
	w[0].position = w[1].position - r.s * p.trackWidth;
	w[3].position = w[1].position - r.f * p.wheelbase;
	w[2].position = w[0].position - r.f * p.wheelbase;
	k.axles[0] = (w[0].position + w[1].position) * 0.5f;
	k.axles[1] = (w[2].position + w[3].position) * 0.5f;
}

bool Body::cachedGround(int wheel, Vec3 a, Vec3 b)
{
	Wheel& w = k.wheels[wheel];
	auto test = [&](int index) {
		const RecentPlanes::Slot& s = k.cache.slots[index];
		const float sa = dotZYX(s.normal, a) + s.offset;
		const float sb = dotZYX(s.normal, b) + s.offset;
		if ((sa >= 0) == (sb >= 0)) {
			return false;
		}
		const float da = std::fabs(sa), db = std::fabs(sb);
		w.hit = a + (b - a) * (da / (da + db));
		return insideTriangle(s.corners[0], s.corners[1], s.corners[2], s.normal, w.hit);
	};
	auto take = [&](int index) {
		k.cache.slots[index].age = 0;
		w.surface = k.cache.slots[index].surface;
		w.cacheSlot = int8_t(index);
		return true;
	};
	if (w.cacheSlot >= 0 && test(w.cacheSlot)) {
		return take(w.cacheSlot);
	}
	for (int i = 0; i < k.cache.count; i++) {
		if (i != w.cacheSlot && test(i)) {
			return take(i);
		}
	}
	return false;
}

void Body::insertPlane(int wheel, const SegmentHit& hit)
{
	RecentPlanes& c = k.cache;
	int index = 0;
	if (c.count < 8) {
		index = c.count++;
	}
	else {
		for (int i = 1; i < 8; i++) {
			if (c.slots[i].age > c.slots[index].age) {
				index = i; // the oldest; on ties the first
			}
		}
	}
	RecentPlanes::Slot& s = c.slots[index];
	s.corners[0] = hit.corners[0];
	s.corners[1] = hit.corners[1];
	s.corners[2] = hit.corners[2];
	s.normal = hit.normal;
	s.offset = hit.offset;
	s.age = 0;
	s.surface = hit.surface;
	k.wheels[wheel].cacheSlot = int8_t(index);
}

// The deepest touching wheel so far (on a tie the first considered).
void Body::consider(int i, int& best, float& bestDepth2) const
{
	if (k.wheels[i].depth2 > bestDepth2) {
		bestDepth2 = k.wheels[i].depth2;
		best = i;
	}
}

// The wheels' rays into the other volumes first, in their own frames. (Their rays reach only `below` above the wheel
// and `above` under it.)
void Body::probeVolumes(float above, float below, int& best, float& bestDepth2)
{
	Wheel* w = k.wheels;
	for (int32_t slot = 1; slot < k.worldCount; slot++) {
		const uint32_t volume = uint32_t(k.worlds[slot]);
		const Track::Volume& v = t.volumes[volume];
		const Basis& o = v.rotation;
		const Vec3 up = {o.f.z, o.s.z, o.u.z};
		const Vec3 c = toLocal(o, w[1].position - v.position);
		const Vec3 f = toLocal(o, k.rotation.f), s = toLocal(o, k.rotation.s);
		const Vec3 corners[4] = {c - s * p.trackWidth, c, (c - s * p.trackWidth) - f * p.wheelbase, c - f * p.wheelbase};
		for (int i = 0; i < 4; i++) {
			if (w[i].touched) {
				continue;
			}
			const Vec3 a = corners[i] + up * below, b = corners[i] - up * above;
			SegmentHit hit;
			if (!t.query(volume, a, b, hit)) {
				continue;
			}
			w[i].surface = hit.surface;
			w[i].touched = true;
			w[i].hit = toWorld(o, hit.point) + v.position;
			w[i].force = toWorld(o, hit.normal);
			w[i].depth2 = lengthSquared(b - hit.point);
			consider(i, best, bestDepth2);
		}
	}
}

// Then the track, in world space (its volume sits at the origin), through the recent planes.
void Body::probeTrack(float above, float below, int& best, float& bestDepth2)
{
	Wheel* w = k.wheels;
	for (int i = 0; i < 4; i++) {
		if (w[i].touched) {
			continue;
		}
		const Vec3 a = w[i].position + Vec3{0, 0, above}, b = w[i].position - Vec3{0, 0, below};
		if (!cachedGround(i, a, b)) {
			SegmentHit hit;
			if (!t.query(t.trackVolume, a, b, hit)) {
				continue;
			}
			w[i].hit = hit.point;
			w[i].surface = hit.surface;
			insertPlane(i, hit);
		}
		w[i].force = k.cache.slots[w[i].cacheSlot].normal;
		w[i].depth2 = lengthSquared(b - w[i].hit);
		w[i].touched = true;
		consider(i, best, bestDepth2);
	}
}

// The wheels within the contact band of the deepest touch it: the ground's normal, the kart snapped onto it, a hard
// landing's bounce, the slope's pull and each touching wheel's share of the weight.
void Body::settle(int best, float bestDepth2, bool landing)
{
	Wheel* w = k.wheels;
	const float band = std::max(0.0f, std::sqrt(bestDepth2) - ContactBand);
	k.groundNormal = {};
	for (int i = 0; i < 4; i++) {
		if (w[i].touched && w[i].depth2 >= band * band) {
			w[i].contact = true;
			k.groundNormal += w[i].force;
			k.contacts++;
		}
		else {
			w[i].contact = false;
			w[i].force = {};
		}
	}
	snapToGround(best);
	if (k.contacts >= 3) {
		k.groundNormal = k.rotation.u;
		for (int i = 0; i < 4; i++) {
			w[i].contact = true;
		}
		k.contacts = 4;
	}
	else {
		k.groundNormal = normalize(k.groundNormal);
	}
	if (landing && k.airborneMs > 400) {
		const float into = dot(k.velocity, k.groundNormal);
		if (into < 0) {
			if (into < -0.05f) {
				k.velocity.z -= into * 1.15f; // bounce: back in the air
				k.contacts = 0;
				for (int i = 0; i < 4; i++) {
					w[i].contact = false;
				}
				return;
			}
			k.velocity.z -= into;
		}
	}
	const float g = gravityForce();
	const Vec3 n = k.groundNormal;
	k.slopeForce = {-n.x * (g * n.z), -n.y * (g * n.z), g - n.z * (g * n.z)};
	const Vec3 share = {0, 0, -g * (1.0f / float(k.contacts + 8))};
	for (int i = 0; i < 4; i++) {
		if (w[i].contact) {
			w[i].force = share;
		}
	}
}

void Body::probeGround(int32_t dt)
{
	if ((k.flags & KartAtRest) && isZero(k.velocity) && isZero(k.angularVelocity)) {
		return;
	}
	k.flags &= ~KartAtRest;
	for (RecentPlanes::Slot& s : k.cache.slots) {
		s.age++;
	}
	const int32_t previous = k.contacts;
	// Rays from above the wheel (the model's size, plus however far the kart fell since the step began) to a little
	// below it; a kart in the air (or tumbling) looks for landing ground only above the wheel.
	const float above = k.boundsRadius + std::max(0.0f, k.saved.position.z - k.position.z);
	const bool landing = previous == 0 || (k.flags & KartSpinOut);
	const float below = landing ? 0.0f : float(dt) * ProbeDepthPerMs;
	k.contacts = 0;
	placeWheels();
	Wheel* w = k.wheels;
	for (int i = 0; i < 4; i++) {
		w[i].contact = w[i].touched = false;
		w[i].force = {};
		w[i].depth2 = -FLT_MAX;
	}
	int best = -1;
	float bestDepth2 = -FLT_MAX;
	probeVolumes(above, below, best, bestDepth2);
	probeTrack(above, below, best, bestDepth2);

	if (best < 0) {
		if (previous != 0) {
			k.velocity.z -= DropOnLoss;
		}
		return;
	}
	settle(best, bestDepth2, landing);
}

// A hover probe's surface, on both its wheels.
void Body::probeSurface(int probe, int16_t surface)
{
	k.wheels[probe * 2].surface = surface;
	k.wheels[probe * 2 + 1].surface = surface;
}

// The two hover probes into the other volumes first, in their own frames.
void Body::hoverVolumes(float above, float below)
{
	Wheel* w = k.wheels;
	for (int32_t slot = 1; slot < k.worldCount; slot++) {
		const uint32_t volume = uint32_t(k.worlds[slot]);
		const Track::Volume& v = t.volumes[volume];
		for (int i = 0; i < 2; i++) {
			if (w[i].touched) {
				continue;
			}
			const Vec3 a = toLocal(v.rotation, k.axles[i] + Vec3{0, 0, above} - v.position);
			const Vec3 b = toLocal(v.rotation, k.axles[i] - Vec3{0, 0, below} - v.position);
			SegmentHit hit;
			if (!t.query(volume, a, b, hit)) {
				continue;
			}
			probeSurface(i, hit.surface);
			w[i].touched = true;
			w[i].hit = toWorld(v.rotation, hit.point) + v.position;
			w[i].force = toWorld(v.rotation, hit.normal);
			w[i].depth2 = lengthSquared(b - hit.point);
		}
	}
}

// Then the track, through the recent planes.
void Body::hoverTrack(float above, float below)
{
	Wheel* w = k.wheels;
	for (int i = 0; i < 2; i++) {
		if (!w[i].touched) {
			const Vec3 a = k.axles[i] + Vec3{0, 0, above}, b = k.axles[i] - Vec3{0, 0, below};
			if (!cachedGround(i, a, b)) { // (a hit in the cache keeps its surface on this probe's own wheel)
				SegmentHit hit;
				if (!t.query(t.trackVolume, a, b, hit)) {
					continue;
				}
				w[i].hit = hit.point;
				probeSurface(i, hit.surface);
				insertPlane(i, hit);
			}
			w[i].force = k.cache.slots[w[i].cacheSlot].normal;
			w[i].depth2 = lengthSquared(b - w[i].hit);
			w[i].touched = true;
		}
	}
}

// The probes within the contact band of the deepest: the heading along the axles, the lift over the ground, a hard
// landing's bounce, the slope's pull and each probe's half of the weight.
void Body::settleHover(int best, float bestDepth2, bool landing)
{
	Wheel* w = k.wheels;
	const float band = std::max(0.0f, std::sqrt(bestDepth2) - ContactBand);
	k.groundNormal = {};
	for (int i = 0; i < 2; i++) {
		if (w[i].touched && w[i].depth2 >= band * band) {
			w[i].contact = true;
			k.groundNormal += w[i].force;
			k.contacts++;
		}
		else {
			w[i].contact = false;
			w[i].force = {};
		}
	}
	if (k.contacts >= 2) {
		k.rotation = basisFromForwardUp(w[0].hit - w[1].hit, k.rotation.u);
		k.groundNormal = k.rotation.u;
	}
	else {
		k.groundNormal = normalize(k.groundNormal);
	}
	const Vec3 lift3 = (w[best].hit - k.axles[best]) + Vec3{0, 0, k.lift};
	k.position += lift3;
	k.axles[0] += lift3;
	k.axles[1] += lift3;
	if (landing) {
		const float into = dot(k.velocity, k.groundNormal);
		if (into < 0) {
			if (into < -0.05f) {
				k.velocity.z -= into * 1.3f;
				k.contacts = 0;
				w[0].contact = w[1].contact = false;
				return;
			}
			k.velocity.z -= into;
		}
	}
	const float g = gravityForce();
	const Vec3 n = k.groundNormal;
	k.slopeForce = {-n.x * (g * n.z), -n.y * (g * n.z), g - n.z * (g * n.z)};
	for (int i = 0; i < 2; i++) {
		if (w[i].contact) {
			w[i].force = {0, 0, -g * 0.5f};
		}
	}
}

void Body::probeHover(int32_t dt)
{
	if ((k.flags & KartAtRest) && isZero(k.velocity) && isZero(k.angularVelocity)) {
		return;
	}
	k.flags &= ~KartAtRest;
	// The lift and the bank ease toward their targets first.
	const float fdt = float(dt);
	const float lift = (k.liftRate * fdt) * 0.001f, bank = fdt * 0.005f;
	k.lift = k.lift < k.liftTarget ? std::min(k.lift + lift, k.liftTarget) : std::max(k.lift - lift, k.liftTarget);
	k.bank = k.bank < k.bankTarget ? std::min(k.bank + bank, k.bankTarget) : std::max(k.bank - bank, k.bankTarget);
	for (RecentPlanes::Slot& s : k.cache.slots) {
		s.age++;
	}
	placeWheels();
	const int32_t previous = k.contacts;
	const float above = k.boundsRadius + std::max(0.0f, k.saved.position.z - k.position.z);
	const bool landing = previous == 0;
	const float below = (landing ? 0.0f : fdt * 0.01f) + k.lift;
	k.contacts = 0;
	// Two probes, at the front and rear axles' middles, kept in wheels 0 and 1. (Wheels 2 and 3 keep what they had.)
	Wheel* w = k.wheels;
	for (int i = 0; i < 2; i++) {
		w[i].contact = w[i].touched = false;
		w[i].force = {};
		w[i].depth2 = -FLT_MAX;
	}
	int best = -1;
	float bestDepth2 = -FLT_MAX;
	hoverVolumes(above, below);
	hoverTrack(above, below);
	for (int i = 0; i < 2; i++) {
		if (w[i].touched && w[i].depth2 > bestDepth2) {
			bestDepth2 = w[i].depth2;
			best = i;
		}
	}
	if (best < 0) {
		if (previous != 0) {
			k.velocity.z -= DropOnLoss;
		}
		return;
	}
	settleHover(best, bestDepth2, landing);
}

void Body::snapToGround(int best)
{
	Wheel* w = k.wheels;
	int anchor = best;
	const Vec3 before[4] = {w[0].position, w[1].position, w[2].position, w[3].position};
	if (k.contacts > 1 && k.pitchMs == 0) {
		// Align to the hits: front minus rear along the kart, left minus right across it.
		const int ref = k.contacts == 4 ? 0 : best;
		const int along = ref ^ 2, across = ref ^ 1;
		const Vec3 forward = w[along].contact ? w[std::min(ref, along)].hit - w[std::max(ref, along)].hit : k.rotation.f;
		const Vec3 side = w[across].contact ? w[std::max(ref, across)].hit - w[std::min(ref, across)].hit : k.rotation.s;
		k.rotation = basisFromForwardSide(forward, side);
		anchor = ref;
	}
	k.position += (w[anchor].hit - before[anchor]) + Vec3{0, 0, GroundClearance};
	placeWheels();
}

void Body::turnHeadingToward(Vec3 target, float dt)
{
	const float d = dot(k.heading, target);
	if (d > HeadingSnap) {
		k.heading = target;
		return;
	}
	const float step = dt * HeadingTurnRate;
	if (step > tableAcos(d)) {
		k.heading = target;
		return;
	}
	k.heading = normalize(turnAbout(k.heading, cross(target, k.heading), step));
}

void Body::updateHeading(int32_t dt)
{
	const float fdt = float(dt);
	const Vec3 f = k.rotation.f;
	if (k.contacts == 0 || k.headingSpeed < 0.03f) {
		turnHeadingToward(f, fdt);
	}
	else if (k.contacts <= 2) {
		const Vec3 n = k.groundNormal;
		turnHeadingToward(normalize(f - n * dot(f, n)), fdt);
	}
	else if ((k.flags & KartSteering) && k.thrust > 0) {
		// Steering under power: the heading lags the nose by the turn, up to the lag limit.
		const float yaw = k.yawRate * fdt;
		const float behind = tableAcos(std::min(1.0f, dot(k.heading, f))) + std::fabs(yaw);
		if (behind > k.lagLimit) {
			k.heading = normalize(turnAbout(f, cross(k.heading, f), k.lagLimit));
		}
		else {
			const float c = cosf64(yaw), s = sinf64(yaw);
			Vec3& h = k.heading;
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
		const bool rolling = !(k.flags & (KartHover | KartSpinOut)) && k.headingSpeed > RollingSpeed;
		for (size_t i = 0; i < 4; i++) {
			s[i] = rolling && k.wheels[i].contact && k.wheels[i].surface >= 0 ? k.wheels[i].surface : int16_t(-1);
		}
	}
	const bool ignore = k.flags & KartIgnoreSurfaces;
	for (Wheel& w : k.wheels) {
		const Surface* s = w.surface >= 0 && w.contact ? &t.surfaces[w.surface] : nullptr;
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
	if (k.flags & KartHover) {
		probeHover(dt);
	}
	else {
		probeGround(dt);
	}
	updateHeading(dt);
	refreshStatistics(k);
	updateWheelSurfaces();
	k.centerOfMassWorld = toWorld(k.rotation, p.centerOfMass) + k.position;
	k.invInertiaWorld = worldInverseInertia(k.rotation, p.invInertia);
}

void Body::save()
{
	KartSnapshot& s = k.saved;
	s.position = k.position;
	s.velocity = k.velocity;
	s.rotation = k.rotation;
	s.invInertiaWorld = k.invInertiaWorld;
	s.angularVelocity = k.angularVelocity;
	s.angularMomentum = k.angularMomentum;
	s.heading = k.heading;
	s.lift = k.lift;
	s.bank = k.bank;
	for (int i = 0; i < 4; i++) {
		const Wheel& w = k.wheels[i];
		s.wheels[i] = {w.position, w.push, w.rolling, w.slopeHold, w.friction, w.grip, w.surface, w.contact, w.touched};
		s.bodyPoints[i] = k.bodyPoints[i];
	}
}

void Body::restore()
{
	const KartSnapshot& s = k.saved;
	k.position = s.position;
	k.velocity = s.velocity;
	k.rotation = s.rotation;
	k.invInertiaWorld = s.invInertiaWorld;
	k.angularVelocity = s.angularVelocity;
	k.angularMomentum = s.angularMomentum;
	k.heading = s.heading;
	k.lift = s.lift;
	k.bank = s.bank;
	for (int i = 0; i < 4; i++) {
		Wheel& w = k.wheels[i];
		const KartSnapshot::WheelPart& sw = s.wheels[i];
		w.position = sw.position;
		w.push = sw.push;
		w.rolling = sw.rolling;
		w.slopeHold = sw.slopeHold;
		w.friction = sw.friction;
		w.grip = sw.grip;
		w.surface = sw.surface;
		w.contact = sw.contact;
		w.touched = sw.touched;
		k.bodyPoints[i] = s.bodyPoints[i];
	}
	k.centerOfMassWorld = toWorld(k.rotation, p.centerOfMass) + k.position;
	refreshStatistics(k);
}

// What the material hit is: a checkpoint's gate (the race counts it, the kart passes), a solid wall, or a surface,
// which can be the finish line or start a touch event for the race and which stops the kart unless non-solid.
bool Body::acceptsWall(const SegmentHit& hit, Vec3 worldNormal, Vec3 worldPoint) const
{
	if (hit.checkpoint >= 0) {
		if (m_hits) {
			const Vec3 n = t.checkpoints[size_t(hit.checkpoint)].normal;
			m_hits->push_back({hit.checkpoint, !(dotZYX(worldNormal, n) < 0), false, -1, worldPoint});
		}
		return false;
	}
	if (hit.surface < 0) {
		return true;
	}
	const Surface& s = t.surfaces[size_t(hit.surface)];
	const bool finish = s.flags & SurfaceFinishLine, touch = s.flags & SurfaceHasTouchEvent;
	if (m_hits && (finish || touch)) {
		m_hits->push_back({-1, false, finish, touch ? s.touchEvent : -1, worldPoint});
	}
	return !(s.flags & SurfaceNonSolid);
}

int32_t Body::sweep(int32_t dt, bool queryTrack)
{
	k.wallHits = 0;
	float best = -FLT_MAX, fraction = 0;
	bool marked[4] = {};
	for (int i = 0; i < 4; i++) {
		k.bodyPoints[i] = toWorld(k.rotation, p.bodyPoints[i]) + k.position;
	}
	// Each corner from where it was at the save to where it is now. Only a hit deeper than the best so far counts.
	auto consider = [&](int i, Vec3 end, const SegmentHit& hit, Vec3 worldNormal) {
		const float depth2 = lengthSquared(end - hit.point);
		if (depth2 > best) {
			best = depth2;
			k.wallNormal = worldNormal;
			marked[i] = true;
			fraction = hit.fraction;
			k.wallHits++;
		}
	};
	for (int32_t slot = 1; slot < k.worldCount; slot++) {
		const uint32_t volume = uint32_t(k.worlds[slot]);
		const Track::Volume& v = t.volumes[volume];
		for (int i = 0; i < 4; i++) {
			if (marked[i]) {
				continue;
			}
			const Vec3 a = toLocal(v.rotation, k.saved.bodyPoints[i] - v.position);
			const Vec3 b = toLocal(v.rotation, k.bodyPoints[i] - v.position);
			SegmentHit hit;
			if (t.query(volume, a, b, hit) && dot(hit.point - b, hit.normal) > 0) {
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
			const Vec3 a = k.saved.bodyPoints[i], b = k.bodyPoints[i];
			SegmentHit hit;
			if (t.query(t.trackVolume, a, b, hit) && dot(hit.point - b, hit.normal) >= 0 && acceptsWall(hit, hit.normal, hit.point)) {
				consider(i, b, hit, hit.normal);
			}
		}
	}
	if (k.wallHits == 0) {
		return 0;
	}
	const int32_t rerun = int32_t(float(dt) * fraction);
	restore();
	return rerun < WallBackOffMs ? 0 : rerun - WallBackOffMs;
}

void Body::bounceOffWall()
{
	const Vec3 n = k.wallNormal;
	if (k.flags & KartSteering) {
		stopSteering();
	}
	const float into = dotZYX(k.velocity, n);
	if (into < 0) {
		k.velocity -= n * into;
	}
	if (!(k.flags & KartSpinning) && dot(k.rotation.f, n) < 0) {
		// Nose into the wall: a yaw kick away from it, harder the more head-on.
		const float q = dot(k.rotation.s, n);
		const float rate = q < 0 ? -WallYawKick * (((q + 1) * 0.5f) + 0.5f) : (((1 - q) * 0.5f) + 0.5f) * WallYawKick;
		yawImpulse(rate, YawKickMs);
		k.flags |= KartYawLock;
	}
	if (into < 0) {
		k.velocity.x -= n.x * (WallRebound * into);
		k.velocity.y -= n.y * (WallRebound * into);
		k.velocity.z -= (WallReboundUp * into) * n.z;
	}
	k.velocity += n * WallPush;
	if (!(k.velocity.z < WallMaxRise)) {
		k.velocity.z = WallMaxRise;
	}
}

void Body::runPieces(int32_t time)
{
	int32_t e = time, remainder = time;
	bool recovered = false;
	save();
	for (;;) {
		if (k.wallHits == 0 && e == 0) {
			break;
		}
		if (e != 0) {
			substep(e);
		}
		if (k.wallHits != 0) {
			if (e != 0) {
				const int32_t rerun = sweep(e, !(k.flags & KartGhost));
				if (k.wallHits != 0) {
					e = rerun; // still hitting: back up further
					continue;
				}
			}
			// The piece ran clean (or there was nothing left to run): bounce here and run the rest.
			bounceOffWall();
			refreshStatistics(k);
			recovered = true;
			k.wallHits = 0;
			save();
			remainder -= e;
			e = remainder;
			continue;
		}
		e = sweep(e, !(k.flags & KartGhost));
		if (e != 0 || !recovered) {
			continue;
		}
		break; // a hit right at the restored pose after a bounce: the rest of the time is lost
	}
	if (recovered) {
		k.wallHits = 1;
	}
}

// A step's timers: the spin-out, the spin (its last piece cuts the step short: the ms the step runs, returned) or
// the yaw lock, the spin alignment and the pitch lock.
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

void Body::step(int32_t dt, Pose* drawn)
{
	k.wallHits = 0;
	const int32_t e = stepTimers(k, dt);
	if (k.flags & KartHover) {
		// The hover banks into the turn: by up to 45 degrees for the tightest.
		const float r = k.turnRadius;
		k.bankTarget = r > 0 ? -(1 - std::min(1.0f, r / 4096)) * 0.78539819f : r < 0 ? (1 - std::min(1.0f, -r / 4096)) * 0.78539819f : 0;
	}
	runPieces(e);
	k.airborneMs = k.contacts == 0 ? k.airborneMs + dt : 0;

	// The drawn car leans into a powerslide (or with a hover's bank).
	float lean = 0;
	if (k.flags & KartHover) {
		lean = -k.bank;
	}
	else if ((k.flags & KartPowerslide) && std::fabs(k.turnRadius) >= 40) {
		lean = std::clamp(((((1 / k.turnRadius) * -1) * 40) * k.slideFactor) * ((k.speed * 1000) / k.maxSpeed), -0.5f, 0.5f);
	}
	const float easing = float(dt) * 0.0020000001f;
	k.visualBank = k.visualBank < lean ? std::min(k.visualBank + easing, lean) : std::max(k.visualBank - easing, lean);
	if (drawn) {
		*drawn = drawnPose(k, p);
	}

	// Race checks: remember a good pose; go back to it out of the world, or while held on the grid.
	const bool held = k.preStart && k.engineStarted;
	if (k.contacts == 4 && k.wallHits == 0 && !held) {
		k.resetPosition = k.position;
		k.resetRotation = k.rotation;
	}
	if (k.position.z < -250 || k.position.z > 340 || held) {
		k.position = k.resetPosition;
		k.rotation = k.resetRotation;
		k.velocity = {};
		k.angularMomentum = {};
	}
}

} // namespace

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
	const int32_t weight = int32_t(float(src.pieceWeight) * 0.050000001f);
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
	k.wheels[1].position = toWorld(rotation, p.anchor) + position;
	k.wheels[0].position = k.wheels[1].position - rotation.s * p.trackWidth;
	k.wheels[3].position = k.wheels[1].position - rotation.f * p.wheelbase;
	k.wheels[2].position = k.wheels[0].position - rotation.f * p.wheelbase;
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

void stepBody(Kart& kart, const KartParams& params, const Track& track, int32_t dt, std::vector<SweepHit>* hits, Pose* drawn, std::vector<std::array<int16_t, 4>>* surfaces)
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
	Wheel* w = k.wheels;
	w[1].position = toWorld(k.rotation, p.anchor) + k.position;
	w[0].position = w[1].position - k.rotation.s * p.trackWidth;
	w[3].position = w[1].position - k.rotation.f * p.wheelbase;
	w[2].position = w[0].position - k.rotation.f * p.wheelbase;
	k.axles[0] = (w[0].position + w[1].position) * 0.5f;
	k.axles[1] = (w[2].position + w[3].position) * 0.5f;
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
