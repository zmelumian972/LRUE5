#pragma once

// The parts of the body that route playback shares (Body.cpp): a route kart's pose is set by its route, but walls of
// the worlds past the track are swept, and backed off from, the body's way.

#include "LRCore/Kart/Kart.h"

namespace lr {

// The world inverse inertia from the rotation and the body-frame diagonal.
Basis worldInverseInertia(const Basis& rotation, Vec3 invInertia);
// The state a wall hit backs up to.
void saveBody(Kart& kart, const KartParams& params, const Track& track);
// The body points swept from the save to the pose now; on a wall the save is restored and the time to re-run
// (short of the hit) returned, else 0.
int32_t sweepBody(Kart& kart, const KartParams& params, const Track& track, int32_t dt, bool queryTrack, std::vector<SweepHit>* hits);

} // namespace lr
