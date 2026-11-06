#pragma once

// The race's events and trigger list (Events.cpp): events started, ended or fired at a place or for a racer, reaching
// the event table's resources and then the hazards; the trigger list's members and the step queue's timed waits, which
// share its 100 entries; the touch spheres and positional spheres. Its operations take the world they run in.

#include "LRCore/Common/Api.h"
#include "LRCore/Powers/Hazards.h"
#include "LRCore/Powers/Powers.h"

#include <cstdint>
#include <vector>

namespace lr {

struct World;

// The trigger list: members in stable slots, the list's order, and the step queue's waits (newest first); the touch
// spheres (Track::touchSpheres): racers inside now and last frame, by bit, their events at their centre.
struct LRCORE_API Triggers {
	List<Member> members;
	std::vector<uint16_t> memberOrder, freeMembers;
	std::vector<Wait> waits;
	size_t waitsWalked = 0; // scratch: those still in this step's walk
	std::vector<uint8_t> touchInside, touchLast;
	std::vector<PlaceState> touchPlaces, places; // and the positional spheres' (Track::places)

	// Events started, ended or both at once, at a place (none: nullptr).
	void startAt(World& w, int32_t event, const Vec3* at);
	void endAt(World& w, int32_t event, const Vec3* at);
	void fireAt(World& w, int32_t start, int32_t end, const Vec3* at);
	// Both at once for a racer.
	void fireFor(World& w, int32_t start, int32_t end, size_t racer);
	// The trigger list's members and the step queue's waits.
	int16_t addMember(World& w, Member member);
	void killMember(int16_t& member);
	bool schedule(World& w, Wait wait);
	// The frame's parts: the positional and touch spheres' frame, the event table (then the hazards and the track
	// timers). A step's: the waits, the racers against the touch spheres (world spheres, by racer).
	void endTouchFrame(World& w);
	void eventFrame(World& w, int32_t frameMs);
	void runWaits(World& w, int32_t dt);
	void touchRacers(World& w, const Sphere* spheres);

private:
	// Events for a racer, an event's resources force-ended, a racer met a touch sphere.
	void startFor(World& w, int32_t event, size_t racer);
	void endFor(World& w, int32_t event, size_t racer);
	void forceEnd(World& w, int32_t event);
	void touchSphere(World& w, size_t sphere, size_t racer);
};

} // namespace lr
