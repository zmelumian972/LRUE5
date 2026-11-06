// The set pieces among the hazards: the pillar that falls, the sphinx that blows up, the hammer, the triggered
// animations that open a wall, the rolling rocks, the snow and the smoke, and the code puzzle.

#include "Powers/HazardParts.h"

#include <algorithm>

namespace lr {

namespace {

constexpr int32_t OpenerEvent = 9; // the triggered animation's start
constexpr int32_t BlowupEvent = 16; // the sphinx blowing up, at its place
constexpr int32_t HammerStrikeEvent = 43; // the hammer's strike
// The code puzzle: the switches 200..205 (each digit's pair), the display's read-out 207..209, the success, the
// display's blink, and the feedback fired for a switch (by its side) and a read-out digit (right or wrong).
constexpr int32_t SwitchEvents = 200;
constexpr int32_t ReadOutEvents = 207;
constexpr int32_t CodeSuccessEvent = 18;
constexpr int32_t CodeDisplayEvent = 28;
constexpr int32_t EvenSwitchFeedback = 21, OddSwitchFeedback = 30;
constexpr int32_t RightDigitFeedback = 29, WrongDigitFeedback = 20;
constexpr int32_t CodeBlinkMs = 2500;
constexpr float PillarLandingFrame = 50.0f;
constexpr int32_t StepQueueSize = 100; // a rock waits its turn only while the step queue has room
constexpr int32_t SnowRestartMs = 1000;

// --- The pillar: its top drops (its animation once) and lands as a wall; it falls once ---

void setupPillar(HazardSite s)
{
	const int16_t m = s.w.track.hazardEntities.pillarTop;
	if (m >= 0) {
		s.w.scenery.clocks[m].animating = false;
	}
}

void dropPillar(HazardSite s, int32_t)
{
	World& w = s.w;
	const int16_t m = w.track.hazardEntities.pillarTop;
	if (m >= 0) {
		Clock& c = w.scenery.clocks[m];
		c.animating = true;
		play(c, w.track.animations[w.track.animated[m].animation], 0);
		c.loop = false;
	}
	s.h.landed = false;
	s.h.state = Hazard::Active;
}

void pillarFrame(HazardSite s, int32_t)
{
	World& w = s.w;
	const int16_t m = w.track.hazardEntities.pillarTop;
	if (!s.h.landed && m >= 0 && w.scenery.clocks[m].time > PillarLandingFrame) {
		s.h.landed = true;
		setSurface(w, w.track.hazardEntities.pillarSurface, false); // a wall now
		w.triggers.fireAt(w, LandingEvent, LandingEvent, nullptr);
	}
}

// --- The sphinx: shown, it blows up (its material animation, if the track has it), its surface made passable, and
// once that has played it is hidden (its three draw distances -1) ---

void showSphinx(HazardSite s) { s.h.on = true; }

Vec3 blowupPlace(const World& w)
{
	const int16_t m = w.track.hazardEntities.blowup;
	return m >= 0 ? w.track.animated[m].pose.position : Vec3{};
}

void blowSphinx(HazardSite s, int32_t)
{
	World& w = s.w;
	if (!s.h.on) {
		return; // (blown up already: hidden)
	}
	if (w.track.blowupDuration > 0) {
		w.scenery.blowup = 0;
		const Vec3 at = blowupPlace(w);
		w.triggers.startAt(w, BlowupEvent, &at);
	}
	setSurface(w, w.track.hazardEntities.sphinxSurface, true);
	s.h.state = Hazard::Active;
}

void endSphinx(HazardSite s)
{
	World& w = s.w;
	if (w.scenery.blowup >= 0) {
		return; // still blowing up
	}
	s.h.on = false;
	const int16_t m = w.track.hazardEntities.blowup;
	if (m >= 0) {
		w.scenery.animatedDistances[m] = {-1, -1, -1};
	}
	s.h.state = Hazard::Idle;
}

void sphinxFrame(HazardSite s, int32_t)
{
	World& w = s.w;
	if (w.scenery.blowup < 0) {
		const Vec3 at = blowupPlace(w);
		if (w.track.blowupDuration > 0) {
			w.triggers.endAt(w, BlowupEvent, &at);
		}
		endHazard(s);
	}
}

// --- The hammer: it strikes twice a swing ---

void armHammer(HazardSite s, int32_t)
{
	s.h.armed = true;
	s.h.state = Hazard::Active;
}

void hammerFrame(HazardSite s, int32_t)
{
	World& w = s.w;
	Hazard& h = s.h;
	const int16_t m = w.track.hazardEntities.hammer;
	const float t = m >= 0 ? w.scenery.clocks[m].time : 0;
	if (h.armed && ((t > 22 && t < 28) || (t > 72 && t < 78))) {
		h.armed = false;
		w.triggers.fireAt(w, HammerStrikeEvent, HammerStrikeEvent, nullptr);
	}
	else if (!h.armed && ((t > 0 && t < 20) || (t > 40 && t < 60))) {
		h.armed = true;
	}
}

// --- A triggered animation: its models play their parts once, its surface passable, and stop once all have ---

void setOpener(HazardSite s, bool animating)
{
	for (int16_t m : s.spec.models) {
		if (m >= 0) {
			s.w.scenery.clocks[m].animating = animating;
		}
	}
}

void setupOpener(HazardSite s) { setOpener(s, false); }

void startOpener(HazardSite s, int32_t)
{
	for (int16_t m : s.spec.models) {
		if (m >= 0) {
			s.w.scenery.clocks[m].animating = true;
			s.w.scenery.clocks[m].loop = false;
		}
	}
	s.w.triggers.fireAt(s.w, OpenerEvent, OpenerEvent, nullptr);
	s.h.state = Hazard::Active;
}

void endOpener(HazardSite s)
{
	for (int16_t m : s.spec.models) {
		if (m >= 0 && !partDone(s.w, size_t(m))) {
			return; // (still playing)
		}
	}
	setOpener(s, false);
	s.h.state = Hazard::Idle;
}

void openerFrame(HazardSite s, int32_t)
{
	setSurface(s.w, s.spec.surface, true);
	if (s.spec.models[0] >= 0 && partDone(s.w, size_t(s.spec.models[0]))) {
		endHazard(s);
	}
}

// --- A rolling rock: a box of the file's size its model's node carries, stepped with the racers while active ---

void setupRock(HazardSite s)
{
	// Its sphere its largest half size; its model's animation starts at a phase.
	s.h.radius = std::max({s.spec.size.x * 0.5f, s.spec.size.y * 0.5f, s.spec.size.z * 0.5f});
	if (s.spec.model >= 0) {
		s.w.scenery.clocks[s.spec.model].time = s.spec.startFrame;
	}
}

void startRock(HazardSite s, int32_t)
{
	World& w = s.w;
	if (w.racers.contactOrder.size() + w.triggers.memberOrder.size() + w.triggers.waits.size() < StepQueueSize) { // (its entries)
		w.racers.contactOrder.insert(w.racers.contactOrder.begin(), uint8_t(Racers::RockEntry + s.index));
	}
	s.h.state = Hazard::Active;
}

void endRock(HazardSite s)
{
	std::vector<uint8_t>& order = s.w.racers.contactOrder;
	order.erase(std::remove(order.begin(), order.end(), uint8_t(Racers::RockEntry + s.index)), order.end());
	s.h.state = Hazard::Idle;
}

void rockFrame(HazardSite s, int32_t)
{
	const World& w = s.w;
	const int16_t model = s.spec.model;
	if (model < 0 || w.track.animated[model].nodes.size() < 2) {
		return;
	}
	// Node 1 places the box: its own position (QUIRK: the parents' not applied), and for a rock ("rk...") its rows
	// turned by node 1 again and then each parent (QUIRK); other entities keep the axes.
	const AnimatedModel& m = w.track.animated[model];
	const Animation* anim = w.track.animationOf(m);
	const Clock& c = w.scenery.clocks[model];
	const Pose node = w.scenery.nodeLocal(m, anim, 1, c.part, c.drawn, c.loop);
	s.h.centre = modelToWorld(m, node.position);
	if (m.name.starts_with("rk")) {
		Vec3 r = node.rotation.f, f = node.rotation.u;
		for (int32_t n = 1; n >= 0; n = m.nodes[n].parent) {
			const Basis rows = w.scenery.nodeLocal(m, anim, size_t(n), c.part, c.drawn, c.loop).rotation;
			r = toWorld(rows, r);
			f = toWorld(rows, f);
		}
		s.h.rows = basisFromForwardUp(toWorld(m.pose.rotation, r), toWorld(m.pose.rotation, f));
	}
}

// --- The snow and the smoke: the show's particles, the snow's restarted about every second (one is assumed to be
// had); ended, finished and let go ---

void setupSnow(HazardSite s)
{
	s.h.state = Hazard::Active;
	s.h.armed = true; // (enabled)
}

void startParticle(HazardSite s, int32_t) { s.h.state = Hazard::Active; }

void endParticle(HazardSite s)
{
	if (s.h.on) {
		s.w.raceView.raise(RaceEvent::Finish, s.owner());
		s.h.on = false;
	}
	s.h.state = Hazard::Idle;
}

void snowFrame(HazardSite s, int32_t ms)
{
	Hazard& h = s.h;
	h.cycleMs += ms;
	if (h.cycleMs > SnowRestartMs && h.on) {
		s.w.raceView.raise(RaceEvent::Finish, s.owner());
		h.on = false;
	}
	if (h.armed && !h.on) {
		s.w.raceView.spawn("snow", 2, s.owner(), {});
		h.on = true;
		h.cycleMs = 0;
	}
}

void smokeFrame(HazardSite s, int32_t)
{
	if (!s.h.on) {
		s.w.raceView.spawn("smoke", 2, s.owner(), {});
		s.h.on = true;
	}
}

// --- The code puzzle: three switch pairs, one right and one resetting in each; the display reads the code out ---

// The code display's three material tracks (mmcode1..3's bindings), -1 where a light is missing.
Array<int32_t, 3> codeTracks(const World& w)
{
	Array<int32_t, 3> out = {-1, -1, -1};
	for (int d = 0; d < 3; d++) {
		const int32_t model = w.track.hazardEntities.codeLights[d];
		for (const Track::MaterialBinding& b : w.track.materialBindings) {
			if (!b.animated && b.entity == model && model >= 0 && out[d] < 0) {
				out[d] = b.track;
			}
		}
	}
	return out;
}

// The display reads the code out (each light still: red for the even switch, blue for the odd one) or blinks (each
// track runs again, looping, from where the read-out left it); the show hears which.
void codeDisplay(World& w, const Hazard& h, bool blink)
{
	const Array<int32_t, 3> tracks = codeTracks(w);
	RaceEvent& e = w.raceView.raise(RaceEvent::CodeDisplay);
	e.flags = blink ? 1 : 0;
	for (int d = 0; d < 3; d++) {
		const bool blue = h.right[d] != 2 * d;
		e.value |= blue ? 1 << d : 0;
		if (tracks[d] < 0) {
			continue;
		}
		RaceView::MaterialState& m = w.raceView.materials[tracks[d]];
		if (blink) {
			m.assigned = m.looping = true;
		}
		else {
			m.cursor = blue ? float(w.track.materialTracks[tracks[d]].duration - 1) : 0;
			w.raceView.showMaterial(w, size_t(tracks[d]));
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

// A code chosen, active at once, the display's tracks at their own rates (the binding kept).
void setupCode(HazardSite s)
{
	s.h.progress = 0;
	chooseCode(s.w, s.h);
	s.h.cycleMs = 1;
	s.h.state = Hazard::Active;
	const Array<int32_t, 3> tracks = codeTracks(s.w);
	const float rates[3] = {0.003f, 0.004f, 0.005f};
	for (int d = 0; d < 3; d++) {
		if (tracks[d] >= 0) {
			s.w.raceView.materials[tracks[d]].rate = rates[d];
		}
	}
}

// A switch: a reset one starts over, the right ones in order solve it (the display blinks, a new code is chosen);
// a switch and a read-out digit fire their feedback.
void codeEvent(HazardSite s, int32_t e, const Vec3* at)
{
	World& w = s.w;
	Hazard& h = s.h;
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
		w.triggers.fireAt(w, CodeSuccessEvent, CodeSuccessEvent, nullptr);
		codeDisplay(w, h, true); // (the colours blink)
		h.cycleMs = CodeBlinkMs;
		chooseCode(w, h);
		w.triggers.startAt(w, CodeDisplayEvent, nullptr);
	}
	int32_t feedback = -1;
	if (e >= SwitchEvents && e <= SwitchEvents + 5) {
		feedback = (e & 1) ? OddSwitchFeedback : EvenSwitchFeedback;
	}
	else if (e >= ReadOutEvents && e <= ReadOutEvents + 2) {
		feedback = h.right[e - ReadOutEvents] == 2 * (e - ReadOutEvents) ? RightDigitFeedback : WrongDigitFeedback;
	}
	if (feedback != -1) {
		w.triggers.fireAt(w, feedback, feedback, at);
	}
}

// Its blinking display runs out (every frame, whatever its state).
void codeFrame(HazardSite s, int32_t ms)
{
	Hazard& h = s.h;
	if (h.cycleMs == 0) {
		return;
	}
	if (ms >= h.cycleMs) {
		h.cycleMs = 0;
		s.w.triggers.endAt(s.w, CodeDisplayEvent, nullptr);
		codeDisplay(s.w, h, false);
	}
	else {
		h.cycleMs -= ms;
	}
}

void stays(HazardSite) {}

} // namespace

const HazardKind PillarHazard{
	.keyword = 0x28, .trigger = 10, .setup = setupPillar, .activate = dropPillar, .deactivate = stays, .frame = pillarFrame};
const HazardKind SphinxHazard{
	.keyword = 0x29, .trigger = 12, .setup = showSphinx, .activate = blowSphinx, .deactivate = endSphinx, .frame = sphinxFrame};
const HazardKind HammerHazard{.keyword = 0x2a, .trigger = 50, .activate = armHammer, .frame = hammerFrame};
const HazardKind CodeHazard{.keyword = 0x2d, .setup = setupCode, .frame = codeFrame, .started = codeEvent, .ownsState = true};
const HazardKind SnowHazard{.keyword = 0x2f, .setup = setupSnow, .activate = startParticle, .deactivate = endParticle, .frame = snowFrame};
const HazardKind SmokeHazard{.keyword = 0x30, .trigger = 10, .activate = startParticle, .deactivate = endParticle, .frame = smokeFrame};
const HazardKind OpenerHazard{
	.keyword = 0x34, .trigger = FromSpec, .setup = setupOpener, .activate = startOpener, .deactivate = endOpener, .frame = openerFrame};
const HazardKind RockHazard{
	.keyword = 0x3e, .trigger = FromSpec, .setup = setupRock, .activate = startRock, .deactivate = endRock, .frame = rockFrame};

} // namespace lr
