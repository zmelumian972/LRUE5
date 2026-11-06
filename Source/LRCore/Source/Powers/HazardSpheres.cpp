// The hazards whose sphere in the trigger list meets the racers: the ghost, the lava, the rocket, the crane's hook and
// the grabbers. Each sphere is added as its hazard starts, follows its model each frame and goes as it ends.

#include "Powers/HazardParts.h"
#include "LRCore/Common/Tables.h"
#include "Kart/Steering.h"
#include "Show/SoundIds.h"

namespace lr {

namespace {

constexpr int32_t CraneHitEvent = 21; // the crane's hook hitting a racer, at the racer
constexpr int32_t HookDropEvent = 20; // the crane's hook dropping, at the hook
constexpr int32_t RocketLitEvent = 35; // the rocket lit (started) or out (ended)
constexpr int32_t EruptionEvent = 43; // a lava eruption's start or end, at its spot
// The spheres' radii: the rocket's, the crane hook's, a grabber's; the ghost's is its model's and this much more.
constexpr float RocketRadius = 350, CraneRadius = 3, GrabberRadius = 45, GhostMargin = 8;
constexpr int32_t GhostSearchMs = 4000;
constexpr float GhostNear2 = 3600; // its "near" sound for a racer within 60 ft
constexpr int32_t EruptionCooldownMs = 400;
constexpr float EruptionFrames = 10; // an eruption's start or end, this many frames from its time
constexpr int32_t GrabHoldMs = 1000, GrabCycleMs = 500;
constexpr float HazardSpinRate = 0.01f; // a racer the ghost, the lava or the crane spins
constexpr float GhostLift = 150; // the ghost's hit throws a racer up
constexpr float GrabbedPace = -0.4f; // a route kart in a grabber's hold

void addSphere(HazardSite s)
{
	s.w.triggers.killMember(s.h.member);
	s.h.member = s.w.triggers.addMember(s.w, {s.h.centre, s.h.radius, Member::Hazard, false, false, uint16_t(s.index)});
}

void moveSphere(HazardSite s, Vec3 centre)
{
	s.h.centre = centre;
	if (s.h.member >= 0) {
		s.w.triggers.members[s.h.member].centre = centre;
	}
}

// Started: its sphere added, active.
void wake(HazardSite s, int32_t)
{
	addSphere(s);
	s.h.cooldownMs = 0;
	s.h.state = Hazard::Active;
}

// Ended: its loop (if sounding) stopped, its sphere gone, idle.
void goIdle(HazardSite s, int32_t loop, bool sounding)
{
	if (sounding) {
		s.w.raceView.stopSound(loop, s.owner());
		s.h.on = false;
	}
	s.w.triggers.killMember(s.h.member);
	s.h.state = Hazard::Idle;
}

// Whether a racer the sphere meets is spun (not spinning already, and moving).
bool spinnable(const Kart& k) { return !(k.flags & KartSpinning) && k.headingSpeed != 0; }

float animatedRadius(const World& w, int16_t model) { return model >= 0 ? w.track.animated[model].radius : 0; }

// --- The ghost: its wail while active, its sphere on its bone 1; it throws a racer up ---

void setupGhost(HazardSite s) { s.h.radius = animatedRadius(s.w, s.w.track.hazardEntities.ghost) + GhostMargin; }
void endGhost(HazardSite s) { goIdle(s, GhostWailSound, true); }

void startGhost(HazardSite s, int32_t racer)
{
	wake(s, racer);
	s.w.raceView.loopSound(GhostWailSound, s.owner(), s.h.centre); // (the show wobbles its pitch.2 step 8)
}

void ghostFrame(HazardSite s, int32_t ms)
{
	World& w = s.w;
	Hazard& h = s.h;
	// Its search for racers near (a sound): every 4 s while it is not idle.
	h.cycleMs += ms;
	const bool search = h.state != Hazard::Idle && h.cycleMs > GhostSearchMs;
	h.cycleMs = search ? 0 : h.cycleMs;
	if (h.state == Hazard::Idle) {
		return;
	}
	if (h.state == Hazard::Ending) {
		endHazard(s); // (the rest still runs)
	}
	const int16_t m = w.track.hazardEntities.ghost;
	if (m >= 0 && h.state != Hazard::Idle) {
		moveSphere(s, bonePoint(w, size_t(m), 1, w.scenery.clocks[m].drawn));
	}
	// A racer within 60 ft of it: its "near" sound (QUIRK: of the bone's scaled local place, the same for "ghostly").
	for (size_t r = 0; search && r < w.racers.race.size(); r++) {
		if (lengthSquared(w.racers.visible[r].position - h.centre) < GhostNear2) {
			w.raceView.playSound(GhostNearSound, s.owner(), &h.centre, 200, 300);
			break;
		}
	}
}

void ghostTouch(HazardSite s, size_t i)
{
	World& w = s.w;
	Kart& k = w.racers.karts[i];
	if (spinnable(k) && !(w.racers.race[i].flags & RacerShielded)) {
		w.powerUps.spin(w, i, 1, HazardSpinRate, 0);
		w.powerUps.spinOut(w, i);
		k.velocity = {};
		w.powerUps.impulse(w, i, {0, 0, GhostLift});
		w.raceView.voice(w, i, RaceEvent::Negative);
		w.raceView.playSound(GhostHitSound, s.owner(), &w.racers.visible[i].position, 200, 300);
	}
}

// --- The lava: its sphere on its bone 0, its eruptions at three spots by its clock, bubbling between them ---

void setupLava(HazardSite s) { s.h.radius = animatedRadius(s.w, s.w.track.hazardEntities.lava); }
void endLava(HazardSite s) { goIdle(s, LavaBubblingSound, s.h.on); }

void lavaFrame(HazardSite s, int32_t ms)
{
	World& w = s.w;
	Hazard& h = s.h;
	if (h.state == Hazard::Idle) {
		return;
	}
	if (h.state == Hazard::Ending) {
		endHazard(s); // (QUIRK: the rest still runs)
	}
	const int16_t m = w.track.hazardEntities.lava;
	const float t = m >= 0 ? w.scenery.clocks[m].time : 0;
	if (m >= 0) {
		moveSphere(s, bonePoint(w, size_t(m), 0, w.scenery.clocks[m].drawn));
	}
	if (h.cooldownMs != 0) {
		h.cooldownMs = ms > h.cooldownMs ? 0 : h.cooldownMs - ms;
		return;
	}
	const ExecutableTables& lava = executableTables();
	for (size_t i = 0; i < 3; i++) {
		Vec3 at = {lava.lavaSpots[i][0], lava.lavaSpots[i][1], lava.lavaSpots[i][2]};
		at.y = w.track.mirrored ? -at.y : at.y;
		const bool start = lava.lavaStarts[i] < t && t < lava.lavaStarts[i] + EruptionFrames;
		const bool end = lava.lavaEnds[i] < t && t < lava.lavaEnds[i] + EruptionFrames;
		if (start || end) {
			w.triggers.fireAt(w, EruptionEvent, EruptionEvent, &at);
			h.cooldownMs = EruptionCooldownMs;
			// Its bubbling: released as an eruption starts, played again as it ends.
			if (start && h.on) {
				w.raceView.stopSound(LavaBubblingSound, s.owner());
			}
			else if (end && !h.on) {
				w.raceView.loopSound(LavaBubblingSound, s.owner(), at, 200, 300);
			}
			h.on = end;
		}
	}
}

void lavaTouch(HazardSite s, size_t i)
{
	if (spinnable(s.w.racers.karts[i])) {
		s.w.powerUps.spin(s.w, i, 1, HazardSpinRate, 0);
		s.w.raceView.voice(s.w, i, RaceEvent::Negative);
	}
}

// --- The rocket: lit while a shielded racer is in its sphere (at the unlit rocket's place) ---

// Its look (the on model's distances the off model's as loaded, the off model's -1; or the
// reverse), its event and its surface.
void lightRocket(World& w, bool on)
{
	const int32_t off = w.track.hazardEntities.rocketOff, lit = w.track.hazardEntities.rocketLit;
	const std::array<float, 3> hidden = {-1, -1, -1};
	if (off >= 0) {
		w.scenery.modelDistances[off] = on ? hidden : w.track.modelDistances[off];
	}
	if (lit >= 0) {
		w.scenery.modelDistances[lit] = on && off >= 0 ? w.track.modelDistances[off] : hidden;
	}
	if (on) {
		w.triggers.startAt(w, RocketLitEvent, nullptr);
	}
	else {
		w.triggers.endAt(w, RocketLitEvent, nullptr);
	}
	setSurface(w, w.track.hazardEntities.rocketSurface, on);
}

void setupRocket(HazardSite s)
{
	lightRocket(s.w, false); // shown off
	const int32_t off = s.w.track.hazardEntities.rocketOff;
	s.h.centre = off >= 0 ? s.w.track.models[off].second.position : Vec3{};
	s.h.radius = RocketRadius;
}

void endRocket(HazardSite s)
{
	s.h.on = false;
	lightRocket(s.w, false);
	s.w.triggers.killMember(s.h.member);
	s.h.state = Hazard::Idle;
}

void rocketFrame(HazardSite s, int32_t)
{
	if (s.h.lit != s.h.on) {
		s.h.on = s.h.lit;
		lightRocket(s.w, s.h.on);
	}
	s.h.lit = false;
}

void rocketTouch(HazardSite s, size_t i) { s.h.lit = s.h.lit || (s.w.racers.race[i].flags & RacerShielded); }

// --- The crane: its hum (at the crane's own place, not the hook's: heard from 300 ft of the "crane" entity), its
// hook's sphere at the end of its arm, dropping twice a swing ---

void setupCrane(HazardSite s) { s.h.radius = CraneRadius; }
void endCrane(HazardSite s) { goIdle(s, CraneHumSound, true); }

void startCrane(HazardSite s, int32_t)
{
	World& w = s.w;
	s.h.armed = true;
	addSphere(s);
	const int16_t m = w.track.hazardEntities.crane;
	w.raceView.loopSound(CraneHumSound, s.owner(), m >= 0 ? w.track.animated[m].pose.position : s.h.centre, 100, 300);
	s.h.state = Hazard::Active;
}

void craneFrame(HazardSite s, int32_t)
{
	World& w = s.w;
	Hazard& h = s.h;
	const int16_t m = w.track.hazardEntities.crane;
	if (m < 0) {
		return;
	}
	// The hook: the end of the crane's arm (node 3, through its parents), as last drawn.
	const Vec3 hookAt = nodeWorld(w, size_t(m), 3, {}, w.scenery.clocks[m].drawn);
	moveSphere(s, hookAt);
	const float t = w.scenery.clocks[m].time;
	if (h.armed && ((t > 150 && t < 180) || (t > 0 && t < 30))) {
		h.armed = false;
		w.triggers.fireAt(w, HookDropEvent, HookDropEvent, &hookAt);
	}
	else if (!h.armed && ((t > 60 && t < 120) || (t > 210 && t < 270))) {
		h.armed = true;
	}
}

void craneTouch(HazardSite s, size_t i)
{
	if (spinnable(s.w.racers.karts[i])) {
		s.w.powerUps.spin(s.w, i, 1, HazardSpinRate, 0);
		const Vec3 at = s.w.racers.visible[i].position;
		s.w.triggers.fireAt(s.w, CraneHitEvent, CraneHitEvent, &at);
	}
}

// --- A grabber: its sphere on its model's node 0; it holds one racer (pulled to it, or a route kart slowed) while
// its claw is closed, lets go after its hold and rests ---

void releaseGrab(World& w, Hazard& h)
{
	if (h.held < 0) {
		return;
	}
	const size_t r = size_t(h.held);
	w.powerUps.endPush(w, r, 1);
	w.racers.race[r].flags &= ~RacerGrabbed;
	if (w.racers.playback[r].active) {
		routePace(w.racers.playback[r], w.racers.karts[r], 1);
	}
	h.held = -1;
}

void setupGrabber(HazardSite s) { s.h.radius = GrabberRadius; }

void startGrabber(HazardSite s, int32_t racer)
{
	wake(s, racer);
	s.h.grab = 0;
}

void endGrabber(HazardSite s)
{
	releaseGrab(s.w, s.h);
	s.w.triggers.killMember(s.h.member);
	s.h.grab = 0;
	s.h.cooldownMs = 0;
	s.h.state = Hazard::Idle;
}

void grabberFrame(HazardSite s, int32_t ms)
{
	Hazard& h = s.h;
	if (s.spec.model >= 0) {
		moveSphere(s, bonePoint(s.w, size_t(s.spec.model), 0, s.w.scenery.clocks[s.spec.model].drawn));
	}
	bool release = false;
	if (h.cycleMs != 0) {
		if (h.cycleMs <= ms) {
			h.cycleMs = 0;
			release = true;
			h.grab = 0;
			h.cooldownMs = 0;
		}
		else {
			h.cycleMs -= ms;
		}
	}
	if (h.cooldownMs != 0) {
		if (ms >= h.cooldownMs) {
			h.cooldownMs = 0;
			if (h.grab == 1) {
				release = true;
				h.cycleMs = 0;
				h.grab = 2;
				h.cooldownMs = GrabHoldMs;
			}
			else if (h.grab == 2) {
				h.grab = 0;
			}
		}
		else {
			h.cooldownMs -= ms;
		}
	}
	if (release) {
		releaseGrab(s.w, h);
	}
}

void grabberTouch(HazardSite s, size_t i)
{
	World& w = s.w;
	Hazard& h = s.h;
	const float t = s.spec.model >= 0 ? w.scenery.clocks[s.spec.model].time : 0;
	if (h.grab == 2 || !(t <= s.spec.low || t >= s.spec.high) || (w.racers.race[i].flags & RacerShielded) ||
		(h.held >= 0 && h.held != int8_t(i))) {
		return;
	}
	if (h.held < 0) {
		h.grab = 1;
		h.cooldownMs = GrabHoldMs;
	}
	h.held = int8_t(i);
	w.racers.race[i].flags |= RacerGrabbed;
	if (w.racers.playback[i].active) {
		routePace(w.racers.playback[i], w.racers.karts[i], GrabbedPace);
	}
	else {
		const Vec3 c = w.racers.visible[i].position;
		const Vec3 q = normalize(Vec3{h.centre.x - c.x, h.centre.y - c.y, 0}) * s.spec.strength;
		w.powerUps.push(w, i, 1, q);
	}
	h.cycleMs = GrabCycleMs;
}

} // namespace

const HazardKind GhostHazard{.keyword = 0x2b,
	.trigger = 10,
	.setup = setupGhost,
	.activate = startGhost,
	.deactivate = endGhost,
	.frame = ghostFrame,
	.touch = ghostTouch,
	.ownsState = true};
const HazardKind LavaHazard{.keyword = 0x2c,
	.trigger = 0,
	.setup = setupLava,
	.activate = wake,
	.deactivate = endLava,
	.frame = lavaFrame,
	.touch = lavaTouch,
	.ownsState = true};
const HazardKind RocketHazard{.keyword = 0x2e,
	.trigger = 1,
	.setup = setupRocket,
	.activate = wake,
	.deactivate = endRocket,
	.frame = rocketFrame,
	.touch = rocketTouch};
const HazardKind CraneHazard{.keyword = 0x32,
	.trigger = 1,
	.setup = setupCrane,
	.activate = startCrane,
	.deactivate = endCrane,
	.frame = craneFrame,
	.touch = craneTouch};
const HazardKind GrabberHazard{.keyword = 0x48,
	.trigger = FromSpec,
	.setup = setupGrabber,
	.activate = startGrabber,
	.deactivate = endGrabber,
	.frame = grabberFrame,
	.touch = grabberTouch};

} // namespace lr
