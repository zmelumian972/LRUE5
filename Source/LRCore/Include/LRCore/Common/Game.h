#pragma once

// What the game keeps between screens and races: the settings, unlocks, best times and controls (saved, in the active
// save slot's file), the player's racers (two save slots of up to 100, and up to 5 that live for the session), and the
// session's own state (cheats, the flags the screens hand each other, the one random index the whole program draws
// from). A plain value like World: menus and races change it, a save writes it.
//
// Saves are ours: one file per slot, written by the presentation from the bytes Game encodes. A slot is written when
// its racers changed since its last write, or (the active slot) when the settings changed.

#include "LRCore/Common/Api.h"
#include "LRData/Index.h"
#include "LRData/Saves.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lr {

struct CarPiece {
	uint16_t type = 0;
	uint8_t x = 0, y = 0, rotation = 0, colour = 0;
	uint16_t part = 0;
	bool operator==(const CarPiece&) const = default;
};

// A built racer: the license's name, the minifig's looks, the car.
struct RacerRecord {
	std::u16string name; // up to 14
	std::array<uint8_t, 4> looks = {}; // face, hat, legs, torso
	uint8_t expression = 0; // 0..127
	bool stockCar = false; // a set's bare chassis or a quick build, not changed in the builder since
	std::string chassis; // up to 8: "ddchas0"
	std::vector<CarPiece> pieces; // up to 64
	std::array<uint8_t, 7> trophies = {}; // per circuit: 0 none, 1 gold, 2 silver, 3 bronze
	bool operator==(const RacerRecord&) const = default;
};

// Where a racer is.
enum class RacerList : uint8_t { None, Card0, Card1, Defaults, Session };
struct RacerRef {
	RacerList list = RacerList::None;
	uint16_t index = 0;
	bool operator==(const RacerRef&) const = default;
};

// A player's controls: a device and nine bindings, each (source << 28 | code): source 1 keyboard (a DirectInput scan
// code), 2 mouse, 3 joystick button, 4 joystick axis-as-button. Layouts 0 and 1 are joysticks 0 and 1, 2..4 keyboards.
struct KeyLayout {
	static constexpr uint8_t Keyboard = 3, Joystick = 4; // the kinds
	uint8_t kind = 0, subKind = 0, device = 0;
	std::array<uint32_t, 9> bindings = {};
	bool operator==(const KeyLayout&) const = default;
};

struct BestTime {
	uint32_t lapMs = 0, raceMs = 0;
	std::u16string lapHolder, raceHolder;
	bool operator==(const BestTime&) const = default;
};

// A race's best-times slot (Settings::best): a mirrored race folds into its plain one; -1 none.
constexpr int32_t RecordSlots = 13;
constexpr int32_t recordSlot(int32_t race) { return race >= 0 && race < 2 * RecordSlots ? race % RecordSlots : -1; }

// The saved state (in the active slot).
struct Settings {
	uint8_t opponents = 5, laps = 3, music = 255, sound = 255, stereo = 1, language = 0;
	std::array<uint8_t, 2> kept = {0, 128}; // meaning unknown: carried through
	std::array<uint8_t, 16> device = {}; // the display device (presentation's): carried through
	uint8_t parts = 0, circuits = 0; // unlock bits
	uint16_t races = 0; // time races won
	Array<BestTime, RecordSlots> best; // by record slot
	std::array<RacerRef, 2> picks = {RacerRef{RacerList::Card0, 0}, RacerRef{RacerList::Card0, 0}}; // each player's last
	std::array<uint8_t, 2> layout = {0, 1}; // each player's KeyLayout
	std::array<KeyLayout, 5> layouts;
	bool operator==(const Settings&) const = default;
};

// The cheat codes' bits (a license named so toggles one).
enum Cheat : uint32_t {
	CheatNoSurfaces = 1u << 0,
	CheatFlySkyHigh = 1u << 1,
	CheatRedBricks = 1u << 2,
	CheatYellowBricks = 1u << 3,
	CheatGreenBricks = 1u << 4,
	CheatMirrorRocket = 1u << 5,
	CheatRedOnly = 1u << 6,
	CheatMaxPower = 1u << 7,
	CheatFastForward = 1u << 8,
	CheatNoWheels = 1u << 9,
	CheatNoBody = 1u << 10,
	CheatNoDriver = 1u << 11,
	CheatNone = 1u << 12,
};

class Cache;
struct MenuData;
struct RaceBrief;
struct World;

// The best lap and the best race of the players who completed a race, with their racer slots (both held by the lap's
// racer).
struct RaceRecord {
	int32_t lapMs = 0, lapBy = 0, raceMs = 0, raceBy = 0;
	bool operator==(const RaceRecord&) const = default;
};

struct LRCORE_API Game {
	static constexpr size_t CardCapacity = 100;

	Settings settings;
	bool dirty = false; // the settings changed since the active slot was last written
	std::array<std::vector<RacerRecord>, 2> cards;
	std::vector<RacerRecord> session;
	// The save slots: whether each has a file we can use, whether its racers changed since it was last written, and
	// which one holds the settings (-1 none yet).
	std::array<bool, 2> usable = {};
	std::array<bool, 2> racersChanged = {};
	int8_t activeSlot = -1;
	uint8_t pads = 0; // joysticks present (the controls' defaults depend on them)

	// The session.
	uint32_t cheats = 0;
	uint32_t random = 0; // the gameplay random index: the menus' draws and every race's
	uint8_t currentCard = 0; // the card new racers go to
	RacerRecord working; // the racer the builders edit (a copy)
	Array<RacerRef, 2> chosen; // the racers chosen for the next race (and for the builders: player 1's)
	uint8_t players = 0; // how many are chosen
	std::array<uint8_t, 2> views = {}; // each player's camera view as last chosen in a race (not saved)
	bool newRacer = false, copying = false, versus = false, backToGarage = false, circuitNotice = false;
	std::vector<RacerRecord> defaults; // MENUDATA/DEFAULT.LRS's racers (read-only, kept by reset)

	// The race the menus set up: the mode, the circuit and its race slots (a circuit's
	// four by place; else slot 0), the racer count, which slot runs; a circuit's points so far; each slot's records;
	// what the race and the menus hand each other.
	enum class Mode : uint8_t { Circuit, Single, TimeRace };
	Mode mode = Mode::Single;
	int32_t circuit = 0;
	Array<int16_t, 4> races = {-1, -1, -1, -1}; // the race table's
	int32_t racerCount = 5; // the racers to race (the setup screens' and racer select's)
	int32_t current = 0;
	Array<int32_t, 6> points = {};
	Array<RaceRecord, 4> times;
	bool firstRace = true, restartCircuit = false, aborted = false, bestTimesPending = false, recordBeaten = false;
	bool demo = false, racing = false; // racing: the slot `current` is being raced
	uint16_t nextScreen = 2;
	bool partsUnlocked = false, granted = false; // by the last award: a part set; anything (it is saved)
	bool saveReminder = false;
	// What a session did, for the presentation and the tests: each event as the game made it.
	struct Event {
		enum Kind : uint8_t { CircuitRace, CircuitLost, CircuitRank, Award, UnlockRace, BestTime };
		Kind kind;
		std::array<int32_t, 4> values;
		std::u16string text;
	};
	std::vector<Event> events;

	// A fresh game: defaults, no racers, the controls for the devices present.
	void reset(uint8_t padCount);
	uint16_t draw();

	std::vector<RacerRecord>& list(RacerList which);
	const RacerRecord* racer(RacerRef ref) const;
	void markRacers(RacerList which); // a card's racers changed

	// The controls (a binding is source << 28 | code).
	void bind(size_t layout, size_t slot, uint32_t value);
	void pickLayout(size_t player, size_t layout);
	size_t spareKeyboard(size_t player) const;
	void fixAbsentLayouts(); // a player on an absent joystick's layout gets another

	// A license name toggles a cheat if it is one (the first 13 characters, low bytes).
	void applyCheat(std::u16string_view name);

	// The menus ended (an empty stack): the session starts: the racer count, the cheats a
	// circuit or time race clears, the attract demo when no racer was chosen (one draw: its race), the records cleared.
	void startSession(const MenuData& data);
	// The race slot to run next (a circuit runs its slots in turn), or -1 when the session is over (the next
	// screen is set: a circuit's award, or the main menu after an exit).
	int32_t nextRace();
	// The brief for the current race slot: its track, the racers (drivers, built cars and their numbers, names), the
	// laps, the cheats, a time race's record ghost (read from the cache).
	RaceBrief brief(const MenuData& data, const Cache& cache) const;
	// What a race hands back: the places, a circuit's points, the records, how it ended, the random index.
	void takeResult(const World& world);
	// The menus start again after a session: a time race's best times committed and a record beaten taken;
	// the screen to open and whether the save screen goes over it.
	uint16_t menusStart(bool& save);
	// An award screen opened: the trophy, a circuit's part set (first place), the next circuit (first and second).
	void grantAward(uint16_t screen, const MenuData& data);

	// Our save file for a slot: the slot's racers, and the settings if it is the active slot.
	std::vector<uint8_t> encodeSlot(size_t slot) const;
	// A slot's file read at the start: its racers, and its settings if it has them and no slot is active yet (it
	// becomes active). False if the file is malformed: the slot stays unusable.
	[[nodiscard]] bool loadSlot(size_t slot, std::span<const uint8_t> bytes);
	// Loading every slot after the legal screen: a missing slot is made (its file returned to write), and with no slot
	// active slot 0 becomes active (on a fresh install).
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> openSlots();
	// Saving a usable slot: the file if its racers changed or (the active slot) the settings did; else empty.
	std::vector<uint8_t> saveSlot(size_t slot);

	// The original's save files (Save/<slot>/LEGORac1; empty or undecodable: none) as our two slot files: each slot's
	// racers, the first slot that decodes holding the settings (it becomes active).
	static std::array<std::vector<uint8_t>, 2> fromOriginal(const std::array<std::vector<uint8_t>, 2>& files);
	static RacerRecord racerFrom(const OriginalSave::Record& r);
};

// The volume a slider step (0..20) stores, and back.
LRCORE_API uint8_t volumeByte(int32_t step);
LRCORE_API int32_t volumeStep(uint8_t byte);

} // namespace lr
