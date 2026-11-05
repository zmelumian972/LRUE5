// The players' cameras and what each draws of the animated scenery.

#include "LRCore/Race/World.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

constexpr float Pi = 3.1415927f;

// The views: pitch and height (degrees), distance (ft), position and rotation lag.
struct Profile {
	PlayerCamera::Kind kind;
	float pitch, height, distance, positionLag, rotationLag;
};
constexpr Profile Profiles[8] = {
	{PlayerCamera::Chase, 5, 35, 20, 0.1f, 0.25f},
	{PlayerCamera::Chase, 8, 25, 30, 0.1f, 0.25f},
	{PlayerCamera::Chase, 8, 45, 10, 0.05f, 0.25f},
	{PlayerCamera::Cockpit, 0, 0, 0, 0.1f, 0.25f},
	{PlayerCamera::Finish, 20, 15, 32, 0.18f, 0.35f},
	{PlayerCamera::Chase, 8, 20, 22, 0.1f, 0.25f},
	{PlayerCamera::Chase, 7, 15, 45, 0.1f, 0.25f},
	{PlayerCamera::Chase, 4, 30, 15, 0.05f, 0.25f},
};
constexpr int32_t FinishView = 4;

float radians(float degrees) { return degrees * (Pi / 180); }
float lagRate(float a) { return (1 - a) / (a * 250); }

// Rows from a look and a down: the look, the down made square to it, right = down × look.
void rowsFrom(Vec3 look, Vec3 down, Vec3& r, Vec3& d, Vec3& l)
{
	l = normalize(look);
	d = normalize(down - l * dot(down, l));
	r = cross(d, l);
}

Quat quatOfRows(Vec3 r, Vec3 d, Vec3 l) { return quaternionOf(Basis{r, d, l}); }

void setRows(PlayerCamera& c, Quat q)
{
	const Basis b = basisOf(q);
	c.right = b.f, c.down = b.s, c.look = b.u;
	c.rotation = q;
}

// The direction's angle, atan2-like with range (-pi, 3pi/2].
float angleOf(float x, float y)
{
	if (std::fabs(y) < std::fabs(x)) {
		return x > 0 ? Pi / 2 - std::acos(y) : std::acos(y) + Pi / 2;
	}
	return y > 0 ? std::acos(x) : -std::acos(x);
}

} // namespace

Vec3 PlayerCamera::shownEye() const
{
	if (!lookBack || kind == Finish || kind == Transition || kind == Flyby) {
		return eye;
	}
	if (kind == Cockpit) {
		return eye;
	}
	return {2 * target.x - eye.x, 2 * target.y - eye.y, eye.z};
}

void PlayerCamera::shownRows(Vec3& r, Vec3& d, Vec3& l) const
{
	r = right, d = down, l = look;
	if (!lookBack || kind == Finish || kind == Transition || kind == Flyby) {
		return;
	}
	if (kind == Cockpit) {
		l = -look;
		r = -right;
		return;
	}
	l = {-look.x, -look.y, look.z};
	d = {-down.x, -down.y, down.z};
	r = cross(d, l);
}

bool PlayerCamera::outside(Vec3 centre, float radius) const
{
	Vec3 r, d, l;
	shownRows(r, d, l);
	const Vec3 p = centre - shownEye();
	const float x = dot(p, r), y = dot(p, d), z = dot(p, l);
	if (z < nearPlane - radius || z > farPlane + radius) {
		return true;
	}
	const float tv = std::tan(fov * 0.0087266462f);
	const float th = tv * aspect;
	// The side planes through the eye: inside when |x| <= z·th, |y| <= z·tv (signed distances scaled to length).
	return (z * th - std::fabs(x)) / std::sqrt(1 + th * th) < -radius || (z * tv - std::fabs(y)) / std::sqrt(1 + tv * tv) < -radius;
}

void World::chooseView(size_t p, uint8_t view)
{
	PlayerCamera& c = cameras[p];
	c.view = view == FinishView ? c.view : view;
	const uint8_t profile = view == FinishView ? FinishView : c.split && view < 3 ? uint8_t(view + 5) : view;
	const PlayerCamera::Kind before = c.kind;
	c.profile = profile;
	c.lookBack = false;
	// (The intro's flyby and transition run on; the finish ends a transition.)
	if ((c.kind != PlayerCamera::Transition && c.kind != PlayerCamera::Flyby) || (c.kind == PlayerCamera::Transition && view == FinishView)) {
		c.kind = Profiles[profile].kind;
	}
	if (c.kind != before) {
		c.clockMs = 0;
	}
	c.dirty = true;
}

void World::updateCameras(int32_t dt)
{
	for (size_t p = 0; p < cameras.size(); p++) {
		updateCamera(p, dt);
	}
}

void World::updateCamera(size_t p, int32_t dt)
{
	PlayerCamera& c = cameras[p];
	if (c.racer < 0) {
		return;
	}
	const size_t i = size_t(c.racer);
	const int32_t e = std::min(dt, 100);
	c.hidden = racers[i].flags & RacerGhost;
	if (c.kind == PlayerCamera::Flyby) {
		// Along the intro's camera node; when the flyby is done, from its last pose over to the chosen view.
		if (track.introEntity >= 0) {
			const AnimatedModel& m = track.animated[size_t(track.introEntity)];
			const Animation* a = m.animation >= 0 ? &track.animations[size_t(m.animation)] : nullptr;
			std::vector<NodeMatrix> nodes;
			// (Its time held at the animation's last frame once it is done.)
			const float last = a && !a->parts.empty() ? float(a->parts[0].frames - 1) : session.flyby;
			poseSkeleton(m.nodes, a, {0, std::min(session.flyby, last), false}, m.pose, m.scale, nodes);
			if (size_t(track.introNode) < nodes.size()) {
				const NodeMatrix& node = nodes[size_t(track.introNode)];
				Vec3 r, d, l;
				rowsFrom(node.rows.f, -node.rows.u, r, d, l); // (down: minus the node's up, not its row 1)
				c.eye = node.position;
				c.right = r, c.down = d, c.look = l;
				c.rotation = quatOfRows(r, d, l);
			}
		}
		if (session.watchingFlyby) {
			return;
		}
		c.kind = PlayerCamera::Transition;
		c.clockMs = 0;
	}
	if (e <= 0 && !c.dirty) {
		return;
	}
	const bool snap = c.dirty;
	c.dirty = false;
	const float ef = float(e);
	const Pose& car = visible[i];
	const Profile& pr = Profiles[c.profile];
	const PlayerCamera::Kind kind = c.kind == PlayerCamera::Transition ? Profiles[c.profile].kind : c.kind;
	const Kart& k = karts[i];

	Vec3 rawEye, O;
	Vec3 look, down;
	if (kind == PlayerCamera::Cockpit) {
		rawEye = car.position + Vec3{0, 0, c.split ? 6.0f : 10.0f};
		look = car.rotation.f;
		down = -car.rotation.u;
		c.target = car.position;
	}
	else {
		// The view direction, led into turns and eased round; the target eased after the car.
		float wx = car.rotation.f.x, wy = car.rotation.f.y;
		float wl = std::sqrt(wx * wx + wy * wy);
		if (wl == 0) {
			wx = c.vx, wy = c.vy;
			wl = std::sqrt(wx * wx + wy * wy);
			if (wl == 0) {
				wx = 1, wy = 0, wl = 1;
			}
		}
		wx /= wl, wy /= wl;
		if (snap) {
			c.lead = 0;
			c.target = car.position;
		}
		else {
			if (k.flags & KartSpinning) {
				wx = c.vx, wy = c.vy;
			}
			else {
				// (A route kart has no drive controller: no turn radius.)
				const float fs = k.headingSpeed, radius = playback[i].active ? 0.0f : k.turnRadius;
				const float wanted = radius == 0 ? 0.0f : std::clamp(fs * 80 / radius, -0.3f, 0.3f);
				c.lead = wanted + (c.lead - wanted) * (1 / (0.0044444446f * ef + 1));
				const float cl = std::cos(c.lead), sl = std::sin(c.lead);
				const float x = cl * wx - sl * wy, y = cl * wy + sl * wx;
				wx = x, wy = y;
			}
			float delta = angleOf(c.vx, c.vy) - angleOf(wx, wy);
			if (std::fabs(delta) > 0.014f) {
				delta = delta > Pi ? delta - 2 * Pi : delta < -Pi ? delta + 2 * Pi : delta;
				const float a = delta * (1 / (0.016666668f * ef + 1)) + angleOf(wx, wy);
				wx = std::cos(a), wy = std::sin(a);
			}
			c.target = car.position + (c.target - car.position) * (1 / (0.09f * ef + 1));
		}
		const float vl = std::sqrt(wx * wx + wy * wy);
		if (vl != 0) {
			c.vx = wx / vl, c.vy = wy / vl;
		}
		// A turbo's start pulls the camera in; it eases back out.
		const bool turbo = drivers[i].flags & DriverTurbo;
		if (turbo && !c.turbo) {
			const uint8_t level = powers.empty() ? 0 : powers[i].turboLevel;
			c.pull = level == 0 ? -0.2f : level == 1 ? -0.4f : level == 2 ? -0.6f : 1.0f;
		}
		c.turbo = turbo;
		float D = pr.distance;
		if (c.pull < 1) {
			c.pull += ef * (c.split ? 0.004f : 0.002f);
			D = c.pull < 1 ? std::max(12.0f, std::fabs(c.pull) * D) : D;
		}
		if (kind == PlayerCamera::Finish && c.kind == PlayerCamera::Finish) {
			c.clockMs += e; // (its clock runs before it is read)
		}
		const float sinH = std::sin(radians(pr.height)), sinP = std::sin(radians(pr.pitch)), cosP = std::cos(radians(pr.pitch));
		if (kind == PlayerCamera::Finish && c.clockMs < 2000) {
			// The finish view swings round the kart over 2 s, ending in front of it looking back. (Its height, pitch and
			// distance hold at once: the stored differences come out zero.)
			const float a = float(c.clockMs) * 0.0015707964f, ca = std::cos(a), sa = std::sin(a);
			O = {(ca * c.vx - sa * c.vy) * cosP, (ca * c.vy + sa * c.vx) * cosP, -sinP};
		}
		else if (kind == PlayerCamera::Finish) {
			O = {-cosP * c.vx, -cosP * c.vy, -sinP};
		}
		else {
			O = {cosP * c.vx, cosP * c.vy, -sinP};
		}
		const Vec3 B = snap ? O : basisOf(c.viewRotation).u; // the view's own last look (not a transition's)
		rawEye = c.target - Vec3{B.x * D, B.y * D, (B.z - sinH) * D};
		look = O;
		down = {0, 0, -1};
	}
	Vec3 r, d, l;
	rowsFrom(look, down, r, d, l);
	const Quat raw = quatOfRows(r, d, l);
	c.rawEye = rawEye;
	c.rawRotation = raw;

	// The view's own pose, smoothed after the raw one (a snap takes it).
	if (snap) {
		c.viewEye = rawEye;
		c.viewRotation = raw;
	}
	else {
		c.viewEye = rawEye + (c.viewEye - rawEye) * (1 / (lagRate(pr.positionLag) * ef + 1));
		c.viewRotation = blend(raw, c.viewRotation, 1 / (lagRate(pr.rotationLag) * ef + 1));
	}
	if (c.kind == PlayerCamera::Transition) {
		// From where it was toward the chosen view's raw pose as the race's setup computed it, over 2 s.
		if (c.clockMs == 0) {
			c.fromEye = c.eye;
			c.fromRotation = c.rotation;
		}
		c.clockMs += e;
		const float t = float(c.clockMs) * 0.0005f;
		c.eye = c.fromEye + (c.toEye - c.fromEye) * t;
		setRows(c, blend(c.fromRotation, c.toRotation, t));
		if (c.clockMs >= 2000) {
			c.kind = Profiles[c.profile].kind;
			c.clockMs = 0;
		}
		return;
	}
	c.eye = c.viewEye;
	setRows(c, c.viewRotation);
}

void World::viewKeys(size_t i)
{
	const uint16_t now = keys[i], before = lastKeys[i];
	lastKeys[i] = now;
	for (size_t p = 0; p < cameras.size(); p++) {
		PlayerCamera& c = cameras[p];
		if (c.racer != int32_t(i)) {
			continue;
		}
		const Racer& r = racers[i];
		if ((now & KeyCamera) && !(before & KeyCamera) && !(r.flags & RacerFinished) && session.phase >= Session::Countdown) {
			chooseView(p, uint8_t((c.view + 1) & 3));
			brief.views[p] = c.view;
		}
		if ((now & KeyMap) && !(before & KeyMap) && p < mapModes.size()) {
			mapModes[p] = uint8_t((mapModes[p] + 1) & 3);
			brief.maps[p] = mapModes[p];
		}
		c.lookBack = (now & KeyLookBack) && c.kind != PlayerCamera::Transition && c.kind != PlayerCamera::Flyby;
		// The finish view, once the racer is over the line and not warping (not back to the garage).
		if ((r.flags & RacerFinished) && !(r.flags & (RacerGhost | RacerWarping)) && c.profile != FinishView &&
			brief.mode != RaceBrief::Mode::TestDrive) {
			chooseView(p, FinishView);
			c.dirty = false; // (it swings from where it is: no snap)
		}
	}
}

void World::drawScenery()
{
	drawnBy.assign(clocks.size(), 0);
	for (size_t p = 0; p < cameras.size(); p++) {
		const PlayerCamera& c = cameras[p];
		if (c.hidden || c.racer < 0) {
			continue;
		}
		const Vec3 eye = c.shownEye();
		for (size_t m = 0; m < clocks.size(); m++) {
			const AnimatedModel& model = track.animated[m];
			if (model.model.empty()) {
				continue;
			}
			// The playing part's bounds (the model's without part animation), scaled and placed.
			Sphere b = model.bounds;
			const Clock& clock = clocks[m];
			if (model.animation >= 0 && clock.part >= 0) {
				b = track.animations[size_t(model.animation)].parts[size_t(clock.part)].bounds;
			}
			const Vec3 centre = toWorld(model.pose.rotation, b.centre * model.scale) + model.pose.position;
			if (m >= animatedDistances.size() || !withinDistance(animatedDistances[m], lengthSquared(centre - eye))) {
				continue;
			}
			if (c.outside(centre, b.radius * model.scale)) {
				continue;
			}
			drawnBy[m] |= uint8_t(1u << p);
			clocks[m].drawn = clocks[m].time;
		}
	}
}

} // namespace lr
