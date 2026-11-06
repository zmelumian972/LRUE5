#pragma once

// The race session around the simulation (Session.cpp): the brief it was set up from, its phases and clocks, the
// dialogs that pause it, a circuit's points, the fade to black, a time race's runs, the tunes, how the race ended and
// its records. Its operations take the world they run in.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Game.h"
#include "LRData/Race.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace lr {

class Cache;
struct World;

// The HUD gadget's modes, in the order the map key steps them.
enum Gadget : uint8_t { GadgetNone, GadgetFixedMap, GadgetSpeedometer, GadgetRotatingMap };

// What the menus hand a race.
struct RaceBrief {
	enum class Mode : uint8_t { Single, Circuit, TimeRace, TestDrive, Demo };
	Mode mode = Mode::Single;
	std::string track; // the folder: "RACEC0R1"
	bool mirrored = false;
	int32_t laps = 3;
	bool autopilot = false; // racer 0 is handed to the AI before the race (the attract demo)
	bool twoPlayers = false;
	bool firstRace = true; // a circuit's: its pause dialog restarts the race, not the circuit
	uint32_t cheats = 0;
	struct Slot {
		bool player = false;
		std::string driver; // a champion's car (and its power-up habits), else a built car:
		std::string chassis;
		float pieces = 0; // the built car's high piece count
		float centroidX = 0, centroidY = 0; // of its pieces
		std::u16string name; // as the standings show it
		int32_t points = 0; // a circuit's so far
		std::vector<CarPiece> car; // the built car's pieces (its body; none: the chassis' sphere stands in)
		uint8_t face = 0; // the built driver's face: its voice
		// A player's slot (no driver): the picked record's looks, face, hat, legs, torso (its own list or DEFAULT.LRS's);
		// the race draws its driver assembled from them. AI slots: unused.
		std::array<uint8_t, 4> looks = {};
		bool built() const { return player && driver.empty(); }
	};
	List<Slot> slots; // up to 6
	uint32_t random = 0; // the random index before the race's draws
	GhostFile ghost; // a time race's record run (no samples: none)
	std::array<uint8_t, 2> views = {}; // each player's camera view, as last chosen
	std::array<uint8_t, 2> maps = {GadgetFixedMap, GadgetFixedMap}; // each player's HUD gadget
};

// A dialog over the paused race: GAME.SRF's lines; the option chosen holds until its key is
// released, which closes it.
struct RaceDialog {
	enum Kind : uint8_t { None, Pause, Confirm, TimeRaceEnd };
	enum Action : uint8_t { Continue, Restart, Exit };
	Kind kind = None;
	Action confirming = Continue;
	int32_t prompt = 0, second = -1; // the lines' strings (-1 none)
	Array<int32_t, 3> options = {};
	int32_t count = 0, selection = 0;
	int32_t blinkMs = 0;
	bool chosen = false;
	uint32_t key = 0; // the key that chose
};

// A time race's runs: the record ghost's, the session's best and the one being recorded, each three
// lap times and a sample every 250 ms (position x32, rotation x127), its first pose as it was.
struct GhostRun {
	std::array<int32_t, 3> laps = {};
	Vec3 position = {};
	Quat rotation = {0, 0, 0, 1};
	std::vector<std::array<int16_t, 3>> positions;
	std::vector<std::array<int8_t, 4>> rotations;
	bool finished = false; // its ghost passed its last sample
	int32_t total() const { return laps[0] + laps[1] + laps[2]; }
};
struct TimeRace {
	static constexpr int32_t SampleMs = 250, MostSamples = 961, ShownMs = 240000;
	GhostRun record, best, current;
	bool running = false, recordValid = false, bestValid = false, bestKept = false;
	int32_t clockMs = 0, countdownMs = 0; // the countdown is never reset (it carries into the next run)
	bool sampled = false; // this step's
	bool beaten() const { return recordValid && (bestValid || bestKept) && best.total() < record.total(); }
};

// A tune the race wants played (the track's LEGOMSC): the intro, the racing tune (the presentation draws it from
// `choices` when there are several), the win or the loss.
struct RaceTune {
	int32_t tune = -1;
	bool loops = false, started = false;
	List<int32_t> choices;
};

// The session: its phase and clocks, the standings' pace, the brief it was set up from, the dialog, a circuit's points
// (each racer's this race and so far), the standings' blink and the fade to black, a time race's runs, the tunes, how
// the race ended and its records.
struct LRCORE_API Session {
	enum Phase : uint8_t { Intro = 1, Countdown, Racing, Finished, Results, Over };
	Phase phase = Intro;
	int32_t ms = 0; // since the phase began
	float flyby = 0; // the intro camera's animation, in frames
	bool watchingFlyby = false;
	int32_t finishers = 0;
	int32_t paceDelay = 15000; // the rubber band waits this long
	float paceBonus = 0; // added to every pace on a mirrored track
	int32_t raceLaps = 3;
	bool autopilot = false;
	enum class End : uint8_t { None, Over, Abort, RestartCircuit };
	RaceBrief brief;
	RaceDialog dialog;
	Array<int32_t, 6> roundPoints = {}, circuitPoints = {};
	int32_t blinkMs = 0;
	struct Fade {
		int32_t ms = 0, left = 0;
		bool active = false, ended = false;
	} fade;
	// The race's opening fade from black: 500 ms from the race's start, its quad's alpha
	// 255 · left / 500, drawn over the HUDs while any is left.
	int32_t openingMs = 0;
	TimeRace timeRace;
	RaceTune tune;
	End ending = End::None;
	bool restarted = false; // this frame (its records were taken first)
	RaceRecord record; // the best of this race and its restarts'

	// The world set up afresh with the session's part of a race (the brief, the racers' kinds, a time race's record
	// ghost, the tunes), what a replayed race needs.
	[[nodiscard]] bool setup(World& w, const Cache& cache, const RaceBrief& race);
	// One frame of the program in the race: the dialog's keys (menu keys: Menus.h), then, unless paused, the world's
	// frame; the fade and the standings' blink. In the demo any key ends the race. A restart sets the race up again
	// from the cache (none: a replayed race, its session alone).
	void update(World& w, int32_t frameMs, std::span<const uint32_t> pressed, std::span<const uint32_t> released, const Cache* cache);
	// The frame's first part: the clock (sessionMs of it), then the phase, and the flyby (frameMs).
	void advance(World& w, int32_t frameMs, int32_t sessionMs);
	// A time race's current run: its clock and samples (a step's last part).
	void recordRun(World& w, int32_t dt);
	// Where a time race's ghost is shown at the run's clock: its first pose before the start, then between its
	// samples; false once it has passed its last (it is finished) or the clock passed what a ghost shows.
	bool ghostAt(const GhostRun& run, Vec3& position, Quat& rotation) const;
	bool paused() const { return dialog.kind != RaceDialog::None; }
	// The standings table showing (0 none, 1 the race's, 2 the circuit's).
	int32_t standingsTable() const;
	// A racer placed; the fade; a dialog opened.
	void finished(World& w, size_t racer);
	void startFade();
	void openDialog(World& w, RaceDialog::Kind kind);

private:
	// Its time race's end dialog opened, the track's tunes, the clock held this frame (a dialog just closed).
	bool m_timeRaceAsked = false;
	int32_t m_musicCount = 0;
	bool m_clockHeld = false;
	// The phases and their moments: the start, the racing's end, the session's end; the race's records taken, a
	// time race's runs; the fade; a dialog's choice.
	void updatePhase(World& w);
	void startCountdown(World& w);
	void startRacing(World& w);
	void crossLine(World& w);
	void placeTheRest(World& w);
	void racingStarted();
	void racingEnded(World& w, size_t racer);
	void ended(World& w);
	void takeRecord(World& w);
	void prepareRuns();
	void resetRuns();
	void updateFade(int32_t frameMs);
	bool choose(World& w, const Cache* cache);
	// Back to the intro (the pause dialog's restart, a time race's TRY AGAIN), its records taken first: set up again
	// from the brief, keeping the session's points, records and time race runs.
	void restart(World& w, const Cache* cache);
};

} // namespace lr
