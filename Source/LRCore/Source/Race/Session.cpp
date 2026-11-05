// The race session around the simulation: the dialogs that
// pause it, a circuit's points and the end of FINISHED and RESULTS, the fade to black, a time race's runs and ghosts,
// the tunes, the race's records and how it ended.

#include "LRCore/Race/World.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Common/Tables.h"

#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Menus.h"
#include "LRData/Race.h"

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

bool World::setupSession(const Cache& cache, const RaceBrief& race)
{
	*this = {}; // (the observer stays)
	session.brief = race;
	session.openingMs = 500;
	session.raceLaps = race.laps;
	session.autopilot = race.autopilot;
	random = race.random;
	const size_t n = race.slots.size();
	// Every racer's parts, what the session reads of them before the simulation sets them up (a replayed race has only
	// these); no track yet, so no dynamic worlds' timers (setup's).
	racers.resize(n, 0);
	for (size_t i = 0; i < n; i++) {
		Racer& r = racers.race[i];
		r.player = race.slots[i].player;
		r.ai = !r.player;
		r.standing = int32_t(i) + 1;
		session.circuitPoints[i] = race.slots[i].points;
	}
	NameList music;
	if (!cache.get("GAMEDATA/" + race.track + "/LEGOMSC", music)) {
		return false;
	}
	musicCount = int32_t(music.names.size());
	session.tune = {0, false, true, {}};
	// The intro's flyby: the race file's intro camera animation.
	RaceFile file;
	if (!cache.get("GAMEDATA/" + race.track + "/" + race.track + ".RAB", file)) {
		return false;
	}
	AnimationFile camera;
	const std::string flyby = file.introCamera.empty() ? std::string() : "GAMEDATA/" + race.track + "/" + binaryName(file.introCamera) + ".ADB";
	if (!flyby.empty() && cache.size(flyby) && cache.get(flyby, camera) && !camera.parts.empty()) {
		track.introFrames = camera.parts[0].frames;
		track.introRate = camera.parts[0].rate;
	}
	session.watchingFlyby = track.introFrames > 0;
	if (race.mode == RaceBrief::Mode::TimeRace) {
		prepareRuns();
	}
	return true;
}

// A time race's runs: the record ghost loaded again, the clock and the current run cleared (TRY AGAIN, a new session);
// a restart from the pause dialog only resets.
void World::prepareRuns()
{
	session.timeRace.record = runOf(session.brief.ghost);
	session.timeRace.recordValid = !session.timeRace.record.positions.empty();
	resetRuns();
}

void World::resetRuns()
{
	TimeRace& t = session.timeRace;
	t.clockMs = 0;
	t.running = false;
	t.current = {};
	t.record.finished = t.best.finished = false;
}

void World::restart(const Cache* cache)
{
	takeRecord();
	raise(RaceEvent::StopEffects); // (the show's effects all stop; the rules end their loops)
	// What the session carries over a restart.
	const RaceBrief carried = session.brief;
	const RaceRecord best = session.record;
	const std::array<int32_t, 6> points = session.circuitPoints;
	const TimeRace runs = session.timeRace;
	const uint32_t index = random;
	const int32_t blink = session.dialog.blinkMs;
	const std::vector<Hud> shown = raceView.huds; // (each keeps its smoothed speed and its visibility)
	bool ready = true;
	if (cache) {
		ready = setup(*cache, carried);
	}
	else {
		// (A replayed race: the session alone, from the files it read.)
		const int32_t count = musicCount;
		// Its phase, clocks and pace start again.
		const Session fresh;
		session.phase = fresh.phase, session.ms = fresh.ms, session.flyby = fresh.flyby, session.finishers = fresh.finishers;
		session.paceDelay = fresh.paceDelay, session.paceBonus = fresh.paceBonus;
		session.watchingFlyby = track.introFrames > 0;
		for (size_t i = 0; i < racers.race.size(); i++) {
			racers.race[i] = {};
			racers.race[i].player = carried.slots[i].player;
			racers.race[i].ai = !racers.race[i].player;
			racers.race[i].standing = int32_t(i) + 1;
		}
		if (session.autopilot) {
			handToAi(0);
		}
		musicCount = count;
		session.tune = {0, false, true, {}};
		session.dialog = {};
		session.fade = {};
		session.roundPoints = {};
		session.ending = Session::End::None;
		timeRaceAsked = false;
	}
	raceView.huds.resize(shown.size());
	for (size_t p = 0; p < raceView.huds.size(); p++) {
		raceView.huds[p] = {};
		raceView.huds[p].speed = shown[p].speed;
		raceView.huds[p].visible = shown[p].visible;
	}
	random = index;
	session.record = best;
	session.circuitPoints = points;
	session.timeRace = runs;
	session.dialog.blinkMs = blink;
	session.restarted = true;
	if (!ready) {
		session.ending = Session::End::Abort; // (the race's files no longer load: the session ends, back to the menus)
	}
}

void World::takeRecord()
{
	RaceRecord now;
	int32_t bestLap = 0, bestLapBy = 0, bestRace = 0;
	for (size_t i = 0; i < racers.race.size(); i++) {
		const Racer& r = racers.race[i];
		if (!r.player || r.laps < session.raceLaps) {
			continue;
		}
		int32_t sum = 0;
		for (int32_t lap = 0; lap < session.raceLaps; lap++) {
			sum += r.lapMs[lap];
			if (bestLap == 0 || r.lapMs[lap] < bestLap) {
				bestLap = r.lapMs[lap];
				bestLapBy = int32_t(i);
			}
		}
		bestRace = bestRace == 0 || sum < bestRace ? sum : bestRace;
	}
	now = {bestLap, bestLapBy, bestRace, bestLapBy};
	keepBest(session.record, now);
}

void World::finished(size_t i)
{
	// A circuit's points by place; a player's finish ends the racing (its tune: the win or the loss), and a time
	// race's run.
	if (session.brief.mode == RaceBrief::Mode::Circuit) {
		session.roundPoints[i] = executableTables().placePoints[size_t(std::min(racers.race[i].place, 5))];
		session.circuitPoints[i] += session.roundPoints[i];
	}
}

void World::racingEnded(size_t i)
{
	// FINISH on the HUD of the player who ended the racing (the demo's: HUD 0), with its place.
	for (size_t p = 0; p < raceView.huds.size() && p < raceView.cameras.size(); p++) {
		if (size_t(raceView.cameras[p].racer) == i) {
			raceView.huds[p].banner = -1;
			raceView.huds[p].finalPlace = racers.race[i].standing - 1;
		}
	}
	if (session.brief.mode == RaceBrief::Mode::TimeRace) {
		TimeRace& t = session.timeRace;
		for (size_t lap = 0; lap < 3; lap++) {
			t.current.laps[lap] = racers.race[i].lapMs[lap];
		}
		if (t.current.total() < t.best.total() || t.best.total() == 0) {
			std::swap(t.current, t.best);
		}
		t.bestValid = t.running = false;
		t.bestKept = true;
	}
	session.tune = {racers.race[i].standing == 1 ? 3 : 2, false, true, {}};
}

void World::racingStarted()
{
	session.roundPoints = {};
	if (session.brief.mode == RaceBrief::Mode::TimeRace) {
		session.timeRace.running = true;
		session.timeRace.bestValid = session.timeRace.bestValid || session.timeRace.bestKept;
	}
	// The racing tune: the track's own in a circuit or with no extras, else the presentation draws among them.
	session.tune = {1, true, true, {}};
	if (session.brief.mode != RaceBrief::Mode::Circuit && musicCount > 4) {
		static constexpr int32_t Extras[4] = {1, 4, 5, 6};
		session.tune.choices.assign(Extras, Extras + std::min(4, musicCount - 3));
	}
}

void World::sessionEnded()
{
	session.phase = Session::Over;
	if (session.ending == Session::End::None) {
		session.ending = Session::End::Over;
	}
	takeRecord();
}

void World::startFade()
{
	if (!session.fade.active) {
		session.fade = {1000, 1000, true, false};
	}
}

void World::recordRun(int32_t dt)
{
	TimeRace& t = session.timeRace;
	t.sampled = false;
	if (session.brief.mode != RaceBrief::Mode::TimeRace || !t.running) {
		return;
	}
	// A ghost that has passed its last sample is finished (hidden from then on).
	const size_t k = size_t(t.clockMs / TimeRace::SampleMs);
	for (GhostRun* run : {&t.record, &t.best}) {
		if ((run == &t.record ? t.recordValid : t.bestValid) && k + 1 >= run->positions.size()) {
			run->finished = true;
		}
	}
	t.clockMs += dt;
	if (dt >= t.countdownMs) {
		t.countdownMs += TimeRace::SampleMs - dt;
		GhostRun& run = t.current;
		if (int32_t(run.positions.size()) < TimeRace::MostSamples) {
			const Vec3 p = racers.visible[0].position;
			const Quat q = rotationOf(racers.visible[0].rotation);
			if (run.positions.empty()) {
				run.position = p;
				run.rotation = q;
			}
			run.positions.push_back({int16_t(p.x * 32), int16_t(p.y * 32), int16_t(p.z * 32)});
			run.rotations.push_back({int8_t(q.x * 127), int8_t(q.y * 127), int8_t(q.z * 127), int8_t(q.w * 127)});
			t.sampled = true;
		}
	}
	else {
		t.countdownMs -= dt;
	}
}

bool World::ghostAt(const GhostRun& run, Vec3& position, Quat& rotation) const
{
	const TimeRace& t = session.timeRace;
	if (run.finished || run.positions.empty() || t.clockMs >= TimeRace::ShownMs) {
		return false;
	}
	if (!t.running) {
		position = run.position;
		rotation = run.rotation;
		return true;
	}
	const size_t k = size_t(t.clockMs / TimeRace::SampleMs);
	const float a = float(t.clockMs - TimeRace::SampleMs * int32_t(k)) * 0.004f;
	if (k + 1 >= run.positions.size()) {
		return false;
	}
	const auto at = [&](size_t i) { return Vec3{run.positions[i][0] / 32.0f, run.positions[i][1] / 32.0f, run.positions[i][2] / 32.0f}; };
	const auto turn = [&](size_t i) { return Quat{run.rotations[i][0] / 127.0f, run.rotations[i][1] / 127.0f, run.rotations[i][2] / 127.0f, run.rotations[i][3] / 127.0f}; };
	const Vec3 p0 = at(k), p1 = at(k + 1);
	position = p0 + (p1 - p0) * a;
	// The rotation blended, to unit length.
	const Quat q = blend(turn(k), turn(k + 1), a);
	const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	rotation = n > 0 ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : q;
	return true;
}

int32_t World::standingsTable() const
{
	if (session.brief.mode != RaceBrief::Mode::Circuit || paused() || session.ending != Session::End::None) {
		return 0;
	}
	return session.phase == Session::Finished && session.ms > 5000 ? 1 : session.phase == Session::Results ? 2 : 0;
}

// The dialogs.

void World::openDialog(RaceDialog::Kind kind)
{
	// (The blink runs on from dialog to dialog.)
	RaceDialog& d = session.dialog;
	const int32_t blink = d.blinkMs;
	d = {};
	d.blinkMs = blink;
	d.kind = kind;
	for (Hud& h : raceView.huds) {
		h.visible = false; // (only the banner, the time behind's '-' and WRONG WAY hide)
	}
	switch (kind) {
	case RaceDialog::Pause:
		d.prompt = LinePaused;
		d.options = {LineContinue, session.brief.mode == RaceBrief::Mode::Circuit ? LineRestartCircuit : LineRestartRace, LineExit};
		d.count = 3;
		break;
	case RaceDialog::Confirm:
		d.prompt = LineSure;
		d.options = {LineYes, LineNo, 0};
		d.count = 2;
		d.selection = 1;
		break;
	case RaceDialog::TimeRaceEnd: {
		const bool beaten = session.timeRace.beaten();
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
bool World::choose(const Cache* cache)
{
	RaceDialog& d = session.dialog;
	const int32_t s = d.selection;
	for (Hud& h : raceView.huds) {
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
			openDialog(RaceDialog::Confirm);
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
			session.ending = Session::End::Abort;
			sessionEnded();
			return false;
		}
		d.kind = RaceDialog::None;
		if (session.brief.mode == RaceBrief::Mode::Circuit && !session.brief.firstRace) {
			session.ending = Session::End::RestartCircuit;
			sessionEnded();
			return false;
		}
		if (session.brief.mode == RaceBrief::Mode::Circuit) {
			for (RaceBrief::Slot& slot : session.brief.slots) {
				slot.points = 0;
			}
			session.circuitPoints = {};
			session.roundPoints = {};
		}
		if (session.brief.mode == RaceBrief::Mode::TimeRace) {
			resetRuns();
		}
		restart(cache);
		return true;
	case RaceDialog::TimeRaceEnd:
		d.kind = RaceDialog::None;
		if (s == 1) {
			sessionEnded();
			return false;
		}
		prepareRuns();
		restart(cache);
		return true;
	case RaceDialog::None: break;
	}
	return true;
}

void World::update(int32_t frameMs, std::span<const uint32_t> pressed, std::span<const uint32_t> released, const Cache* cache)
{
	session.tune.started = false;
	session.restarted = false;
	raceView.raceEvents.clear();
	if (session.ending != Session::End::None) {
		return;
	}
	// The demo: any key (but a pad's axis) ends it at once.
	if (session.brief.mode == RaceBrief::Mode::Demo) {
		for (uint32_t k : pressed) {
			if ((k >> 28) <= 3) {
				session.ending = Session::End::Abort;
				sessionEnded();
				return;
			}
		}
	}
	// Esc pauses. The race runs unless a dialog is up; then the dialog: its blink (it runs on from dialog to dialog),
	// Up and Down move, Enter or Space chooses and the key's release acts. A dialog closing lets the race run this
	// frame, its clock held.
	if (!paused()) {
		for (uint32_t k : pressed) {
			if (k == MenuEscape && session.brief.mode != RaceBrief::Mode::Demo) {
				openDialog(RaceDialog::Pause);
				break;
			}
		}
	}
	bool ran = false;
	const auto run = [&](bool held) {
		clockHeld = held;
		const int32_t raceMs = session.brief.cheats & CheatFastForward ? int32_t(float(frameMs) * 1.75f) : frameMs;
		frame(raceMs, frameMs);
		clockHeld = false;
		updateFade(raceMs);
		if (standingsTable()) {
			session.blinkMs = frameMs > session.blinkMs ? 400 : session.blinkMs - frameMs;
		}
		ran = true;
	};
	if (!paused()) {
		run(false);
	}
	if (!paused()) {
		return;
	}
	RaceDialog& d = session.dialog;
	for (uint32_t k : pressed) {
		if (d.chosen) {
			continue;
		}
		if (k == MenuUp || k == MenuDown) {
			d.selection = (d.selection + (k == MenuUp ? d.count - 1 : 1)) % d.count;
		}
		else if (k == MenuEnter || k == MenuSpace || k == MenuPadButton) {
			d.chosen = true;
			d.key = k;
		}
	}
	bool resumed = false;
	for (uint32_t k : released) {
		if (paused() && d.chosen && k == d.key) {
			resumed = choose(cache);
		}
	}
	if (session.ending != Session::End::None) {
		updateFade(frameMs);
		return; // (the blink stops with the race)
	}
	d.blinkMs += frameMs;
	d.blinkMs = d.blinkMs >= 500 ? 0 : d.blinkMs;
	if (resumed && !ran) {
		run(true);
	}
}

void World::updateFade(int32_t frameMs)
{
	if (!session.fade.active || paused()) {
		return;
	}
	if (session.fade.left == 0) {
		if (session.fade.ended) {
			session.fade.active = false;
			return;
		}
		session.fade.ended = true;
	}
	session.fade.left = std::max(0, session.fade.left - frameMs);
}

} // namespace lr
