// The driver (steering and throttle into a turn radius, a thrust and steering parameters) and the player's controls.

#include "LRCore/Kart/Kart.h"
#include "Kart/Steering.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

constexpr float FullThrust = 54.0f;
constexpr float MaxRadius = 4096.0f, MinRadius = 40.0f;
// The stick's turn (1 / radius) from the slightest to full lock.
constexpr float WidestCurvature = 0.00025f, TightestCurvature = 0.025f;
// Steering starts only with the nose this square to the motion (unless already steering) and below this speed.
constexpr float SteerAlign = 0.89999998f, SteerMaxSpeed = 0.155f;
// A powerslide needs the nose this square to the motion and this heading speed.
constexpr float SlideAlign = 0.85000002f, SlideMinSpeed = 0.050000001f;
constexpr float MinSlideTightness = 0.050000001f; // a slide's least tightness (its steering gain less one)

// The tightest turn the wheels in contact can hold at this speed.
float tightestTurn(const Kart& k, const KartParams& p)
{
	float grip = 0;
	int touching = 0;
	for (const Wheel& w : k.wheels) {
		if (w.contact) {
			grip += w.grip;
			touching++;
		}
	}
	grip = touching ? grip / float(touching) * k.gripScale : 0;
	const float hold = grip * (Gravity * p.massScale);
	if (hold == 0) {
		return k.contacts == 0 ? 100.0f : MaxRadius;
	}
	return ((k.headingSpeed * k.headingSpeed) * p.mass) / hold;
}

bool canSteer(const Kart& k, float radius)
{
	const float align = dot(k.rotation.f, k.velocityDirection);
	return !(align <= 0 || (radius < 0 && k.turnRadius > 0) || (radius > 0 && k.turnRadius < 0) || (!(k.flags & KartSteering) && align < SteerAlign) || k.wallHits != 0 ||
		k.speed > SteerMaxSpeed);
}

bool canPowerslide(const Kart& k)
{
	return !(k.wallHits != 0 || (!(k.flags & KartHover) && k.contacts < 3) || dot(k.rotation.f, k.velocityDirection) <= SlideAlign || k.headingSpeed < SlideMinSpeed);
}

float clampRadius(float r)
{
	if (r > 0) {
		return r < MinRadius ? MinRadius : r <= MaxRadius ? r : 0;
	}
	if (r < 0) {
		return r > -MinRadius ? -MinRadius : r >= -MaxRadius ? r : 0;
	}
	return 0;
}

void setThrust(Kart& k, const KartParams& p, float thrust)
{
	k.thrust = thrust * p.accelerationScale;
	k.dragCoefficient = (p.mass * std::fabs(k.thrust)) / (k.maxSpeed * k.maxSpeed);
}

} // namespace

void steerInput(Driver& d, const KartParams& p, float steer)
{
	const float x = ((d.flags & DriverCursed) ? -steer : steer) * p.handlingScale;
	const float q = (TightestCurvature - WidestCurvature) * x;
	d.radius = x > 0 ? 1 / (WidestCurvature + q) : x < 0 ? 1 / (q - WidestCurvature) : 0;
}

void throttleInput(Driver& d, const Kart& k, float y)
{
	if (d.flags & DriverCursed) {
		y = std::clamp(y + d.curseThrottle, -1.0f, 1.0f);
	}
	const float fs = k.headingSpeed;
	const bool against = (y < 0 && fs > 0) || (y > 0 && fs < 0); // braking: twice the force
	d.thrust = against ? (FullThrust * y) * 2 : FullThrust * y;
}

void startPowerslide(Driver& d, Kart& k, bool tight)
{
	if (canPowerslide(k)) {
		d.flags |= DriverSliding;
		d.slideMs = 0;
		d.tightSlide = tight;
		k.flags |= KartPowerslide;
		k.slideFactor = tight ? 0.4f : 0.3f;
		d.drifting = true;
	}
}

void endPowerslide(Driver& d, Kart& k)
{
	d.flags &= ~DriverSliding;
	stopSteering(k);
	d.slideMs = 0;
	k.flags &= ~KartPowerslide;
	k.slideFactor = 0.2f;
}

void stepDriver(Driver& d, Kart& k, const KartParams& p, int32_t dt)
{
	d.prevRadius = d.radius;
	if (d.flags & DriverSliding) {
		if (k.headingSpeed < 0.03f) {
			endPowerslide(d, k);
		}
		else {
			d.slideMs += dt;
		}
	}
	if ((d.flags & DriverTurbo) && k.wallHits != 0) {
		d.flags |= DriverTurboWeak;
	}

	applyDriver(d, k, p);
}

void applyDriver(Driver& d, Kart& k, const KartParams& p)
{
	if (d.radius == 0 && d.prevRadius == 0) {
		k.turnRadius = 0;
		stopSteering(k);
	}
	else {
		if (d.radius != 0 && k.speed < 0.04f) {
			d.radius *= 0.2f;
		}
		const float m = std::fabs(d.radius);
		const float tightest = tightestTurn(k, p);
		if (d.flags & DriverSliding) {
			if (!canSteer(k, d.radius) || m <= 0) {
				stopSteering(k);
			}
			else {
				const float j = std::max(MinSlideTightness, 1 - m / tightest);
				if (d.tightSlide) {
					startSteering(k, j + j + 1, 0.25f, Pi);
				}
				else {
					startSteering(k, j + 1, 0.85f, Pi);
				}
				if (m < tightest) {
					d.radius = std::copysign(((tightest - m) * 0.5f) * 0.5f + m, d.radius);
				}
			}
		}
		else {
			if (canSteer(k, d.radius) && m > 0 && m < tightest) {
				startSteering(k, (1 - m / tightest) + 1, 0.85f, 0.70709997f);
			}
			else {
				stopSteering(k);
			}
			if (m < tightest) {
				d.radius = std::copysign((tightest + m) * 0.5f, d.radius);
			}
		}
		k.turnRadius = clampRadius(d.radius);
	}

	const float thrust = (d.flags & DriverTurbo) ? ((d.flags & DriverTurboWeak) ? 216.0f : 432.0f) : d.thrust;
	setThrust(k, p, thrust);
}

void updateControls(Controls& c, uint16_t keys, bool blocked, int32_t frameMs, Driver& d, Kart& k, const KartParams& p)
{
	constexpr float Return = 8.25f;
	const bool left = keys & KeyLeft, right = keys & KeyRight;
	const float rate = d.drifting ? (d.tightSlide ? 16.0f : 5.0f) : (left || right) ? 1.25f : 2.5f;
	float upper = 1, lower = -1, delta = 0;
	float& s = c.steering;
	if (left && !right) {
		delta = s < 0 ? Return : rate;
	}
	else if (right && !left) {
		delta = s > 0 ? -Return : -rate;
	}
	else if (left && right) {
		if (s > 0) {
			delta = -Return;
			lower = 0;
		}
		else if (s < 0) {
			delta = Return;
			upper = 0;
		}
		else {
			lower = 0;
		}
	}
	if (delta == 0) {
		// Back to the centre.
		if (s > 0) {
			delta = -Return;
			lower = 0;
		}
		else if (s < 0) {
			delta = Return;
			upper = 0;
		}
		else {
			upper = lower = 0;
		}
	}
	s += delta * (float(frameMs) * 0.001f);
	if (s > upper) {
		s = upper;
	}
	if (s < lower) {
		s = lower;
	}

	c.sentSteering = c.sentThrottle = -2;
	c.startedDrift = -1;
	c.endedDrift = 0;
	if (blocked) {
		return;
	}
	steerInput(d, p, s);
	c.sentSteering = s;

	const float forward = (keys & KeyThrottle) ? 1.0f : 0.0f, back = (keys & KeyBrake) ? -1.0f : 0.0f;
	float y = forward + back;
	if (forward > 0 && (keys & KeyDrift)) {
		y = 1;
		if (s == 0) {
			endPowerslide(d, k);
			c.endedDrift++;
		}
		else if (!d.drifting) {
			c.startedDrift = back < 0 ? 1 : 0;
			startPowerslide(d, k, back < 0);
		}
	}
	else if (d.drifting) {
		endPowerslide(d, k);
		c.endedDrift++;
	}
	if (!d.drifting && back < 0 && forward > 0) {
		y = 0.5f; // brake and throttle together
	}
	throttleInput(d, k, y);
	c.sentThrottle = y;
}

} // namespace lr
