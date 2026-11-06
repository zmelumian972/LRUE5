#pragma once

// The kart body's solver (Body.cpp: forces, integration, heading, walls and the step; Ground.cpp: the wheel probes
// that find the ground and snap the body onto it), and what its two halves share.

#include "LRCore/Kart/Kart.h"
#include "Kart/Steering.h"

#include <algorithm>
#include <array>
#include <vector>

namespace lr {

struct SegmentHit;

// The wheels' probes: the box's height over the ground, how far a probe reaches per ms of the step.
constexpr float GroundClearance = 0.2f;
constexpr float ProbeDepthPerMs = 0.04f;
constexpr float ContactBand = 0.4f; // wheels this far below the highest hit still touch
constexpr float DropOnLoss = 0.008f; // the instant fall when the ground is lost
// A hard landing bounces back by this share of its speed into the ground (on wheels, hovering); on wheels, only after
// this long in the air.
constexpr float WheelBounce = 1.15f, HoverBounce = 1.3f;
constexpr int32_t BounceAirborneMs = 400;

// v moved toward target by at most step.
inline float easeToward(float v, float target, float step) { return v < target ? std::min(v + step, target) : std::max(v - step, target); }
// A yaw kick: the spin about up replaced by `rate` for `ms`.
void yawKick(Kart& k, const KartParams& p, float rate, int32_t ms);

// A kart's body for one call (stepBody, saveBody, sweepBody): the kart, its parameters, the track it runs on.
class Body {
public:
	Body(Kart& kart, const KartParams& params, const Track& track, std::vector<SweepHit>* hits = nullptr,
		std::vector<std::array<int16_t, 4>>* surfaces = nullptr)
		: m_kart(kart)
		, m_params(params)
		, m_track(track)
		, m_hits(hits)
		, m_surfaces(surfaces)
	{
	}

	void step(int32_t dt, Pose* drawn);
	void save();
	void restore();
	int32_t sweep(int32_t dt, bool queryTrack);

private:
	float gravityForce() const { return -(Gravity * m_params.massScale); } // negative: down

	void cancelPositiveAbout(Vec3 direction, Vec3 point)
	{
		const Vec3 axis = normalize(cross(point - m_kart.centerOfMassWorld, direction));
		const float along = dot(m_kart.angularMomentum, axis);
		if (along >= 0) {
			m_kart.angularMomentum -= axis * along;
		}
	}
	void yawImpulse(float rate, int32_t ms) { yawKick(m_kart, m_params, rate, ms); }
	void stopSteering() { lr::stopSteering(m_kart); }

	void addForce(Vec3 f) { m_forceSum += f; }
	void addForceAt(Vec3 f, Vec3 at) { m_torqueSum += cross(at - m_kart.centerOfMassWorld, f); }

	void accumulateForces();
	void groundForces(float align);
	void integrate(float dt);
	void limitTilt();
	void placeWheels();
	void consider(int i, int& best, float& bestDepth2) const;
	void probeVolumes(int probes, float above, float below, int& best, float& bestDepth2);
	void probeTrack(int probes, float above, float below, int& best, float& bestDepth2);
	void setSurface(int probes, int i, int16_t surface);
	bool wake();
	int probe(int probes, float above, float below, int32_t previous, float& bestDepth2);
	void touchBand(int probes, float bestDepth2);
	void weigh(int probes, bool landing, float bounce);
	void probeGround(int32_t dt);
	void probeHover(int32_t dt);
	bool cachedGround(int wheel, Vec3 a, Vec3 b);
	void insertPlane(int wheel, const SegmentHit& hit);
	void snapToGround(int best);
	void turnHeadingToward(Vec3 target, float dt);
	void updateHeading(int32_t dt);
	void updateWheelSurfaces();
	void substep(int32_t dt);

	bool acceptsWall(const SegmentHit& hit, Vec3 worldNormal, Vec3 worldPoint) const;
	void bounceOffWall();
	void runPieces(int32_t time);

	Kart& m_kart;
	const KartParams& m_params;
	const Track& m_track;
	std::vector<SweepHit>* m_hits;
	std::vector<std::array<int16_t, 4>>* m_surfaces;
	Vec3 m_forceSum, m_torqueSum;
};

} // namespace lr
