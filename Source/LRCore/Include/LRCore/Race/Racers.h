#pragma once

// Every racer in a race and the race's rules over them (Race.cpp): who drives (the players, the AI returning to a route
// or braking), what moves (bodies, routes), karts against karts and rolling rocks, the checkpoint gates and lap marks,
// the racers' timers (the AI's use of its brick, taunts, wrong way), the standings and the rubber band. Each racer's
// parts are in parallel arrays (index = racer), so a step walks flat arrays. Its operations take the world they run in.

#include "LRCore/Common/Api.h"
#include "LRCore/Kart/Kart.h"
#include "LRCore/Powers/Powers.h"
#include "LRCore/Race/Route.h"
#include "LRCore/Race/Track.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

struct World;

enum RacerFlag : uint32_t {
	RacerShielded = 1u << 0,
	RacerOnGrid = 1u << 1,
	RacerHalted = 1u << 3, // held hovering (a level-2 turbo, a magnet, the grabber)
	RacerGhost = 1u << 4, // warping: passes walls and racers, takes nothing
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
	RacerBoxRamp = 1u << 28, // in contact this step
	RacerGrabbed = 1u << 29, // the rubber band leaves it alone
};

// A racer's race: what it took, its laps and times, and its timers.
struct Racer {
	uint32_t flags = RacerOnGrid; // RacerFlag
	bool player = false; // the slot's kind
	bool ai = false; // driven by the AI (an AI's slot, a finished player, racer 0 on autopilot)
	int16_t checkpoint = -1; // the last gate taken
	int32_t checkpointCount = -1; // forward passes of the line's gate
	bool forward = true; // the last gate's direction
	LapMark::Zone zones[3] = {LapMark::Open, LapMark::Past, LapMark::Line}; // now, before, before that
	int32_t laps = 0, lineCrossings = -1;
	int32_t lapMs[5] = {};
	int32_t standing = 0; // 1: leading
	int32_t place = -1; // the finishing order, from 0
	int32_t timeBehind = 0, timeBehindMs = 0; // behind the racer ahead at the last lap, and how long it shows
	int32_t wrongWayMs = 0;
	int32_t tauntMs = 0; // an AI taunts (an event) a racer close in front when this runs out
	int32_t boxMs = 0;
	float boxScale = 1; // the contact box's: shrinks two AI karts in contact apart
};

// Two karts that touched this step (the first is moved out of the second).
struct Contact {
	uint8_t first, second;
	Vec3 normal; // toward the first
	float penetration;
	Vec3 point;
	float impulse; // on the first along the normal (the second gets its opposite)
};

// Every racer's parts, in parallel arrays (index = racer), sized together; a step's contact order and scratch.
struct LRCORE_API Racers {
	List<Racer> race;
	std::vector<KartParams> params;
	List<Kart> karts;
	std::vector<Driver> drivers;
	std::vector<Controls> controls;
	std::vector<uint16_t> keys; // the players' keys this frame (Key)
	std::vector<uint16_t> lastKeys; // and last frame
	List<Pose> visible; // the car as drawn; a route kart's is set by its route, the body follows
	std::vector<Playback> playback;
	List<Power> powers;
	std::vector<Sphere> carBounds; // in the car's frame: the visible car's bounds
	std::vector<Sphere> bodyBounds; // in the car's frame: its body model's (what lightning strikes)
	List<Sphere> groups; // the whole racer's (body, car, driver), world, as its visible models last updated
	bool groupsStale = false; // not yet updated since the setup
	std::vector<int32_t> worldMs; // racers x volumes: a dynamic world's time left
	std::vector<uint8_t> contactOrder; // racers and active rocks (RockEntry + hazard), by their sphere's lowest x
	static constexpr uint8_t RockEntry = 128;
	std::vector<SweepHit> hits; // scratch
	std::vector<Contact> contacts; // this step's (that had a response)
	// n racers on a track of so many volumes (what an array has keeps).
	void resize(size_t n, size_t volumes);

	// A frame's parts: the dynamic worlds' timers, a player's controls. A step's, in their order: each racer driven,
	// then moved; the contacts; the racers' timers; the standings; the rubber band.
	void countDownWorlds(World& w, int32_t frameMs);
	void playerControls(World& w, size_t racer, int32_t frameMs);
	void driveRacer(World& w, size_t racer, int32_t dt);
	void moveRacer(World& w, size_t racer, int32_t dt);
	void contactSweep(World& w);
	void racerTimers(World& w, int32_t dt);
	void updateStandings(World& w);
	void pace(World& w, int32_t dt);
	// The race's rules, called from the steps (and the tests): a gate taken, a lap mark passed, a racer finished,
	// handed to the AI, its standing set.
	void crossGate(World& w, size_t racer, int16_t checkpoint, bool forward);
	void lapMark(World& w, size_t racer, LapMark::Zone zone);
	void finish(World& w, size_t racer);
	void handToAi(World& w, size_t racer);
	void setStanding(World& w, size_t racer, int32_t standing);
	// The response: the first moved out, both pushed apart (sets the impulse). False if a ghost or a tumbling kart
	// takes no part.
	bool contact(World& w, Contact& contact);
	// How far round a racer is; its car's and body's spheres, world.
	float progress(const World& w, size_t racer) const;
	Sphere carSphere(size_t racer) const;
	Sphere bodySphere(const World& w, size_t racer) const;

private:
	// What a racer's sweep met (hits); racerTimers' parts.
	void applyHits(World& w, size_t racer);
	int32_t timeBehind(const World& w, size_t racer) const;
	void tauntNearby(World& w, size_t racer);
	void updateWrongWay(World& w, size_t racer, int32_t dt);
	// The contacts' parts: a racer moved out, a kart bounced off a rolling rock's box; a racer's group sphere.
	void moveOut(World& w, size_t racer, Vec3 move);
	void hitRock(World& w, size_t racer, size_t hazard);
	// A racer pushed along n by an impulse (a route kart through its playback).
	void push(size_t racer, Vec3 n, float impulse);
	Sphere groupSphere(size_t racer) const;
	// The contact boxes of two karts: true and the normal (toward the first), penetration and point if they overlap.
	bool boxContact(size_t first, size_t second, Vec3& normal, float& penetration, Vec3& point) const;
	// A body step's wheel surfaces, substep by substep (scratch).
	std::vector<std::array<int16_t, 4>> m_wheelSurfaces;
};

} // namespace lr
