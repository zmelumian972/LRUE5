// Events: their dispatch to the event table (timers, lap marks, forces, links) and then to the hazards; the track
// timers; the step queue (timed waits, and the trigger list racers are swept against each step) with its shared budget
// of 100 entries; the touch spheres' and positional spheres' events.

#include "Common/Assert.h"
#include "LRCore/Race/World.h"

#include <algorithm>

namespace lr {

namespace {

constexpr size_t QueueEntries = 100;

size_t entries(const World& w)
{
	return w.triggers.waits.size() + w.triggers.waitsWalked + w.racers.contactOrder.size() + w.triggers.memberOrder.size();
}

// A random length: below 1023 a remainder, above it a multiple.
int32_t randomLength(int32_t length, int32_t x) { return length > 1023 ? x * (length / 1023) : x % length; }

// The event chained to a phase: as it starts (S), while active (A), as it ends (E).
int32_t chainOf(const int32_t chains[3], EventState::Phase phase)
{
	switch (phase) {
	case EventState::Starting: return chains[0];
	case EventState::Active: return chains[1];
	case EventState::Ended: return chains[2];
	default: return -1;
	}
}

// A phase change's chain: the new phase's event starts, the old one's ends (both at once if both).
void chain(World& w, const int32_t chains[3], EventState::Phase from, int32_t to)
{
	const int32_t old = chainOf(chains, from);
	if (old != -1 && to != -1) {
		w.triggers.fireAt(w, to, old, nullptr);
	}
	else if (old != -1) {
		w.triggers.endAt(w, old, nullptr);
	}
	else if (to != -1) {
		w.triggers.startAt(w, to, nullptr);
	}
}

// A phase change with its chain.
void changePhase(World& w, const int32_t chains[3], EventState& s, EventState::Phase to, int32_t event)
{
	const EventState::Phase from = s.phase;
	s.phase = to;
	chain(w, chains, from, event);
}

// An event timer's end: only once it has run its time and nothing holds it.
void endTimer(World& w, size_t index)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	if (s.remainingMs == 0 && !s.held) {
		changePhase(w, r.chains, s, EventState::Ended, -1);
	}
}

void startTimer(World& w, size_t index)
{
	EventState& s = w.scenery.events[index];
	s.remainingMs = w.track.events[index].durationMs;
	s.phase = EventState::Active;
	chain(w, w.track.events[index].chains, EventState::Idle, w.track.events[index].chains[1]);
}

// A force, started or ended for a racer: it pushes while armed.
void pushStart(World& w, size_t index, size_t racer)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	if (r.onEnd || !(s.phase == EventState::Idle || s.phase == EventState::Ended) || !s.armed) {
		return;
	}
	if (r.channel == 0 || r.channel == 1) {
		w.powerUps.push(w, racer, r.channel, r.force);
	}
	s.phase = EventState::Active;
	chain(w, r.chains, EventState::Idle, r.chains[1]);
}

void pushEnd(World& w, size_t index, size_t racer)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	if (s.phase == EventState::Idle || r.noEnd) {
		return;
	}
	if (r.channel == 0 || r.channel == 1) {
		w.powerUps.endPush(w, racer, r.channel); // (QUIRK: for whichever racer ends it)
	}
	changePhase(w, r.chains, s, EventState::Idle, -1);
}

// The lap marks, for a racer: its zone, and their chains (a mark stays ended).
void markStart(World& w, size_t racer, int32_t event)
{
	for (const LapMark& m : w.track.lapMarks) {
		if (m.event == event) {
			w.racers.lapMark(w, racer, m.zone);
			chain(w, m.chains, EventState::Idle, m.chains[1]);
		}
	}
}

void markEnd(World& w, int32_t event)
{
	for (const LapMark& m : w.track.lapMarks) {
		if (m.event == event) {
			chain(w, m.chains, EventState::Ended, -1);
		}
	}
}

// The show's resources: a sound plays (a retriggered one only arms the show's retriggers; a
// one-shot leaves its resource idle, so each start plays it), a particle's emitter is spawned (the show retries while
// it has none), a sky state begins its transition and ends at the next update.
void showStart(World& w, size_t index, const Vec3* at)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	const Owner owner{Owner::TrackEvent, int16_t(index)};
	switch (r.kind) {
	case EventResource::Sound: {
		if (r.atEvent && at) {
			s.at = *at;
		}
		RaceEvent& e = w.raceView.raise(RaceEvent::Sound, owner);
		e.sound = r.sound;
		e.place = s.at;
		e.near = r.nearDistance, e.far = r.farDistance, e.volume = r.volume, e.pitch = r.pitch;
		e.looping = r.looping;
		e.value = r.chance;
		s.phase = r.chance < 255 || r.looping ? EventState::Active : EventState::Idle;
		break;
	}
	case EventResource::Particle: {
		if (r.atEvent && at) {
			s.at = *at;
		}
		RaceEvent& e = w.raceView.raise(RaceEvent::Spawn, owner);
		e.set = 2;
		e.name = r.name;
		e.place = s.at;
		changePhase(w, r.chains, s, EventState::Active, r.chains[1]);
		break;
	}
	case EventResource::Sky: {
		RaceEvent& e = w.raceView.raise(RaceEvent::Sky, owner);
		e.name = r.name;
		e.value = r.skyMs;
		e.flags = r.skyFlags;
		changePhase(w, r.chains, s, EventState::EndPending, r.chains[1]);
		break;
	}
	default: break;
	}
}

void showEnd(World& w, size_t index)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	const Owner owner{Owner::TrackEvent, int16_t(index)};
	const EventState::Phase from = s.phase;
	s.phase = EventState::Idle;
	switch (r.kind) {
	case EventResource::Sound:
		if (r.looping || r.chance < 255) {
			w.raceView.stopSound(r.sound, owner);
		}
		break;
	case EventResource::Particle:
		w.raceView.raise(RaceEvent::Finish, owner);
		chain(w, r.chains, from, -1);
		break;
	case EventResource::Sky: chain(w, r.chains, from, -1); break;
	default: break;
	}
}

// A part animation queues a part on its entity's playback; its phase follows the entity's part.
void queuePart(World& w, int16_t entity, int32_t part)
{
	if (entity < 0 || part < 0) {
		return;
	}
	Clock& c = w.scenery.clocks[entity];
	c.queued = int16_t(part);
	c.loop = false; // (the current part finishes)
	c.done = false;
}

void partStart(World& w, size_t index)
{
	const EventResource& r = w.track.events[index];
	queuePart(w, r.entity, r.parts[2] != -1 ? r.parts[2] : r.parts[0]);
}

void partEnd(World& w, size_t index)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	const int32_t playing = r.entity >= 0 ? w.scenery.clocks[r.entity].part : -1;
	if (s.phase != EventState::Idle && playing != r.parts[1] && !(r.parts[3] != -1 && playing == r.parts[3])) {
		queuePart(w, r.entity, r.parts[3] != -1 ? r.parts[3] : r.parts[1]);
	}
	s.phase = EventState::Idle; // (no chain move: the next update follows the entity again)
}

void partUpdate(World& w, size_t index)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	if (s.phase == EventState::EndPending) {
		partEnd(w, index);
	}
	if (r.entity < 0) {
		return;
	}
	const Clock& clock = w.scenery.clocks[r.entity];
	const int32_t playing = clock.part;
	const int32_t active = r.parts[0], idle = r.parts[1], start = r.parts[2], end = r.parts[3];
	if (clock.queueFree()) {
		if (s.phase == EventState::Starting && playing == start) {
			queuePart(w, r.entity, active);
		}
		else if (s.phase == EventState::Ended && playing == end) {
			queuePart(w, r.entity, idle);
		}
		else if (s.phase == EventState::Active && !r.looping && playing == active) {
			queuePart(w, r.entity, end != -1 ? end : idle);
		}
	}
	const auto follow = [&](EventState::Phase to, int32_t slot) { changePhase(w, r.chains, s, to, slot < 0 ? -1 : r.chains[slot]); };
	if (playing == start) {
		if (s.phase != EventState::Starting) {
			follow(EventState::Starting, 0);
		}
	}
	else if (playing == active) {
		if (s.phase != EventState::Active) {
			follow(EventState::Active, 1);
		}
	}
	else if (playing == end) {
		if (s.phase != EventState::Ended) {
			follow(EventState::Ended, 2);
		}
	}
	else if (playing == idle && s.phase != EventState::Idle) {
		follow(EventState::Idle, -1);
	}
}

// A node transform: while not idle, its collision entity takes its animated entity's node's pose
// now (the node's own transform, scaled, placed by the entity; its orientation from the node's forward and up rows).
void nodeTransform(World& w, size_t index)
{
	const EventResource& r = w.track.events[index];
	if (r.entity < 0 || r.collision < 0) {
		return;
	}
	LR_ASSERT(inRange(w.track.animated, r.entity) && inRange(w.scenery.clocks, r.entity) && inRange(w.track.volumes, r.collision));
	const AnimatedModel& m = w.track.animated[r.entity];
	const Animation* a = w.track.animationOf(m);
	const Clock& c = w.scenery.clocks[r.entity];
	const Pose local = sampleNode(m.nodes, a, size_t(r.node), c.part, c.time, c.loop);
	Track::Volume& v = w.track.volumes[r.collision];
	v.position = toWorld(m.pose.rotation, local.position * m.scale) + m.pose.position;
	v.rotation = basisFromForwardUp(toWorld(m.pose.rotation, local.rotation.f), toWorld(m.pose.rotation, local.rotation.u));
}

// A model distance: its model shown (its distances as loaded) or hidden (-1).
void showModel(World& w, const EventResource& r, bool shown)
{
	if (r.entity < 0) {
		return;
	}
	std::array<float, 3>& d = r.plain ? w.scenery.modelDistances[r.entity] : w.scenery.animatedDistances[r.entity];
	d = shown ? (r.plain ? w.track.modelDistances[r.entity] : w.track.animated[r.entity].distances) : std::array<float, 3>{-1, -1, -1};
}

// A kind's start and end at a place (the entry rules call them).
void kindStart(World& w, size_t index, const Vec3* at)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	switch (r.kind) {
	case EventResource::PartAnimation: partStart(w, index); break;
	case EventResource::NodeTransform:
		s.phase = EventState::Active;
		chain(w, r.chains, EventState::Idle, r.chains[1]);
		break;
	case EventResource::ModelDistance:
		showModel(w, r, !r.hide);
		s.phase = EventState::Active;
		break;
	default: showStart(w, index, at); break;
	}
}

void kindEnd(World& w, size_t index)
{
	const EventResource& r = w.track.events[index];
	EventState& s = w.scenery.events[index];
	switch (r.kind) {
	case EventResource::PartAnimation: partEnd(w, index); break;
	case EventResource::NodeTransform: changePhase(w, r.chains, s, EventState::Idle, -1); break;
	case EventResource::ModelDistance:
		showModel(w, r, r.hide);
		s.phase = EventState::Idle;
		break;
	default: showEnd(w, index); break;
	}
}

// The entry rules at a place, a timer's (which only listens there) or another kind's: a start
// (ending false) starts an event at rest unless it starts on its end; an end starts one that does (and ends it, unless
// it never ends), or ends one not idle.
void enter(World& w, size_t index, const Vec3* at, bool ending, bool timer)
{
	const EventResource& r = w.track.events[index];
	const EventState::Phase phase = w.scenery.events[index].phase;
	const auto end = [&] { timer ? endTimer(w, index) : kindEnd(w, index); };
	if (r.onEnd == ending && (phase == EventState::Idle || phase == EventState::Ended)) {
		timer ? startTimer(w, index) : kindStart(w, index, at);
		if (ending && !r.noEnd) {
			end();
		}
	}
	else if (ending && phase != EventState::Idle && !r.noEnd) {
		end();
	}
}

// A track timer's wait ran out: its event turns on or off, and the other phase's length is scheduled (shortened by
// how late this one fired; a random length is drawn from that).
void timerFired(World& w, size_t index, int32_t elapsed)
{
	const TrackTimer& t = w.track.timers[index];
	TrackTimerState& s = w.scenery.timers[index];
	if (t.event != -1) {
		if (s.on) {
			w.triggers.endAt(w, t.event, nullptr);
		}
		else {
			w.triggers.startAt(w, t.event, nullptr);
		}
	}
	const bool on = !s.on;
	int32_t length = on ? t.onMs : t.offMs;
	const uint32_t late = uint32_t(elapsed) - uint32_t(s.on ? t.onMs : t.offMs);
	if (late < uint32_t(length)) {
		length -= int32_t(late);
	}
	if (on ? t.randomOn : t.randomOff) {
		length = randomLength(length, w.draw(Draw::TrackTimer));
	}
	w.triggers.schedule(w, {length, 0, Wait::TrackTimer, uint8_t(index)});
	s.on = on;
}

} // namespace

int16_t Triggers::addMember(World& w, Member m)
{
	if (entries(w) >= QueueEntries) {
		return -1;
	}
	int16_t id;
	if (!freeMembers.empty()) {
		id = int16_t(freeMembers.back());
		freeMembers.pop_back();
		LR_ASSERT(inRange(members, id));
		members[id] = m;
	}
	else {
		id = int16_t(members.size());
		members.push_back(m);
	}
	memberOrder.insert(memberOrder.begin(), uint16_t(id));
	return id;
}

void Triggers::killMember(int16_t& id)
{
	if (id >= 0) {
		LR_ASSERT(inRange(members, id));
		members[id].dead = true;
		id = -1;
	}
}

bool Triggers::schedule(World& w, Wait wait)
{
	if (entries(w) >= QueueEntries) {
		return false; // (silently dropped: that timer stops for good)
	}
	waits.insert(waits.begin(), wait);
	return true;
}

void Triggers::runWaits(World& w, int32_t dt)
{
	w.raceView.stage = RaceEvent::Queue;
	// Newest first: one scheduled during the walk goes at the head and waits for the next step. A wait is dropped
	// after it fires, so it counts against the queue's entries until then.
	std::vector<Wait> walk, kept;
	walk.swap(waits);
	waitsWalked = walk.size();
	for (Wait& wait : walk) {
		wait.elapsedMs += dt;
		if (wait.elapsedMs < wait.lengthMs) {
			kept.push_back(wait);
			continue;
		}
		if (wait.kind == Wait::TrackTimer) {
			timerFired(w, wait.ref, wait.elapsedMs);
		}
		else {
			w.powerUps.endShove(w, wait.ref);
		}
		waitsWalked--;
	}
	waitsWalked = 0;
	waits.insert(waits.end(), kept.begin(), kept.end());
}

void Triggers::startAt(World& w, int32_t x, const Vec3* at)
{
	fireAt(w, x, -1, at);
	for (size_t t = 0; t < w.track.targets.size(); t++) {
		if (x != -1 && w.track.targets[t].disabledBy == x) {
			w.powerUps.targetsOn[t] = 0;
		}
	}
}

void Triggers::endAt(World& w, int32_t x, const Vec3* at) { fireAt(w, -1, x, at); }

// (-1: no event; startAt and endAt are fireAt with one of them.)
void Triggers::fireAt(World& w, int32_t s, int32_t e, const Vec3* at)
{
	if (w.observer.to) {
		w.observer.to->onEvent(w, s, e, at);
	}
	// The positional kinds in their order (sounds, part animations, particles, sky states, timers, node transforms, model
	// distances; material animations are in no file).
	const auto each = [&](EventResource::Kind kind) {
		for (size_t i = 0; i < w.track.events.size(); i++) {
			const EventResource& r = w.track.events[i];
			if (r.kind != kind) {
				continue;
			}
			if (s != -1 && r.id == s) {
				enter(w, i, at, false, false);
			}
			if (e != -1 && r.id == e) {
				enter(w, i, at, true, false);
			}
		}
	};
	for (const EventResource::Kind kind :
		{EventResource::Sound, EventResource::PartAnimation, EventResource::Particle, EventResource::Sky}) {
		each(kind);
	}
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		if (r.kind != EventResource::Timer) {
			continue;
		}
		if (s != -1 && r.id == s) {
			enter(w, i, at, false, true);
		}
		if (s != -1 && r.holdEvent == s) {
			w.scenery.events[i].held = true;
		}
		if (e != -1 && r.id == e) {
			enter(w, i, at, true, true);
		}
		if (e != -1 && r.holdEvent == e) {
			w.scenery.events[i].held = false;
		}
	}
	each(EventResource::NodeTransform);
	each(EventResource::ModelDistance);
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		if (r.kind == EventResource::Link && s != -1 && r.id == s) {
			forceEnd(w, r.forced);
		}
	}
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		if (r.kind == EventResource::Force) {
			w.scenery.events[i].armed = s != -1 && r.armingEvent == s ? true
				: e != -1 && r.armingEvent == e						  ? false
																	  : w.scenery.events[i].armed;
		}
	}
	w.scenery.hazardEvent(w, s, e, at, -1, false);
}

void Triggers::startFor(World& w, int32_t x, size_t racer) { fireFor(w, x, -1, racer); }
void Triggers::endFor(World& w, int32_t x, size_t racer) { fireFor(w, -1, x, racer); }

void Triggers::fireFor(World& w, int32_t s, int32_t e, size_t racer)
{
	// The colour transforms (the tint is the presentation's; whether a racer has one, the snow's).
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		if (r.kind != EventResource::Colour) {
			continue;
		}
		EventState& st = w.scenery.events[i];
		const auto start = [&] {
			if (!r.onEntity && racer < w.raceView.tinted.size()) {
				w.raceView.tinted[racer] = !r.clear;
			}
			chain(w, r.chains, EventState::Idle, r.chains[1]);
		};
		const auto end = [&] {
			if (!r.clear && !r.onEntity && racer < w.raceView.tinted.size()) {
				w.raceView.tinted[racer] = 0;
			}
			const EventState::Phase from = st.phase;
			st.phase = EventState::Ended;
			chain(w, r.chains, from, -1);
		};
		const bool waiting = st.phase == EventState::Idle || st.phase == EventState::Ended;
		if (s != -1 && r.id == s && !r.onEnd && waiting) {
			start();
		}
		if (e != -1 && r.id == e) {
			if (r.onEnd && waiting) {
				start();
				if (!r.noEnd) {
					end();
				}
			}
			else if (st.phase != EventState::Idle && !r.noEnd) {
				end();
			}
		}
	}
	if (s != -1) {
		markStart(w, racer, s);
	}
	if (e != -1) {
		markEnd(w, e);
	}
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		if (r.kind == EventResource::Force) {
			if (s != -1 && r.id == s) {
				pushStart(w, i, racer);
			}
			if (e != -1 && r.id == e) {
				pushEnd(w, i, racer);
			}
		}
	}
	w.scenery.hazardEvent(w, s, e, &w.racers.visible[racer].position, int32_t(racer), false);
}

void Triggers::forceEnd(World& w, int32_t y)
{
	for (size_t i = 0; i < w.track.events.size(); i++) {
		if (w.track.events[i].id == y && w.track.events[i].kind != EventResource::Link) {
			w.scenery.events[i].phase = EventState::EndPending;
		}
	}
	w.scenery.hazardEvent(w, -1, y, nullptr, -1, true);
}

void Triggers::eventFrame(World& w, int32_t ms)
{
	w.raceView.stage = RaceEvent::Events;
	// The event table, kind by kind: sounds, particles and sky states forced or begun end; part
	// animations follow their entities; timers count down and end once run and not held; node transforms move their
	// collision; model distances and forces forced end.
	for (const EventResource::Kind kind :
		{EventResource::Sound, EventResource::PartAnimation, EventResource::Particle, EventResource::Sky}) {
		for (size_t i = 0; i < w.track.events.size(); i++) {
			if (w.track.events[i].kind != kind) {
				continue;
			}
			if (kind == EventResource::PartAnimation) {
				partUpdate(w, i);
			}
			else if (w.scenery.events[i].phase == EventState::EndPending) {
				showEnd(w, i);
			}
		}
	}
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		EventState& s = w.scenery.events[i];
		if (r.kind == EventResource::Timer) {
			if (s.phase == EventState::EndPending) {
				endTimer(w, i);
			}
			if (s.phase != EventState::Ended) {
				s.remainingMs = std::max(0, s.remainingMs - ms);
				if (s.remainingMs == 0) {
					endTimer(w, i);
				}
			}
		}
	}
	for (size_t i = 0; i < w.track.events.size(); i++) {
		if (w.track.events[i].kind == EventResource::NodeTransform && w.scenery.events[i].phase != EventState::Idle) {
			// (QUIRK: a forced end still moves it this once.)
			if (w.scenery.events[i].phase == EventState::EndPending) {
				kindEnd(w, i);
			}
			nodeTransform(w, i);
		}
	}
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		EventState& s = w.scenery.events[i];
		if (r.kind == EventResource::ModelDistance && s.phase == EventState::EndPending) {
			kindEnd(w, i);
		}
		else if (r.kind == EventResource::Force && s.phase == EventState::EndPending) {
			s.phase = EventState::Idle;
			chain(w, r.chains, EventState::EndPending, -1);
		}
	}
	w.scenery.hazardFrame(w, ms);
	w.raceView.stage = RaceEvent::Events;
	// The track timers' first delays: when one runs out, its first wait (an on length) is scheduled.
	for (size_t t = 0; t < w.scenery.timers.size(); t++) {
		TrackTimerState& s = w.scenery.timers[t];
		if (s.delayMs <= 0) {
			continue;
		}
		if (ms < s.delayMs) {
			s.delayMs -= ms;
			continue;
		}
		s.delayMs = 0;
		const TrackTimer& timer = w.track.timers[t];
		schedule(w, {timer.randomOn ? randomLength(timer.onMs, w.draw(Draw::TrackTimer)) : timer.onMs, 0, Wait::TrackTimer, uint8_t(t)});
	}
}

void Triggers::endTouchFrame(World& w)
{
	w.raceView.stage = RaceEvent::Events;
	// Positional spheres that went untouched for a frame end their events.
	const auto untouched = [&](PlaceState& p, const TouchSphere& t) {
		if (p.active && !p.touched) {
			p.active = false;
			if (t.event != -1) {
				endAt(w, t.event, &t.centre);
			}
		}
		p.touched = false;
	};
	for (size_t s = 0; s < places.size(); s++) {
		untouched(places[s], w.track.places[s]);
	}
	// The touch spheres: likewise at their centre; and racers that left end their events.
	for (size_t s = 0; s < touchInside.size(); s++) {
		const TouchSphere& t = w.track.touchSpheres[s];
		untouched(touchPlaces[s], t);
		const uint8_t left = touchLast[s] & ~touchInside[s] & 63;
		touchLast[s] = touchInside[s];
		touchInside[s] = 0;
		for (size_t i = 0; i < w.racers.race.size() && t.event != -1; i++) {
			if (left & (1u << i)) {
				endFor(w, t.event, i);
			}
		}
	}
}

namespace {

// The dead members leave the list (their slots free for the next added, in the list's order); the rest keep theirs.
void pruneMembers(World& w)
{
	size_t kept = 0;
	for (size_t n = 0; n < w.triggers.memberOrder.size(); n++) {
		const uint16_t m = w.triggers.memberOrder[n];
		LR_ASSERT(m < w.triggers.members.size());
		if (w.triggers.members[m].dead) {
			w.triggers.freeMembers.push_back(m);
		}
		else {
			w.triggers.memberOrder[kept++] = m;
		}
	}
	w.triggers.memberOrder.resize(kept);
}

// Insertion sort by the lowest x, equals keeping their order.
void sortMembers(World& w)
{
	auto xlo = [&](uint16_t m) { return w.triggers.members[m].centre.x - w.triggers.members[m].radius; };
	for (size_t n = 1; n < w.triggers.memberOrder.size(); n++) {
		const uint16_t e = w.triggers.memberOrder[n];
		size_t j = n;
		while (j > 0 && xlo(e) < xlo(w.triggers.memberOrder[j - 1])) {
			w.triggers.memberOrder[j] = w.triggers.memberOrder[j - 1];
			j--;
		}
		w.triggers.memberOrder[j] = e;
	}
}

} // namespace

// A racer touching a touch sphere: on its lap (or any), it collides with the sphere's world (if it has one) while
// touching it and 150 ms after; the sphere's event starts at its centre once, and for the racer as it enters.
void Triggers::touchSphere(World& w, size_t sphere, size_t i)
{
	LR_ASSERT(
		sphere < w.track.touchSpheres.size() && sphere < touchPlaces.size() && sphere < touchInside.size() && sphere < touchLast.size());
	const TouchSphere& t = w.track.touchSpheres[sphere];
	if (t.lap >= 0 && w.racers.race[i].laps != t.lap) {
		return;
	}
	const size_t volumes = w.track.volumes.size();
	if (t.world >= 0 && size_t(t.world) < volumes) {
		// The racer collides with the sphere's world while touching it (and 150 ms after).
		LR_ASSERT(i * volumes + size_t(t.world) < w.racers.worldMs.size());
		int32_t& ms = w.racers.worldMs[i * volumes + size_t(t.world)];
		Kart& k = w.racers.karts[i];
		if (ms == 0 && std::find(k.worlds, k.worlds + k.worldCount, t.world) == k.worlds + k.worldCount && k.worldCount < 5) {
			k.worlds[k.worldCount++] = t.world;
		}
		ms = 150;
	}
	PlaceState& p = touchPlaces[sphere];
	if (!p.active && t.event != -1) {
		startAt(w, t.event, &t.centre);
	}
	p.active = p.touched = true;
	const uint8_t bit = uint8_t(1u << i);
	const bool was = touchLast[sphere] & bit;
	touchInside[sphere] |= bit;
	if (!was && t.event != -1) {
		startFor(w, t.event, i);
	}
}

void Triggers::touchRacers(World& w, const Sphere* spheres)
{
	w.raceView.stage = RaceEvent::Queue;
	pruneMembers(w);
	sortMembers(w);
	// The list as it is now: a member added by a touch (put at the front) waits for the next step.
	const size_t listed = memberOrder.size();
	const auto at = [&](size_t n) { return memberOrder[memberOrder.size() - listed + n]; };
	for (uint8_t i : w.racers.contactOrder) {
		if (i >= Racers::RockEntry) {
			continue;
		}
		const Sphere& g = spheres[i];
		const float lo = g.centre.x - g.radius, hi = g.radius + g.centre.x;
		size_t n = 0;
		while (n < listed && members[at(n)].radius + members[at(n)].centre.x < lo) {
			n++;
		}
		for (; n < listed; n++) {
			const Member& m = members[at(n)];
			if (m.dead || (m.playersOnly && w.racers.race[i].ai)) {
				continue;
			}
			if (m.centre.x - m.radius > hi) {
				break;
			}
			const Vec3 d = m.centre - g.centre;
			const float r = m.radius + g.radius;
			if (!(lengthSquared(d) < r * r)) {
				continue;
			}
			switch (m.kind) {
			case Member::TouchSphere: touchSphere(w, m.ref, i); break;
			case Member::Positional: {
				LR_ASSERT(m.ref < places.size() && m.ref < w.track.places.size());
				PlaceState& p = places[m.ref];
				const TouchSphere& t = w.track.places[m.ref];
				if (!p.active && t.event != -1) {
					startAt(w, t.event, &t.centre);
				}
				p.active = p.touched = true;
				break;
			}
			case Member::Brick: w.powerUps.touchBrick(w, m.ref, i); break;
			case Member::Trap: w.powerUps.touchTrap(w, m.ref, i); break;
			case Member::Explosion: w.powerUps.blast(w, m.ref, i); break;
			case Member::Projectile: w.powerUps.touchProjectile(w, m.ref, i); break;
			case Member::Hazard: w.scenery.touchHazard(w, m.ref, i); break;
			}
		}
	}
	pruneMembers(w);
}

// The event table, the track timers, the trigger list's spheres, at load: timers ended, the touch spheres (each put
// at the front: in reverse order of the file) and the positional spheres; a track timer's first wait (its off length,
// drawn if random) unless it has a delay.
// The event table as the file is read (before the hazards, whose setup may fire events).
void setupEventTable(World& w)
{
	// The world databases as loaded: the models' draw distances, the bound material tracks running (looping, at their
	// files' rates).
	w.scenery.animatedDistances.resize(w.track.animated.size());
	for (size_t m = 0; m < w.track.animated.size(); m++) {
		w.scenery.animatedDistances[m] = w.track.animated[m].distances;
	}
	w.scenery.modelDistances = w.track.modelDistances;
	w.raceView.materials.assign(w.track.materialTracks.size(), {});
	for (const Track::MaterialBinding& b : w.track.materialBindings) {
		RaceView::MaterialState& m = w.raceView.materials[b.track];
		m.assigned = m.looping = true;
		m.rate = w.track.materialTracks[b.track].rate;
		m.shown = 0;
	}
	for (const Sprite& s : w.track.sprites) {
		if (s.track >= 0) {
			RaceView::MaterialState& m = w.raceView.materials[s.track];
			m.assigned = m.looping = true;
			m.rate = w.track.materialTracks[s.track].rate;
			m.shown = 0;
		}
	}
	w.scenery.events.assign(w.track.events.size(), {});
	w.raceView.tinted.assign(w.racers.race.size(), 0);
	for (size_t i = 0; i < w.track.events.size(); i++) {
		const EventResource& r = w.track.events[i];
		w.scenery.events[i].phase = r.kind == EventResource::Timer || r.kind == EventResource::Colour ? EventState::Ended
																									  : EventState::Idle;
		w.scenery.events[i].at = r.position;
		if (r.kind == EventResource::ModelDistance && !r.hide) {
			showModel(w, r, false); // (shown only while active)
		}
	}
}

void setupEvents(World& w)
{
	w.triggers.touchPlaces.assign(w.track.touchSpheres.size(), {});
	w.triggers.places.assign(w.track.places.size(), {});
	std::vector<size_t> byFile(w.track.touchSpheres.size());
	for (size_t s = 0; s < byFile.size(); s++) {
		byFile[s] = s;
	}
	std::sort(byFile.begin(), byFile.end(),
		[&](size_t a, size_t b) { return w.track.touchSpheres[a].index < w.track.touchSpheres[b].index; });
	for (size_t s : byFile) {
		const TouchSphere& t = w.track.touchSpheres[s];
		w.triggers.addMember(w, {t.centre, t.radius, Member::TouchSphere, t.playersOnly, false, uint16_t(s)});
	}
	for (size_t s = 0; s < w.track.places.size(); s++) {
		const TouchSphere& t = w.track.places[s];
		w.triggers.addMember(w, {t.centre, t.radius, Member::Positional, t.playersOnly, false, uint16_t(s)});
	}
	w.scenery.timers.assign(w.track.timers.size(), {});
	for (size_t t = 0; t < w.track.timers.size(); t++) {
		const TrackTimer& timer = w.track.timers[t];
		const int32_t length = timer.randomOff ? randomLength(timer.offMs, w.draw(Draw::TrackTimer)) : timer.offMs;
		w.scenery.timers[t].delayMs = timer.delayMs;
		if (timer.delayMs == 0) {
			w.triggers.schedule(w, {length, 0, Wait::TrackTimer, uint8_t(t)});
		}
	}
}

} // namespace lr
