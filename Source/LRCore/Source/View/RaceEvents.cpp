// The race view's part for the show: the events the rules raise for effects and sounds, and
// the HUDs' state and clocks (their elements are drawn with the menus' 2D, Draw.cpp).

#include "LRCore/Common/Game.h"
#include "LRCore/Race/World.h"
#include "Show/SoundIds.h"

#include <algorithm>

namespace lr {

RaceEvent& RaceView::raise(RaceEvent::Kind kind, Owner owner)
{
	RaceEvent& e = raceEvents.emplace_back();
	e.kind = kind;
	e.stage = stage;
	e.owner = owner;
	return e;
}

void RaceView::voice(World& w, size_t racer, RaceEvent::VoiceLine line)
{
	RaceEvent& e = raise(RaceEvent::Voice, {Owner::Racer, int16_t(racer)});
	e.value = line;
	e.place = w.racers.visible[racer].position;
}

void RaceView::spawn(const char* emitter, uint8_t set, Owner owner, Vec3 at)
{
	RaceEvent& e = raise(RaceEvent::Spawn, owner);
	for (size_t c = 0; c < 8 && emitter[c]; c++) {
		e.name[c] = emitter[c];
	}
	e.set = set;
	e.place = at;
}

void RaceView::playSound(int32_t sound, Owner owner, const Vec3* at, float near, float far)
{
	RaceEvent& e = raise(RaceEvent::Sound, owner);
	e.sound = sound;
	e.positional = at != nullptr;
	e.place = at ? *at : Vec3{};
	e.near = near;
	e.far = far;
}

void RaceView::loopSound(int32_t sound, Owner owner, Vec3 at, float near, float far)
{
	playSound(sound, owner, &at, near, far);
	raceEvents.back().looping = true;
}

void RaceView::stopSound(int32_t sound, Owner owner)
{
	RaceEvent& e = raise(RaceEvent::Sound, owner);
	e.sound = sound;
	e.stop = true;
}

void RaceView::updateHuds(World& w, int32_t realMs)
{
	w.session.openingMs = std::max(0, w.session.openingMs - realMs); // (the opening fade runs on through a pause)
	const Session::Phase phase = w.session.phase;
	// The attract demo's DEMO: on for 750 ms, off for 250 (the race's ms).
	if (w.session.brief.mode == RaceBrief::Mode::Demo && !w.session.paused()) {
		const int32_t ms = w.session.brief.cheats & CheatFastForward ? int32_t(float(realMs) * 1.75f) : realMs;
		demoMs = ms > demoMs ? 1000 : demoMs - ms;
	}
	if (phase != Session::Countdown && phase != Session::Racing && phase != Session::Finished) {
		return;
	}
	const RaceEvent::Stage was = stage;
	stage = RaceEvent::Hud;
	for (size_t p = 0; p < huds.size() && p < cameras.size(); p++) {
		Hud& h = huds[p];
		const size_t i = size_t(cameras[p].racer);
		// A circuit's FINISH goes once the race's standings have been drawn over it.
		if (h.standings) {
			h.banner = 0;
		}
		h.standings = w.session.standingsTable() == 1;
		h.clockMs += w.session.paused() ? 0 : realMs;
		const int32_t e = h.clockMs;
		h.clockMs = 0;
		// The banner: 3, 2, 1 (a beep as each begins), GO (the "go"), then nothing; FINISH counts down.
		if (h.banner > 0) {
			const int32_t before = h.banner;
			h.banner += e;
			const int32_t s = h.banner / 1000;
			if (s < 3 && (before == 1 || before / 1000 != s)) {
				playSound(CountdownBeepSound, {}, nullptr);
			}
			else if (s == 3 && before / 1000 != 3) {
				playSound(CountdownGoSound, {}, nullptr);
			}
			else if (s >= 5) {
				h.banner = 0;
			}
		}
		else if (h.banner < 0) {
			h.banner = std::max(-60000, h.banner - e);
		}
		if (w.racers.race[i].flags & RacerGhost) {
			continue; // (in the warp tube: only the banner)
		}
		h.speed = 0.2f * w.racers.karts[i].headingSpeed + 0.8f * h.speed;
		// The best lap: the lap just completed, at each change of the count but the first seen.
		const Racer& r = w.racers.race[i];
		if (r.laps != h.laps) {
			const int32_t old = h.laps;
			h.laps = r.laps;
			if (old >= 0 && old < w.session.raceLaps && (r.lapMs[old] <= h.bestMs || h.bestMs == 0)) {
				h.bestMs = r.lapMs[old];
			}
		}
		// The place: re-read while no pulse runs; a change pulses it in.
		if (h.pulseMs == 0) {
			const int32_t shown = h.finalPlace >= 0 ? h.finalPlace : std::min(r.standing - 1, 8);
			if (shown != h.place) {
				h.place = shown;
				h.pulseMs = 1;
			}
		}
		if (h.pulseMs > 0) {
			h.pulseMs += e;
			if (h.pulseMs > 1050) {
				h.pulseMs = 0;
			}
		}
	}
	stage = was;
}

} // namespace lr
