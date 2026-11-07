#pragma once

// Route playback: how AI karts move. A route is a racing line recorded while someone drove the track; a kart in route
// mode is posed along it at a time that runs at a playback rate (1 = as recorded), which the rubber band, bumps and
// power-ups push up and down. It is never simulated by the body, but its corners are swept against the worlds past
// the track (the checkpoint gates, the dynamic worlds), and a wall there sends it back along its line.
//
// A cursor is a place on the route: the segment it is in (between two points), the time, and the pose, widths and
// ground contacts interpolated there.

#include "LRCore/Common/Api.h"
#include "LRCore/Kart/Kart.h"
#include "LRData/Index.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace lr {

class Cache;
class Track;

struct LRCORE_API Route {
	struct Point {
		Vec3 offset; // from the previous point
		Quat rotation;
		float left, right; // the free width beside the line (ft)
		int32_t length; // ms: the segment that ends at this point
		int32_t contacts; // ground contacts while on it
	};
	Vec3 start, loop; // the start pose, and the pose at the end of the loop point's segment: where laps go on
	Quat startRotation, loopRotation;
	int32_t loopTime, loopPoint;
	List<Point> points;

	// "GAMEDATA/RACEC0R1/R1_F_0.RRB". False if it is missing or malformed.
	[[nodiscard]] bool load(const Cache& cache, std::string_view path, bool mirrored);
};

struct LRCORE_API RoutePlace {
	Vec3 position;
	Quat rotation;
	int32_t contacts = 0;
	float left = 0, right = 0;
	float rate = 1;
	float time = 0; // ms along the route
	int32_t a = -1, b = 0; // the segment's start and end points (a = -1: before the first point)
	int32_t ta = 0; // the segment's start time
	Vec3 pa, pb;
	Quat qa, qb;

	void attachAtStart() { *this = {}; }
	// At the end of the loop point's segment. qb is left as it was.
	void joinAtLoop(const Route& route);
	// time += rate * ms, then the pose there.
	void advance(const Route& route, float ms);
	// To where a kart moved by `moved` would be along the line; moved keeps what the line didn't take. False (and no
	// change) past the last point.
	bool seek(const Route& route, Vec3& moved);
};

// A racer's route playback.
struct Playback {
	RoutePlace cursor;
	int32_t route = -1; // the slot whose route it has; -1 none
	bool active = false; // in route mode (for good, once in)
	bool paused = false; // until the race starts
	float sink = 0, hop = 0, hopVelocity = 0, side = 0; // side: along the kart's left
	float spinRate = 0, spinAngle = 0;
	float tiltRate = 0, tiltAngle = 0, tiltHeight = 0;
	float baseRate = 1, targetRate = 1;
};

// Route mode from the route's start (paused) or its loop. The visible pose and the body follow the cursor.
LRCORE_API void attachAtStart(Playback& play, Kart& kart, Pose& visible, const KartParams& params, const Route& route, int32_t slot);
LRCORE_API void joinAtLoop(Playback& play, Kart& kart, Pose& visible, const KartParams& params, const Route& route, int32_t slot);

// One step of dt ms: the rate, the cursor, the motion (side offset, hop, lift, tilt, spin) into the visible pose, the
// velocity, and the sweep of the worlds past the track. What the sweep met is appended to hits.
LRCORE_API void stepPlayback(Playback& play, Kart& kart, Pose& visible, const KartParams& params, const Track& track, const Route& route,
	int32_t dt, std::vector<SweepHit>* hits);

// A bump: the kart is pushed along its line (dir, magnitude) and moved by `moved` (along the line, then sideways
// within its widths).
LRCORE_API void routeImpulse(Playback& play, const Kart& kart, Vec3 dir, float magnitude);
LRCORE_API void routeMove(Playback& play, Kart& kart, Pose& visible, const KartParams& params, const Route& route, Vec3 moved);
// The rubber band's pace (and a magnet's hold, which stops the kart).
inline void routePace(Playback& play, const Kart& kart, float pace)
{
	play.baseRate = pace;
	if (!(kart.flags & KartPushed)) {
		play.targetRate = pace;
	}
}

// What power-ups and hazards do to a route kart: the route side of the body's calls (Kart.h).
LRCORE_API void routeSpin(Playback& play, Kart& kart, float turns, float rate);
LRCORE_API void routeSpinOut(Playback& play, Kart& kart);
LRCORE_API void routeTilt(Playback& play, Kart& kart, float rate, int32_t ms);
LRCORE_API void routePush(Playback& play, Kart& kart, Vec3 push);
LRCORE_API void routeEndPush(Playback& play, Kart& kart);
// A boost (or its end) and a curse's slowing: the rates only (the body's part is startBoost's).
LRCORE_API void routeBoost(Playback& play, Kart& kart, bool on);
LRCORE_API void routeCurse(Playback& play, Kart& kart, bool on);
// A warping AI flies its route fast, hovering.
LRCORE_API void routeGhost(Playback& play, Kart& kart, bool on);
LRCORE_API void routeHover(Playback& play, Kart& kart, bool on);

} // namespace lr
