// The race session around the simulation: its phases, the
// dialogs that pause it, a circuit's points and the end of FINISHED and RESULTS, the fade to black, a time race's runs and ghosts,
// the tunes, the race's records and how it ended.

#include "LRCore/Race/World.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Common/Tables.h"

#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Menus.h"
#include "LRData/Race.h"
#include "Show/SoundIds.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

// GAME.SRF's lines.
enum : int32_t {
	LineContinue = 14,
	LineRestartRace = 15,
	LineRestartCircuit = 16,
	LineExit = 17,
	LineYes = 18,
	LineNo = 19,
	LinePaused = 20,
	LineNotBeaten = 21,
	LineBeaten = 23,
	LineSure = 44,
};

// A rotation's quaternion by the trace: w first when the trace is positive, else the largest diagonal first.
Quat rotationOf(const Basis& m)
{
	const float trace = m.f.x + m.s.y + m.u.z;
	Quat q;
	if (trace > 0) {
		const float r = std::sqrt(trace + 1), s = 0.5f / r;
		q = {(m.u.y - m.s.z) * s, (m.f.z - m.u.x) * s, (m.s.x - m.f.y) * s, 0.5f * r};
	}
	else if (m.f.x >= m.s.y && m.f.x >= m.u.z) {
		const float r = std::sqrt(m.f.x - m.s.y - m.u.z + 1), s = 0.5f / r;
		q = {0.5f * r, (m.f.y + m.s.x) * s, (m.f.z + m.u.x) * s, (m.u.y - m.s.z) * s};
	}
	else if (m.s.y >= m.u.z) {
		const float r = std::sqrt(m.s.y - m.u.z - m.f.x + 1), s = 0.5f / r;
		q = {(m.f.y + m.s.x) * s, 0.5f * r, (m.s.z + m.u.y) * s, (m.f.z - m.u.x) * s};
	}
	else {
		const float r = std::sqrt(m.u.z - m.f.x - m.s.y + 1), s = 0.5f / r;
		q = {(m.f.z + m.u.x) * s, (m.s.z + m.u.y) * s, 0.5f * r, (m.s.x - m.f.y) * s};
	}
	return q;
}

GhostRun runOf(const GhostFile& g)
{
	GhostRun run;
	run.laps = g.laps;
	run.position = {g.position[0], g.position[1], g.position[2]};
	run.rotation = {g.rotation[0], g.rotation[1], g.rotation[2], g.rotation[3]};
	run.positions = g.positions;
	run.rotations = g.rotations;
	return run;
}

// Taking a race's best lap and race into a record: when there's none yet or it's as good (both held by the lap's racer).
void keepBest(RaceRecord& into, const RaceRecord& from)
{
	if (from.lapMs == 0) {
		return;
	}
	if (into.lapMs == 0 || from.lapMs <= into.lapMs) {
		into.lapMs = from.lapMs;
		into.lapBy = from.lapBy;
	}
	if (into.raceMs == 0 || from.raceMs <= into.raceMs) {
		into.raceMs = from.raceMs;
		into.raceBy = from.lapBy;
	}
}

} // namespace

bool Session::setup(World& w, const Cache& cache, const RaceBrief& race)
{
	w = {}; // (the observer stays)
	brief = race;
	openingMs = 500;
	raceLaps = race.laps;
	autopilot = race.autopilot;
	w.random = race.random;
	const size_t n = race.slots.size();
	// Every racer's parts, what the session reads of them before the simulation sets them up (a replayed race has only
	// these); no track yet, so no dynamic worlds' timers (setup's).
	w.racers.resize(n, 0);
	for (size_t i = 0; i < n; i++) {
		Racer& r = w.racers.race[i];
		r.player = race.slots[i].player;
		r.ai = !r.player;
		r.standing = int32_t(i) + 1;
		circuitPoints[i] = race.slots[i].points;
	}
	NameList music;
	if (!cache.get("GAMEDATA/" + race.track + "/LEGOMSC", music)) {
		return false;
	}
	m_musicCount = int32_t(music.names.size());
	tune = {0, false, true, {}};
	// The intro's flyby: the race file's intro camera animation.
	RaceFile file;
	if (!cache.get("GAMEDATA/" + race.track + "/" + race.track + ".RAB", file)) {
		return false;
	}
	AnimationFile camera;
	const std::string flybyFile = file.introCamera.empty() ? std::string()
														   : "GAMEDATA/" + race.track + "/" + binaryName(file.introCamera) + ".ADB";
	if (!flybyFile.empty() && cache.size(flybyFile) && cache.get(flybyFile, camera) && !camera.parts.empty()) {
		w.track.introFrames = camera.parts[0].frames;
		w.track.introRate = camera.parts[0].rate;
	}
	watchingFlyby = w.track.introFrames > 0;
	if (race.mode == RaceBrief::Mode::TimeRace) {
		prepareRuns();
	}
	return true;
}

// A time race's runs: the record ghost loaded again, the clock and the current run cleared (TRY AGAIN, a new session);
// a restart from the pause dialog only resets.
void Session::prepareRuns()
{
	timeRace.record = runOf(brief.ghost);
	timeRace.recordValid = !timeRace.record.positions.empty();
	resetRuns();
}

void Session::resetRuns()
{
	TimeRace& t = timeRace;
	t.clockMs = 0;
	t.running = false;
	t.current = {};
	t.record.finished = t.best.finished = false;
}

void Session::restart(World& w, const Cache* cache)
{
	takeRecord(w);
	w.raceView.raise(RaceEvent::StopEffects); // (the show's effects all stop; the rules end their loops)
	// What the session carries over a restart.
	const RaceBrief carried = brief;
	const RaceRecord best = record;
	const Array<int32_t, 6> points = circuitPoints;
	const TimeRace runs = timeRace;
	const uint32_t index = w.random;
	const int32_t blink = dialog.blinkMs;
	const std::vector<Hud> shown = w.raceView.huds; // (each keeps its smoothed speed and its visibility)
	bool ready = true;
	if (cache) {
		ready = w.setup(*cache, carried);
	}
	else {
		// (A replayed race: the session alone, from the files it read.)
		const int32_t count = m_musicCount;
		// Its phase, clocks and pace start again.
		const Session fresh;
		phase = fresh.phase, ms = fresh.ms, flyby = fresh.flyby, finishers = fresh.finishers;
		paceDelay = fresh.paceDelay, paceBonus = fresh.paceBonus;
		watchingFlyby = w.track.introFrames > 0;
		for (size_t i = 0; i < w.racers.race.size(); i++) {
			w.racers.race[i] = {};
			w.racers.race[i].player = carried.slots[i].player;
			w.racers.race[i].ai = !w.racers.race[i].player;
			w.racers.race[i].standing = int32_t(i) + 1;
		}
		if (autopilot) {
			w.racers.handToAi(w, 0);
		}
		m_musicCount = count;
		tune = {0, false, true, {}};
		dialog = {};
		fade = {};
		roundPoints = {};
		ending = Session::End::None;
		m_timeRaceAsked = false;
	}
	w.raceView.huds.resize(shown.size());
	for (size_t p = 0; p < w.raceView.huds.size(); p++) {
		w.raceView.huds[p] = {};
		w.raceView.huds[p].speed = shown[p].speed;
		w.raceView.huds[p].visible = shown[p].visible;
	}
	w.random = index;
	record = best;
	circuitPoints = points;
	timeRace = runs;
	dialog.blinkMs = blink;
	restarted = true;
	if (!ready) {
		ending = Session::End::Abort; // (the race's files no longer load: the session ends, back to the menus)
	}
}

void Session::takeRecord(World& w)
{
	RaceRecord now;
	int32_t bestLap = 0, bestLapBy = 0, bestRace = 0;
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		const Racer& r = w.racers.race[i];
		if (!r.player || r.laps < raceLaps) {
			continue;
		}
		int32_t sum = 0;
		for (int32_t lap = 0; lap < raceLaps; lap++) {
			sum += r.lapMs[lap];
			if (bestLap == 0 || r.lapMs[lap] < bestLap) {
				bestLap = r.lapMs[lap];
				bestLapBy = int32_t(i);
			}
		}
		bestRace = bestRace == 0 || sum < bestRace ? sum : bestRace;
	}
	now = {bestLap, bestLapBy, bestRace, bestLapBy};
	keepBest(record, now);
}

void Session::finished(World& w, size_t i)
{
	// A circuit's points by place; a player's finish ends the racing (its tune: the win or the loss), and a time
	// race's run.
	if (brief.mode == RaceBrief::Mode::Circuit) {
		roundPoints[i] = executableTables().placePoints[size_t(std::min(w.racers.race[i].place, 5))];
		circuitPoints[i] += roundPoints[i];
	}
}

void Session::racingEnded(World& w, size_t i)
{
	// FINISH on the HUD of the player who ended the racing (the demo's: HUD 0), with its place.
	for (size_t p = 0; p < w.raceView.huds.size() && p < w.raceView.cameras.size(); p++) {
		if (size_t(w.raceView.cameras[p].racer) == i) {
			w.raceView.huds[p].banner = -1;
			w.raceView.huds[p].finalPlace = w.racers.race[i].standing - 1;
		}
	}
	if (brief.mode == RaceBrief::Mode::TimeRace) {
		TimeRace& t = timeRace;
		for (size_t lap = 0; lap < 3; lap++) {
			t.current.laps[lap] = w.racers.race[i].lapMs[lap];
		}
		if (t.current.total() < t.best.total() || t.best.total() == 0) {
			std::swap(t.current, t.best);
		}
		t.bestValid = t.running = false;
		t.bestKept = true;
	}
	tune = {w.racers.race[i].standing == 1 ? 3 : 2, false, true, {}};
}

void Session::racingStarted()
{
	roundPoints = {};
	if (brief.mode == RaceBrief::Mode::TimeRace) {
		timeRace.running = true;
		timeRace.bestValid = timeRace.bestValid || timeRace.bestKept;
	}
	// The racing tune: the track's own in a circuit or with no extras, else the presentation draws among them.
	tune = {1, true, true, {}};
	if (brief.mode != RaceBrief::Mode::Circuit && m_musicCount > 4) {
		static constexpr int32_t Extras[4] = {1, 4, 5, 6};
		tune.choices.assign(Extras, Extras + std::min(4, m_musicCount - 3));
	}
}

void Session::ended(World& w)
{
	phase = Session::Over;
	if (ending == Session::End::None) {
		ending = Session::End::Over;
	}
	takeRecord(w);
}

void Session::startFade()
{
	if (!fade.active) {
		fade = {1000, 1000, true, false};
	}
}

void Session::recordRun(World& w, int32_t dt)
{
	TimeRace& t = timeRace;
	t.sampled = false;
	if (brief.mode != RaceBrief::Mode::TimeRace || !t.running) {
		return;
	}
	// A ghost that has passed its last sample is finished (hidden from then on).
	const size_t sample = size_t(t.clockMs / TimeRace::SampleMs);
	for (GhostRun* run : {&t.record, &t.best}) {
		if ((run == &t.record ? t.recordValid : t.bestValid) && sample + 1 >= run->positions.size()) {
			run->finished = true;
		}
	}
	t.clockMs += dt;
	if (dt >= t.countdownMs) {
		t.countdownMs += TimeRace::SampleMs - dt;
		GhostRun& run = t.current;
		if (int32_t(run.positions.size()) < TimeRace::MostSamples) {
			const Vec3 at = w.racers.visible[0].position;
			const Quat turn = rotationOf(w.racers.visible[0].rotation);
			if (run.positions.empty()) {
				run.position = at;
				run.rotation = turn;
			}
			run.positions.push_back({int16_t(at.x * 32), int16_t(at.y * 32), int16_t(at.z * 32)});
			run.rotations.push_back({int8_t(turn.x * 127), int8_t(turn.y * 127), int8_t(turn.z * 127), int8_t(turn.w * 127)});
			t.sampled = true;
		}
	}
	else {
		t.countdownMs -= dt;
	}
}

bool Session::ghostAt(const GhostRun& run, Vec3& position, Quat& rotation) const
{
	const TimeRace& t = timeRace;
	if (run.finished || run.positions.empty() || t.clockMs >= TimeRace::ShownMs) {
		return false;
	}
	if (!t.running) {
		position = run.position;
		rotation = run.rotation;
		return true;
	}
	const size_t sample = size_t(t.clockMs / TimeRace::SampleMs);
	const float between = float(t.clockMs - TimeRace::SampleMs * int32_t(sample)) * 0.004f;
	if (sample + 1 >= run.positions.size()) {
		return false;
	}
	const auto at = [&](size_t i) { return Vec3{run.positions[i][0] / 32.0f, run.positions[i][1] / 32.0f, run.positions[i][2] / 32.0f}; };
	const auto turn = [&](size_t i) {
		return Quat{run.rotations[i][0] / 127.0f, run.rotations[i][1] / 127.0f, run.rotations[i][2] / 127.0f, run.rotations[i][3] / 127.0f};
	};
	const Vec3 p0 = at(sample), p1 = at(sample + 1);
	position = p0 + (p1 - p0) * between;
	// The rotation blended, to unit length.
	const Quat q = blend(turn(sample), turn(sample + 1), between);
	const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	rotation = norm > 0 ? Quat{q.x / norm, q.y / norm, q.z / norm, q.w / norm} : q;
	return true;
}

int32_t Session::standingsTable() const
{
	if (brief.mode != RaceBrief::Mode::Circuit || paused() || ending != Session::End::None) {
		return 0;
	}
	return phase == Session::Finished && ms > 5000 ? 1 : phase == Session::Results ? 2 : 0;
}

// The dialogs.

void Session::openDialog(World& w, RaceDialog::Kind kind)
{
	// (The blink runs on from dialog to dialog.)
	RaceDialog& d = dialog;
	const int32_t blink = d.blinkMs;
	d = {};
	d.blinkMs = blink;
	d.kind = kind;
	for (Hud& h : w.raceView.huds) {
		h.visible = false; // (only the banner, the time behind's '-' and WRONG WAY hide)
	}
	switch (kind) {
	case RaceDialog::Pause:
		d.prompt = LinePaused;
		d.options = {LineContinue, brief.mode == RaceBrief::Mode::Circuit ? LineRestartCircuit : LineRestartRace, LineExit};
		d.count = 3;
		break;
	case RaceDialog::Confirm:
		d.prompt = LineSure;
		d.options = {LineYes, LineNo, 0};
		d.count = 2;
		d.selection = 1;
		break;
	case RaceDialog::TimeRaceEnd: {
		const bool beaten = timeRace.beaten();
		d.prompt = beaten ? LineBeaten : LineNotBeaten;
		d.second = d.prompt + 1;
		d.options = {LineYes, LineNo, 0};
		d.count = 2;
		break;
	}
	case RaceDialog::None: break;
	}
}

// What a chosen option does once its key is released; true if the race runs this frame.
bool Session::choose(World& w, const Cache* cache)
{
	RaceDialog& d = dialog;
	const int32_t s = d.selection;
	for (Hud& h : w.raceView.huds) {
		h.visible = true;
	}
	switch (d.kind) {
	case RaceDialog::Pause:
		if (s == 0) {
			d.kind = RaceDialog::None;
			return true;
		}
		{
			const RaceDialog::Action what = s == 1 ? RaceDialog::Restart : RaceDialog::Exit;
			openDialog(w, RaceDialog::Confirm);
			d.confirming = what;
		}
		return false;
	case RaceDialog::Confirm:
		if (s == 1) {
			d.kind = RaceDialog::None;
			return true;
		}
		if (d.confirming == RaceDialog::Exit) {
			d.kind = RaceDialog::None;
			ending = Session::End::Abort;
			ended(w);
			return false;
		}
		d.kind = RaceDialog::None;
		if (brief.mode == RaceBrief::Mode::Circuit && !brief.firstRace) {
			ending = Session::End::RestartCircuit;
			ended(w);
			return false;
		}
		if (brief.mode == RaceBrief::Mode::Circuit) {
			for (RaceBrief::Slot& slot : brief.slots) {
				slot.points = 0;
			}
			circuitPoints = {};
			roundPoints = {};
		}
		if (brief.mode == RaceBrief::Mode::TimeRace) {
			resetRuns();
		}
		restart(w, cache);
		return true;
	case RaceDialog::TimeRaceEnd:
		d.kind = RaceDialog::None;
		if (s == 1) {
			ended(w);
			return false;
		}
		prepareRuns();
		restart(w, cache);
		return true;
	case RaceDialog::None: break;
	}
	return true;
}

void Session::update(World& w, int32_t frameMs, std::span<const uint32_t> pressed, std::span<const uint32_t> released, const Cache* cache)
{
	tune.started = false;
	restarted = false;
	w.raceView.raceEvents.clear();
	if (ending != Session::End::None) {
		return;
	}
	// The demo: any key (but a pad's axis) ends it at once.
	if (brief.mode == RaceBrief::Mode::Demo) {
		for (uint32_t k : pressed) {
			if ((k >> 28) <= 3) {
				ending = Session::End::Abort;
				ended(w);
				return;
			}
		}
	}
	// Esc pauses. The race runs unless a dialog is up; then the dialog: its blink (it runs on from dialog to dialog),
	// Up and Down move, Enter or Space chooses and the key's release acts. A dialog closing lets the race run this
	// frame, its clock held.
	bool opened = false; // (the Esc that opens the dialog doesn't also cancel it)
	if (!paused()) {
		for (uint32_t k : pressed) {
			if (k == MenuEscape && brief.mode != RaceBrief::Mode::Demo) {
				openDialog(w, RaceDialog::Pause);
				opened = true;
				break;
			}
		}
	}
	bool ran = false;
	const auto run = [&](bool held) {
		m_clockHeld = held;
		const int32_t raceMs = brief.cheats & CheatFastForward ? int32_t(float(frameMs) * 1.75f) : frameMs;
		w.frame(raceMs, frameMs);
		m_clockHeld = false;
		updateFade(raceMs);
		if (standingsTable()) {
			blinkMs = frameMs > blinkMs ? 400 : blinkMs - frameMs;
		}
		ran = true;
	};
	if (!paused()) {
		run(false);
	}
	if (!paused()) {
		return;
	}
	RaceDialog& d = dialog;
	for (uint32_t k : pressed) {
		if (d.chosen) {
			continue;
		}
		// A pad's stick (or d-pad) moves as Up and Down do.
		const bool up = k == MenuUp || k == (SourceAxis | 3), down = k == MenuDown || k == (SourceAxis | 2);
		if (up || down) {
			d.selection = (d.selection + (up ? d.count - 1 : 1)) % d.count;
		}
		else if (k == MenuEnter || k == MenuSpace || k == MenuPadButton) {
			d.chosen = true;
			d.key = k;
		}
		// Esc (a pad's B) cancels: CONTINUE, or NO.
		else if (k == MenuEscape && !opened && d.kind != RaceDialog::TimeRaceEnd) {
			d.selection = d.kind == RaceDialog::Pause ? 0 : 1;
			d.chosen = true;
			d.key = k;
		}
	}
	bool resumed = false;
	for (uint32_t k : released) {
		if (paused() && d.chosen && k == d.key) {
			resumed = choose(w, cache);
		}
	}
	if (ending != Session::End::None) {
		updateFade(frameMs);
		return; // (the blink stops with the race)
	}
	d.blinkMs += frameMs;
	d.blinkMs = d.blinkMs >= 500 ? 0 : d.blinkMs;
	if (resumed && !ran) {
		run(true);
	}
}

void Session::updateFade(int32_t frameMs)
{
	if (!fade.active || paused()) {
		return;
	}
	if (fade.left == 0) {
		if (fade.ended) {
			fade.active = false;
			return;
		}
		fade.ended = true;
	}
	fade.left = std::max(0, fade.left - frameMs);
}

void Session::advance(World& w, int32_t frameMs, int32_t sessionMs)
{
	ms += m_clockHeld ? 0 : sessionMs;
	updatePhase(w);
	if (watchingFlyby) {
		flyby += (float(w.track.introRate) / 1000) * float(frameMs);
	}
}

void Session::updatePhase(World& w)
{
	const RaceBrief::Mode mode = brief.mode;
	switch (phase) {
	case Session::Intro:
		if (watchingFlyby && flyby >= float(w.track.introFrames - 1)) {
			watchingFlyby = false;
			ms = 0;
		}
		if (ms < 2000 || watchingFlyby) {
			return;
		}
		startCountdown(w);
		[[fallthrough]];
	case Session::Countdown:
		// (A test drive's ends at once.)
		if (ms < 3000 && mode != RaceBrief::Mode::TestDrive) {
			return;
		}
		startRacing(w);
		[[fallthrough]];
	case Session::Racing:
		crossLine(w);
		if (phase != Session::Finished) {
			return;
		}
		[[fallthrough]];
	case Session::Finished:
		// Outside a circuit the screen fades to black from 9 s; a time race then asks to race again (and goes no
		// further).
		if (mode != RaceBrief::Mode::Circuit && ms > 9000) {
			startFade();
		}
		if (mode == RaceBrief::Mode::TimeRace && ms > 9500 && !m_timeRaceAsked) {
			m_timeRaceAsked = true;
			openDialog(w, RaceDialog::TimeRaceEnd);
			return;
		}
		if (ms < 10000 || mode == RaceBrief::Mode::TimeRace) {
			for (size_t i = 0; i < w.racers.race.size(); i++) {
				if (w.racers.race[i].laps >= raceLaps && !(w.racers.race[i].flags & RacerFinished)) {
					w.racers.finish(w, i);
					if (w.racers.race[i].player) {
						w.racers.handToAi(w, i);
					}
				}
			}
			return;
		}
		placeTheRest(w);
		[[fallthrough]];
	case Session::Results:
		// A circuit shows its standings for 5 s, fading to black in the last.
		if (mode == RaceBrief::Mode::Circuit) {
			if (ms > 4000) {
				startFade();
			}
			if (ms < 5000) {
				return;
			}
		}
		ended(w);
		break;
	case Session::Over: break;
	}
}

// The intro over: the countdown starts, the cameras take their views, the players' engines start.
void Session::startCountdown(World& w)
{
	const RaceBrief::Mode mode = brief.mode;
	phase = Session::Countdown;
	ms = 0;
	// The cameras take their views again: each snaps to its pose.
	for (size_t p = 0; p < w.raceView.cameras.size(); p++) {
		w.raceView.cameras[p].kind = PlayerCamera::Chase;
		w.raceView.chooseView(p, w.raceView.cameras[p].view);
	}
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		if (w.racers.race[i].player) {
			w.racers.race[i].flags |= RacerEngine;
			w.raceView.playSound(EngineStartSound, {Owner::Racer, int16_t(i)},
				&w.racers.visible[i].position); // (its engine starts: the show's)
		}
	}
	// The HUDs' countdown (a test drive has none).
	for (Hud& h : w.raceView.huds) {
		h.banner = mode == RaceBrief::Mode::TestDrive ? h.banner : 1;
	}
}

// The start: the racers leave the grid (an AI may start with a turbo, a player with its start boost).
void Session::startRacing(World& w)
{
	phase = Session::Racing;
	ms = 0;
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		Racer& r = w.racers.race[i];
		r.flags &= ~RacerOnGrid;
		w.racers.playback[i].paused = false;
		r.zones[0] = LapMark::Open;
		r.zones[1] = LapMark::Past;
		r.zones[2] = LapMark::Line;
		if (r.ai && (w.draw(Draw::StartGreen) & 255) < w.racers.powers[i].chances[Green - 1]) {
			w.powerUps.make(w, int8_t(i), Green, 0); // an AI may start with a turbo
		}
	}
	// A player's start boost: the race starts in the window its throttle opened.
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		Controls& c = w.racers.controls[i];
		if (w.racers.race[i].player && !w.racers.race[i].ai && c.boostWindowMs > 0) {
			w.powerUps.make(w, int8_t(i), Green, c.boostWindowMs >= 60 ? 1 : 0);
		}
		c.boostWindowMs = 0;
	}
	racingStarted();
	finishers = 0;
}

// The racers over the line this frame finish; the first player over it ends the racing (the others race on).
void Session::crossLine(World& w)
{
	const RaceBrief::Mode mode = brief.mode;
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		if (w.racers.race[i].laps < raceLaps || (w.racers.race[i].flags & RacerFinished)) {
			continue;
		}
		w.racers.finish(w, i);
		if (!w.racers.race[i].ai || (autopilot && i == 0)) {
			// The first player over the line: the others race on for a while (a test drive's for 1 s).
			phase = Session::Finished;
			ms = mode == RaceBrief::Mode::TestDrive ? 9000 : 0;
			racingEnded(w, i);
			w.racers.handToAi(w, i);
			break;
		}
	}
}

// Time up: the rest are placed in racer order (not by standing), not finished; the results follow.
void Session::placeTheRest(World& w)
{
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		if (!(w.racers.race[i].flags & RacerFinished)) {
			w.racers.race[i].place = finishers++;
			w.racers.setStanding(w, i, finishers);
			finished(w, i);
		}
	}
	phase = Session::Results;
	ms = 0;
}

} // namespace lr
