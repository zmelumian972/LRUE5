#pragma once

// The simulation's state, owned in one place: the track, the routes, the random index and the parts, so the whole world
// copies and compares as a value. Each part owns its state and its operations, which take the world they run in:
// Session (the race's flow), Racers (every racer and the race's rules over them), PowerUps, Scenery (the animated
// scenery and the hazards), Triggers (events and the trigger list) and RaceView (what the show reads). World sets a
// race up and runs a frame's parts in their order.
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
#include "LRCore/Powers/PowerUps.h"
#include "LRCore/Race/Racers.h"
#include "LRCore/Race/Route.h"
#include "LRCore/Race/Session.h"
#include "LRCore/Race/Track.h"
#include "LRCore/Race/Triggers.h"
#include "LRCore/View/RaceView.h"

#include <array>
#include <string>
#include <vector>

namespace lr {

// The game's "random" numbers: a fixed table of 1024, read at one index that runs for the whole program.
LRCORE_API uint16_t randomTable(uint32_t index);

// The route files each slot may play, by slot: its fast, medium and slow lines, numbered from 0 up to the first gap
// (none for a player's slot). The race draws one per slot that has any.
LRCORE_API std::vector<std::vector<std::string>> routeChoices(const Cache& cache, const RaceBrief& race);

// Why the race draws a random number (the tests follow the draws by these).
enum class Draw : uint8_t { Route, Taunt, StartGreen, Use, UseGreen, CurseJitter, Dynamite, TrackTimer, Code, Hazard };

struct World;

// What a test or a replay feed follows of a race (World::observer), each told as it happens; by default nothing.
struct LRCORE_API WorldObserver {
	virtual ~WorldObserver() = default;
	// As a race's setup begins, before its draws: a circuit's next race is set up inside the frame the last one ends,
	// so a replay feed moves to the new race's setup there.
	virtual void onSetup(World&) {}
	// Before each draw (and each look draw).
	virtual void onDraw(World&, Draw) {}
	virtual void onLook(World&, size_t /*racer*/, bool /*cooldown*/) {}
	// Before and after each step's bodies move: a replay feed puts captured physics bodies in there.
	virtual void onBodies(World&, bool /*moved*/) {}
	// Every event started, ended or fired at a place (-1: none of that half): the tests follow the event dispatch by it.
	virtual void onEvent(World&, int32_t /*start*/, int32_t /*end*/, const Vec3* /*at*/) {}
	// If replaying, a frame's simulation (everything after the session's phase) is simulate instead: a test replaying
	// a recorded race sets what the race did.
	virtual bool replaying() const { return false; }
	virtual void simulate(World&, int32_t /*frameMs*/) {}
};

struct LRCORE_API World {
	Track track;
	List<Route> routes; // by slot; no points: the slot has none
	RoutePlace preview; // shared by the racers returning to a route
	uint32_t random = 0;
	// The observer, if any (`observer.to`). It belongs to this world, not to its value: a copy of the world starts
	// without one, a world assigned over this one keeps this one's (so setup keeps it).
	struct Observer {
		WorldObserver* to = nullptr;
		Observer() = default;
		Observer(const Observer&) {}
		Observer& operator=(const Observer&) { return *this; }
	} observer;
	bool replaying() const { return observer.to && observer.to->replaying(); }

	// The parts.
	Session session;
	Racers racers;
	PowerUps powerUps;
	Scenery scenery;
	Triggers triggers;
	RaceView raceView;

	// The track, the routes, every racer at its start place; the session in its intro. False if a file is missing.
	[[nodiscard]] bool setup(const Cache& cache, const RaceBrief& race);
	// One frame of frameMs: the session's phase, the frame's timers and the players' controls, then the steps.
	// sessionMs: the session clock's part (fast forward runs the race faster, not its clock); -1 the frame's.
	void frame(int32_t frameMs, int32_t sessionMs = -1);
	void step(int32_t dt);
	// A frame's first and last parts. It begins with the session's clock and phase, the flyby and the scenery's
	// animations, the power-ups, the dynamic worlds' timers, the positional and touch spheres' frame, then the event
	// table, the hazards and the track timers; the players' controls come after it, the warp's shrinking after the
	// steps.
	void beginFrame(int32_t frameMs, int32_t sessionMs = -1);
	void endFrame();
	uint16_t draw(Draw why)
	{
		if (observer.to) {
			observer.to->onDraw(*this, why);
		}
		return randomTable(random = (random + 1) & 1023);
	}
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
		return model < scenery.modelDistances.size() && model < track.models.size() &&
			withinDistance(scenery.modelDistances[model], lengthSquared(track.models[model].second.position - eye));
	}

private:
	// setup's part: a racer at its start place.
	void placeRacer(size_t racer, const RaceBrief& race);
};

// A point in an animated model's node frame, through the node's parents up to the root, scaled and placed in the world:
// the model's playing part at a time.
LRCORE_API Vec3 nodeWorld(const World& w, size_t model, int32_t node, Vec3 p, float time);

} // namespace lr
