#pragma once

// The show: what a race shows that no rule reads, run from the events the
// world raises each frame and from its race view: the two effect sets (their slots, handles and particles), the car
// visuals' handles, the decals (the cars' shadows, the skid trails, explosion scars, oil slicks), the sky's animation,
// and the presentation's own random stream. A plain value beside World: the world never reads it; the presentation
// advances it once per race frame (after the world's frame) and draws what it holds.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Show/Show.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lr {

class Cache;
struct World;

struct ShowParticle {
	Vec3 position, velocity; // world; units, units per second
	int32_t age = 0; // ms
};

// An effect set's slot and where its particles come from.
struct EffectSlot {
	enum Place : uint8_t { AtPoint, Wheel, RearAxle, Exhaust, AboveOwner, OilSlick, Dynamite };
	bool active = false;
	int16_t emitter = -1; // the definition it runs; -1 draining
	int16_t look = -1; // the emitter its particles look like (kept while draining)
	int32_t timerMs = 0, ageMs = 0, emissions = 0;
	bool finishPending = false;
	int16_t handle = -1; // the handle controlling it, -1 none
	Place place = AtPoint;
	Owner owner;
	int16_t racer = -1; // the racer it follows (wheels, axle, exhaust, above), or the action (oil, dynamite)
	uint8_t wheel = 0;
	Vec3 position, velocity; // the slot's, this frame (velocity per second)
	Basis rotation; // kept when a spawn gives none (QUIRK)
	Vec3 offset; // an oil slick's bubble place about its centre (drawn each frame)
	bool bubbled = false; // this frame
	bool slickWas = false; // its slick was active as the frame began
	Vec3 anchor; // its first particle's birthplace since it was taken
	bool anchored = false;
	std::vector<ShowParticle> particles; // newest first, at most 16
};

struct EffectSet {
	static constexpr size_t Handles = 100;
	EmitterFile file;
	MaterialAnimationFile tracks; // the set's material animation
	std::vector<EffectSlot> slots;
	std::array<int16_t, Handles> handles; // the slot each controls (-1 none)
	std::array<bool, Handles> used = {};
	std::string libraries[2]; // the material libraries its materials are found in (.MDB paths), in order
};

// A handle of either set.
struct EffectHandle {
	int8_t set = -1;
	int16_t index = -1;
	bool held() const { return set >= 0; }
};

// A decal projected on the track: triangles in world space with their box's uvs.
struct Decal {
	enum Kind : uint8_t { Shadow, Skid, Scar, Slick };
	Kind kind = Shadow;
	int16_t owner = -1; // racer (shadow), trail (skid), explosion (scar), action (slick)
	std::string material; // RACERFX's (carshadN, skid, trbskid), EMITTER's (exscar, oilslck)
	uint8_t alpha = 255; // the vertex alpha (a shadow's per camera: Show::shadowAlpha)
	std::vector<Vec3> points; // three per triangle
	std::vector<std::array<float, 2>> uvs;
};

// A race sound the show plays or holds: a rule's one-shot or loop, or one the show derives (the
// engines, the loops that follow the race view, the voices). A 3D sound is heard from the players' cars; a loop that
// is off or can't be heard keeps no voice and waits.
struct ShowSound {
	enum Role : uint8_t { Rule, Engine, Brake, Others, Skid, Spin, Shield, Turbo, Warp, Fuse, Flight, Crackle, Retrigger, Surface };
	int32_t id = -1; // the race's sound id
	uint32_t serial = 0; // unique for the race (a voice names its sound by it)
	Owner owner;
	Role role = Rule;
	uint8_t slot = 0; // the role's: an engine's layer, a turbo's level, a flight's kind
	bool looping = false, positional = true;
	bool standing = false; // made at the load (never asked again: turned on and off)
	bool on = true; // a loop that wants to play
	bool asked = true; // a one-shot not heard yet (it plays at its first hearing or never)
	float near = 30, far = 300, volume = 1, pitch = 1;
	Vec3 place, velocity;
	float playedMs = 0; // a one-shot's, at its pitch
	int8_t voice = -1; // the voice playing it
	// As last heard: volume, pan (negative: to the left), pitch (with the Doppler), the priority.
	float heard = 0, pan = 0, heardPitch = 1;
	int32_t priority = 0;
};

class LRCORE_API Show {
public:
	// Why the presentation draws (the tests follow the draws by these).
	enum class Draw : uint8_t {
		ParticleChance,
		ParticlePoint,
		DustCorner,
		TyreWheel,
		OilBubble,
		Hazard,
		// The sounds'.
		ReactionSound,
		ReactionCooldown,
		Scrape,
		Bump,
		ShieldHit,
		SoundDelayLoad,
		SoundDelay,
		SoundChance,
		Crackle,
		Wail,
	};

	// Set up for a world just set up from the cache (its race file's effect sets and sky, the chassis' skid and shadow
	// sizes). Keeps the presentation stream's index.
	[[nodiscard]] bool load(const Cache& cache, const World& world);
	// One race frame of `ms` (the race's: 0 while paused) after the world's frame: its events, the effect sets'
	// update, the car visuals, the particles, decals and sky.
	void advance(const World& world, int32_t ms);

	// The race's ms since it was set up (the race's ms each frame: the power-up models' looping parts run on it).
	int32_t clockMs = 0;
	// The presentation stream: starts at 0 and runs for the whole program.
	uint32_t random = 0;
	uint16_t (*drawWatch)(Show& show, Draw why, int16_t racer, void* context) = nullptr; // a test's: the value to use
	void* drawContext = nullptr;
	uint16_t draw(Draw why, int16_t racer = -1);
	std::string drawingFor; // the emitter (and its owner) an emission draw is for

	std::array<EffectSet, 2> sets; // the race's (10 slots), the track's (4)
	// This frame's spawn attempts: the emitter, the set (-1 neither has it), the slot taken (-1 none), whether it
	// replaced a busy slot.
	struct Spawned {
		std::string name;
		int8_t set;
		int16_t slot;
		bool replaced;
	};
	std::vector<Spawned> spawned;
	// A slot's particles' look: a particle's size (width, height) and material name; false if not drawn (shrunk away).
	bool particleLook(size_t set, const EffectSlot& slot, const ShowParticle& p, float& width, float& height, std::string& material) const;

	// Per racer: the car visuals' handles (dust, car smoke, tyre smoke, each wheel's surface particle) and the names the
	// wheels want.
	struct Visuals {
		EffectHandle dust, smoke, tyre;
		std::array<EffectHandle, 4> wheels;
		std::array<std::array<char, 9>, 4> wanted = {};
		bool skidding = false, spinning = false, turbo = false; // what the skid trails are laying for
		uint8_t skidWheels = 0; // the wheels laying marks
		std::array<int32_t, 4> liftedMs = {};
	};
	std::vector<Visuals> visuals;
	// Spawns owned by other things (power-ups, hazards, track events): their handles by owner key.
	std::vector<std::pair<Owner, EffectHandle>> owned;

	// Decals: each racer's shadow (alpha by camera, shadowAlpha), the skid trails' pieces, scars and slicks.
	std::vector<Decal> decals;
	std::vector<std::array<uint8_t, 2>> shadowAlpha; // by racer, per player camera (0: not drawn)
	struct Trail {
		int16_t racer = -1;
		uint8_t wheel = 0;
		bool turbo = false, spinning = false, fading = false;
		int32_t fadeMs = 0;
		Vec3 anchor; // where the live piece starts (world)
		struct Piece {
			Vec3 from, to;
			int32_t ageMs = 0;
		};
		std::vector<Piece> pieces; // baked, oldest first (at most 3), then the live one
		bool live = false;
		Piece current;
	};
	std::array<Trail, 4> trails;
	std::vector<float> scarMs; // by explosion: ms since its scar began (-1 none)

	// The sky: its state, keyframe and clock, a blend toward another state, the rings' colours now;
	// whether the dome and the sky's world are shown.
	SkyFile sky;
	bool hasSky = false, domeShown = true, worldShown = true;
	int32_t skyState = 0, skyKey = 0, skyMs = 0;
	int32_t blendFrom = -1, blendMs = 0, blendLength = 0;
	std::array<std::array<float, 3>, 3> skyColours = {}; // horizon, middle, high (0..255)

	// The race's sounds: the four banks (effects, the two voice banks, the track's), the sounds held,
	// the 32 voices (the serial of the sound each plays, 0 free), and what this frame asked for and started.
	static constexpr size_t Voices = 32;
	struct Bank {
		std::string folder; // where its samples are: "GAMEDATA/COMMON/"
		std::vector<std::string> names; // its samples' files, as the cache names them: "321.PCM"
		std::vector<float> lengthsMs; // each one's length at pitch 1 (0: none, plays nothing)
	};
	std::array<Bank, 4> banks;
	std::vector<ShowSound> sounds;
	std::array<uint32_t, Voices> voices = {};
	struct Started {
		uint32_t serial;
		int32_t id;
	};
	std::vector<Started> asked, started, stopped; // in order (stopped: a loop's voice let go)
	float effectsVolume = 1; // the effects setting (0..1)
	// A sound id's sample (the cache's path), empty if it has none.
	std::string sample(int32_t id) const;
	const ShowSound* find(uint32_t serial) const;
	float lengthOf(int32_t id) const; // its sample's ms at pitch 1
	// The device's law: a heard volume to a gain (linear; 0 silent), and a pan's far channel's.
	static float gainOf(float volume);
	static float panGain(float pan);

private:
	// The sounds' half (Sounds.cpp).
	void loadSounds(const Cache& cache, const World& world);
	void standingLoops(const World& world); // (loadSounds')
	void updateSounds(const World& world, int32_t ms);
	void soundEvent(const World& world, const RaceEvent& e);
	void ruleSound(const RaceEvent& e, bool racer, size_t i); // soundEvent's parts
	void voiceLine(const World& world, const RaceEvent& e, size_t i);
	ShowSound& play(int32_t id, Owner owner, const Vec3* at, float near = 30, float far = 300, bool looping = false);
	ShowSound* held(ShowSound::Role role, Owner owner, uint8_t slot = 0);
	void stopSound(ShowSound& s); // off: its voice freed
	void letGo(ShowSound& s); // its voice freed (a loop waits for another)
	void hear(const World& world);
	void allocate(bool paused);
	void deriveSounds(const World& world, int32_t ms);
	void othersEngine(const World& world, size_t n, bool one, int8_t listener);
	void kartLoops(const World& world, size_t n);
	void actionLoops(const World& world, size_t n, int32_t ms);
	void flights(const World& world, int8_t listener);
	void ghostWails(const World& world, int32_t ms);
	void retrigger(const World& world, int32_t ms);
	void engines(const World& world, int32_t ms);
	Vec3 placeOf(const World& world, const ShowSound& s, Vec3& velocity) const;
	struct Voicing {
		int32_t base = -1; // the racer's first voice line's sound id (-1 silent)
		int32_t reactionMs = 0, scrapeMs = 0; // cooldowns (the shield-hit one gates nothing: Absorb)
		bool engineOn = false; // a player's engine (from the countdown)
		std::array<float, 3> layers = {}; // idle, drive, coast
		bool braking = false;
	};
	std::vector<Voicing> m_voicing; // by racer
	std::vector<int32_t> m_delays; // the track's retriggered sounds' delays, by event resource
	std::vector<uint8_t> m_retriggering; // and whether each was on as the last frame ended
	std::vector<int32_t> m_crackleMs; // by action: a lightning's next crackle
	std::vector<int32_t> m_wailMs; // by hazard: a ghost's wail
	std::vector<float> m_wail; // its depth
	uint32_t m_serial = 0;
	bool m_othersMade = false;
	bool m_racing = false;

private:
	void loadChassis(const Cache& cache, const World& world); // load's parts
	void groundCells(const World& world);
	void apply(const World& world, const RaceEvent& e);
	void spawnFor(const World& world, const RaceEvent& e); // apply's parts
	void letGoOf(const RaceEvent& e);
	void wheelParticle(const World& world, const RaceEvent& e, size_t i);
	void skyEvent(const RaceEvent& e);
	void bubble(EffectSlot& slot);
	void updateSets(const World& world, int32_t ms);
	bool emitParticle(const World& world, EffectSlot& slot, const EmitterFile::Emitter& em); // (updateSets')
	void carVisuals(const World& world);
	void retries(const World& world, bool hazards);
	std::vector<uint8_t> m_wanting, m_hazardsOn; // last frame's: a track event's particle active, a hazard on
	bool holds(Owner owner) const;
	void moveParticles(const World& world, int32_t ms);
	void place(const World& world, EffectSlot& slot);
	void updateDecals(const World& world, int32_t ms);
	void carShadows(const World& world);
	void skidMarks(const World& world, int32_t ms);
	void layTrail(const World& world, size_t i, uint8_t w, int32_t ms);
	void trailDecals(int32_t ms);
	void groundMarks(const World& world);
	void updateSky(int32_t ms);
	EffectHandle spawn(const World& world, int32_t set, const char* name, Owner owner, const Vec3* at, EffectSlot::Place where, int16_t racer, uint8_t wheel = 0);
	void finish(EffectHandle& h);
	void release(EffectHandle& h);
	bool controls(EffectHandle h, int32_t atLeast) const; // controls a slot with at least that many emissions
	void stopAll();
	void project(Decal& d, Vec3 centre, Vec3 along, float width, float length) const;

	// The track's collision triangles under a box, found through a grid of cells.
	struct Triangle {
		Vec3 v[3];
	};
	std::vector<Triangle> m_ground;
	std::vector<std::vector<uint32_t>> m_cells;
	float m_minX = 0, m_minY = 0, m_cell = 16;
	int32_t m_columns = 0, m_rows = 0;
	struct Chassis {
		float shadowWidth = 8, shadowLength = 9;
		std::array<float, 2> skidWidths = {1.2f, 1.2f};
		std::array<Vec3, 4> skidPoints = {};
		float enginePitch = 0;
	};
	std::vector<Chassis> m_chassis; // by racer
};

} // namespace lr
