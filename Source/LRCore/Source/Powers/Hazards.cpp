// The animated scenery's clocks and poses, and the hazards: each started and stopped by its trigger event (or a
// racer's, for the warp pad), updated once a frame after the event table, and some with a sphere in the trigger list
// that does something to the racers it meets. Each type's behaviour is its HazardKind (HazardParts.h); this file
// keeps the table and runs them.

#include "Common/Assert.h"
#include "Powers/HazardParts.h"

#include <algorithm>
#include <iterator>

namespace lr {

namespace {

constexpr uint32_t Passable = SurfaceNonSolid | SurfaceProjectilesPass;

// The oscillator: only the water's texture scrolls.
constexpr HazardKind OscillatorHazard{.keyword = 0x36};

// The kinds in Hazard::Type's order; a keyword not among them leaves its hazard inert.
const HazardKind* const Kinds[] = {&PillarHazard, &SphinxHazard, &HammerHazard, &GhostHazard, &LavaHazard, &CodeHazard, &RocketHazard,
	&SnowHazard, &SmokeHazard, &CraneHazard, &LauncherHazard, &OpenerHazard, &OscillatorHazard, &LaunchersHazard, &RockHazard,
	&CurseDropHazard, &SweepHazard, &RainHazard, &GrabberHazard, &WarpPadHazard};
static_assert(std::size(Kinds) == size_t(Hazard::WarpPad) + 1);

const HazardKind& kindOf(Hazard::Type type)
{
	static constexpr HazardKind Inert{};
	return type < std::size(Kinds) ? *Kinds[type] : Inert;
}

HazardSite site(World& w, size_t index)
{
	LR_ASSERT(index < w.scenery.hazards.size() && index < w.track.hazards.size());
	return {w, index, w.scenery.hazards[index], w.track.hazards[index]};
}

// A clock's frame: its time runs; at its part's last frame a looping part wraps, a
// queued part takes over (from time 0, looping: QUIRK, done set), else it holds there, done.
void advance(Clock& c, const Animation& a, int32_t ms)
{
	const Animation::Part& p = a.parts[c.part];
	c.time = c.rate * float(ms) + c.time;
	if (c.time >= float(p.frames - 1)) {
		if (c.loop) {
			c.time = wrapLoop(c.time, float(p.loopFrames));
		}
		else if (inRange(a.parts, c.queued)) {
			play(c, a, c.queued);
			c.done = true;
		}
		else {
			c.time = float(p.frames - 1);
			c.done = true;
		}
	}
}

// An animated model's clock (or a brick model's) set playing its first part, if it has one.
void startClock(Clock& c, const World& w, const AnimatedModel& m)
{
	c = {};
	if (m.animation >= 0 && m.part >= 0) {
		play(c, w.track.animations[m.animation], m.part);
		c.animating = true;
	}
}

void activate(HazardSite s, int32_t racer)
{
	const HazardKind& k = kindOf(s.h.type);
	if (s.h.state == Hazard::Idle && k.activate) {
		k.activate(s, racer);
	}
}

} // namespace

void play(Clock& c, const Animation& a, int16_t part)
{
	c.part = part;
	c.time = 0;
	c.loop = true;
	c.done = false;
	c.queued = -1;
	c.rate = a.parts[part].rate;
}

bool partDone(const World& w, size_t model)
{
	const Clock& c = w.scenery.clocks[model];
	const AnimatedModel& m = w.track.animated[model];
	if (c.done) {
		return true;
	}
	return m.animation >= 0 && c.part >= 0 && !c.loop && float(w.track.animations[m.animation].parts[c.part].frames - 1) <= c.time;
}

void setSurface(World& w, int16_t surface, bool passable)
{
	if (surface >= 0) {
		uint32_t& f = w.track.surfaces[surface].flags;
		f = passable ? f | Passable : f & ~Passable;
	}
}

Pose localPose(const World& w, size_t model, size_t node, float time)
{
	const AnimatedModel& m = w.track.animated[model];
	const Animation* a = w.track.animationOf(m);
	return w.scenery.nodeLocal(m, a, node, w.scenery.clocks[model].part, time, w.scenery.clocks[model].loop);
}

void endHazard(HazardSite s)
{
	if (s.h.state == Hazard::Idle) {
		return;
	}
	const HazardKind& k = kindOf(s.h.type);
	if (k.deactivate) {
		k.deactivate(s);
	}
	else {
		s.h.state = Hazard::Idle;
	}
}

Pose Scenery::nodeLocal(const AnimatedModel& m, const Animation* a, size_t node, int32_t part, float time, bool loop) const
{
	return sampleNode(m.nodes, a, node, part, time, loop);
}

void Scenery::advanceClocks(World& w, int32_t ms)
{
	for (size_t i = 0; i < clocks.size(); i++) {
		Clock& c = clocks[i];
		const AnimatedModel& m = w.track.animated[i];
		if (c.animating && m.animation >= 0 && c.part >= 0) {
			advance(c, w.track.animations[m.animation], ms);
		}
	}
	// The bricks' shared entities run from the load whether a brick is drawn or not (their clock is
	// the power-ups' update's, the same frame's ms).
	for (size_t b = 0; b < w.powerUps.brickClocks.size(); b++) {
		Clock& c = w.powerUps.brickClocks[b];
		const AnimatedModel& m = w.track.brickModels[b];
		if (c.animating && m.animation >= 0 && c.part >= 0) {
			advance(c, w.track.animations[m.animation], ms);
		}
	}
	// A sphinx's blow-up: its material track runs to its end once.
	if (blowup >= 0) {
		blowup = (float(w.track.blowupRate) / 1000) * float(ms) + blowup;
		if (int32_t(blowup) >= w.track.blowupDuration) {
			blowup = -1;
		}
	}
}

// Each hazard as the race starts: its kind (by its file's keyword), its trigger event, then its kind's own setup.
void Scenery::setupHazards(World& w)
{
	for (size_t b = 0; b < w.powerUps.brickClocks.size(); b++) {
		startClock(w.powerUps.brickClocks[b], w, w.track.brickModels[b]);
	}
	clocks.assign(w.track.animated.size(), {});
	for (size_t i = 0; i < w.track.animated.size(); i++) {
		startClock(clocks[i], w, w.track.animated[i]);
	}
	hazards.assign(w.track.hazards.size(), {});
	for (size_t index = 0; index < w.track.hazards.size(); index++) {
		const HazardSite s = site(w, index);
		const auto found = std::find_if(std::begin(Kinds), std::end(Kinds), [&](const HazardKind* k) { return k->keyword == s.spec.type; });
		s.h.type = Hazard::Type(found - std::begin(Kinds));
		const HazardKind& k = kindOf(s.h.type);
		s.h.trigger = k.trigger == FromSpec ? s.spec.trigger : k.trigger;
		if (k.setup) {
			k.setup(s);
		}
	}
}

void Scenery::hazardEvent(World& w, int32_t start, int32_t end, const Vec3* at, int32_t racer, bool force)
{
	for (size_t index = 0; index < hazards.size(); index++) {
		const HazardSite s = site(w, index);
		const HazardKind& k = kindOf(s.h.type);
		const int32_t t = s.h.trigger;
		if (force) {
			if (t != -1 && t == end) {
				s.h.state = Hazard::Ending;
			}
			continue;
		}
		const bool byRacer = racer >= 0;
		if (t != -1 && (!byRacer || k.byRacer)) {
			if (start != -1 && t == start) {
				activate(s, racer);
			}
			if (end != -1 && t == end && (!k.byRacer || byRacer)) {
				endHazard(s);
			}
		}
		if (start != -1 && k.started) {
			k.started(s, start, at);
		}
	}
}

void Scenery::hazardFrame(World& w, int32_t ms)
{
	w.raceView.stage = RaceEvent::Hazards;
	for (size_t index = 0; index < hazards.size(); index++) {
		const HazardSite s = site(w, index);
		const HazardKind& k = kindOf(s.h.type);
		if (!k.ownsState) {
			if (s.h.state == Hazard::Ending) {
				endHazard(s);
			}
			if (s.h.state == Hazard::Idle) {
				continue;
			}
		}
		if (k.frame) {
			k.frame(s, ms);
		}
	}
}

void Scenery::touchHazard(World& w, size_t index, size_t i)
{
	LR_ASSERT(i < w.racers.karts.size());
	const HazardSite s = site(w, index);
	const HazardKind& k = kindOf(s.h.type);
	if (k.touch) {
		k.touch(s, i);
	}
}

} // namespace lr
