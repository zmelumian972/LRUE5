#pragma once

// The show's effects (EffectSets.cpp): the two effect sets (their slots,
// handles and particles), the car visuals' handles and the spawns other things own, run from the frame's events. Its
// operations that draw take the show (its stream).

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Show/Show.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lr {

class Cache;
class Show;
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
	List<EffectSlot> slots;
	Array<int16_t, Handles> handles; // the slot each controls (-1 none)
	Array<bool, Handles> used = {};
	std::string libraries[2]; // the material libraries its materials are found in (.MDB paths), in order
};

// A handle of either set.
struct EffectHandle {
	int8_t set = -1;
	int16_t index = -1;
	bool held() const { return set >= 0; }
};

struct LRCORE_API Effects {
	Array<EffectSet, 2> sets; // the race's (10 slots), the track's (4)
	// This frame's spawn attempts: the emitter, the set (-1 neither has it), the slot taken (-1 none), whether it
	// replaced a busy slot.
	struct Spawned {
		std::string name;
		int8_t set;
		int16_t slot;
		bool replaced;
	};
	std::vector<Spawned> spawned;
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

	// The race file's two sets (the track's folder `dir`) with their worlds' material animations and libraries; the
	// racers' car visuals.
	void load(const Cache& cache, const std::string& dir, const RaceFile& race, size_t racers);
	// A slot's particles' look: a particle's size (width, height) and material name; false if not drawn (shrunk away).
	bool particleLook(size_t set, const EffectSlot& slot, const ShowParticle& p, float& width, float& height, std::string& material) const;

	// A frame's parts (Show::advance runs them in its order): its spawns cleared, an event applied, the sets' update
	// (emitters and particles), the rules' particles tried again, what the next frame's retries and bubbles look back
	// on, the car visuals, the particles' places.
	void beginFrame();
	void apply(Show& show, const World& world, const RaceEvent& e);
	void update(Show& show, const World& world, int32_t ms);
	void retries(const World& world, bool hazards);
	void endEvents(const World& world);
	void carVisuals(Show& show, const World& world);
	void moveParticles(Show& show, const World& world);

private:
	void spawnFor(const World& world, const RaceEvent& e); // apply's parts
	void letGoOf(Show& show, const RaceEvent& e);
	void wheelParticle(const World& world, const RaceEvent& e, size_t i);
	void bubble(Show& show, EffectSlot& slot);
	void updateSets(Show& show, const World& world, int32_t ms);
	void emitParticle(Show& show, const World& world, EffectSlot& slot, const EmitterFile::Emitter& em); // (updateSets')
	std::vector<uint8_t> m_wanting, m_hazardsOn; // last frame's: a track event's particle active, a hazard on
	bool holds(Owner owner) const;
	void place(const World& world, EffectSlot& slot);
	EffectHandle spawn(const World& world, int32_t set, const char* name, Owner owner, const Vec3* at, EffectSlot::Place where,
		int16_t racer, uint8_t wheel = 0);
	void finish(EffectHandle& h);
	void release(EffectHandle& h);
	bool controls(EffectHandle h, int32_t atLeast) const; // controls a slot with at least that many emissions
	void stopAll();
};

} // namespace lr
