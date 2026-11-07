// The kart body's ground (Body.cpp's substeps run it): each wheel's (or hover probe's) ray through the other volumes and
// then the track, with a cache of recent ground planes; the deepest touch, the contact band, the weight and a landing's
// bounce; and the body snapped onto the hits.

#include "Kart/BodyParts.h"

#include "LRCore/Race/Track.h"

#include <cfloat>
#include <cmath>

namespace lr {

bool Body::cachedGround(int wheel, Vec3 a, Vec3 b)
{
	Wheel& w = m_kart.wheels[wheel];
	auto test = [&](int index) {
		const RecentPlanes::Slot& s = m_kart.cache.slots[index];
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
		m_kart.cache.slots[index].age = 0;
		w.surface = m_kart.cache.slots[index].surface;
		w.cacheSlot = int8_t(index);
		return true;
	};
	if (w.cacheSlot >= 0 && test(w.cacheSlot)) {
		return take(w.cacheSlot);
	}
	for (int i = 0; i < m_kart.cache.count; i++) {
		if (i != w.cacheSlot && test(i)) {
			return take(i);
		}
	}
	return false;
}

void Body::insertPlane(int wheel, const SegmentHit& hit)
{
	RecentPlanes& c = m_kart.cache;
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
	m_kart.wheels[wheel].cacheSlot = int8_t(index);
}

// The deepest touching wheel so far (on a tie the first considered).
void Body::consider(int i, int& best, float& bestDepth2) const
{
	if (m_kart.wheels[i].depth2 > bestDepth2) {
		bestDepth2 = m_kart.wheels[i].depth2;
		best = i;
	}
}

// The probes' rays into the other volumes first, in their own frames: the four wheels' (which reach only `below` above
// the wheel and `above` under it) or the two hover probes'.
void Body::probeVolumes(int probes, float above, float below, int& best, float& bestDepth2)
{
	Wheel* w = m_kart.wheels;
	for (int32_t slot = 1; slot < m_kart.worldCount; slot++) {
		const uint32_t volume = uint32_t(m_kart.worlds[slot]);
		const Track::Volume& v = m_track.volumes[volume];
		const Basis& o = v.rotation;
		Vec3 a[4], b[4];
		if (probes == 4) {
			const Vec3 up = {o.f.z, o.s.z, o.u.z};
			const Vec3 c = toLocal(o, w[1].position - v.position);
			const Vec3 f = toLocal(o, m_kart.rotation.f), s = toLocal(o, m_kart.rotation.s);
			const Vec3 corners[4] = {c - s * m_params.trackWidth, c, (c - s * m_params.trackWidth) - f * m_params.wheelbase,
				c - f * m_params.wheelbase};
			for (int i = 0; i < 4; i++) {
				a[i] = corners[i] + up * below;
				b[i] = corners[i] - up * above;
			}
		}
		else {
			for (int i = 0; i < 2; i++) {
				a[i] = toLocal(o, m_kart.axles[i] + Vec3{0, 0, above} - v.position);
				b[i] = toLocal(o, m_kart.axles[i] - Vec3{0, 0, below} - v.position);
			}
		}
		for (int i = 0; i < probes; i++) {
			SegmentHit hit;
			if (w[i].touched || !m_track.query(volume, a[i], b[i], hit)) {
				continue;
			}
			setSurface(probes, i, hit.surface);
			w[i].touched = true;
			w[i].hit = toWorld(o, hit.point) + v.position;
			w[i].force = toWorld(o, hit.normal);
			w[i].depth2 = lengthSquared(b[i] - hit.point);
			if (probes == 4) {
				consider(i, best, bestDepth2);
			}
		}
	}
}

// Then the track, in world space (its volume sits at the origin), through the recent planes. (A hit in the cache keeps
// its surface on the probe's own wheel.)
void Body::probeTrack(int probes, float above, float below, int& best, float& bestDepth2)
{
	Wheel* w = m_kart.wheels;
	for (int i = 0; i < probes; i++) {
		if (w[i].touched) {
			continue;
		}
		const Vec3 at = probes == 4 ? w[i].position : m_kart.axles[i];
		const Vec3 a = at + Vec3{0, 0, above}, b = at - Vec3{0, 0, below};
		if (!cachedGround(i, a, b)) {
			SegmentHit hit;
			if (!m_track.query(m_track.trackVolume, a, b, hit)) {
				continue;
			}
			w[i].hit = hit.point;
			setSurface(probes, i, hit.surface);
			insertPlane(i, hit);
		}
		w[i].force = m_kart.cache.slots[w[i].cacheSlot].normal;
		w[i].depth2 = lengthSquared(b - w[i].hit);
		w[i].touched = true;
		if (probes == 4) {
			consider(i, best, bestDepth2);
		}
	}
}

// A probe's surface: a wheel's own, or a hover probe's on both its wheels.
void Body::setSurface(int probes, int i, int16_t surface)
{
	if (probes == 2) {
		m_kart.wheels[i * 2 + 1].surface = surface;
		i *= 2;
	}
	m_kart.wheels[i].surface = surface;
}

// False for a kart at rest; otherwise it wakes and the recent planes age a step.
bool Body::wake()
{
	if ((m_kart.flags & KartAtRest) && isZero(m_kart.velocity) && isZero(m_kart.angularVelocity)) {
		return false;
	}
	m_kart.flags &= ~KartAtRest;
	for (RecentPlanes::Slot& s : m_kart.cache.slots) {
		s.age++;
	}
	return true;
}

// The probes' rays, through the volumes and then the track, and the deepest touch (-1 for none, and a drop if the
// ground was just lost). Wheels rank as they touch, a tie going to the first touched; hover probes after, to the first.
int Body::probe(int probes, float above, float below, int32_t previous, float& bestDepth2)
{
	m_kart.contacts = 0;
	Wheel* w = m_kart.wheels;
	for (int i = 0; i < probes; i++) {
		w[i].contact = w[i].touched = false;
		w[i].force = {};
		w[i].depth2 = -FLT_MAX;
	}
	int best = -1;
	bestDepth2 = -FLT_MAX;
	probeVolumes(probes, above, below, best, bestDepth2);
	probeTrack(probes, above, below, best, bestDepth2);
	for (int i = 0; i < probes && probes == 2; i++) {
		consider(i, best, bestDepth2);
	}
	if (best < 0 && previous != 0) {
		m_kart.velocity.z -= DropOnLoss;
	}
	return best;
}

// The probes within the contact band of the deepest touch it, with the sum of their ground normals.
void Body::touchBand(int probes, float bestDepth2)
{
	Wheel* w = m_kart.wheels;
	const float band = std::max(0.0f, std::sqrt(bestDepth2) - ContactBand);
	m_kart.groundNormal = {};
	for (int i = 0; i < probes; i++) {
		if (w[i].touched && w[i].depth2 >= band * band) {
			w[i].contact = true;
			m_kart.groundNormal += w[i].force;
			m_kart.contacts++;
		}
		else {
			w[i].contact = false;
			w[i].force = {};
		}
	}
}

// A hard landing's bounce back into the air; otherwise the slope's pull and each touching probe's share of the weight.
void Body::weigh(int probes, bool landing, float bounce)
{
	Wheel* w = m_kart.wheels;
	if (landing) {
		const float into = dot(m_kart.velocity, m_kart.groundNormal);
		if (into < 0) {
			if (into < -0.05f) {
				m_kart.velocity.z -= into * bounce;
				m_kart.contacts = 0;
				for (int i = 0; i < probes; i++) {
					w[i].contact = false;
				}
				return;
			}
			m_kart.velocity.z -= into;
		}
	}
	const float g = gravityForce();
	const Vec3 n = m_kart.groundNormal;
	m_kart.slopeForce = {-n.x * (g * n.z), -n.y * (g * n.z), g - n.z * (g * n.z)};
	const Vec3 share = {0, 0, probes == 4 ? -g * (1.0f / float(m_kart.contacts + 8)) : -g * 0.5f};
	for (int i = 0; i < probes; i++) {
		if (w[i].contact) {
			w[i].force = share;
		}
	}
}

// Four wheels: those touching snap the kart onto the ground, three or more holding it flat on all four.
void Body::probeGround(int32_t dt)
{
	if (!wake()) {
		return;
	}
	const int32_t previous = m_kart.contacts;
	// Rays from above the wheel (the model's size, plus however far the kart fell since the step began) to a little
	// below it; a kart in the air (or tumbling) looks for landing ground only above the wheel.
	const float above = m_kart.boundsRadius + std::max(0.0f, m_kart.saved.position.z - m_kart.position.z);
	const bool landing = previous == 0 || (m_kart.flags & KartSpinOut);
	const float below = landing ? 0.0f : float(dt) * ProbeDepthPerMs;
	placeWheels();
	float bestDepth2;
	const int best = probe(4, above, below, previous, bestDepth2);
	if (best < 0) {
		return;
	}
	touchBand(4, bestDepth2);
	settleOnGround(best);
	if (m_kart.contacts >= 3) {
		m_kart.groundNormal = m_kart.rotation.u;
		for (int i = 0; i < 4; i++) {
			m_kart.wheels[i].contact = true;
		}
		m_kart.contacts = 4;
	}
	else {
		m_kart.groundNormal = normalize(m_kart.groundNormal);
	}
	weigh(4, landing && m_kart.airborneMs > BounceAirborneMs, WheelBounce);
}

// Two probes, at the front and rear axles' middles, kept in wheels 0 and 1 (wheels 2 and 3 keep what they had): the
// heading along the axles and the lift over the ground.
void Body::probeHover(int32_t dt)
{
	if (!wake()) {
		return;
	}
	// The lift and the bank ease toward their targets first.
	const float fdt = float(dt);
	m_kart.lift = easeToward(m_kart.lift, m_kart.liftTarget, (m_kart.liftRate * fdt) * 0.001f);
	m_kart.bank = easeToward(m_kart.bank, m_kart.bankTarget, fdt * 0.005f);
	placeWheels();
	const int32_t previous = m_kart.contacts;
	const float above = m_kart.boundsRadius + std::max(0.0f, m_kart.saved.position.z - m_kart.position.z);
	const bool landing = previous == 0;
	const float below = (landing ? 0.0f : fdt * 0.01f) + m_kart.lift;
	float bestDepth2;
	const int best = probe(2, above, below, previous, bestDepth2);
	if (best < 0) {
		return;
	}
	touchBand(2, bestDepth2);
	Wheel* w = m_kart.wheels;
	if (m_kart.contacts >= 2) {
		m_kart.rotation = basisFromForwardUp(w[0].hit - w[1].hit, m_kart.rotation.u);
		m_kart.groundNormal = m_kart.rotation.u;
	}
	else {
		m_kart.groundNormal = normalize(m_kart.groundNormal);
	}
	const Vec3 lift3 = (w[best].hit - m_kart.axles[best]) + Vec3{0, 0, m_kart.lift};
	m_kart.position += lift3;
	m_kart.axles[0] += lift3;
	m_kart.axles[1] += lift3;
	weigh(2, landing, HoverBounce);
}

void Body::settleOnGround(int best)
{
	Wheel* w = m_kart.wheels;
	int anchor = best;
	const Vec3 before[4] = {w[0].position, w[1].position, w[2].position, w[3].position};
	if (m_kart.contacts > 1 && m_kart.pitchMs == 0) {
		// Align to the hits: front minus rear along the kart, left minus right across it.
		const int ref = m_kart.contacts == 4 ? 0 : best;
		const int along = ref ^ 2, across = ref ^ 1;
		const Vec3 forward = w[along].contact ? w[std::min(ref, along)].hit - w[std::max(ref, along)].hit : m_kart.rotation.f;
		const Vec3 side = w[across].contact ? w[std::max(ref, across)].hit - w[std::min(ref, across)].hit : m_kart.rotation.s;
		m_kart.rotation = basisFromForwardSide(forward, side);
		anchor = ref;
	}
	m_kart.position += (w[anchor].hit - before[anchor]) + Vec3{0, 0, GroundClearance};
	placeWheels();
}

} // namespace lr
