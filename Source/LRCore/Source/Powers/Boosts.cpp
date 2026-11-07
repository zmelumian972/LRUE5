// The green actions (the turbo, the warp) and the blue one (the shield).

#include "Powers/ActionParts.h"
#include "Show/SoundIds.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

// A turbo: its ignition (the car's tilt, longer above level 0), its fade, its level 2 smoke before its end; a hovering
// one ends early once the car is this slow.
constexpr int32_t TurboIgniteMs = 400, TurboFadeMs = 700, TurboSmokeMs = 800, TurboHoverMs = 4500;
constexpr float TurboTilt = -0.0025f;
constexpr int32_t TurboTiltMs = 125, StrongTurboTiltMs = 250;
constexpr float TurboStopped = 0.04f;
// A warp: its start (without a target), its flight (along the course in ft per ms, or to its target over the
// flight: 1 / its ms), its landing found from 5 ft over to 50 ft under, 5 ft above it, launched on.
constexpr int32_t WarpStartMs = 500, WarpFlightMs = 1500;
constexpr float WarpPathSpeed = 0.6f, WarpTargetRate = 0.00066666666f;
constexpr float WarpLift = 5, WarpLaunch = 700;
constexpr int32_t ShieldFadeMs = 1000;

// The checkpoint a racer heads for: the one after its last (the first if it has none), -1 if the track has none.
int32_t headingFor(const World& w, size_t racer)
{
	const int16_t c = w.racers.race[racer].checkpoint;
	if (w.track.checkpoints.empty()) {
		return -1;
	}
	return c < 0 ? 0 : nextCheckpoint(w.track.checkpoints[c]);
}

void startTurbo(World& w, Action& a, int32_t level, const Aim*)
{
	if (!takeSlots(w, a, 3)) {
		a.state = Action::Done;
		return;
	}
	const size_t r = size_t(a.owner);
	if (w.racers.drivers[r].flags & DriverTurbo) {
		w.powerUps.cancelTurbo(r);
		w.powerUps.turboOff(w, r);
		w.powerUps.resume(w, r);
	}
	a.ms = TurboIgniteMs;
	a.smoked = false;
	w.powerUps.tilt(w, r, TurboTilt, level == 0 ? TurboTiltMs : StrongTurboTiltMs);
	w.powerUps.turboOn(w, r, level);
	if (level == 2) {
		w.powerUps.halt(w, r);
	}
	w.raceView.playSound(TurboStartSounds + level, ownerOf(w, a), &w.racers.visible[r].position);
	w.raceView.playSound(WhooshSound, ownerOf(w, a), &w.racers.visible[r].position);
	a.state = TurboIgnite;
}

void turboFrame(World& w, Action& a, int32_t ms)
{
	auto advance = [&] {
		if (a.state == TurboIgnite) {
			static constexpr uint32_t boosts[] = {1000, 1500, 5000};
			a.state = TurboBoosting;
			a.ms = boosts[std::min<uint8_t>(a.level, 2)];
			if (a.owner >= 0) {
				w.raceView.voice(w, size_t(a.owner), RaceEvent::Positive);
			}
		}
		else if (a.state == TurboBoosting) {
			a.state = TurboFading;
			a.ms = TurboFadeMs;
		}
		else {
			a.state = Action::Done;
			if (a.owner >= 0) {
				// Its end: level 0 and 1 softly, 2 its own.
				w.raceView.playSound(a.level == 2 ? StrongTurboEndSound : TurboEndSound, ownerOf(w, a),
					&w.racers.visible[a.owner].position);
				w.raceView.raceEvents.back().volume = a.level == 0 ? 0.6f : a.level == 1 ? 0.8f : 1.0f;
			}
		}
	};
	if (timeUp(a, ms)) {
		advance();
	}
	// A level 2 turbo's smoke, as it nears its end.
	if (a.level == 2 && a.state == TurboBoosting && a.ms < TurboSmokeMs && !a.smoked && a.owner >= 0) {
		a.smoked = true;
		w.raceView.spawn("trbsmke", 0, ownerOf(w, a), w.racers.visible[a.owner].position);
	}
	// A hovering turbo that has stopped for good ends early.
	if (a.owner >= 0 && (w.racers.race[a.owner].flags & RacerHalted) && a.state == TurboBoosting && a.ms < TurboHoverMs) {
		const float speed = w.racers.karts[a.owner].speed;
		if (speed > -TurboStopped && speed < TurboStopped) {
			advance();
		}
	}
}

void startWarp(World& w, Action& a, int32_t, const Aim* aim)
{
	const size_t r = size_t(a.owner);
	if (w.racers.race[r].flags & (RacerGhost | RacerWarping)) {
		a.state = Action::Done;
		return;
	}
	a.hasTarget = aim != nullptr;
	if (aim) {
		a.point = aim->position;
		a.direction = aim->direction;
	}
	a.ms = aim ? 0 : WarpStartMs;
	a.state = WarpStarting;
	w.powerUps.dropMagnetGrip(r);
	a.demo = w.racers.race[r].ai;
	w.racers.race[r].flags |= RacerWarping;
}

// Released: a turbo's racer back to its own speed, a warp's car to its own size.
void releaseTurbo(World& w, Action& a)
{
	if (a.owner >= 0) {
		w.powerUps.turboOff(w, size_t(a.owner));
		w.powerUps.resume(w, size_t(a.owner));
	}
}

void releaseWarp(World& w, Action& a)
{
	if (a.owner >= 0) {
		w.racers.powers[a.owner].carScale = 1;
	}
	a.followsPath = false;
}

// Along the checkpoints' gates from x by d ft, heading for checkpoint c.
Vec3 alongPath(const World& w, Vec3 x, float d, int32_t c)
{
	while (d > 0 && c >= 0) {
		const Vec3 to = w.track.checkpoints[c].position;
		const Vec3 delta = x - to;
		const float r = float(std::sqrt(double(dotZYX(delta, delta))));
		if (r < d) {
			d -= r;
			x = to;
		}
		else {
			x = {x.x + ((to.x - x.x) / r) * d, x.y + ((to.y - x.y) / r) * d, x.z + ((to.z - x.z) / r) * d};
			d = 0;
		}
		c = nextCheckpoint(w.track.checkpoints[c]);
	}
	return x;
}

void warpFrame(World& w, Action& a, int32_t ms)
{
	const size_t r = size_t(a.owner);
	if (a.state == WarpActive) {
		bool moved = false;
		Vec3 x;
		if (a.hasTarget) {
			const float f = float(int32_t(a.ms)) * WarpTargetRate;
			const Vec3 t = a.point, s = a.centre;
			x = {t.x - (t.x - s.x) * f, t.y - (t.y - s.y) * f, t.x - (t.z - s.z) * f}; // QUIRK: z from the target's x
			moved = true;
		}
		else if (a.followsPath) {
			x = alongPath(w, w.racers.visible[r].position, WarpPathSpeed * float(ms), headingFor(w, r));
			moved = true;
		}
		if (moved) {
			w.racers.visible[r].position = x;
			w.racers.karts[r].position = x;
		}
	}
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == WarpStarting) {
		a.state = WarpActive;
		a.ms = WarpFlightMs;
		w.racers.race[r].flags &= ~RacerWarping;
		w.powerUps.enterGhost(w, r);
		w.racers.powers[r].carScale = 1;
		w.powerUps.endPush(w, r, 0);
		w.powerUps.endPush(w, r, 1);
		a.centre = w.racers.visible[r].position; // where it starts from
		a.followsPath = !a.demo;
		if (!w.racers.race[r].ai) {
			w.raceView.playSound(WarpOutSound, ownerOf(w, a), nullptr);
		}
		w.raceView.playSound(WhooshSound, ownerOf(w, a), &a.centre, 200, 600);
		return;
	}
	// Arrive: on the ground, facing the course (or the target's way), launched forward.
	Vec3 at = a.hasTarget ? a.point : w.racers.visible[r].position;
	SegmentHit hit;
	if (w.track.query(w.track.trackVolume, at + Vec3{0, 0, WarpLift}, at - DropProbe, hit)) {
		at = hit.point;
	}
	at.z += WarpLift;
	Kart& k = w.racers.karts[r];
	w.racers.visible[r].position = k.position = at;
	if (!a.demo) {
		Vec3 dir{1, 0, 0};
		const int32_t c = headingFor(w, r);
		if (a.hasTarget) {
			dir = a.direction;
		}
		else if (c >= 0 && w.racers.race[r].checkpoint >= 0) {
			dir = -w.track.checkpoints[c].normal;
		}
		w.racers.visible[r].rotation = k.rotation = basisFromForwardUp(dir, {0, 0, 1});
		k.velocity = {};
		if (w.racers.playback[r].active) {
			routeImpulse(w.racers.playback[r], k, dir, WarpLaunch);
		}
		else {
			k.velocity += dir * (w.racers.params[r].invMass * WarpLaunch);
			snapHeading(k);
		}
	}
	a.state = Action::Done;
	w.powerUps.leaveGhost(w, r);
	if (!w.racers.race[r].ai) {
		w.raceView.playSound(WarpInSound, ownerOf(w, a), nullptr);
	}
}

// --- Blue ---

void startShield(World& w, Action& a, int32_t level, const Aim*)
{
	if (!takeSlots(w, a, 2)) {
		a.state = Action::Done;
		return;
	}
	const size_t r = size_t(a.owner);
	if (w.racers.race[r].flags & RacerShielded) {
		w.powerUps.cancelShield(r);
	}
	static constexpr uint32_t lasts[] = {4000, 6000, 8000, 10000};
	a.ms = lasts[std::clamp(level, 0, 3)];
	a.state = ShieldActive;
	w.powerUps.shieldOn(w, r, level);
	if (w.racers.race[r].flags & RacerCursed) {
		w.powerUps.curseOff(w, r);
	}
}

void shieldFrame(World& w, Action& a, int32_t ms)
{
	if (!timeUp(a, ms)) {
		return;
	}
	if (a.state == ShieldActive) {
		a.state = ShieldFading;
		a.ms = ShieldFadeMs;
	}
	else {
		a.state = Action::Done;
		w.racers.race[a.owner].flags &= ~RacerShielded;
		const Vec3 at = w.racers.visible[a.owner].position + Vec3{0, 0, 5};
		w.raceView.playSound(ShieldEndSound, ownerOf(w, a), &at);
	}
}

} // namespace

const ActionKind TurboAction{.start = startTurbo, .frame = turboFrame, .release = releaseTurbo, .framesWhenDone = true};
const ActionKind WarpAction{.start = startWarp, .frame = warpFrame, .release = releaseWarp};
const ActionKind ShieldAction{.start = startShield, .frame = shieldFrame};

} // namespace lr
