// The animated scenery's clocks and poses, and the hazards: each started and stopped by its trigger event (or a
// racer's, for the warp pad), updated once a frame after the event table, and some with a sphere in the trigger list
// that does something to the racers it meets.

#include "LRCore/Race/World.h"
#include "LRCore/Common/Tables.h"
#include "Common/Assert.h"
#include "Kart/Steering.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace lr {

namespace {

// The hazard file's keywords, in Hazard::Type's order.
constexpr int32_t TypeKeywords[] = {0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x32, 0x33, 0x34, 0x36, 0x3d, 0x3e, 0x3f, 0x40, 0x43, 0x48, 0x49};

constexpr uint32_t Passable = SurfaceNonSolid | SurfaceProjectilesPass;

// The events the hazards fire, start and end.
constexpr int32_t LaunchEvent = 6; // a launcher's launch, at the launch point
constexpr int32_t LandingEvent = 7; // the pillar's landing; a launcher's ball hitting something, at the hit point
constexpr int32_t OpenerEvent = 9; // the triggered animation's start
constexpr int32_t BlowupEvent = 16; // the sphinx blowing up, at its place
constexpr int32_t CraneHitEvent = 21; // the crane's hook hitting a racer, at the racer
constexpr int32_t HookDropEvent = 20; // the crane's hook dropping, at the hook
constexpr int32_t RocketLitEvent = 35; // the rocket lit (started) or out (ended)
constexpr int32_t HammerStrikeEvent = 43; // the hammer's strike
constexpr int32_t EruptionEvent = 43; // a lava eruption's start or end, at its spot
// The code puzzle: the switches 200..205 (each digit's pair), the display's read-out 207..209, the success, the
// display's blink, and the feedback fired for a switch (by its side) and a read-out digit (right or wrong).
constexpr int32_t SwitchEvents = 200;
constexpr int32_t ReadOutEvents = 207;
constexpr int32_t CodeSuccessEvent = 18;
constexpr int32_t CodeDisplayEvent = 28;
constexpr int32_t EvenSwitchFeedback = 21, OddSwitchFeedback = 30;
constexpr int32_t RightDigitFeedback = 29, WrongDigitFeedback = 20;

// The hazards' own sounds.
constexpr int32_t LavaBubblingSound = 3002;
constexpr int32_t CraneHumSound = 3003;
constexpr int32_t GhostWailSound = 3012;
constexpr int32_t GhostNearSound = 3013;
constexpr int32_t GhostHitSound = 3015;

void play(Clock& c, const Animation& a, int16_t part)
{
	c.part = part;
	c.time = 0;
	c.loop = true;
	c.done = false;
	c.queued = -1;
	c.rate = a.parts[size_t(part)].rate;
}

// A clock's frame: its time runs; at its part's last frame a looping part wraps, a
// queued part takes over (from time 0, looping: QUIRK, done set), else it holds there, done.
void advance(Clock& c, const Animation& a, int32_t ms)
{
	const Animation::Part& p = a.parts[size_t(c.part)];
	c.time = c.rate * float(ms) + c.time;
	if (c.time >= float(p.frames - 1)) {
		if (c.loop) {
			c.time = wrapLoop(c.time, float(p.loopFrames));
		}
		else if (c.queued >= 0 && size_t(c.queued) < a.parts.size()) {
			play(c, a, c.queued);
			c.done = true;
		}
		else {
			c.time = float(p.frames - 1);
			c.done = true;
		}
	}
}

bool partDone(const World& w, size_t model)
{
	const Clock& c = w.scenery.clocks[model];
	const AnimatedModel& m = w.track.animated[model];
	if (c.done) {
		return true;
	}
	return m.animation >= 0 && c.part >= 0 && !c.loop && float(w.track.animations[size_t(m.animation)].parts[size_t(c.part)].frames - 1) <= c.time;
}

void setSurface(World& w, int16_t surface, bool passable)
{
	if (surface >= 0) {
		uint32_t& f = w.track.surfaces[size_t(surface)].flags;
		f = passable ? f | Passable : f & ~Passable;
	}
}

// A model's point (in a node's frame, through its parents up to the root), scaled and placed in the world, at the
// pose its last drawing gave it.
Vec3 scenePoint(const World& w, size_t model, size_t node, Vec3 p, bool parents, float time)
{
	const AnimatedModel& m = w.track.animated[model];
	const Animation* a = m.animation >= 0 ? &w.track.animations[size_t(m.animation)] : nullptr;
	const Clock& c = w.scenery.clocks[model];
	for (int32_t n = int32_t(node); n >= 0; n = parents ? m.nodes[size_t(n)].parent : -1) {
		const Pose local = w.nodeLocal(m, a, size_t(n), c.part, time, c.loop);
		p = toWorld(local.rotation, p) + local.position;
	}
	return toWorld(m.pose.rotation, p * m.scale) + m.pose.position;
}

// A node's own pose (in its parent's frame) at a time of the model's part.
Pose localPose(const World& w, size_t model, size_t node, float time)
{
	const AnimatedModel& m = w.track.animated[model];
	const Animation* a = m.animation >= 0 ? &w.track.animations[size_t(m.animation)] : nullptr;
	return w.nodeLocal(m, a, node, w.scenery.clocks[model].part, time, w.scenery.clocks[model].loop);
}

// A bone's place: its own position (parents not applied), placed in the world.
Vec3 bonePoint(const World& w, size_t model, size_t node, float time)
{
	const AnimatedModel& m = w.track.animated[model];
	return toWorld(m.pose.rotation, localPose(w, model, node, time).position * m.scale) + m.pose.position;
}

void addSphere(World& w, size_t index)
{
	Hazard& h = w.scenery.hazards[index];
	w.killMember(h.member);
	h.member = w.addMember({h.centre, h.radius, Member::Hazard, false, false, uint16_t(index)});
}

void moveSphere(World& w, Hazard& h, Vec3 centre)
{
	h.centre = centre;
	if (h.member >= 0) {
		w.triggers.members[size_t(h.member)].centre = centre;
	}
}

// The rocket's look: the on model's distances the off model's as loaded, the off model's -1; or
// the reverse.
void rocketShown(World& w, bool on)
{
	const int32_t off = w.track.modelOf("mmrocof"), lit = w.track.modelOf("mmrocon");
	const std::array<float, 3> hidden = {-1, -1, -1};
	if (off >= 0) {
		w.scenery.modelDistances[size_t(off)] = on ? hidden : w.track.modelDistances[size_t(off)];
	}
	if (lit >= 0) {
		w.scenery.modelDistances[size_t(lit)] = on && off >= 0 ? w.track.modelDistances[size_t(off)] : hidden;
	}
}

// The code display's three material tracks (mmcode1..3's bindings), -1 where a light is missing.
std::array<int32_t, 3> codeTracks(const World& w)
{
	std::array<int32_t, 3> out = {-1, -1, -1};
	for (int d = 0; d < 3; d++) {
		const int32_t model = w.track.modelOf("mmcode" + std::to_string(d + 1));
		for (const Track::MaterialBinding& b : w.track.materialBindings) {
			if (!b.animated && b.entity == model && model >= 0 && out[size_t(d)] < 0) {
				out[size_t(d)] = b.track;
			}
		}
	}
	return out;
}

// The display reads the code out (each light still: red for the even switch, blue for the odd one) or blinks (each
// track runs again, looping, from where the read-out left it); the show hears which.
void codeDisplay(World& w, const Hazard& h, bool blink)
{
	const std::array<int32_t, 3> tracks = codeTracks(w);
	RaceEvent& e = w.raise(RaceEvent::CodeDisplay);
	e.flags = blink ? 1 : 0;
	for (int d = 0; d < 3; d++) {
		const bool blue = h.right[d] != 2 * d;
		e.value |= blue ? 1 << d : 0;
		if (tracks[size_t(d)] < 0) {
			continue;
		}
		RaceView::MaterialState& m = w.raceView.materials[size_t(tracks[size_t(d)])];
		if (blink) {
			m.assigned = m.looping = true;
		}
		else {
			m.cursor = blue ? float(w.track.materialTracks[size_t(tracks[size_t(d)])].duration - 1) : 0;
			w.showMaterial(size_t(tracks[size_t(d)]));
			m.assigned = false;
		}
	}
}

// A code puzzle's choice: for each switch pair, which one is right (an odd draw: the even id) and which resets.
void chooseCode(World& w, Hazard& h)
{
	for (int d = 0; d < 3; d++) {
		const bool odd = w.draw(Draw::Code) & 1;
		const uint8_t even = uint8_t(2 * d), other = uint8_t(even + 1); // (kept as offsets from SwitchEvents)
		h.right[d] = odd ? even : other;
		h.reset[d] = odd ? other : even;
	}
}

// A launcher's ball, from its launch point at its target.
void launchBall(World& w, Hazard& h, Vec3 from, Vec3 to)
{
	h.state = Hazard::Active;
	Projectile& p = h.ball;
	p.state = Projectile::Flying;
	p.start = p.at = from;
	p.target = to;
	p.gravity = -32.1759987f;
	p.lifeMs = 3000;
	p.ageMs = 0;
	const Vec3 d = to - from;
	const float dist = std::sqrt(d.y * d.y + d.x * d.x);
	const float s = dist / 180, inv = 1 / s;
	p.velocity = {inv * d.x, inv * d.y, (d.z / dist) * 180 - (p.gravity * dist) / (180 + 180)};
	w.fireAt(LaunchEvent, LaunchEvent, &from);
}

void releaseGrab(World& w, Hazard& h)
{
	if (h.held < 0) {
		return;
	}
	const size_t r = size_t(h.held);
	w.endPush(r, 1);
	w.racers.race[r].flags &= ~RacerGrabbed;
	if (w.racers.playback[r].active) {
		routePace(w.racers.playback[r], w.racers.karts[r], 1);
	}
	h.held = -1;
}

} // namespace

Pose World::nodeLocal(const AnimatedModel& m, const Animation* a, size_t node, int32_t part, float time, bool loop) const { return sampleNode(m.nodes, a, node, part, time, loop); }

void World::advanceClocks(int32_t ms)
{
	for (size_t i = 0; i < scenery.clocks.size(); i++) {
		Clock& c = scenery.clocks[i];
		const AnimatedModel& m = track.animated[i];
		if (!c.animating || m.animation < 0 || c.part < 0) {
			continue;
		}
		advance(c, track.animations[size_t(m.animation)], ms);
	}
	// The bricks' shared entities run from the load whether a brick is drawn or not (their clock is
	// the power-ups' update's, the same frame's ms).
	for (size_t b = 0; b < powerUps.brickClocks.size(); b++) {
		Clock& c = powerUps.brickClocks[b];
		const AnimatedModel& m = track.brickModels[b];
		if (c.animating && m.animation >= 0 && c.part >= 0) {
			advance(c, track.animations[size_t(m.animation)], ms);
		}
	}
	// A sphinx's blow-up: its material track runs to its end once.
	if (scenery.blowup >= 0) {
		scenery.blowup = (float(track.blowupRate) / 1000) * float(ms) + scenery.blowup;
		if (int32_t(scenery.blowup) >= track.blowupDuration) {
			scenery.blowup = -1;
		}
	}
}

void World::brickNodes(Track::BrickModel which, Vec3 at, float size, std::vector<NodeMatrix>& out) const
{
	const AnimatedModel& m = track.brickModels[which];
	const Clock& c = powerUps.brickClocks[which];
	const Animation* a = m.animation >= 0 ? &track.animations[size_t(m.animation)] : nullptr;
	poseSkeleton(m.nodes, a, {a ? c.part : -1, c.time, true}, {at, Basis{}}, m.scale * size, out);
}

namespace {

// The code display's hazard: a code chosen, active at once, the display's tracks at their own rates (the binding kept).
void setupCode(World& w, Hazard& h)
{
	h.progress = 0;
	chooseCode(w, h);
	h.timer = 1;
	h.state = Hazard::Active;
	// The display's tracks at their own rates (the binding kept).
	const std::array<int32_t, 3> tracks = codeTracks(w);
	const float rates[3] = {0.003f, 0.0040000002f, 0.0049999999f};
	for (int d = 0; d < 3; d++) {
		if (tracks[size_t(d)] >= 0) {
			w.raceView.materials[size_t(tracks[size_t(d)])].rate = rates[d];
		}
	}
}

// The rocket's hazard: shown off, its sphere at the unlit rocket's place.
void setupRocket(World& w, Hazard& h)
{
	h.trigger = 1;
	rocketShown(w, false);
	w.endAt(RocketLitEvent, nullptr); // shown off
	setSurface(w, w.track.surfaceOf("mmrocc"), false);
	const Pose* off = w.track.modelPlace("mmrocof");
	h.centre = off ? off->position : Vec3{};
	h.radius = 350;
}

// A hazard as the race starts, by its type: its trigger event, its sphere's radius, what it shows.
void setupHazard(World& w, size_t index)
{
	const HazardSpec& spec = w.track.hazards[index];
	Hazard& h = w.scenery.hazards[index];
	const int32_t* type = std::find(std::begin(TypeKeywords), std::end(TypeKeywords), spec.type);
	h.type = Hazard::Type(type - TypeKeywords);
	switch (h.type) {
	case Hazard::Pillar: {
		h.trigger = 10;
		const int16_t m = w.track.animatedOf("piltop");
		if (m >= 0) {
			w.scenery.clocks[size_t(m)].animating = false;
		}
		break;
	}
	case Hazard::Sphinx:
		h.trigger = 12;
		h.on = true; // shown
		break;
	case Hazard::Hammer: h.trigger = 50; break;
	case Hazard::Ghost: {
		h.trigger = 10;
		const int16_t m = w.track.animatedOf("ghostly");
		h.radius = (m >= 0 ? w.track.animated[size_t(m)].radius : 0) + 8;
		break;
	}
	case Hazard::Lava: {
		h.trigger = 0;
		const int16_t m = w.track.animatedOf("mmlavbl");
		h.radius = m >= 0 ? w.track.animated[size_t(m)].radius : 0;
		break;
	}
	case Hazard::Code: setupCode(w, h); break;
	case Hazard::Rocket: setupRocket(w, h); break;
	case Hazard::Snow:
		h.state = Hazard::Active;
		h.armed = true; // (enabled)
		break;
	case Hazard::Smoke: h.trigger = 10; break;
	case Hazard::Crane: {
		h.trigger = 1;
		h.radius = 3;
		break;
	}
	case Hazard::Launcher:
	case Hazard::Launchers: h.radius = spec.triggerRadius * spec.triggerRadius; break;
	case Hazard::Opener:
		h.trigger = spec.trigger;
		for (int16_t m : spec.models) {
			if (m >= 0) {
				w.scenery.clocks[size_t(m)].animating = false;
			}
		}
		break;
	case Hazard::CurseDrop: h.trigger = 8; break;
	case Hazard::Rock:
		// A box of the file's size, its sphere its largest half size; its model's animation starts at a phase.
		h.trigger = spec.trigger;
		h.radius = std::max({spec.size.x * 0.5f, spec.size.y * 0.5f, spec.size.z * 0.5f});
		if (spec.model >= 0) {
			w.scenery.clocks[size_t(spec.model)].time = spec.startFrame;
		}
		break;
	case Hazard::Sweep:
	case Hazard::Rain: h.trigger = spec.trigger; break;
	case Hazard::Grabber:
		h.trigger = spec.trigger;
		h.radius = 45;
		break;
	case Hazard::WarpPad: h.trigger = 0; break;
	case Hazard::Oscillator: break; // (only the water's texture scrolls)
	}
}

} // namespace

void World::setupHazards()
{
	for (size_t b = 0; b < powerUps.brickClocks.size(); b++) {
		const AnimatedModel& m = track.brickModels[b];
		powerUps.brickClocks[b] = {};
		if (m.animation >= 0 && m.part >= 0) {
			play(powerUps.brickClocks[b], track.animations[size_t(m.animation)], m.part);
			powerUps.brickClocks[b].animating = true;
		}
	}
	scenery.clocks.assign(track.animated.size(), {});
	for (size_t i = 0; i < track.animated.size(); i++) {
		const AnimatedModel& m = track.animated[i];
		if (m.part >= 0 && m.animation >= 0) {
			play(scenery.clocks[i], track.animations[size_t(m.animation)], m.part);
			scenery.clocks[i].animating = true;
		}
	}
	scenery.hazards.assign(track.hazards.size(), {});
	for (size_t index = 0; index < track.hazards.size(); index++) {
		setupHazard(*this, index);
	}
}

namespace {

// The pillar's top drops (its animation once).
void dropPillar(World& w, Hazard& h)
{
	const int16_t m = w.track.animatedOf("piltop");
	if (m >= 0) {
		Clock& c = w.scenery.clocks[size_t(m)];
		c.animating = true;
		play(c, w.track.animations[size_t(w.track.animated[size_t(m)].animation)], 0);
		c.loop = false;
	}
	h.landed = false;
	h.state = Hazard::Active;
}

// The sphinx blows up (its material animation, if the track has it), its surface made passable.
void blowSphinx(World& w, Hazard& h)
{
	const int16_t m = w.track.animatedOf("blowup");
	if (w.track.blowupDuration > 0) {
		w.scenery.blowup = 0;
		const Vec3 at = m >= 0 ? w.track.animated[size_t(m)].pose.position : Vec3{};
		w.startAt(BlowupEvent, &at);
	}
	setSurface(w, w.track.surfaceOf("sphinx"), true);
	h.state = Hazard::Active;
}

// The crane's hum, at the crane's own place, not the hook's (it is heard from 300 ft of the "crane" entity).
void craneHum(World& w, size_t index, const Hazard& h)
{
	const int16_t m = w.track.animatedOf("crane");
	RaceEvent& loop = w.raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
	loop.sound = CraneHumSound, loop.looping = true, loop.near = 100, loop.far = 300;
	loop.place = m >= 0 ? w.track.animated[size_t(m)].pose.position : h.centre;
}

// The curse drop: a yellow power at level 3, no racer's, made at one of three spots (drawn).
void dropCurse(World& w, Hazard& h)
{
	const std::array<float, 3>& spot = executableTables().curseDrop[size_t(int16_t(w.draw(Draw::Hazard)) % 3)];
	Vec3 at = {spot[0], spot[1], spot[2]};
	at.y = w.track.mirrored ? -at.y : at.y;
	const Aim aim{at, {1, 0, 0}};
	w.make(-1, Yellow, 3, &aim);
	h.state = Hazard::Active;
}

void activate(World& w, size_t index, int32_t racer)
{
	LR_ASSERT(index < w.scenery.hazards.size() && index < w.track.hazards.size());
	Hazard& h = w.scenery.hazards[index];
	const HazardSpec& spec = w.track.hazards[index];
	if (h.state != Hazard::Idle) {
		return;
	}
	switch (h.type) {
	case Hazard::Pillar: dropPillar(w, h); break;
	case Hazard::Sphinx:
		if (h.on) { // (else blown up already: hidden)
			blowSphinx(w, h);
		}
		break;
	case Hazard::Hammer:
	case Hazard::Crane:
		h.armed = true;
		if (h.type == Hazard::Crane) {
			addSphere(w, index);
			craneHum(w, index, h);
		}
		h.state = Hazard::Active;
		break;
	case Hazard::Ghost:
	case Hazard::Rocket:
	case Hazard::Lava:
		addSphere(w, index);
		h.timer2 = 0;
		h.state = Hazard::Active;
		if (h.type == Hazard::Ghost) {
			// Its wail (the show wobbles its pitch.2 step 8).
			RaceEvent& loop = w.raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
			loop.sound = GhostWailSound, loop.looping = true, loop.place = h.centre;
		}
		break;
	case Hazard::Snow:
	case Hazard::Smoke: h.state = Hazard::Active; break;
	case Hazard::Opener:
		for (int16_t m : spec.models) {
			if (m >= 0) {
				w.scenery.clocks[size_t(m)].animating = true;
				w.scenery.clocks[size_t(m)].loop = false;
			}
		}
		w.fireAt(OpenerEvent, OpenerEvent, nullptr);
		h.state = Hazard::Active;
		break;
	case Hazard::CurseDrop: dropCurse(w, h); break;
	case Hazard::Sweep:
		h.timer2 = 8300;
		h.state = Hazard::Active;
		break;
	case Hazard::Rain:
		h.timer = spec.intervalMs;
		h.state = Hazard::Active;
		break;
	case Hazard::Grabber:
		addSphere(w, index);
		h.grab = 0;
		h.timer2 = 0;
		h.state = Hazard::Active;
		break;
	case Hazard::Rock:
		if (w.contactOrder.size() + w.triggers.memberOrder.size() + w.triggers.waits.size() < 100) { // (the step queue's entries)
			w.contactOrder.insert(w.contactOrder.begin(), uint8_t(World::RockEntry + index));
		}
		h.state = Hazard::Active;
		break;
	case Hazard::WarpPad:
		if (racer >= 0 && !(w.racers.race[size_t(racer)].flags & RacerGhost)) {
			const Aim aim{{132.673004f, w.track.mirrored ? -86.304001f : 86.304001f, 14.722f}, {1, w.track.mirrored ? 0.5f : -0.5f, 0}};
			w.make(int8_t(racer), Green, 3, &aim);
			h.state = Hazard::Active;
		}
		break;
	default: break; // (rocks, launchers, the code puzzle, oscillators: never by their trigger)
	}
}

// deactivate's sphinx: hidden once it has blown up (its three draw distances -1).
void endSphinx(World& w, Hazard& h)
{
	if (w.scenery.blowup >= 0) {
		return; // still blowing up
	}
	h.on = false;
	const int16_t m = w.track.animatedOf("blowup");
	if (m >= 0) {
		w.scenery.animatedDistances[size_t(m)] = {-1, -1, -1};
	}
	h.state = Hazard::Idle;
}

// deactivate's triggered animation: its models stop once every one has played its part.
void endOpener(World& w, Hazard& h, const HazardSpec& spec)
{
	for (int16_t m : spec.models) {
		if (m >= 0 && !partDone(w, size_t(m))) {
			return; // (still playing)
		}
	}
	for (int16_t m : spec.models) {
		if (m >= 0) {
			w.scenery.clocks[size_t(m)].animating = false;
		}
	}
	h.state = Hazard::Idle;
}

void deactivate(World& w, size_t index)
{
	LR_ASSERT(index < w.scenery.hazards.size() && index < w.track.hazards.size());
	Hazard& h = w.scenery.hazards[index];
	const HazardSpec& spec = w.track.hazards[index];
	if (h.state == Hazard::Idle) {
		return;
	}
	switch (h.type) {
	case Hazard::Pillar: break; // (it falls once)
	case Hazard::Sphinx: endSphinx(w, h); break;
	case Hazard::Rocket:
		if (h.on) {
			h.on = false;
		}
		rocketShown(w, false);
		w.endAt(RocketLitEvent, nullptr);
		setSurface(w, w.track.surfaceOf("mmrocc"), false);
		w.killMember(h.member);
		h.state = Hazard::Idle;
		break;
	case Hazard::Ghost:
	case Hazard::Lava:
	case Hazard::Crane:
		if (h.type != Hazard::Lava || h.on) {
			RaceEvent& loop = w.raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
			loop.sound = h.type == Hazard::Crane ? CraneHumSound : h.type == Hazard::Ghost ? GhostWailSound : LavaBubblingSound, loop.stop = true;
			h.on = false;
		}
		w.killMember(h.member);
		h.state = Hazard::Idle;
		break;
	case Hazard::Launcher:
	case Hazard::Launchers:
		if (h.type == Hazard::Launchers) {
			const std::array<float, 4>& t = spec.targets[size_t(h.aimed)];
			const Vec3 at{t[0], t[1], t[2]};
			w.fireAt(int32_t(t[3]), int32_t(t[3]), &at);
		}
		h.ball.state = Projectile::Idle;
		h.state = Hazard::Idle;
		break;
	case Hazard::Opener: endOpener(w, h, spec); break;
	case Hazard::Rock: {
		std::vector<uint8_t>& order = w.contactOrder;
		order.erase(std::remove(order.begin(), order.end(), uint8_t(World::RockEntry + index)), order.end());
		h.state = Hazard::Idle;
		break;
	}
	case Hazard::Grabber:
		releaseGrab(w, h);
		w.killMember(h.member);
		h.grab = 0;
		h.timer2 = 0;
		h.state = Hazard::Idle;
		break;
	case Hazard::Snow:
	case Hazard::Smoke:
		// Its particle finished and let go.
		if (h.on) {
			w.raise(RaceEvent::Finish, {Owner::Hazard, int16_t(index)});
			h.on = false;
		}
		h.state = Hazard::Idle;
		break;
	default: h.state = Hazard::Idle;
	}
}

// What an event's start tells a hazard besides its trigger: the code puzzle's switches, a launcher's launch event.
void started(World& w, size_t index, int32_t e, const Vec3* at)
{
	Hazard& h = w.scenery.hazards[index];
	const HazardSpec& spec = w.track.hazards[index];
	if (h.type == Hazard::Code) {
		const int32_t c[3] = {SwitchEvents + h.right[0], SwitchEvents + h.right[1], SwitchEvents + h.right[2]};
		if (e == SwitchEvents + h.reset[0] || e == SwitchEvents + h.reset[1] || e == SwitchEvents + h.reset[2]) {
			h.progress = 0;
		}
		else if (e == c[0]) {
			h.progress = 1;
		}
		else if (e == c[1]) {
			h.progress = h.progress == 1 ? 3 : h.progress;
		}
		else if (e == c[2] && h.progress == 3) {
			h.progress = 0;
			w.fireAt(CodeSuccessEvent, CodeSuccessEvent, nullptr);
			codeDisplay(w, h, true); // (the colours blink)
			h.timer = 2500;
			chooseCode(w, h);
			w.startAt(CodeDisplayEvent, nullptr);
		}
		int32_t feedback = -1;
		if (e >= SwitchEvents && e <= SwitchEvents + 5) {
			feedback = (e & 1) ? OddSwitchFeedback : EvenSwitchFeedback;
		}
		else if (e >= ReadOutEvents && e <= ReadOutEvents + 2) {
			feedback = h.right[e - ReadOutEvents] == 2 * (e - ReadOutEvents) ? RightDigitFeedback : WrongDigitFeedback;
		}
		if (feedback != -1) {
			w.fireAt(feedback, feedback, at);
		}
		return;
	}
	if ((h.type == Hazard::Launcher || h.type == Hazard::Launchers) && e == spec.trigger && spec.trigger != -1 && h.state == Hazard::Idle) {
		if (at && lengthSquared(spec.triggerPoint - *at) >= h.radius) {
			return;
		}
		if (h.type == Hazard::Launchers) {
			h.launch = int8_t(uint16_t(w.draw(Draw::Hazard)) % spec.launches.size());
			h.aimed = int8_t(uint16_t(w.draw(Draw::Hazard)) % spec.targets.size());
			const std::array<float, 4>& l = spec.launches[size_t(h.launch)];
			const std::array<float, 4>& t = spec.targets[size_t(h.aimed)];
			const Vec3 from{l[0], l[1], l[2]};
			w.fireAt(int32_t(l[3]), int32_t(l[3]), &from);
			launchBall(w, h, from, {t[0], t[1], t[2]});
		}
		else {
			launchBall(w, h, spec.source, spec.target);
		}
	}
}

} // namespace

void World::hazardEvent(int32_t start, int32_t end, const Vec3* at, int32_t racer, bool force)
{
	for (size_t index = 0; index < scenery.hazards.size(); index++) {
		Hazard& h = scenery.hazards[index];
		const int32_t t = h.trigger;
		if (force) {
			if (t != -1 && t == end) {
				h.state = Hazard::Ending;
			}
			continue;
		}
		const bool byRacer = racer >= 0;
		if (t != -1 && (!byRacer || h.type == Hazard::WarpPad)) {
			if (start != -1 && t == start) {
				activate(*this, index, racer);
			}
			if (end != -1 && t == end && (!byRacer || h.type == Hazard::WarpPad)) {
				if (h.type != Hazard::WarpPad || byRacer) {
					deactivate(*this, index);
				}
			}
		}
		if (start != -1) {
			started(*this, index, start, at);
		}
	}
}

namespace {

// The code puzzle's frame: its blinking display runs out.
void codeFrame(World& w, Hazard& h, int32_t ms)
{
	if (h.timer != 0) {
		if (ms >= h.timer) {
			h.timer = 0;
			w.endAt(CodeDisplayEvent, nullptr);
			codeDisplay(w, h, false);
		}
		else {
			h.timer -= ms;
		}
	}
}

void pillarFrame(World& w, Hazard& h)
{
	const int16_t m = w.track.animatedOf("piltop");
	if (!h.landed && m >= 0 && w.scenery.clocks[size_t(m)].time > 50.0f) {
		h.landed = true;
		setSurface(w, w.track.surfaceOf("pilcol"), false); // a wall now
		w.fireAt(LandingEvent, LandingEvent, nullptr);
	}
}

void sphinxFrame(World& w, size_t index)
{
	if (w.scenery.blowup < 0) {
		const int16_t m = w.track.animatedOf("blowup");
		const Vec3 at = m >= 0 ? w.track.animated[size_t(m)].pose.position : Vec3{};
		if (w.track.blowupDuration > 0) {
			w.endAt(BlowupEvent, &at);
		}
		deactivate(w, index);
	}
}

void hammerFrame(World& w, Hazard& h)
{
	const int16_t m = w.track.animatedOf("rkhamm02");
	const float t = m >= 0 ? w.scenery.clocks[size_t(m)].time : 0;
	if (h.armed && ((t > 22 && t < 28) || (t > 72 && t < 78))) {
		h.armed = false;
		w.fireAt(HammerStrikeEvent, HammerStrikeEvent, nullptr);
	}
	else if (!h.armed && ((t > 0 && t < 20) || (t > 40 && t < 60))) {
		h.armed = true;
	}
}

// search: its search for racers near came round this frame.
void ghostFrame(World& w, Hazard& h, size_t index, bool search)
{
	const int16_t m = w.track.animatedOf("ghostly");
	if (m >= 0 && h.state != Hazard::Idle) {
		moveSphere(w, h, bonePoint(w, size_t(m), 1, w.scenery.clocks[size_t(m)].drawn));
	}
	// A racer within 60 ft of it: its "near" sound (QUIRK: of the bone's scaled local place, the same for "ghostly").
	for (size_t r = 0; search && r < w.racers.race.size(); r++) {
		if (lengthSquared(w.racers.visible[r].position - h.centre) < 3600) {
			w.playSound(GhostNearSound, {Owner::Hazard, int16_t(index)}, &h.centre, 200, 300);
			break;
		}
	}
}

void lavaFrame(World& w, Hazard& h, size_t index, int32_t ms)
{
	const int16_t m = w.track.animatedOf("mmlavbl");
	const float t = m >= 0 ? w.scenery.clocks[size_t(m)].time : 0;
	if (h.state == Hazard::Ending) {
		deactivate(w, index); // (QUIRK: the rest still runs)
	}
	if (m >= 0) {
		moveSphere(w, h, bonePoint(w, size_t(m), 0, w.scenery.clocks[size_t(m)].drawn));
	}
	const ExecutableTables& lava = executableTables();
	const std::array<float, 3>& starts = lava.lavaStarts;
	const std::array<float, 3>& ends = lava.lavaEnds;
	if (h.timer2 != 0) {
		h.timer2 = ms > h.timer2 ? 0 : h.timer2 - ms;
		return;
	}
	for (size_t i = 0; i < 3; i++) {
		Vec3 at = {lava.lavaSpots[i][0], lava.lavaSpots[i][1], lava.lavaSpots[i][2]};
		at.y = w.track.mirrored ? -at.y : at.y;
		const bool start = starts[i] < t && t < starts[i] + 10, end = ends[i] < t && t < ends[i] + 10;
		if (start || end) {
			w.fireAt(EruptionEvent, EruptionEvent, &at);
			h.timer2 = 400;
			// Its bubbling: released as an eruption starts, played again as it ends.
			if (start && h.on) {
				RaceEvent& loop = w.raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
				loop.sound = LavaBubblingSound, loop.stop = true;
			}
			else if (end && !h.on) {
				RaceEvent& loop = w.raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
				loop.sound = LavaBubblingSound, loop.looping = true, loop.near = 200, loop.far = 300, loop.place = at;
			}
			h.on = end;
		}
	}
}

// The snow restarts about every second (its particle is the show's: one is assumed to be had).
void snowFrame(World& w, Hazard& h, size_t index, int32_t ms)
{
	h.timer += ms;
	if (h.timer > 1000 && h.on) {
		w.raise(RaceEvent::Finish, {Owner::Hazard, int16_t(index)});
		h.on = false;
	}
	if (h.armed && !h.on) {
		w.spawn("snow", 2, {Owner::Hazard, int16_t(index)}, {});
		h.on = true;
		h.timer = 0;
	}
}

void smokeFrame(World& w, Hazard& h, size_t index)
{
	if (!h.on) {
		w.spawn("smoke", 2, {Owner::Hazard, int16_t(index)}, {});
		h.on = true;
	}
}

void rocketFrame(World& w, Hazard& h)
{
	if (h.lit && !h.on) {
		h.on = true;
		rocketShown(w, true);
		w.startAt(RocketLitEvent, nullptr);
		setSurface(w, w.track.surfaceOf("mmrocc"), true);
	}
	else if (!h.lit && h.on) {
		h.on = false;
		rocketShown(w, false);
		w.endAt(RocketLitEvent, nullptr);
		setSurface(w, w.track.surfaceOf("mmrocc"), false);
	}
	h.lit = false;
}

void craneFrame(World& w, Hazard& h)
{
	const int16_t m = w.track.animatedOf("crane");
	if (m < 0) {
		return;
	}
	// The hook: the end of the crane's arm (node 3, through its parents), as last drawn.
	const Vec3 hookAt = scenePoint(w, size_t(m), 3, {}, true, w.scenery.clocks[size_t(m)].drawn);
	moveSphere(w, h, hookAt);
	const float t = w.scenery.clocks[size_t(m)].time;
	if (h.armed && ((t > 150 && t < 180) || (t > 0 && t < 30))) {
		h.armed = false;
		w.fireAt(HookDropEvent, HookDropEvent, &hookAt);
	}
	else if (!h.armed && ((t > 60 && t < 120) || (t > 210 && t < 270))) {
		h.armed = true;
	}
}

// A launcher's ball in flight, until it hits something or its life runs out.
void launcherFrame(World& w, Hazard& h, size_t index, int32_t ms)
{
	Projectile& p = h.ball;
	if (p.state == Projectile::Flying) {
		p.ageMs += ms;
		if (p.ageMs >= p.lifeMs) {
			p.state = Projectile::Expired; // QUIRK: it sticks, the launcher never fires again
		}
		else {
			const float e = float(p.ageMs) * 0.001f;
			const Vec3 x = {e * p.velocity.x + p.start.x, p.velocity.y * e + p.start.y, (p.velocity.z * e + p.start.z) + ((e * e) * 0.5f) * p.gravity};
			Vec3 hit, normal;
			if (w.shotHits(p.at, x, hit, normal)) {
				p.at = hit;
				w.explode(false, hit, false, -1);
				p.state = Projectile::Idle;
				w.fireAt(LandingEvent, LandingEvent, &hit);
			}
			else {
				p.at = x;
			}
		}
	}
	if (p.state == Projectile::Idle) {
		deactivate(w, index);
	}
}

void openerFrame(World& w, size_t index)
{
	const HazardSpec& spec = w.track.hazards[index];
	setSurface(w, spec.surface, true);
	if (spec.models[0] >= 0 && partDone(w, size_t(spec.models[0]))) {
		deactivate(w, index);
	}
}

// Its aim sweeps: across by its angle, up and down by its range about its base.
void sweepFrame(World& w, Hazard& h, const HazardSpec& spec, int32_t ms)
{
	const int32_t period = spec.periodMs;
	h.timer += ms;
	if (h.timer >= period) {
		h.timer = 0;
	}
	const float n = float(h.timer);
	const int32_t half = period >> 1, quarter = period >> 2;
	float angle = (n / float(half)) * spec.sweep.x;
	if (h.timer > half) {
		angle = Pi - angle;
	}
	Vec3 f{cosEntry(int32_t(angle * 162.974655f)), cosEntry(-256 - int32_t(angle * -162.97466f)),
		cosEntry(int32_t(((n / float(quarter)) * Tau) * 162.974655f)) * spec.sweep.y + spec.sweep.z};
	f.y = w.track.mirrored ? -f.y : f.y;
	f = normalize(f);
	const int16_t model = spec.model;
	const Vec3 source = model >= 0 ? w.track.animated[size_t(model)].pose.position - Vec3{0, 0, 17} : spec.source;
	h.timer2 += ms;
	if (h.timer2 >= 8300) {
		h.timer2 = 0;
		const Aim aim{source, f};
		w.make(-1, Red, 2, &aim);
	}
}

void rainFrame(World& w, Hazard& h, size_t index, const HazardSpec& spec, int32_t ms)
{
	const int16_t model = spec.model;
	h.timer += ms;
	if (h.timer < spec.intervalMs || model < 0) {
		return;
	}
	h.timer = 0;
	// A ball dropped from under the emitter's node 1 (sampled now), a little to one side.
	const Pose node = localPose(w, size_t(model), 1, w.scenery.clocks[size_t(model)].time);
	const AnimatedModel& m = w.track.animated[size_t(model)];
	const Vec3 at = toWorld(m.pose.rotation, node.position * m.scale) + m.pose.position;
	const Vec3 u = toWorld(m.pose.rotation, node.rotation.u);
	h.muzzle = {at.x - u.x * 35, at.y - u.y * 35, at.z - u.z * 35};
	const int16_t rx = int16_t(w.draw(Draw::Hazard)), ry = int16_t(w.draw(Draw::Hazard));
	Vec3 drop = {(float(rx % 4) + h.muzzle.x) - 2, (float(ry % 4) + h.muzzle.y) - 2, h.muzzle.z + 1};
	if (drop.x == h.muzzle.x && drop.y == h.muzzle.y) {
		drop.x = drop.x + 2;
	}
	h.point = drop;
	const Aim aim{h.muzzle, {}, int8_t(index)};
	w.make(-1, Red, 0, &aim);
}

void rockFrame(const World& w, Hazard& h, int16_t model)
{
	if (model < 0 || w.track.animated[size_t(model)].nodes.size() < 2) {
		return;
	}
	// Node 1 places the box: its own position (QUIRK: the parents' not applied), and for a rock ("rk...") its rows
	// turned by node 1 again and then each parent (QUIRK); other entities keep the axes.
	const AnimatedModel& m = w.track.animated[size_t(model)];
	const Animation* anim = m.animation >= 0 ? &w.track.animations[size_t(m.animation)] : nullptr;
	const Clock& c = w.scenery.clocks[size_t(model)];
	const Pose node = w.nodeLocal(m, anim, 1, c.part, c.drawn, c.loop);
	h.centre = toWorld(m.pose.rotation, node.position * m.scale) + m.pose.position;
	if (m.name.starts_with("rk")) {
		Vec3 r = node.rotation.f, f = node.rotation.u;
		for (int32_t n = 1; n >= 0; n = m.nodes[size_t(n)].parent) {
			const Basis rows = w.nodeLocal(m, anim, size_t(n), c.part, c.drawn, c.loop).rotation;
			r = toWorld(rows, r);
			f = toWorld(rows, f);
		}
		h.rows = basisFromForwardUp(toWorld(m.pose.rotation, r), toWorld(m.pose.rotation, f));
	}
}

void grabberFrame(World& w, Hazard& h, int16_t model, int32_t ms)
{
	if (model >= 0) {
		const Pose node = localPose(w, size_t(model), 0, w.scenery.clocks[size_t(model)].drawn);
		const AnimatedModel& m = w.track.animated[size_t(model)];
		moveSphere(w, h, toWorld(m.pose.rotation, node.position * m.scale) + m.pose.position);
	}
	bool release = false;
	if (h.timer != 0) {
		if (h.timer <= ms) {
			h.timer = 0;
			release = true;
			h.grab = 0;
			h.timer2 = 0;
		}
		else {
			h.timer -= ms;
		}
	}
	if (h.timer2 != 0) {
		if (ms >= h.timer2) {
			h.timer2 = 0;
			if (h.grab == 1) {
				release = true;
				h.timer = 0;
				h.grab = 2;
				h.timer2 = 1000;
			}
			else if (h.grab == 2) {
				h.grab = 0;
			}
		}
		else {
			h.timer2 -= ms;
		}
	}
	if (release) {
		releaseGrab(w, h);
	}
}

} // namespace

void World::hazardFrame(int32_t ms)
{
	raceView.stage = RaceEvent::Hazards;
	for (size_t index = 0; index < scenery.hazards.size(); index++) {
		Hazard& h = scenery.hazards[index];
		const HazardSpec& spec = track.hazards[index];
		if (h.type == Hazard::Code) {
			codeFrame(*this, h, ms);
			continue;
		}
		bool search = false;
		if (h.type == Hazard::Ghost) {
			h.timer += ms; // its search for racers near (a sound)
			if (h.state != Hazard::Idle && h.timer > 4000) {
				h.timer = 0;
				search = true;
			}
		}
		if (h.state == Hazard::Idle) {
			continue;
		}
		if (h.state == Hazard::Ending && h.type != Hazard::Lava) {
			deactivate(*this, index);
			if (h.state == Hazard::Idle && h.type != Hazard::Ghost) {
				continue;
			}
		}
		switch (h.type) {
		case Hazard::Pillar: pillarFrame(*this, h); break;
		case Hazard::Sphinx: sphinxFrame(*this, index); break;
		case Hazard::Hammer: hammerFrame(*this, h); break;
		case Hazard::Ghost: ghostFrame(*this, h, index, search); break;
		case Hazard::Lava: lavaFrame(*this, h, index, ms); break;
		case Hazard::Snow: snowFrame(*this, h, index, ms); break;
		case Hazard::Smoke: smokeFrame(*this, h, index); break;
		case Hazard::Rocket: rocketFrame(*this, h); break;
		case Hazard::Crane: craneFrame(*this, h); break;
		case Hazard::Launcher:
		case Hazard::Launchers: launcherFrame(*this, h, index, ms); break;
		case Hazard::Opener: openerFrame(*this, index); break;
		case Hazard::Sweep: sweepFrame(*this, h, spec, ms); break;
		case Hazard::Rain: rainFrame(*this, h, index, spec, ms); break;
		case Hazard::Rock: rockFrame(*this, h, spec.model); break;
		case Hazard::Grabber: grabberFrame(*this, h, spec.model, ms); break;
		default: break;
		}
	}
}

void World::touchHazard(size_t index, size_t i)
{
	LR_ASSERT(index < scenery.hazards.size() && i < racers.karts.size());
	Hazard& h = scenery.hazards[index];
	Kart& k = racers.karts[i];
	const bool spinning = k.flags & KartSpinning, moving = k.headingSpeed != 0;
	switch (h.type) {
	case Hazard::Ghost:
		if (!spinning && moving && !(racers.race[i].flags & RacerShielded)) {
			spin(i, 1, 0.0099999998f, 0);
			spinOut(i);
			k.velocity = {};
			impulse(i, {0, 0, 150});
			voice(i, RaceEvent::Negative);
			playSound(GhostHitSound, {Owner::Hazard, int16_t(index)}, &racers.visible[i].position, 200, 300);
		}
		break;
	case Hazard::Lava:
		if (!spinning && moving) {
			spin(i, 1, 0.0099999998f, 0);
			voice(i, RaceEvent::Negative);
		}
		break;
	case Hazard::Rocket: h.lit = h.lit || (racers.race[i].flags & RacerShielded); break;
	case Hazard::Crane:
		if (!spinning && moving) {
			spin(i, 1, 0.0099999998f, 0);
			const Vec3 at = racers.visible[i].position;
			fireAt(CraneHitEvent, CraneHitEvent, &at);
		}
		break;
	case Hazard::Grabber: {
		const HazardSpec& spec = track.hazards[index];
		const float t = spec.model >= 0 ? scenery.clocks[size_t(spec.model)].time : 0;
		if (h.grab == 2 || !(t <= spec.low || t >= spec.high) || (racers.race[i].flags & RacerShielded) || (h.held >= 0 && h.held != int8_t(i))) {
			break;
		}
		if (h.held < 0) {
			h.grab = 1;
			h.timer2 = 1000;
		}
		h.held = int8_t(i);
		racers.race[i].flags |= RacerGrabbed;
		if (racers.playback[i].active) {
			routePace(racers.playback[i], k, -0.40000001f);
		}
		else {
			const Vec3 c = racers.visible[i].position;
			const Vec3 q = normalize(Vec3{h.centre.x - c.x, h.centre.y - c.y, 0}) * spec.strength;
			push(i, 1, q);
		}
		h.timer = 500;
		break;
	}
	default: break;
	}
}

} // namespace lr
