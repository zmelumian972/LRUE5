// The animated scenery's clocks and poses, and the hazards: each started and stopped by its trigger event (or a
// racer's, for the warp pad), updated once a frame after the event table, and some with a sphere in the trigger list
// that does something to the racers it meets.

#include "LRCore/Race/World.h"
#include "LRCore/Common/Tables.h"
#include "Kart/Steering.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace lr {

namespace {

// The hazard file's keywords, in Hazard::Type's order.
constexpr int32_t TypeKeywords[] = {0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x32, 0x33, 0x34, 0x36, 0x3d, 0x3e, 0x3f, 0x40, 0x43, 0x48, 0x49};

constexpr uint32_t Passable = SurfaceNonSolid | SurfaceProjectilesPass;

// A looping time wrapped to the part's loop.
float wrapped(float t, float loop) { return t >= loop ? t - loop * float(int32_t(t / loop)) : t; }

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
			c.time = wrapped(c.time, float(p.loopFrames));
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
	const Clock& c = w.clocks[model];
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
	const Clock& c = w.clocks[model];
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
	return w.nodeLocal(m, a, node, w.clocks[model].part, time, w.clocks[model].loop);
}

// A bone's place: its own position (parents not applied), placed in the world.
Vec3 bonePoint(const World& w, size_t model, size_t node, float time)
{
	const AnimatedModel& m = w.track.animated[model];
	return toWorld(m.pose.rotation, localPose(w, model, node, time).position * m.scale) + m.pose.position;
}

void addSphere(World& w, size_t index)
{
	Hazard& h = w.hazards[index];
	w.killMember(h.member);
	h.member = w.addMember({h.centre, h.radius, Member::Hazard, false, false, uint16_t(index)});
}

void moveSphere(World& w, Hazard& h, Vec3 centre)
{
	h.centre = centre;
	if (h.member >= 0) {
		w.members[size_t(h.member)].centre = centre;
	}
}

// The rocket's look: the on model's distances the off model's as loaded, the off model's -1; or
// the reverse.
void rocketShown(World& w, bool on)
{
	const int32_t off = w.track.modelOf("mmrocof"), lit = w.track.modelOf("mmrocon");
	const std::array<float, 3> hidden = {-1, -1, -1};
	if (off >= 0) {
		w.modelDistances[size_t(off)] = on ? hidden : w.track.modelDistances[size_t(off)];
	}
	if (lit >= 0) {
		w.modelDistances[size_t(lit)] = on && off >= 0 ? w.track.modelDistances[size_t(off)] : hidden;
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
		World::MaterialState& m = w.materials[size_t(tracks[size_t(d)])];
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
		const uint8_t even = uint8_t(200 + 2 * d - 200), other = uint8_t(even + 1); // (kept as offsets from 200)
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
	w.fireAt(6, 6, &from);
}

void releaseGrab(World& w, Hazard& h)
{
	if (h.held < 0) {
		return;
	}
	const size_t r = size_t(h.held);
	w.endPush(r, 1);
	w.racers[r].flags &= ~RacerGrabbed;
	if (w.playback[r].active) {
		routePace(w.playback[r], w.karts[r], 1);
	}
	h.held = -1;
}

} // namespace

Pose World::nodeLocal(const AnimatedModel& m, const Animation* a, size_t node, int32_t part, float time, bool loop) const
{
	return sampleNode(m.nodes, a, node, part, time, loop);
}

void World::advanceClocks(int32_t ms)
{
	for (size_t i = 0; i < clocks.size(); i++) {
		Clock& c = clocks[i];
		const AnimatedModel& m = track.animated[i];
		if (!c.animating || m.animation < 0 || c.part < 0) {
			continue;
		}
		advance(c, track.animations[size_t(m.animation)], ms);
	}
	// The bricks' shared entities run from the load whether a brick is drawn or not (their clock is
	// the power-ups' update's, the same frame's ms).
	for (size_t b = 0; b < brickClocks.size(); b++) {
		Clock& c = brickClocks[b];
		const AnimatedModel& m = track.brickModels[b];
		if (c.animating && m.animation >= 0 && c.part >= 0) {
			advance(c, track.animations[size_t(m.animation)], ms);
		}
	}
	// A sphinx's blow-up: its material track runs to its end once.
	if (blowup >= 0) {
		blowup = (float(track.blowupRate) / 1000) * float(ms) + blowup;
		if (int32_t(blowup) >= track.blowupDuration) {
			blowup = -1;
		}
	}
}

void World::brickNodes(Track::BrickModel which, Vec3 at, float size, std::vector<NodeMatrix>& out) const
{
	const AnimatedModel& m = track.brickModels[which];
	const Clock& c = brickClocks[which];
	const Animation* a = m.animation >= 0 ? &track.animations[size_t(m.animation)] : nullptr;
	poseSkeleton(m.nodes, a, {a ? c.part : -1, c.time, true}, {at, Basis{}}, m.scale * size, out);
}

void World::setupHazards()
{
	for (size_t b = 0; b < brickClocks.size(); b++) {
		const AnimatedModel& m = track.brickModels[b];
		brickClocks[b] = {};
		if (m.animation >= 0 && m.part >= 0) {
			play(brickClocks[b], track.animations[size_t(m.animation)], m.part);
			brickClocks[b].animating = true;
		}
	}
	clocks.assign(track.animated.size(), {});
	for (size_t i = 0; i < track.animated.size(); i++) {
		const AnimatedModel& m = track.animated[i];
		if (m.part >= 0 && m.animation >= 0) {
			play(clocks[i], track.animations[size_t(m.animation)], m.part);
			clocks[i].animating = true;
		}
	}
	hazards.assign(track.hazards.size(), {});
	for (size_t index = 0; index < track.hazards.size(); index++) {
		const HazardSpec& spec = track.hazards[index];
		Hazard& h = hazards[index];
		const int32_t* type = std::find(std::begin(TypeKeywords), std::end(TypeKeywords), spec.type);
		h.type = Hazard::Type(type - TypeKeywords);
		switch (h.type) {
		case Hazard::Pillar: {
			h.trigger = 10;
			const int16_t m = track.animatedOf("piltop");
			if (m >= 0) {
				clocks[size_t(m)].animating = false;
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
			const int16_t m = track.animatedOf("ghostly");
			h.radius = (m >= 0 ? track.animated[size_t(m)].radius : 0) + 8;
			break;
		}
		case Hazard::Lava: {
			h.trigger = 0;
			const int16_t m = track.animatedOf("mmlavbl");
			h.radius = m >= 0 ? track.animated[size_t(m)].radius : 0;
			break;
		}
		case Hazard::Code: {
			h.progress = 0;
			chooseCode(*this, h);
			h.timer = 1;
			h.state = Hazard::Active;
			// The display's tracks at their own rates (the binding kept).
			const std::array<int32_t, 3> tracks = codeTracks(*this);
			const float rates[3] = {0.003f, 0.0040000002f, 0.0049999999f};
			for (int d = 0; d < 3; d++) {
				if (tracks[size_t(d)] >= 0) {
					materials[size_t(tracks[size_t(d)])].rate = rates[d];
				}
			}
			break;
		}
		case Hazard::Rocket: {
			h.trigger = 1;
			rocketShown(*this, false);
			endAt(35, nullptr); // shown off
			setSurface(*this, track.surfaceOf("mmrocc"), false);
			const Pose* off = track.modelPlace("mmrocof");
			h.centre = off ? off->position : Vec3{};
			h.radius = 350;
			break;
		}
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
					clocks[size_t(m)].animating = false;
				}
			}
			break;
		case Hazard::CurseDrop: h.trigger = 8; break;
		case Hazard::Rock:
			// A box of the file's size, its sphere its largest half size; its model's animation starts at a phase.
			h.trigger = spec.trigger;
			h.radius = std::max({spec.size.x * 0.5f, spec.size.y * 0.5f, spec.size.z * 0.5f});
			if (spec.model >= 0) {
				clocks[size_t(spec.model)].time = spec.startFrame;
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
}

namespace {

void activate(World& w, size_t index, int racer)
{
	Hazard& h = w.hazards[index];
	const HazardSpec& spec = w.track.hazards[index];
	if (h.state != Hazard::Idle) {
		return;
	}
	switch (h.type) {
	case Hazard::Pillar: {
		const int16_t m = w.track.animatedOf("piltop");
		if (m >= 0) {
			Clock& c = w.clocks[size_t(m)];
			c.animating = true;
			play(c, w.track.animations[size_t(w.track.animated[size_t(m)].animation)], 0);
			c.loop = false;
		}
		h.landed = false;
		h.state = Hazard::Active;
		break;
	}
	case Hazard::Sphinx: {
		if (!h.on) {
			return; // (blown up already: hidden)
		}
		const int16_t m = w.track.animatedOf("blowup");
		if (w.track.blowupDuration > 0) {
			w.blowup = 0;
			const Vec3 at = m >= 0 ? w.track.animated[size_t(m)].pose.position : Vec3{};
			w.startAt(16, &at);
		}
		setSurface(w, w.track.surfaceOf("sphinx"), true);
		h.state = Hazard::Active;
		break;
	}
	case Hazard::Hammer:
	case Hazard::Crane:
		h.armed = true;
		if (h.type == Hazard::Crane) {
			addSphere(w, index);
			// Its hum, at the crane's own place, not the hook's (it is heard from 300 ft of the "crane" entity).
			const int16_t m = w.track.animatedOf("crane");
			RaceEvent& loop = w.raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
			loop.sound = 3003, loop.looping = true, loop.near = 100, loop.far = 300;
			loop.place = m >= 0 ? w.track.animated[size_t(m)].pose.position : h.centre;
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
			loop.sound = 3012, loop.looping = true, loop.place = h.centre;
		}
		break;
	case Hazard::Snow:
	case Hazard::Smoke: h.state = Hazard::Active; break;
	case Hazard::Opener:
		for (int16_t m : spec.models) {
			if (m >= 0) {
				w.clocks[size_t(m)].animating = true;
				w.clocks[size_t(m)].loop = false;
			}
		}
		w.fireAt(9, 9, nullptr);
		h.state = Hazard::Active;
		break;
	case Hazard::CurseDrop: {
		const std::array<float, 3>& spot = executableTables().curseDrop[size_t(int16_t(w.draw(Draw::Hazard)) % 3)];
		Vec3 at = {spot[0], spot[1], spot[2]};
		at.y = w.track.mirrored ? -at.y : at.y;
		const Aim aim{at, {1, 0, 0}};
		w.make(-1, Yellow, 3, &aim);
		h.state = Hazard::Active;
		break;
	}
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
		if (w.contactOrder.size() + w.memberOrder.size() + w.waits.size() < 100) { // (the step queue's entries)
			w.contactOrder.insert(w.contactOrder.begin(), uint8_t(World::RockEntry + index));
		}
		h.state = Hazard::Active;
		break;
	case Hazard::WarpPad:
		if (racer >= 0 && !(w.racers[size_t(racer)].flags & RacerGhost)) {
			const Aim aim{{132.673004f, w.track.mirrored ? -86.304001f : 86.304001f, 14.722f}, {1, w.track.mirrored ? 0.5f : -0.5f, 0}};
			w.make(int8_t(racer), Green, 3, &aim);
			h.state = Hazard::Active;
		}
		break;
	default: break; // (rocks, launchers, the code puzzle, oscillators: never by their trigger)
	}
}

void deactivate(World& w, size_t index)
{
	Hazard& h = w.hazards[index];
	const HazardSpec& spec = w.track.hazards[index];
	if (h.state == Hazard::Idle) {
		return;
	}
	switch (h.type) {
	case Hazard::Pillar: break; // (it falls once)
	case Hazard::Sphinx: {
		if (w.blowup >= 0) {
			return; // still blowing up
		}
		h.on = false;
		// (Hidden: its three draw distances -1)
		const int16_t m = w.track.animatedOf("blowup");
		if (m >= 0) {
			w.animatedDistances[size_t(m)] = {-1, -1, -1};
		}
		h.state = Hazard::Idle;
		break;
	}
	case Hazard::Rocket:
		if (h.on) {
			h.on = false;
		}
		rocketShown(w, false);
		w.endAt(35, nullptr);
		setSurface(w, w.track.surfaceOf("mmrocc"), false);
		w.killMember(h.member);
		h.state = Hazard::Idle;
		break;
	case Hazard::Ghost:
	case Hazard::Lava:
	case Hazard::Crane:
		if (h.type != Hazard::Lava || h.on) {
			RaceEvent& loop = w.raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
			loop.sound = h.type == Hazard::Crane ? 3003 : h.type == Hazard::Ghost ? 3012 : 3002, loop.stop = true;
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
	case Hazard::Opener:
		for (int16_t m : spec.models) {
			if (m >= 0 && !partDone(w, size_t(m))) {
				return; // (still playing)
			}
		}
		for (int16_t m : spec.models) {
			if (m >= 0) {
				w.clocks[size_t(m)].animating = false;
			}
		}
		h.state = Hazard::Idle;
		break;
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
	Hazard& h = w.hazards[index];
	const HazardSpec& spec = w.track.hazards[index];
	if (h.type == Hazard::Code) {
		const int32_t c[3] = {200 + h.right[0], 200 + h.right[1], 200 + h.right[2]};
		if (e == 200 + h.reset[0] || e == 200 + h.reset[1] || e == 200 + h.reset[2]) {
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
			w.fireAt(18, 18, nullptr);
			codeDisplay(w, h, true); // (the colours blink)
			h.timer = 2500;
			chooseCode(w, h);
			w.startAt(28, nullptr);
		}
		int32_t feedback = -1;
		if (e >= 200 && e <= 205) {
			feedback = (e & 1) ? 30 : 21;
		}
		else if (e >= 207 && e <= 209) {
			feedback = h.right[e - 207] == 2 * (e - 207) ? 29 : 20;
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

void World::hazardEvent(int32_t start, int32_t end, const Vec3* at, int racer, bool force)
{
	for (size_t index = 0; index < hazards.size(); index++) {
		Hazard& h = hazards[index];
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

void World::hazardFrame(int32_t ms)
{
	stage = RaceEvent::Hazards;
	for (size_t index = 0; index < hazards.size(); index++) {
		Hazard& h = hazards[index];
		const HazardSpec& spec = track.hazards[index];
		if (h.type == Hazard::Code) {
			if (h.timer != 0) {
				if (ms >= h.timer) {
					h.timer = 0;
					endAt(28, nullptr);
					codeDisplay(*this, h, false);
				}
				else {
					h.timer -= ms;
				}
			}
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
		const int16_t model = spec.model;
		switch (h.type) {
		case Hazard::Pillar: {
			const int16_t m = track.animatedOf("piltop");
			if (!h.landed && m >= 0 && clocks[size_t(m)].time > 50.0f) {
				h.landed = true;
				setSurface(*this, track.surfaceOf("pilcol"), false); // a wall now
				fireAt(7, 7, nullptr);
			}
			break;
		}
		case Hazard::Sphinx:
			if (blowup < 0) {
				const int16_t m = track.animatedOf("blowup");
				const Vec3 at = m >= 0 ? track.animated[size_t(m)].pose.position : Vec3{};
				if (track.blowupDuration > 0) {
					endAt(16, &at);
				}
				deactivate(*this, index);
			}
			break;
		case Hazard::Hammer: {
			const int16_t m = track.animatedOf("rkhamm02");
			const float t = m >= 0 ? clocks[size_t(m)].time : 0;
			if (h.armed && ((t > 22 && t < 28) || (t > 72 && t < 78))) {
				h.armed = false;
				fireAt(43, 43, nullptr);
			}
			else if (!h.armed && ((t > 0 && t < 20) || (t > 40 && t < 60))) {
				h.armed = true;
			}
			break;
		}
		case Hazard::Ghost: {
			const int16_t m = track.animatedOf("ghostly");
			if (m >= 0 && h.state != Hazard::Idle) {
				moveSphere(*this, h, bonePoint(*this, size_t(m), 1, clocks[size_t(m)].drawn));
			}
			// A racer within 60 ft of it: its "near" sound (QUIRK: of the bone's scaled local place, the same for "ghostly").
			for (size_t r = 0; search && r < racers.size(); r++) {
				if (lengthSquared(visible[r].position - h.centre) < 3600) {
					playSound(3013, {Owner::Hazard, int16_t(index)}, &h.centre, 200, 300);
					break;
				}
			}
			break;
		}
		case Hazard::Lava: {
			const int16_t m = track.animatedOf("mmlavbl");
			const float t = m >= 0 ? clocks[size_t(m)].time : 0;
			if (h.state == Hazard::Ending) {
				deactivate(*this, index); // (QUIRK: the rest still runs)
			}
			if (m >= 0) {
				moveSphere(*this, h, bonePoint(*this, size_t(m), 0, clocks[size_t(m)].drawn));
			}
			const ExecutableTables& lava = executableTables();
			const std::array<float, 3>& starts = lava.lavaStarts;
			const std::array<float, 3>& ends = lava.lavaEnds;
			if (h.timer2 == 0) {
				for (size_t i = 0; i < 3; i++) {
					Vec3 at = {lava.lavaSpots[i][0], lava.lavaSpots[i][1], lava.lavaSpots[i][2]};
					at.y = track.mirrored ? -at.y : at.y;
					const bool start = starts[i] < t && t < starts[i] + 10, end = ends[i] < t && t < ends[i] + 10;
					if (start || end) {
						fireAt(43, 43, &at);
						h.timer2 = 400;
						// Its bubbling: released as an eruption starts, played again as it ends.
						if (start && h.on) {
							RaceEvent& loop = raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
							loop.sound = 3002, loop.stop = true;
						}
						else if (end && !h.on) {
							RaceEvent& loop = raise(RaceEvent::Sound, {Owner::Hazard, int16_t(index)});
							loop.sound = 3002, loop.looping = true, loop.near = 200, loop.far = 300, loop.place = at;
						}
						h.on = end;
					}
				}
			}
			else {
				h.timer2 = ms > h.timer2 ? 0 : h.timer2 - ms;
			}
			break;
		}
		case Hazard::Snow:
			// The snow restarts about every second (its particle is the show's: one is assumed to be had).
			h.timer += ms;
			if (h.timer > 1000 && h.on) {
				raise(RaceEvent::Finish, {Owner::Hazard, int16_t(index)});
				h.on = false;
			}
			if (h.armed && !h.on) {
				spawn("snow", 2, {Owner::Hazard, int16_t(index)}, {});
				h.on = true;
				h.timer = 0;
			}
			break;
		case Hazard::Smoke:
			if (!h.on) {
				spawn("smoke", 2, {Owner::Hazard, int16_t(index)}, {});
				h.on = true;
			}
			break;
		case Hazard::Rocket:
			if (h.lit && !h.on) {
				h.on = true;
				rocketShown(*this, true);
				startAt(35, nullptr);
				setSurface(*this, track.surfaceOf("mmrocc"), true);
			}
			else if (!h.lit && h.on) {
				h.on = false;
				rocketShown(*this, false);
				endAt(35, nullptr);
				setSurface(*this, track.surfaceOf("mmrocc"), false);
			}
			h.lit = false;
			break;
		case Hazard::Crane: {
			const int16_t m = track.animatedOf("crane");
			if (m < 0) {
				break;
			}
			// The hook: the end of the crane's arm (node 3, through its parents), as last drawn.
			const Vec3 hookAt = scenePoint(*this, size_t(m), 3, {}, true, clocks[size_t(m)].drawn);
			moveSphere(*this, h, hookAt);
			const float t = clocks[size_t(m)].time;
			if (h.armed && ((t > 150 && t < 180) || (t > 0 && t < 30))) {
				h.armed = false;
				fireAt(20, 20, &hookAt);
			}
			else if (!h.armed && ((t > 60 && t < 120) || (t > 210 && t < 270))) {
				h.armed = true;
			}
			break;
		}
		case Hazard::Launcher:
		case Hazard::Launchers: {
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
					if (shotHits(p.at, x, hit, normal)) {
						p.at = hit;
						explode(false, hit, false, -1);
						p.state = Projectile::Idle;
						fireAt(7, 7, &hit);
					}
					else {
						p.at = x;
					}
				}
			}
			if (p.state == Projectile::Idle) {
				deactivate(*this, index);
			}
			break;
		}
		case Hazard::Opener:
			setSurface(*this, spec.surface, true);
			if (spec.models[0] >= 0 && partDone(*this, size_t(spec.models[0]))) {
				deactivate(*this, index);
			}
			break;
		case Hazard::Sweep: {
			// Its aim sweeps: across by its angle, up and down by its range about its base.
			const int32_t period = spec.periodMs;
			h.timer += ms;
			if (h.timer >= period) {
				h.timer = 0;
			}
			const float n = float(h.timer);
			const int32_t half = period >> 1, quarter = period >> 2;
			float angle = (n / float(half)) * spec.sweep.x;
			if (h.timer > half) {
				angle = 3.1415927f - angle;
			}
			Vec3 f{cosEntry(int32_t(angle * 162.974655f)), cosEntry(-256 - int32_t(angle * -162.97466f)),
				cosEntry(int32_t(((n / float(quarter)) * 6.2831855f) * 162.974655f)) * spec.sweep.y + spec.sweep.z};
			f.y = track.mirrored ? -f.y : f.y;
			f = normalize(f);
			const Vec3 source = model >= 0 ? track.animated[size_t(model)].pose.position - Vec3{0, 0, 17} : spec.source;
			h.timer2 += ms;
			if (h.timer2 >= 8300) {
				h.timer2 = 0;
				const Aim aim{source, f};
				make(-1, Red, 2, &aim);
			}
			break;
		}
		case Hazard::Rain: {
			h.timer += ms;
			if (h.timer < spec.intervalMs || model < 0) {
				break;
			}
			h.timer = 0;
			// A ball dropped from under the emitter's node 1 (sampled now), a little to one side.
			const Pose node = localPose(*this, size_t(model), 1, clocks[size_t(model)].time);
			const AnimatedModel& m = track.animated[size_t(model)];
			const Vec3 at = toWorld(m.pose.rotation, node.position * m.scale) + m.pose.position;
			const Vec3 u = toWorld(m.pose.rotation, node.rotation.u);
			h.muzzle = {at.x - u.x * 35, at.y - u.y * 35, at.z - u.z * 35};
			const int16_t rx = int16_t(draw(Draw::Hazard)), ry = int16_t(draw(Draw::Hazard));
			Vec3 drop = {(float(rx % 4) + h.muzzle.x) - 2, (float(ry % 4) + h.muzzle.y) - 2, h.muzzle.z + 1};
			if (drop.x == h.muzzle.x && drop.y == h.muzzle.y) {
				drop.x = drop.x + 2;
			}
			h.point = drop;
			const Aim aim{h.muzzle, {}, int8_t(index)};
			make(-1, Red, 0, &aim);
			break;
		}
		case Hazard::Rock: {
			if (model < 0 || track.animated[size_t(model)].nodes.size() < 2) {
				break;
			}
			// Node 1 places the box: its own position (QUIRK: the parents' not applied), and for a rock ("rk...") its rows
			// turned by node 1 again and then each parent (QUIRK); other entities keep the axes.
			const AnimatedModel& m = track.animated[size_t(model)];
			const Animation* anim = m.animation >= 0 ? &track.animations[size_t(m.animation)] : nullptr;
			const Clock& c = clocks[size_t(model)];
			const Pose node = nodeLocal(m, anim, 1, c.part, c.drawn, c.loop);
			h.centre = toWorld(m.pose.rotation, node.position * m.scale) + m.pose.position;
			if (m.name.starts_with("rk")) {
				Vec3 r = node.rotation.f, f = node.rotation.u;
				for (int32_t n = 1; n >= 0; n = m.nodes[size_t(n)].parent) {
					const Basis rows = nodeLocal(m, anim, size_t(n), c.part, c.drawn, c.loop).rotation;
					r = toWorld(rows, r);
					f = toWorld(rows, f);
				}
				h.rows = basisFromForwardUp(toWorld(m.pose.rotation, r), toWorld(m.pose.rotation, f));
			}
			break;
		}
		case Hazard::Grabber: {
			if (model >= 0) {
				const Pose node = localPose(*this, size_t(model), 0, clocks[size_t(model)].drawn);
				const AnimatedModel& m = track.animated[size_t(model)];
				moveSphere(*this, h, toWorld(m.pose.rotation, node.position * m.scale) + m.pose.position);
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
				releaseGrab(*this, h);
			}
			break;
		}
		default: break;
		}
	}
}

void World::touchHazard(size_t index, size_t i)
{
	Hazard& h = hazards[index];
	Kart& k = karts[i];
	const bool spinning = k.flags & KartSpinning, moving = k.headingSpeed != 0;
	switch (h.type) {
	case Hazard::Ghost:
		if (!spinning && moving && !(racers[i].flags & RacerShielded)) {
			spin(i, 1, 0.0099999998f, 0);
			spinOut(i);
			k.velocity = {};
			impulse(i, {0, 0, 150});
			voice(i, RaceEvent::Negative);
			playSound(3015, {Owner::Hazard, int16_t(index)}, &visible[i].position, 200, 300);
		}
		break;
	case Hazard::Lava:
		if (!spinning && moving) {
			spin(i, 1, 0.0099999998f, 0);
			voice(i, RaceEvent::Negative);
		}
		break;
	case Hazard::Rocket: h.lit = h.lit || (racers[i].flags & RacerShielded); break;
	case Hazard::Crane:
		if (!spinning && moving) {
			spin(i, 1, 0.0099999998f, 0);
			const Vec3 at = visible[i].position;
			fireAt(21, 21, &at);
		}
		break;
	case Hazard::Grabber: {
		const HazardSpec& spec = track.hazards[index];
		const float t = spec.model >= 0 ? clocks[size_t(spec.model)].time : 0;
		if (h.grab == 2 || !(t <= spec.low || t >= spec.high) || (racers[i].flags & RacerShielded) || (h.held >= 0 && h.held != int8_t(i))) {
			break;
		}
		if (h.held < 0) {
			h.grab = 1;
			h.timer2 = 1000;
		}
		h.held = int8_t(i);
		racers[i].flags |= RacerGrabbed;
		if (playback[i].active) {
			routePace(playback[i], k, -0.40000001f);
		}
		else {
			const Vec3 c = visible[i].position;
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
