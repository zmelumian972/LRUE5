#pragma once

// What the driver and the body share: the kart's gravity and the steering parameters they both set.

#include "LRCore/Kart/Kart.h"

namespace lr {

constexpr float Gravity = 39.0f; // ft/s^2: times massFactor (mass * 1e-6), a force in ft/ms^2

// Steering: the yaw is scaled by gain, the skid takes `slip` of the grip, and the heading may lag the nose by `lag`.
inline void beginTurn(Kart& k, float gain, float slip, float lag)
{
	k.steerGain = gain;
	k.skidShare = slip;
	k.lagLimit = lag;
	k.flags |= KartSteering;
	k.steerAlignMin = tableCos(lag);
}

// The gain and slip are kept (a later spin's skid uses the last slip).
inline void endTurn(Kart& k)
{
	k.flags &= ~(KartSteering | KartSteeringAux);
	k.steerAlignMin = -1;
	k.lagLimit = Pi;
}

} // namespace lr
