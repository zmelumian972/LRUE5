#pragma once

// The race view, what the show reads after each step and frame (Look.cpp, Camera.cpp, RaceEvents.cpp):
// the racers' looks and group spheres, the players' cameras, what each camera draws of the animated scenery, the
// material tracks, the HUDs, and the events raised for the show. Its operations take the world they run in.

#include "LRCore/Common/Api.h"
#include "LRCore/Race/Track.h"
#include "LRCore/Show/Show.h"
#include "LRCore/Show/View.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lr {

class Cache;
struct RaceBrief;
struct World;

// The race view: the racers' shapes and looks, the drivers' and chassis' animations and skeletons
// ([0] the driver's, then each chassis' and variant's by name), the look draws' index (an observer's onLook
// follows a captured run), the players' cameras and HUD gadgets, which cameras drew each animated entity this frame
// (bits), the material tracks' cursors and shown frames, the texture scrolls' offsets (1/65536 UV).
struct LRCORE_API RaceView {
	std::vector<RacerShape> shapes;
	std::vector<RacerLook> looks;
	List<Animation> figures;
	List<std::vector<SkeletonNode>> skeletons;
	std::vector<std::string> figureNames; // the part files' names, by figure
	uint32_t lookRandom = 0;
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
	List<MaterialState> materials;
	std::vector<std::array<uint16_t, 2>> scrollOffsets;
	// The HUDs, one per player camera, and the attract demo's DEMO blink; this frame's events for the
	// show, cleared as a frame begins, each tagged with the part of the frame raising it.
	std::vector<Hud> huds;
	std::vector<uint8_t> tinted; // by racer: an event's colour change is on it (the snow's camera hook reads it)
	int32_t demoMs = 0;
	std::vector<RaceEvent> raceEvents;
	RaceEvent::Stage stage = RaceEvent::Setup;

	// Its setup (the racers' shapes and figures, the cameras, the HUDs, the material tracks), from the bodies' models.
	void setup(World& w, const Cache& cache, const RaceBrief& race, const std::vector<std::string>& bodies);
	// A step's parts: the touch spheres against the racers' group spheres; each racer's visible models (its look, then
	// its group sphere); the players' cameras (a dt of 0: only a dirty one, snapped). A frame's: the material tracks'
	// cursors, what each camera draws of the animated scenery.
	void racerTouches(World& w);
	void visuals(World& w, int32_t dt);
	void updateGroup(World& w, size_t racer);
	void updateCameras(World& w, int32_t dt);
	void updateCamera(World& w, size_t player, int32_t dt);
	void advanceMaterials(World& w, int32_t ms);
	void drawScenery(World& w);
	// A player's view chosen; its camera, map and look-back keys (with its controls); a track's slots take its cursor's
	// frame.
	void chooseView(size_t player, uint8_t view);
	void viewKeys(World& w, size_t racer);
	void showMaterial(World& w, size_t track);
	// A racer's look draw; its hit mark; its car's visuals; a powerslide begun or ended (the drift's dust and skid
	// effects), the skid effects on or off.
	uint16_t lookDraw(World& w, size_t racer, bool cooldown);
	void markHit(size_t racer) { looks[racer].marks |= 2; }
	void carVisuals(World& w, size_t racer);
	void driftMarks(World& w, size_t racer);
	void skidEffects(World& w, size_t racer, bool on, RaceEvent::Reason why);
	// A racer's members' poses and bounds (shapes' order: body, variant, car, driver), world; false if absent.
	bool member(const World& w, size_t racer, RacerShape::Member which, Pose& pose, float& scale) const;

	// The events for the show (RaceEvents.cpp). A spawn of an emitter (set 0 the race's, 1 the
	// track's, 2 either) for an owner, at a place; a sound, a looping sound started at a place or the owner's loop of it
	// stopped; a reaction or a taunt (the show draws the line, keeps the cooldown).
	RaceEvent& raise(RaceEvent::Kind kind, Owner owner = {});
	void spawn(const char* emitter, uint8_t set, Owner owner, Vec3 at);
	void playSound(int32_t sound, Owner owner, const Vec3* at, float near = 30, float far = 300);
	void loopSound(int32_t sound, Owner owner, Vec3 at, float near = 30, float far = 300);
	void stopSound(int32_t sound, Owner owner);
	void voice(World& w, size_t racer, RaceEvent::VoiceLine line);
	// The HUDs' update after a frame: their clocks take the frame's real ms (not while paused,
	// nor outside the countdown, the racing and the finish), the banner, the place's pulse, the best lap, the speed.
	void updateHuds(World& w, int32_t realMs);

private:
	// A racer's look (the body's lean, the chassis' and driver's parts).
	void racerLook(World& w, size_t racer, int32_t dt);
};

} // namespace lr
