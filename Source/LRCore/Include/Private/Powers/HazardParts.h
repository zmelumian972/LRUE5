#pragma once

// What the hazards' files share. Each hazard type is one HazardKind (its keyword, trigger and handlers), defined with
// its family: HazardScenery.cpp (the set pieces and the code puzzle), HazardSpheres.cpp (the hazards whose sphere
// meets the racers) and HazardShots.cpp (the ones that launch or make something). Hazards.cpp holds the clocks, the
// table in Hazard::Type's order and the dispatch.

#include "LRCore/Race/World.h"

namespace lr {

// A hazard as its handlers see it: the world, its index, its race state and its file's spec.
struct HazardSite {
	World& w;
	size_t index;
	Hazard& h;
	const HazardSpec& spec;
	Owner owner() const { return {Owner::Hazard, int16_t(index)}; }
};

constexpr int32_t FromSpec = -2; // a kind's trigger that is its file's

// A hazard type: its keyword in the hazard file, its trigger event as the race starts (-1 none) and what it does. A
// null handler does nothing, except a null deactivate, which just leaves it idle. activate runs only from idle and
// deactivate only from not idle. Its frame runs while it is active (an ending one deactivated first, and skipped if
// that left it idle) unless it owns its state: then every frame, the frame ending it itself.
struct HazardKind {
	int32_t keyword = -1;
	int32_t trigger = -1;
	void (*setup)(HazardSite) = nullptr; // as the race starts
	void (*activate)(HazardSite, int32_t racer) = nullptr; // its trigger started (racer: whose, -1 none)
	void (*deactivate)(HazardSite) = nullptr; // its trigger ended
	void (*frame)(HazardSite, int32_t ms) = nullptr;
	void (*touch)(HazardSite, size_t racer) = nullptr; // its sphere met a racer
	void (*started)(HazardSite, int32_t event, const Vec3* at) = nullptr; // any event's start
	bool byRacer = false; // its trigger is a racer's event (the warp pad), not the world's
	bool ownsState = false;
};

extern const HazardKind PillarHazard, SphinxHazard, HammerHazard, CodeHazard, SnowHazard, SmokeHazard, OpenerHazard, RockHazard;
extern const HazardKind GhostHazard, LavaHazard, RocketHazard, CraneHazard, GrabberHazard;
extern const HazardKind LauncherHazard, LaunchersHazard, CurseDropHazard, SweepHazard, RainHazard, WarpPadHazard;

constexpr int32_t LandingEvent = 7; // the pillar's landing; a launcher's ball hitting something, at the hit point

// The hazard deactivated (if it is not idle), by its kind (Hazards.cpp).
void endHazard(HazardSite s);
// A clock set to play a part from its start, looping.
void play(Clock& c, const Animation& a, int16_t part);
// Whether an animated model's clock has played its part to the end.
bool partDone(const World& w, size_t model);
// A surface (-1 none) made passable (not solid, projectiles pass) or solid.
void setSurface(World& w, int16_t surface, bool passable);
// A node's own pose (in its parent's frame) at a time of the model's part.
Pose localPose(const World& w, size_t model, size_t node, float time);
// A point in a model's frame, scaled and placed in the world.
inline Vec3 modelToWorld(const AnimatedModel& m, Vec3 p) { return toWorld(m.pose.rotation, p * m.scale) + m.pose.position; }
// A bone's place: its own position (parents not applied), placed in the world.
inline Vec3 bonePoint(const World& w, size_t model, size_t node, float time)
{
	return modelToWorld(w.track.animated[model], localPose(w, model, node, time).position);
}

} // namespace lr
