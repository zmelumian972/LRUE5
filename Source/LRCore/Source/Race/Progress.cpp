// Progression: a session set up from what the menus chose, a circuit's run of races and its
// award, each race's brief and what it hands back, a time race's records and unlocks.

#include "LRCore/Common/Game.h"
#include "LRData/Archive.h"

#include "LRCore/Kart/Car.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/World.h"

#include "LRData/Cache.h"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace lr {

namespace {

constexpr uint16_t MainMenu = 2, Garage = 3, LostAward = 25, VeronicasCar = 28;
constexpr int32_t TestRace = 26, RecordSlots = 13, UnlockingRaces = 12;

} // namespace

void Game::startSession(const MenuData& data)
{
	if (mode == Mode::Circuit || mode == Mode::TimeRace) {
		cheats = 0; // for good
	}
	demo = players == 0 && !backToGarage;
	if (demo) {
		// The attract demo: one of the first circuit's races, six of its drivers, racer 0 on autopilot.
		mode = Mode::Single;
		circuit = 0;
		races = {int16_t(data.circuits.circuits[0].races[draw() % 4]), -1, -1, -1};
		racerCount = 6;
	}
	if (backToGarage) {
		mode = Mode::Single;
		circuit = -1;
		races = {int16_t(TestRace), -1, -1, -1};
		racerCount = 1;
	}
	current = 0;
	points = {};
	times = {};
	firstRace = true;
	restartCircuit = aborted = bestTimesPending = recordBeaten = racing = false;
	events.clear();
}

int32_t Game::nextRace()
{
	if (racing) {
		racing = false;
		current++;
	}
	if (mode != Mode::Circuit || demo || backToGarage) {
		racing = current == 0;
		nextScreen = backToGarage ? Garage : MainMenu;
		return racing ? 0 : -1;
	}
	for (;; current++) {
		if (current >= 4 && !restartCircuit) {
			break;
		}
		if (restartCircuit) {
			restartCircuit = false;
			current = 0;
			points = {};
			firstRace = true;
		}
		else {
			firstRace = current == 0;
		}
		if (races[size_t(current)] < 0) {
			continue;
		}
		if (aborted) {
			nextScreen = MainMenu;
			return -1;
		}
		events.push_back({Event::CircuitRace, {current, points[0], players, (firstRace ? 64 : 0) | 4}, {}});
		// One player must have 10 points for every race run so far.
		if (players == 1 && points[0] < 10 * current) {
			events.push_back({Event::CircuitLost, {current, points[0], 0, 0}, {}});
			nextScreen = LostAward;
			return -1;
		}
		racing = true;
		return current;
	}
	if (aborted) {
		nextScreen = MainMenu;
		return -1;
	}
	// The award by the player's rank: the slots with more points.
	int32_t rank = 0;
	for (int32_t p : points) {
		rank += p > points[0];
	}
	events.push_back({Event::CircuitRank, {0, rank, 0, 0}, {}});
	nextScreen = uint16_t(22 + std::min(rank, 3));
	return -1;
}

RaceBrief Game::brief(const MenuData& data, const Cache& cache) const
{
	RaceBrief b;
	const RaceTable::Race& race = data.races.races[size_t(races[size_t(current)])];
	b.track = upper(race.folder);
	b.mirrored = race.mirrored || ((cheats & CheatMirrorRocket) && circuit == 6);
	b.laps = players == 2 ? settings.laps : 3;
	b.autopilot = demo;
	b.twoPlayers = players == 2;
	b.firstRace = firstRace;
	b.cheats = cheats;
	b.random = random;
	b.views = views;
	b.mode = demo				 ? RaceBrief::Mode::Demo
		: backToGarage			 ? RaceBrief::Mode::TestDrive
		: mode == Mode::Circuit	 ? RaceBrief::Mode::Circuit
		: mode == Mode::TimeRace ? RaceBrief::Mode::TimeRace
								 : RaceBrief::Mode::Single;
	// The circuit's drivers in the slots the players don't take (AI slot i races driver i), the players first.
	const std::vector<std::string> none;
	const std::vector<std::string>& drivers = circuit >= 0 ? data.circuits.circuits[size_t(circuit)].drivers : none;
	const int32_t picked = demo ? 0 : players;
	int32_t count = racerCount;
	if (count > picked) {
		count = std::min(count, int32_t(drivers.size()));
	}
	count = std::max(count, picked);
	const MenuData::Language& text = data.languages[settings.language];
	b.slots.resize(size_t(count));
	for (int32_t i = 0; i < count; i++) {
		RaceBrief::Slot& s = b.slots[size_t(i)];
		s.points = points[size_t(i)];
		if (i >= picked) {
			s.driver = drivers[size_t(i)];
			s.player = demo && i == 0; // the demo's racer 0 is a player's slot the AI drives
			const DriverTable::Driver* d = data.drivers.find(s.driver);
			s.name = d ? std::u16string(text.drivers.at(size_t(d->nameIndex))) : std::u16string();
			continue;
		}
		s.player = true;
		const RacerRecord* r = racer(chosen[size_t(i)]);
		if (!r) {
			continue;
		}
		Car car;
		car.load(data.pieces, r->pieces);
		const Car::Numbers n = car.numbers(data.pieces);
		s.chassis = r->chassis;
		s.pieces = float(n.volume);
		s.centroidX = n.centroidX;
		s.centroidY = n.centroidY;
		s.car = r->pieces;
		s.name = r->name;
		s.face = r->looks[0];
		s.looks = r->looks;
	}
	if (b.mode == RaceBrief::Mode::TimeRace) {
		(void)cache.get("GAMEDATA/" + b.track + "/GHOST.GHB", b.ghost, b.mirrored); // (none: no record ghost)
	}
	return b;
}

void Game::takeResult(const World& w)
{
	views = w.session.brief.views; // (each player's camera view, for the session's next race)
	if (mode == Mode::Circuit) {
		points = w.session.circuitPoints;
	}
	RaceRecord& entry = times[size_t(current)];
	const RaceRecord& r = w.session.record;
	if (r.lapMs) {
		if (entry.lapMs == 0 || r.lapMs <= entry.lapMs) {
			entry.lapMs = r.lapMs, entry.lapBy = r.lapBy;
		}
		if (entry.raceMs == 0 || r.raceMs <= entry.raceMs) {
			entry.raceMs = r.raceMs, entry.raceBy = r.raceBy;
		}
	}
	aborted = w.session.ending == Session::End::Abort;
	restartCircuit = w.session.ending == Session::End::RestartCircuit;
	if (w.session.brief.mode == RaceBrief::Mode::TimeRace) {
		bestTimesPending = true;
		recordBeaten = w.session.timeRace.beaten();
	}
	random = w.random;
}

uint16_t Game::menusStart(bool& save)
{
	bool changed = false;
	// A time race's best times: each race slot's lap and race against the record slot its race folds into.
	if (bestTimesPending) {
		bestTimesPending = false;
		for (size_t i = 0; i < 4; i++) {
			const RaceRecord& t = times[i];
			const int32_t n = races[i];
			const int32_t slot = n < 0 ? -1 : n < RecordSlots ? n : n < 2 * RecordSlots ? n - RecordSlots : -1;
			if (t.lapMs == 0 || slot < 0) {
				continue;
			}
			BestTime& best = settings.best[size_t(slot)];
			const auto holder = [&](int32_t by) {
				const RacerRecord* r = size_t(by) < chosen.size() ? racer(chosen[size_t(by)]) : nullptr;
				return r ? r->name.substr(0, 14) : std::u16string();
			};
			for (const auto& [ms, held, by, race] : {std::tuple{t.lapMs, &best.lapMs, t.lapBy, 0}, std::tuple{t.raceMs, &best.raceMs, t.raceBy, 1}}) {
				const bool taken = *held == 0 || uint32_t(ms) < *held;
				if (taken) {
					*held = uint32_t(ms);
					(race ? best.raceHolder : best.lapHolder) = holder(by);
					dirty = changed = true;
				}
				events.push_back({Event::BestTime, {slot, race, ms, taken}, holder(by)});
			}
		}
	}
	uint16_t next = nextScreen;
	if (recordBeaten) {
		// A race won against the record ghost; the twelfth unlocks Veronica Voltage's car.
		recordBeaten = false;
		const int32_t n = races[0];
		const uint16_t before = settings.races;
		if (n >= 0 && n < UnlockingRaces) {
			settings.races = uint16_t(settings.races | 1u << n);
		}
		const bool won = settings.races != before;
		if (won) {
			dirty = changed = true;
			if (settings.races == 0x0fff) {
				settings.parts |= 0x80;
				next = VeronicasCar;
			}
		}
		events.push_back({Event::UnlockRace, {n >= 0 && n < UnlockingRaces ? 1 << n : 0, won, 0, 0}, {}});
	}
	save = changed && (usable[0] || usable[1]);
	return next;
}

void Game::grantAward(uint16_t screen, const MenuData& data)
{
	partsUnlocked = granted = false;
	const int32_t place = screen - 21; // 1st, 2nd, 3rd
	if (place < 1 || place > 3 || circuit < 0) {
		return;
	}
	const CircuitTable::Circuit& c = data.circuits.circuits[size_t(circuit)];
	bool trophy = false, parts = false, opened = false;
	if (chosen[0].list != RacerList::Defaults) {
		if (const RacerRecord* r = racer(chosen[0])) {
			uint8_t& held = list(chosen[0].list)[chosen[0].index].trophies[size_t(circuit)];
			// (Not a change of the card's racers: it is saved only with something else.)
			trophy = held == 0 || held > place;
			if (trophy) {
				held = uint8_t(place);
			}
			(void)r;
		}
	}
	if (place == 1 && c.partSet >= 0 && !(settings.parts & 1u << c.partSet)) {
		settings.parts = uint8_t(settings.parts | 1u << c.partSet);
		parts = partsUnlocked = true;
	}
	const int32_t next = data.circuits.find(c.next);
	if (place <= 2 && next >= 0 && !(settings.circuits & 1u << next)) {
		settings.circuits = uint8_t(settings.circuits | 1u << next);
		opened = circuitNotice = true;
	}
	dirty = dirty || parts || opened;
	granted = trophy || parts || opened;
	events.push_back({Event::Award, {screen, trophy, parts, opened}, {}});
}

} // namespace lr
