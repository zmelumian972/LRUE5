// The race's show as the core keeps it: the events the rules raise for effects and sounds, and
// the HUDs' state and clocks (their elements are drawn with the menus' 2D, Draw.cpp).

#include "LRCore/Common/Game.h"
#include "LRCore/Race/World.h"

#include <algorithm>

namespace lr {

namespace {

constexpr int32_t SoundBeep = 0, SoundGo = 15; // the countdown's, in the race's effects bank

} // namespace

RaceEvent& World::raise(RaceEvent::Kind kind, Owner owner)
{
	RaceEvent& e = raceView.raceEvents.emplace_back();
	e.kind = kind;
	e.stage = raceView.stage;
	e.owner = owner;
	return e;
}

void World::voice(size_t racer, RaceEvent::VoiceLine line)
{
	RaceEvent& e = raise(RaceEvent::Voice, {Owner::Racer, int16_t(racer)});
	e.value = line;
	e.place = racers.visible[racer].position;
}

void World::spawn(const char* emitter, uint8_t set, Owner owner, Vec3 at)
{
	RaceEvent& e = raise(RaceEvent::Spawn, owner);
	for (size_t c = 0; c < 8 && emitter[c]; c++) {
		e.name[c] = emitter[c];
	}
	e.set = set;
	e.place = at;
}

void World::playSound(int32_t sound, Owner owner, const Vec3* at, float near, float far)
{
	RaceEvent& e = raise(RaceEvent::Sound, owner);
	e.sound = sound;
	e.positional = at != nullptr;
	e.place = at ? *at : Vec3{};
	e.near = near;
	e.far = far;
}

void World::updateHuds(int32_t realMs)
{
	session.openingMs = std::max(0, session.openingMs - realMs); // (the opening fade runs on through a pause)
	const Session::Phase phase = session.phase;
	// The attract demo's DEMO: on for 750 ms, off for 250 (the race's ms).
	if (session.brief.mode == RaceBrief::Mode::Demo && !paused()) {
		const int32_t ms = session.brief.cheats & CheatFastForward ? int32_t(float(realMs) * 1.75f) : realMs;
		raceView.demoMs = ms > raceView.demoMs ? 1000 : raceView.demoMs - ms;
	}
	if (phase != Session::Countdown && phase != Session::Racing && phase != Session::Finished) {
		return;
	}
	const RaceEvent::Stage was = raceView.stage;
	raceView.stage = RaceEvent::Hud;
	for (size_t p = 0; p < raceView.huds.size() && p < raceView.cameras.size(); p++) {
		Hud& h = raceView.huds[p];
		const size_t i = size_t(raceView.cameras[p].racer);
		// A circuit's FINISH goes once the race's standings have been drawn over it.
		if (h.standings) {
			h.banner = 0;
		}
		h.standings = standingsTable() == 1;
		h.clockMs += paused() ? 0 : realMs;
		const int32_t e = h.clockMs;
		h.clockMs = 0;
		// The banner: 3, 2, 1 (a beep as each begins), GO (the "go"), then nothing; FINISH counts down.
		if (h.banner > 0) {
			const int32_t before = h.banner;
			h.banner += e;
			const int32_t s = h.banner / 1000;
			if (s < 3 && (before == 1 || before / 1000 != s)) {
				playSound(SoundBeep, {}, nullptr);
			}
			else if (s == 3 && before / 1000 != 3) {
				playSound(SoundGo, {}, nullptr);
			}
			else if (s >= 5) {
				h.banner = 0;
			}
		}
		else if (h.banner < 0) {
			h.banner = std::max(-60000, h.banner - e);
		}
		if (racers.race[i].flags & RacerGhost) {
			continue; // (in the warp tube: only the banner)
		}
		h.speed = 0.2f * racers.karts[i].headingSpeed + 0.8f * h.speed;
		// The best lap: the lap just completed, at each change of the count but the first seen.
		const Racer& r = racers.race[i];
		if (r.laps != h.laps) {
			const int32_t old = h.laps;
			h.laps = r.laps;
			if (old >= 0 && old < session.raceLaps && (r.lapMs[old] <= h.bestMs || h.bestMs == 0)) {
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
	raceView.stage = was;
}

} // namespace lr
