#pragma once

// The simulation's state, owned in one place: the track, the routes, the session and every racer, each racer's parts
// in parallel arrays (index = racer), so a frame walks flat arrays and the whole world copies and compares as a value.
//
// A race: the session runs its phases (intro, countdown, racing, finished, results); each frame the scenery's
// animations, the power-ups (bricks, actions, explosions), the event table, the hazards and the track timers, the
// players' controls, then steps of at most 40 ms: drivers, bodies (physics or route playback), the step queue (timed
// waits, kart-against-kart contact, racers against the trigger list: touch spheres, bricks, traps, explosions,
// projectiles, hazards), the racers' timers (the AI's use of its brick), the standings and the rubber band. A racer's
// laps come from the lap marks (zones it passes through: the open track, the line, just past it), its standing from
// the checkpoint gates it took.
//
// Random numbers: one index into a fixed table, for the race's own choices only (routes, taunts, the AI's power-ups,
// dynamite, timers, hazards); a second index for the drivers' looks (their part feeds the group sphere), starting at
// the race's start index. Sounds and particles draw from the presentation's.
//
// What the race shows is state too (View.h): after each step the players' cameras and each racer's look
// (the body's lean, the chassis' and driver's parts, the group sphere the touch spheres test), after the frame which
// animated scenery each camera draws (a hazard reading a bone sees its last drawn pose).

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Powers/Hazards.h"
#include "LRCore/Kart/Kart.h"
#include "LRCore/Powers/Powers.h"
#include "LRCore/Race/Route.h"
#include "LRCore/Show/Show.h"
#include "LRCore/Race/Track.h"
#include "LRCore/Show/View.h"
#include "LRData/Race.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace lr {

// The game's "random" numbers: a fixed table of 1024, read at one index that runs for the whole program.
LRCORE_API uint16_t randomTable(uint32_t index);

// What the menus hand a race.
struct RaceBrief {
	enum class Mode : uint8_t { Single, Circuit, TimeRace, TestDrive, Demo };
	Mode mode = Mode::Single;
	std::string track;  // the folder: "RACEC0R1"
	bool mirrored = false;
	int32_t laps = 3;
	bool autopilot = false; // racer 0 is handed to the AI before the race (the attract demo)
	bool twoPlayers = false;
	bool firstRace = true;  // a circuit's: its pause dialog restarts the race, not the circuit
	uint32_t cheats = 0;
	struct Slot {
		bool player = false;
		std::string driver;  // a champion's car (and its power-up habits), else a built car:
		std::string chassis;
		float pieces = 0;     // the built car's high piece count
		float centroidX = 0, centroidY = 0; // of its pieces
		std::u16string name;  // as the standings show it
		int32_t points = 0;   // a circuit's so far
		std::vector<CarPiece> car; // the built car's pieces (its body; none: the chassis' sphere stands in)
		uint8_t face = 0;     // the built driver's face: its voice
		// A player's slot (no driver): the picked record's looks, face, hat, legs, torso (its own list or DEFAULT.LRS's);
		// the race draws its driver assembled from them. AI slots: unused.
		std::array<uint8_t, 4> looks = {};
		bool built() const { return player && driver.empty(); }
	};
	std::vector<Slot> slots; // up to 6
	uint32_t random = 0;     // the random index before the race's draws
	GhostFile ghost;         // a time race's record run (no samples: none)
	std::array<uint8_t, 2> views = {}; // each player's camera view, as last chosen
	std::array<uint8_t, 2> maps = {1, 1}; // each player's HUD gadget
};

enum RacerFlag : uint32_t {
	RacerShielded = 1u << 0,
	RacerOnGrid = 1u << 1,
	RacerHalted = 1u << 3,   // held hovering (a level-2 turbo, a magnet, the grabber)
	RacerGhost = 1u << 4,    // warping: passes walls and racers, takes nothing
	RacerTurbo = 1u << 6,
	RacerShoved = 1u << 8,
	RacerEngine = 1u << 10, // holds a physics kart on the grid until the start
	RacerCursed = 1u << 11,
	RacerFinished = 1u << 12,
	RacerWrongWayPending = 1u << 13,
	RacerWrongWayShown = 1u << 14,
	RacerCrossedBackward = 1u << 19, // went back over the line's gate
	RacerMagnetHeld = 1u << 20, // the rubber band leaves it alone
	RacerWarping = 1u << 21,
	RacerBoxRamp = 1u << 28,  // in contact this step
	RacerGrabbed = 1u << 29,  // the rubber band leaves it alone
};

// A racer's race: what it took, its laps and times, and its timers.
struct Racer {
	uint32_t flags = RacerOnGrid; // RacerFlag
	bool player = false;  // the slot's kind
	bool ai = false;      // driven by the AI (an AI's slot, a finished player, racer 0 on autopilot)
	int16_t checkpoint = -1; // the last gate taken
	int32_t checkpointCount = -1; // forward passes of the line's gate
	bool forward = true;  // the last gate's direction
	LapMark::Zone zones[3] = {LapMark::Open, LapMark::Past, LapMark::Line}; // now, before, before that
	int32_t laps = 0, lineCrossings = -1;
	int32_t lapMs[5] = {};
	int32_t standing = 0;   // 1: leading
	int32_t place = -1;     // the finishing order, from 0
	int32_t timeBehind = 0, timeBehindMs = 0; // behind the racer ahead at the last lap, and how long it shows
	int32_t wrongWayMs = 0;
	int32_t tauntMs = 0; // an AI taunts (an event) a racer close in front when this runs out
	int32_t boxMs = 0;
	float boxScale = 1;     // the contact box's: shrinks two AI karts in contact apart
};

struct Session {
	enum Phase : uint8_t { Intro = 1, Countdown, Racing, Finished, Results, Over };
	Phase phase = Intro;
	int32_t ms = 0;        // since the phase began
	float flyby = 0;       // the intro camera's animation, in frames
	bool watchingFlyby = false;
	int32_t finishers = 0;
	int32_t paceDelay = 15000; // the rubber band waits this long
	float paceBonus = 0;   // added to every pace on a mirrored track
};

// A dialog over the paused race: GAME.SRF's lines; the option chosen holds until its key is
// released, which closes it.
struct RaceDialog {
	enum Kind : uint8_t { None, Pause, Confirm, TimeRaceEnd };
	enum Action : uint8_t { Continue, Restart, Exit };
	Kind kind = None;
	Action confirming = Continue;
	int32_t prompt = 0, second = -1; // the lines' strings (-1 none)
	std::array<int32_t, 3> options = {};
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
	bool sampled = false;                 // this step's
	bool beaten() const { return recordValid && (bestValid || bestKept) && best.total() < record.total(); }
};

// A tune the race wants played (the track's LEGOMSC): the intro, the racing tune (the presentation draws it from
// `choices` when there are several), the win or the loss.
struct RaceTune {
	int32_t tune = -1;
	bool loops = false, started = false;
	std::vector<int32_t> choices;
};

// Two karts that touched this step (the first is moved out of the second).
struct Contact {
	uint8_t first, second;
	Vec3 normal; // toward the first
	float penetration;
	Vec3 point;
	float impulse; // on the first along the normal (the second gets its opposite)
};

// The route files each slot may play, by slot: its fast, medium and slow lines, numbered from 0 up to the first gap
// (none for a player's slot). The race draws one per slot that has any.
LRCORE_API std::vector<std::vector<std::string>> routeChoices(const Cache& cache, const RaceBrief& race);

// Why the race draws a random number (the tests follow the draws by these).
enum class Draw : uint8_t { Route, Taunt, StartGreen, Use, UseGreen, CurseJitter, Dynamite, TrackTimer, Code, Hazard };

// What a fired weapon is aimed by, besides its racer: a hazard's place and direction.
struct Aim {
	Vec3 position, direction;
	int8_t emplacement = -1; // the hazard that fired it
};

struct LRCORE_API World {
	Track track;
	std::vector<Route> routes; // by slot; no points: the slot has none
	RoutePlace preview;       // shared by the racers returning to a route
	Session session;
	int32_t raceLaps = 3;
	bool autopilot = false;
	uint32_t random = 0;
	// Told before each draw, if set (setup keeps it).
	void (*drawWatch)(World& world, Draw why, void* context) = nullptr;
	void* drawContext = nullptr;
	// If set, a frame's simulation (everything after the session's phase) is this instead: a test replaying a
	// recorded race sets what the race did (setup keeps it).
	void (*simulate)(World& world, int32_t frameMs, void* context) = nullptr;
	void* simulateContext = nullptr;
	// Told before and after each step's bodies move, if set (setup keeps it): a replay feed puts captured
	// physics bodies in there.
	void (*bodiesWatch)(World& world, bool moved, void* context) = nullptr;
	void* bodiesContext = nullptr;
	// Told as a race's setup begins, before its draws, if set (setup keeps it): a circuit's next race is set up inside
	// the frame the last one ends, so a replay feed moves to the new race's setup there.
	void (*setupWatch)(World& world, void* context) = nullptr;
	void* setupContext = nullptr;
	// Told of every event started, ended or fired at a place (-1: none of that half), if set (setup keeps it): the tests
	// follow the event dispatch by it.
	void (*eventWatch)(World& world, int32_t start, int32_t end, const Vec3* at, void* context) = nullptr;
	void* eventContext = nullptr;

	// The session around the simulation (Session.cpp): the brief it was set up from, the dialog, a circuit's points
	// (each racer's this race and so far), the standings' blink and the fade to black, a time race's runs, the tunes,
	// how the race ended and its records.
	enum class End : uint8_t { None, Over, Abort, RestartCircuit };
	RaceBrief brief;
	RaceDialog dialog;
	std::array<int32_t, 6> roundPoints = {}, circuitPoints = {};
	int32_t blinkMs = 0;
	struct Fade {
		int32_t ms = 0, left = 0;
		bool active = false, ended = false;
	} fade;
	// The race's opening fade from black: 500 ms from the race's start, its quad's alpha
	// 255 · left / 500, drawn over the HUDs while any is left.
	int32_t openingMs = 0;
	TimeRace timeRace;
	bool timeRaceAsked = false; // its end dialog opened
	int32_t musicCount = 0;     // the track's tunes
	RaceTune tune;
	End ending = End::None;
	bool restarted = false;     // this frame (its records were taken first)
	bool clockHeld = false;     // the session's clock doesn't run this frame (a dialog just closed)
	RaceRecord record;          // the best of this race and its restarts'
	// The touch spheres (Track::touchSpheres): racers inside now and last frame, by bit; their events at their centre.
	std::vector<uint8_t> touchInside, touchLast;
	std::vector<PlaceState> touchPlaces, places; // and the positional spheres' (Track::places)
	std::vector<uint8_t> contactOrder; // racers and active rocks (RockEntry + hazard), by their sphere's lowest x
	static constexpr uint8_t RockEntry = 128;

	// Per racer.
	std::vector<KartParams> params;
	std::vector<Kart> karts;
	std::vector<Driver> drivers;
	std::vector<Controls> controls;
	std::vector<uint16_t> keys; // the players' keys this frame (Key)
	std::vector<uint16_t> lastKeys; // and last frame's
	std::vector<Pose> visible; // the car as drawn; a route kart's is set by its route, the body follows
	std::vector<Playback> playback;
	std::vector<Racer> racers;
	std::vector<Sphere> carBounds;   // in the car's frame: the visible car's bounds
	std::vector<Sphere> groups;      // the whole racer's (body, car, driver), world, as its visible models last updated
	bool groupsStale = false;        // not yet updated since the setup
	std::vector<Sphere> bodyBounds;  // in the car's frame: its body model's (what lightning strikes)
	std::vector<int32_t> worldMs;    // racers x volumes: a dynamic world's time left
	std::vector<SweepHit> hits;      // scratch
	std::vector<std::array<int16_t, 4>> wheelSurfaces; // scratch: a body step's wheel surfaces, substep by substep
	std::vector<Contact> contacts;   // this step's (that had a response)
	std::vector<Power> powers;

	// The power-ups: the bricks (Track::bricks), every pool's actions (kind by kind) with the active list (newest
	// first) and each kind's free list (its head first), the two explosion pools likewise, the effect slots.
	std::vector<Brick> bricks;
	std::vector<Action> actions;
	std::vector<uint8_t> actionList;
	std::array<std::vector<uint8_t>, Action::Kinds> freeActions;
	std::array<Explosion, 8> explosions; // 6, then 2 spike explosions
	std::array<std::vector<uint8_t>, 2> explosionList, freeExplosions;
	uint32_t effectMask = 0;
	int32_t effectsUsed = 0;
	std::vector<uint8_t> targetsOn; // Track::targets still enabled
	Pose grapple;                   // the one grapple model every hook moves
	// The trigger list: members in stable slots, the list's order, and the step queue's waits (newest first).
	std::vector<Member> members;
	std::vector<uint16_t> memberOrder, freeMembers;
	std::vector<Wait> waits;
	size_t waitsWalked = 0; // scratch: those still in this step's walk
	// The track's moving parts.
	std::vector<Clock> clocks;       // Track::animated's
	std::array<Clock, 4> brickClocks; // Track::brickModels' (advanced in the power-ups' update)
	// The draw distances as they are now, squared, per level of detail: the animated entities' and the
	// plain models' (Track::models). Hazards and the event table's model distances hide a model (-1) and show it.
	std::vector<std::array<float, 3>> animatedDistances, modelDistances;
	std::vector<Hazard> hazards;     // Track::hazards'
	std::vector<EventState> events;  // Track::events'
	std::vector<TrackTimerState> timers;
	float blowup = -1;               // a sphinx's blow-up track's cursor while it plays

	// The race view: the racers' shapes and looks, the drivers' and chassis' animations and skeletons
	// ([0] the driver's, then each chassis' and variant's by name), the look draws' index (tests set lookWatch to
	// follow a captured run), the players' cameras and HUD gadgets, which cameras drew each animated entity this frame
	// (bits), the material tracks' cursors and shown frames, the texture scrolls' offsets (1/65536 UV).
	std::vector<RacerShape> shapes;
	std::vector<RacerLook> looks;
	std::vector<Animation> figures;
	std::vector<std::vector<SkeletonNode>> skeletons;
	std::vector<std::string> figureNames; // the part files' names, by figure
	uint32_t lookRandom = 0;
	void (*lookWatch)(World& world, size_t racer, bool cooldown, void* context) = nullptr;
	void* lookContext = nullptr;
	std::vector<PlayerCamera> cameras;
	std::vector<uint8_t> mapModes;
	std::vector<uint8_t> drawnBy;
	// The material tracks (Track::materialTracks): each its cursor (frames), rate (frames per ms),
	// whether it is assigned (runs) and loops, and the frame its slots show (an index into its frames; -1 none yet).
	struct MaterialState {
		float cursor = 0, rate = 0;
		bool assigned = false, looping = false;
		int16_t shown = -1;
	};
	std::vector<MaterialState> materials;
	std::vector<std::array<uint16_t, 2>> scrollOffsets;
	// The HUDs, one per player camera, and the attract demo's DEMO blink; this frame's events for the
	// show, cleared as a frame begins, each tagged with the part of the frame raising it.
	std::vector<Hud> huds;
	std::vector<uint8_t> tinted; // by racer: an event's colour change is on it (the snow's camera hook reads it)
	int32_t demoMs = 0;
	std::vector<RaceEvent> raceEvents;
	RaceEvent::Stage stage = RaceEvent::Setup;

	// The track, the routes, every racer at its start place; the session in its intro. False if a file is missing.
	bool setup(const Cache& cache, const RaceBrief& race);
	// The session's part of it (the brief, the racers' kinds, a time race's record ghost, the tunes), what a replayed
	// race needs.
	bool setupSession(const Cache& cache, const RaceBrief& race);
	// One frame of the program in the race: the dialog's keys (menu keys: Menus.h), then, unless paused, the frame;
	// the fade and the standings' blink. In the demo any key ends the race.
	// A restart sets the race up again from the cache (none: a replayed race, its session alone).
	void update(int32_t frameMs, std::span<const uint32_t> pressed, std::span<const uint32_t> released, const Cache* cache);
	// Back to the intro (the pause dialog's restart, a time race's TRY AGAIN), its records taken first: set up again
	// from the brief, keeping the session's points, records and time race runs.
	void restart(const Cache* cache);
	// Where a time race's ghost is shown at the run's clock: its first pose before the start, then between its
	// samples; false once it has passed its last (it is finished) or the clock passed what a ghost shows.
	bool ghostAt(const GhostRun& run, Vec3& position, Quat& rotation) const;
	bool paused() const { return dialog.kind != RaceDialog::None; }
	// Each racer's final place (0 first) once the race is over; the standings table showing (0 none, 1 the race's, 2
	// the circuit's).
	int32_t standingsTable() const;
	// One frame of frameMs: the session's phase, the frame's timers and the players' controls, then the steps.
	// sessionMs: the session clock's part (fast forward runs the race faster, not its clock); -1 the frame's.
	void frame(int32_t frameMs, int32_t sessionMs = -1);
	void step(int32_t dt);

	// The parts of a frame and a step, in their order. A frame begins with the session's clock and phase, the flyby
	// and the scenery's animations, the power-ups, the dynamic worlds' timers, the positional and touch spheres' frame,
	// then the event table, the hazards and the track timers; the players' controls come after it, the warp's shrinking
	// after the steps.
	void beginFrame(int32_t frameMs, int32_t sessionMs = -1);
	void advanceSession(int32_t frameMs, int32_t sessionMs); // the session's clock, then its phase; the flyby (the race's ms)
	void updateSession();
	void advanceClocks(int32_t frameMs);
	void powerFrame(int32_t frameMs);
	void countDownWorlds(int32_t frameMs);
	void endTouchFrame();
	void eventFrame(int32_t frameMs);
	void playerControls(size_t racer, int32_t frameMs);
	void endFrame();
	void driveRacer(size_t racer, int32_t dt);
	void moveRacer(size_t racer, int32_t dt);
	void runWaits(int32_t dt);
	void contactSweep();
	void touchRacers(const Sphere* spheres); // world spheres, by racer
	void racerTimers(int32_t dt);
	void updateStandings();
	void pace(int32_t dt);
	void recordRun(int32_t dt); // a time race's current run: its clock and samples (the step's last part)
	// The race view's parts (Look.cpp, Camera.cpp): each racer's visible models (its look, then its group sphere), the
	// players' cameras (a dt of 0: only a dirty one, snapped), the drawn rule (after the steps), the material tracks
	// and scrolls (with the scenery's clocks); a player's camera, map and look-back keys (with its controls).
	void setupView(const Cache& cache, const RaceBrief& race, const std::vector<std::string>& bodies);
	void racerTouches(); // the touch spheres against the racers' group spheres
	void visuals(int32_t dt);
	void racerLook(size_t racer, int32_t dt);
	void updateGroup(size_t racer);
	void updateCameras(int32_t dt);
	void updateCamera(size_t player, int32_t dt);
	void chooseView(size_t player, uint8_t view);
	void drawScenery();
	void advanceMaterials(int32_t ms);
	void showMaterial(size_t track); // a track's slots take its cursor's frame
	// Whether a model is drawn at a squared distance by its distances now (the first level not nearer).
	static bool withinDistance(const std::array<float, 3>& distances, float squared)
	{
		return distances[0] >= squared || distances[1] >= squared || distances[2] >= squared;
	}
	// Whether a plain model (Track::models) is in reach of a camera's eye now: its
	// position (not its bounds) to the eye, squared, against its stored levels; no scalability factor. The frustum test
	// with its bounds sphere comes after (the presentation's).
	bool plainInReach(size_t model, Vec3 eye) const
	{
		return model < modelDistances.size() && model < track.models.size() &&
			withinDistance(modelDistances[model], lengthSquared(track.models[model].second.position - eye));
	}
	// The power-up bricks' entities posed: a brick's cube or streaks at a place and size, every node's
	// matrix in the world.
	void brickNodes(Track::BrickModel model, Vec3 at, float size, std::vector<NodeMatrix>& out) const;
	void viewKeys(size_t racer);
	uint16_t lookDraw(size_t racer, bool cooldown);
	void markHit(size_t racer) { looks[racer].marks |= 2; }
	void carVisuals(size_t racer);
	void driftMarks(size_t racer); // a powerslide begun or ended: the drift's dust and skid effects
	void skidEffects(size_t racer, bool on, RaceEvent::Reason why);
	// A spawn of an emitter (set 0 the race's, 1 the track's, 2 either) for an owner, at a place.
	void spawn(const char* emitter, uint8_t set, Owner owner, Vec3 at);
	// The HUDs' update after a frame: their clocks take the frame's real ms (not while paused,
	// nor outside the countdown, the racing and the finish), the banner, the place's pulse, the best lap, the speed.
	void updateHuds(int32_t realMs);
	// The events (Show.cpp).
	RaceEvent& raise(RaceEvent::Kind kind, Owner owner = {});
	void playSound(int32_t sound, Owner owner, const Vec3* at, float near = 30, float far = 300);
	void voice(size_t racer, RaceEvent::VoiceLine line); // a reaction or a taunt (the show draws the line, keeps the cooldown)
	// A racer's members' poses and bounds (World::shapes' order: body, variant, car, driver), world; false if absent.
	bool member(size_t racer, RacerShape::Member which, Pose& pose, float& scale) const;
	// The session's moments (Session.cpp): the start, a racer placed, the racing's end (the first player over the
	// line), the session's end; the race's records taken; the fade; the dialogs.
	void racingStarted();
	void finished(size_t racer);
	void racingEnded(size_t racer);
	void sessionEnded();
	void takeRecord();
	void prepareRuns();
	void resetRuns();
	void startFade();
	void updateFade(int32_t frameMs);
	void openDialog(RaceDialog::Kind kind);
	bool choose(const Cache* cache);

	// The race's rules, called from the steps (and the tests).
	void handToAi(size_t racer);
	void applyHits(size_t racer); // what its sweep met (hits)
	void crossGate(size_t racer, int16_t checkpoint, bool forward);
	void lapMark(size_t racer, LapMark::Zone zone);

	// Events (Events.cpp): started, ended or both at once, at a place (none: nullptr) or for a racer.
	void startAt(int32_t event, const Vec3* at);
	void endAt(int32_t event, const Vec3* at);
	void fireAt(int32_t start, int32_t end, const Vec3* at);
	void startFor(int32_t event, size_t racer);
	void endFor(int32_t event, size_t racer);
	void fireFor(int32_t start, int32_t end, size_t racer);
	void forceEnd(int32_t event);
	// The trigger list's members and the step queue's waits (both share the queue's 100 entries).
	int16_t addMember(Member member);
	void killMember(int16_t& member);
	bool schedule(Wait wait);

	// Power-ups (Powers.cpp, Weapons.cpp).
	void useHeld(size_t racer);
	void consider(size_t racer);
	int16_t make(int8_t racer, uint8_t colour, int32_t level, const Aim* aim = nullptr);
	void touchBrick(size_t brick, size_t racer);
	void dropWhite(size_t racer);
	void sendHome(size_t brick);
	void cancelTurbo(size_t racer);
	void cancelShield(size_t racer);
	void cancelMagnetHold(size_t racer);
	void touchTrap(size_t action, size_t racer);
	void touchProjectile(size_t action, size_t racer);
	void blast(size_t explosion, size_t racer);
	int8_t explode(bool spike, Vec3 at, bool scar, int8_t owner);
	// A projectile's segment against the track (its surfaces' projectile events fired): true and the hit if it stops.
	bool shotHits(Vec3 a, Vec3 b, Vec3& hit, Vec3& normal);

	// What happens to a racer (Effects.cpp), in either mode.
	void halt(size_t racer);
	void resume(size_t racer);
	void enterGhost(size_t racer);
	void leaveGhost(size_t racer);
	void turboOn(size_t racer, int32_t level);
	void turboOff(size_t racer);
	void shieldOn(size_t racer, int32_t level);
	void curseOn(size_t racer, int32_t ms);
	void curseOff(size_t racer);
	void shove(size_t racer, Vec3 push);
	void endShove(size_t racer);
	void magnetHold(size_t racer, bool on);
	void spin(size_t racer, float turns, float rate, float align);
	void spinOut(size_t racer);
	void endSpinOut(size_t racer);
	void tilt(size_t racer, float rate, int32_t ms);
	void impulse(size_t racer, Vec3 j);
	void push(size_t racer, int which, Vec3 q);
	void endPush(size_t racer, int which);
	void boost(size_t racer, bool on);
	void hover(size_t racer, bool on);

	// Hazards (Hazards.cpp): their load steps, frames, what events do to them and what their spheres do to racers.
	void setupHazards();
	void hazardFrame(int32_t frameMs);
	void hazardEvent(int32_t start, int32_t end, const Vec3* at, int racer, bool force);
	void touchHazard(size_t hazard, size_t racer);
	// The animated scenery: a node's pose in its parent's frame, sampled at a time of a part (looping or not); a time
	// below 0 (never drawn) is its rest pose.
	Pose nodeLocal(const AnimatedModel& model, const Animation* animation, size_t node, int32_t part, float time, bool loop) const;
	int32_t timeBehind(size_t racer) const;
	// The response: the first moved out, both pushed apart (sets the impulse). False if a ghost or a tumbling kart
	// takes no part.
	bool contact(Contact& contact);
	void moveOut(size_t racer, Vec3 move);
	void hitRock(size_t racer, size_t hazard); // a kart bounces off a rolling rock's box
	void finish(size_t racer);
	void setStanding(size_t racer, int32_t standing);
	float progress(size_t racer) const;
	Sphere carSphere(size_t racer) const;
	Sphere groupSphere(size_t racer) const;
	Sphere bodySphere(size_t racer) const;
	uint16_t draw(Draw why)
	{
		if (drawWatch) {
			drawWatch(*this, why, drawContext);
		}
		return randomTable(random = (random + 1) & 1023);
	}

	// The contact boxes of two karts: true and the normal (toward the first), penetration and point if they overlap.
	bool boxContact(size_t first, size_t second, Vec3& normal, float& penetration, Vec3& point) const;
};

} // namespace lr
