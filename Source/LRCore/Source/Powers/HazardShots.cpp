// The hazards that launch or make something: the launchers' balls, the curse drop, the sweepers' lightning, the
// emplacements' rain of cannonballs and the warp pad.

#include "Powers/HazardParts.h"
#include "LRCore/Common/Tables.h"
#include "Powers/PowerParts.h"

#include <cmath>

namespace lr {

namespace {

constexpr int32_t LaunchEvent = 6; // a launcher's launch, at the launch point
// A launcher's ball: its life and speed.
constexpr int32_t BallLifeMs = 3000;
constexpr float BallSpeed = 180;
constexpr int32_t SweepMs = 8300; // a sweeper's lightning, this often
constexpr float SweepBelow = 17; // its bolts start this far below its model
constexpr float RainBelow = 35; // a ball drops this far under its emitter's node
// The warp pad's green power: made here, aimed this way (y negated on a mirrored track).
constexpr Vec3 WarpPadAt = {132.673f, 86.304f, 14.722f}, WarpPadAim = {1, -0.5f, 0};

Vec3 mirrored(const World& w, Vec3 v) { return {v.x, w.track.mirrored ? -v.y : v.y, v.z}; }
Vec3 pointOf(const std::array<float, 4>& p) { return {p[0], p[1], p[2]}; }

// --- A launcher: its trigger event's start within its reach of its trigger point launches its ball (a multi-launcher
// from a launch point at a target, both drawn), which flies until it hits something or its life runs out ---

void launchBall(HazardSite s, Vec3 from, Vec3 to)
{
	s.h.state = Hazard::Active;
	Projectile& p = s.h.ball;
	p.state = Projectile::Flying;
	p.start = p.at = from;
	p.target = to;
	p.gravity = ShotGravity;
	p.lifeMs = BallLifeMs;
	p.ageMs = 0;
	const Vec3 d = to - from;
	const float dist = std::sqrt(d.y * d.y + d.x * d.x);
	const float t = dist / BallSpeed, inv = 1 / t;
	p.velocity = {inv * d.x, inv * d.y, (d.z / dist) * BallSpeed - (p.gravity * dist) / (BallSpeed + BallSpeed)};
	s.w.triggers.fireAt(s.w, LaunchEvent, LaunchEvent, &from);
}

bool launches(HazardSite s, int32_t e, const Vec3* at)
{
	return e == s.spec.trigger && s.spec.trigger != -1 && s.h.state == Hazard::Idle &&
		!(at && lengthSquared(s.spec.triggerPoint - *at) >= s.h.radius);
}

void launcherEvent(HazardSite s, int32_t e, const Vec3* at)
{
	if (launches(s, e, at)) {
		launchBall(s, s.spec.source, s.spec.target);
	}
}

void launchersEvent(HazardSite s, int32_t e, const Vec3* at)
{
	if (!launches(s, e, at)) {
		return;
	}
	World& w = s.w;
	s.h.launch = int8_t(uint16_t(w.draw(Draw::Hazard)) % s.spec.launches.size());
	s.h.aimed = int8_t(uint16_t(w.draw(Draw::Hazard)) % s.spec.targets.size());
	const std::array<float, 4>& l = s.spec.launches[s.h.launch];
	const Vec3 from = pointOf(l);
	w.triggers.fireAt(w, int32_t(l[3]), int32_t(l[3]), &from);
	launchBall(s, from, pointOf(s.spec.targets[s.h.aimed]));
}

void endLauncher(HazardSite s)
{
	s.h.ball.state = Projectile::Idle;
	s.h.state = Hazard::Idle;
}

// A multi-launcher's end fires its target's event there.
void endLaunchers(HazardSite s)
{
	const std::array<float, 4>& t = s.spec.targets[s.h.aimed];
	const Vec3 at = pointOf(t);
	s.w.triggers.fireAt(s.w, int32_t(t[3]), int32_t(t[3]), &at);
	endLauncher(s);
}

void launcherFrame(HazardSite s, int32_t ms)
{
	World& w = s.w;
	Projectile& p = s.h.ball;
	if (p.state == Projectile::Flying) {
		p.ageMs += ms;
		if (p.ageMs >= p.lifeMs) {
			p.state = Projectile::Expired; // QUIRK: it sticks, the launcher never fires again
		}
		else {
			const float e = float(p.ageMs) * 0.001f;
			const Vec3 x = {e * p.velocity.x + p.start.x, p.velocity.y * e + p.start.y,
				(p.velocity.z * e + p.start.z) + ((e * e) * 0.5f) * p.gravity};
			Vec3 hit, normal;
			if (w.powerUps.shotHits(w, p.at, x, hit, normal)) {
				p.at = hit;
				w.powerUps.explode(w, false, hit, false, -1);
				p.state = Projectile::Idle;
				w.triggers.fireAt(w, LandingEvent, LandingEvent, &hit);
			}
			else {
				p.at = x;
			}
		}
	}
	if (p.state == Projectile::Idle) {
		endHazard(s);
	}
}

// --- The curse drop: a yellow power at level 3, no racer's, made at one of three spots (drawn) ---

void dropCurse(HazardSite s, int32_t)
{
	World& w = s.w;
	const std::array<float, 3>& spot = executableTables().curseDrop[size_t(int16_t(w.draw(Draw::Hazard)) % 3)];
	const Aim aim{mirrored(w, {spot[0], spot[1], spot[2]}), {1, 0, 0}};
	w.powerUps.make(w, -1, Yellow, 3, &aim);
	s.h.state = Hazard::Active;
}

// --- A sweeper: its aim sweeps (across by its angle, up and down by its range about its base), its lightning made
// every 8.3 s ---

void startSweep(HazardSite s, int32_t)
{
	s.h.cooldownMs = SweepMs;
	s.h.state = Hazard::Active;
}

void sweepFrame(HazardSite s, int32_t ms)
{
	World& w = s.w;
	Hazard& h = s.h;
	const HazardSpec& spec = s.spec;
	const int32_t period = spec.periodMs;
	h.cycleMs += ms;
	if (h.cycleMs >= period) {
		h.cycleMs = 0;
	}
	const float n = float(h.cycleMs);
	const int32_t half = period >> 1, quarter = period >> 2;
	float angle = (n / float(half)) * spec.sweep.x;
	if (h.cycleMs > half) {
		angle = Pi - angle;
	}
	Vec3 f{cosEntry(int32_t(angle * 162.97466f)), cosEntry(-256 - int32_t(angle * -162.97466f)),
		cosEntry(int32_t(((n / float(quarter)) * Tau) * 162.97466f)) * spec.sweep.y + spec.sweep.z};
	f = normalize(mirrored(w, f));
	const int16_t model = spec.model;
	const Vec3 source = model >= 0 ? w.track.animated[model].pose.position - Vec3{0, 0, SweepBelow} : spec.source;
	h.cooldownMs += ms;
	if (h.cooldownMs >= SweepMs) {
		h.cooldownMs = 0;
		const Aim aim{source, f};
		w.powerUps.make(w, -1, Red, 2, &aim);
	}
}

// --- An emplacement's rain: a cannonball (a red power at level 0, no racer's) every interval, dropped from under its
// emitter's node 1 (sampled now), a little to one side ---

void startRain(HazardSite s, int32_t)
{
	s.h.cycleMs = s.spec.intervalMs;
	s.h.state = Hazard::Active;
}

void rainFrame(HazardSite s, int32_t ms)
{
	World& w = s.w;
	Hazard& h = s.h;
	const int16_t model = s.spec.model;
	h.cycleMs += ms;
	if (h.cycleMs < s.spec.intervalMs || model < 0) {
		return;
	}
	h.cycleMs = 0;
	const Pose node = localPose(w, size_t(model), 1, w.scenery.clocks[model].time);
	const AnimatedModel& m = w.track.animated[model];
	const Vec3 at = modelToWorld(m, node.position);
	const Vec3 u = toWorld(m.pose.rotation, node.rotation.u);
	h.muzzle = {at.x - u.x * RainBelow, at.y - u.y * RainBelow, at.z - u.z * RainBelow};
	const int16_t rx = int16_t(w.draw(Draw::Hazard)), ry = int16_t(w.draw(Draw::Hazard));
	Vec3 drop = {(float(rx % 4) + h.muzzle.x) - 2, (float(ry % 4) + h.muzzle.y) - 2, h.muzzle.z + 1};
	if (drop.x == h.muzzle.x && drop.y == h.muzzle.y) {
		drop.x = drop.x + 2;
	}
	h.point = drop;
	const Aim aim{h.muzzle, {}, int8_t(s.index)};
	w.powerUps.make(w, -1, Red, 0, &aim);
}

// --- The warp pad: a racer's event (not a ghost's) makes its green power at level 3, a warp to its place ---

void warpPad(HazardSite s, int32_t racer)
{
	World& w = s.w;
	if (racer >= 0 && !(w.racers.race[racer].flags & RacerGhost)) {
		const Aim aim{mirrored(w, WarpPadAt), mirrored(w, WarpPadAim)};
		w.powerUps.make(w, int8_t(racer), Green, 3, &aim);
		s.h.state = Hazard::Active;
	}
}

void setupLauncher(HazardSite s) { s.h.radius = s.spec.triggerRadius * s.spec.triggerRadius; }

} // namespace

const HazardKind LauncherHazard{
	.keyword = 0x33, .setup = setupLauncher, .deactivate = endLauncher, .frame = launcherFrame, .started = launcherEvent};
const HazardKind LaunchersHazard{
	.keyword = 0x3d, .setup = setupLauncher, .deactivate = endLaunchers, .frame = launcherFrame, .started = launchersEvent};
const HazardKind CurseDropHazard{.keyword = 0x3f, .trigger = 8, .activate = dropCurse};
const HazardKind SweepHazard{.keyword = 0x40, .trigger = FromSpec, .activate = startSweep, .frame = sweepFrame};
const HazardKind RainHazard{.keyword = 0x43, .trigger = FromSpec, .activate = startRain, .frame = rainFrame};
const HazardKind WarpPadHazard{.keyword = 0x49, .trigger = 0, .activate = warpPad, .byRacer = true};

} // namespace lr
