#pragma once

// The show: what a race shows that no rule reads, run from the events the
// world raises each frame and from its race view. A plain value beside World: the world never reads it; the
// presentation advances it once per race frame (after the world's frame) and draws what it holds. Its parts own their
// state and operations: Effects (the effect sets, the car visuals' handles, particles), Decals (shadows, skid trails,
// scars, slicks), Sky and RaceSounds. Show keeps the presentation's own random stream, the racers' chassis, and runs a
// frame's parts in their order.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Show/Decals.h"
#include "LRCore/Show/Effects.h"
#include "LRCore/Show/RaceSounds.h"
#include "LRCore/Show/Show.h"
#include "LRCore/Show/Sky.h"

#include <array>
#include <cstdint>
#include <string>

namespace lr {

class Cache;
struct World;
struct ShowObserver;

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
	// sizes, the sounds). Keeps the presentation stream's index, the observer and the effects volume.
	[[nodiscard]] bool load(const Cache& cache, const World& world);
	// One race frame of `ms` (the race's: 0 while paused) after the world's frame: its events, the effect sets'
	// update, the car visuals, the particles, sky, decals and sounds.
	void advance(const World& world, int32_t ms);

	// The race's ms since it was set up (the race's ms each frame: the power-up models' looping parts run on it).
	int32_t clockMs = 0;
	// The presentation stream: starts at 0 and runs for the whole program.
	uint32_t random = 0;
	ShowObserver* observer = nullptr; // a test's, if any: it gives the draws' values
	uint16_t draw(Draw why, int16_t racer = -1);
	float effectsVolume = 1; // the effects setting (0..1)

	// The racers' chassis (load's): the shadows' and skid trails' sizes, the engines' pitch.
	struct Chassis {
		float shadowWidth = 8, shadowLength = 9;
		std::array<float, 2> skidWidths = {1.2f, 1.2f};
		std::array<Vec3, 4> skidPoints = {};
		float enginePitch = 0;
	};
	List<Chassis> chassis; // by racer

	// The parts.
	Effects effects;
	Decals decals;
	Sky sky;
	RaceSounds sounds;

private:
	void loadChassis(const Cache& cache, const World& world); // load's part
	void apply(const World& world, const RaceEvent& e);
};

// What a test follows of the show (Show::observer): each draw, whose value it gives, and each emission before its draws.
struct LRCORE_API ShowObserver {
	virtual ~ShowObserver() = default;
	virtual uint16_t onDraw(Show&, Show::Draw why, int16_t racer) = 0;
	virtual void onEmit(const std::string& /*emitter*/, Owner /*owner*/) {}
};

} // namespace lr
