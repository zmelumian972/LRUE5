// What power-ups and hazards do to a racer: its own states (halted, ghost, turbo, shield, curse, shove, magnet hold)
// and the body's calls they make, each on a physics kart's body or on a route kart's playback; what shields and curses
// do when two karts touch.

#include "LRCore/Race/World.h"
#include "Kart/Steering.h"
#include "Show/SoundIds.h"

namespace lr {

constexpr int32_t ShoveMs = 750, SpinOutMs = 600;
constexpr float ShoveGain = 2.5f, ShoveSlip = 0.9f; // a shoved kart's steering

void PowerUps::halt(World& w, size_t i)
{
	Racer& r = w.racers.race[i];
	if (!(r.flags & RacerHalted)) {
		r.flags |= RacerHalted;
		if (w.racers.playback[i].active) {
			w.racers.karts[i].flags &= ~(KartSpinning | KartSpinFresh);
		}
		else {
			endSpin(w.racers.karts[i]);
		}
		hover(w, i, true);
	}
}

void PowerUps::resume(World& w, size_t i)
{
	Racer& r = w.racers.race[i];
	if (r.flags & RacerHalted) {
		r.flags &= ~RacerHalted;
		hover(w, i, false);
	}
}

void PowerUps::enterGhost(World& w, size_t i)
{
	w.raceView.skidEffects(w, i, false, RaceEvent::Ghost);
	w.racers.race[i].flags |= RacerGhost;
	curseOff(w, i);
	Kart& k = w.racers.karts[i];
	if (w.racers.playback[i].active) {
		k.flags &= ~(KartSpinning | KartSpinFresh);
	}
	else {
		endSpin(k);
	}
	if (w.racers.race[i].ai && w.racers.playback[i].active) {
		routeGhost(w.racers.playback[i], k, true);
	}
	else {
		k.flags |= KartGhost;
	}
}

void PowerUps::leaveGhost(World& w, size_t i)
{
	w.racers.race[i].flags &= ~RacerGhost;
	if (w.racers.race[i].ai && w.racers.playback[i].active) {
		routeGhost(w.racers.playback[i], w.racers.karts[i], false);
	}
	else {
		w.racers.karts[i].flags &= ~KartGhost;
	}
}

void PowerUps::turboOn(World& w, size_t i, int32_t level)
{
	w.racers.race[i].flags |= RacerTurbo;
	Driver& d = w.racers.drivers[i];
	d.flags = (d.flags & ~DriverTurboWeak) | DriverTurbo;
	snapHeading(w.racers.karts[i]); // (a route kart's heading is its route's: only its statistics are refreshed)
	boost(w, i, true);
	// A player's tyre smoke (QUIRK: if the turbo before was strong), dust and car smoke.
	if (!w.racers.race[i].ai) {
		if (w.racers.powers[i].turboLevel > 0) {
			w.raceView.skidEffects(w, i, true, RaceEvent::Turbo);
		}
		w.raceView.raise(RaceEvent::DustStart, {Owner::Racer, int16_t(i)});
		w.raceView.raise(RaceEvent::SmokeStart, {Owner::Racer, int16_t(i)});
	}
	w.racers.powers[i].turboLevel = uint8_t(level);
}

void PowerUps::turboOff(World& w, size_t i)
{
	w.racers.race[i].flags &= ~RacerTurbo;
	w.racers.drivers[i].flags &= ~(DriverTurbo | DriverTurboWeak);
	boost(w, i, false);
	w.raceView.skidEffects(w, i, false, RaceEvent::Turbo);
}

void PowerUps::shieldOn(World& w, size_t i, int32_t level)
{
	w.racers.race[i].flags |= RacerShielded;
	w.racers.powers[i].shieldLevel = uint8_t(level);
}

void PowerUps::curseOn(World& w, size_t i, int32_t ms)
{
	Power& pw = w.racers.powers[i];
	if (!(w.racers.race[i].flags & RacerCursed)) {
		w.raceView.loopSound(CurseSkullSound, {Owner::Racer, int16_t(i), 1}, w.racers.visible[i].position);
	}
	pw.curseMs = ms;
	pw.cursePassed = true;
	w.racers.race[i].flags |= RacerCursed;
	w.racers.drivers[i].flags |= DriverCursed;
	w.raceView.markHit(i);
	// The curse slows the kart, ending any boost first.
	boost(w, i, false);
	if (w.racers.playback[i].active) {
		routeCurse(w.racers.playback[i], w.racers.karts[i], true);
	}
	else {
		w.racers.karts[i].flags |= KartCurseSlow;
	}
}

void PowerUps::curseOff(World& w, size_t i)
{
	if (w.racers.race[i].flags & RacerCursed) {
		w.raceView.stopSound(CurseSkullSound, {Owner::Racer, int16_t(i), 1});
	}
	w.racers.powers[i].curseMs = 0;
	w.racers.race[i].flags &= ~RacerCursed;
	w.racers.drivers[i].flags &= ~DriverCursed;
	if (w.racers.playback[i].active) {
		routeCurse(w.racers.playback[i], w.racers.karts[i], false);
	}
	else {
		w.racers.karts[i].flags &= ~KartCurseSlow;
	}
}

void PowerUps::shove(World& w, size_t i, Vec3 q)
{
	Racer& r = w.racers.race[i];
	if ((r.flags & RacerShoved) || !w.triggers.schedule(w, {ShoveMs, 0, Wait::Shove, uint8_t(i)})) {
		return;
	}
	Kart& k = w.racers.karts[i];
	beginTurn(k, ShoveGain, ShoveSlip, Pi); // (nothing to a route kart but the flag)
	Power& pw = w.racers.powers[i];
	if (!(k.flags & KartPushA)) {
		push(w, i, 0, q);
		pw.shoveSlot = 1;
	}
	else if (!(k.flags & KartPushB)) {
		push(w, i, 1, q);
		pw.shoveSlot = 2;
	}
	r.flags |= RacerShoved;
}

void PowerUps::endShove(World& w, size_t i)
{
	w.racers.race[i].flags &= ~RacerShoved;
	Power& pw = w.racers.powers[i];
	if (pw.shoveSlot != 0) {
		endPush(w, i, pw.shoveSlot - 1);
	}
	pw.shoveSlot = 0;
}

void PowerUps::magnetHold(World& w, size_t i, bool on)
{
	Racer& r = w.racers.race[i];
	if (!r.ai) {
		w.raceView.skidEffects(w, i, on, RaceEvent::Magnet);
	}
	r.flags = on ? r.flags | RacerMagnetHeld : r.flags & ~RacerMagnetHeld;
	if (!r.ai) {
		w.racers.drivers[i].flags = on ? w.racers.drivers[i].flags | DriverBrakeToStop : w.racers.drivers[i].flags & ~DriverBrakeToStop;
	}
	if (w.racers.playback[i].active) {
		routePace(w.racers.playback[i], w.racers.karts[i], on ? 0.0f : 1.0f);
	}
	resume(w, i);
}

void PowerUps::spin(World& w, size_t i, float turns, float rate, float align)
{
	if (w.racers.playback[i].active) {
		routeSpin(w.racers.playback[i], w.racers.karts[i], turns, rate);
	}
	else {
		startSpin(w.racers.karts[i], w.racers.params[i], turns, rate, align);
	}
}

void PowerUps::spinOut(World& w, size_t i)
{
	if (w.racers.playback[i].active) {
		routeSpinOut(w.racers.playback[i], w.racers.karts[i]);
	}
	else {
		w.racers.karts[i].flags |= KartSpinOut;
		w.racers.karts[i].spinOutMs = SpinOutMs;
	}
}

void PowerUps::endSpinOut(World& w, size_t i)
{
	if (!w.racers.playback[i].active) {
		w.racers.karts[i].flags &= ~KartSpinOut;
		w.racers.karts[i].spinOutMs = 0;
	}
}

void PowerUps::tilt(World& w, size_t i, float rate, int32_t ms)
{
	if (w.racers.playback[i].active) {
		routeTilt(w.racers.playback[i], w.racers.karts[i], rate, ms);
	}
	else {
		pitchKick(w.racers.karts[i], w.racers.params[i], rate, ms);
	}
}

void PowerUps::impulse(World& w, size_t i, Vec3 j)
{
	if (!w.racers.playback[i].active) {
		w.racers.karts[i].velocity += j * w.racers.params[i].invMass;
	}
}

void PowerUps::push(World& w, size_t i, int32_t which, Vec3 q)
{
	if (w.racers.playback[i].active) {
		routePush(w.racers.playback[i], w.racers.karts[i], q);
	}
	else {
		startPush(w.racers.karts[i], w.racers.params[i], which, q);
	}
}

void PowerUps::endPush(World& w, size_t i, int32_t which)
{
	if (w.racers.playback[i].active) {
		routeEndPush(w.racers.playback[i], w.racers.karts[i]);
	}
	else {
		w.racers.karts[i].flags &= ~(which ? KartPushB : KartPushA);
	}
}

void PowerUps::boost(World& w, size_t i, bool on)
{
	Kart& k = w.racers.karts[i];
	if (on && (k.flags & KartCurseSlow)) {
		return;
	}
	startBoost(k, w.racers.params[i], on);
	if (w.racers.playback[i].active) {
		routeBoost(w.racers.playback[i], k, on);
	}
}

void PowerUps::hover(World& w, size_t i, bool on)
{
	if (w.racers.playback[i].active) {
		routeHover(w.racers.playback[i], w.racers.karts[i], on);
	}
	else if (on) {
		enterHover(w.racers.karts[i], w.racers.params[i]);
	}
	else {
		leaveHover(w.racers.karts[i]);
	}
}

// A contact's power-ups: the shields' shoves and spins, a curse passed on.
void PowerUps::contactPowers(World& w, size_t first, size_t second, Vec3 point)
{
	// A shield: at level 1 it shoves the other racer when that one is hit from the side, at 2 and 3 spins it.
	const size_t pair[2] = {first, second};
	const Vec3 offsets[2] = {normalize(w.racers.karts[first].centerOfMassWorld - point),
		normalize(w.racers.karts[second].centerOfMassWorld - point)};
	if (((w.racers.race[first].flags ^ w.racers.race[second].flags) & RacerShielded) != 0) {
		for (int s = 0; s < 2; s++) {
			const size_t own = pair[s], other = pair[1 - s];
			if (!(w.racers.race[own].flags & RacerShielded)) {
				continue;
			}
			const Vec3 f = w.racers.visible[other].rotation.f, d = offsets[1 - s];
			const float side = dot(f, d);
			switch (w.racers.powers[own].shieldLevel) {
			case 1:
				if (side > -0.7f && side < 0.7f) {
					shove(w, other, d * 200);
				}
				break;
			case 2: spin(w, other, 1, 0.009f, 0); break;
			case 3: spin(w, other, 2, 0.009f, 0); break;
			default: break;
			}
		}
	}
	// A curse passes to the other racer (once a racer has had one).
	for (int s = 0; s < 2; s++) {
		const size_t from = pair[s], to = pair[1 - s];
		if ((w.racers.race[from].flags & RacerCursed) && !(w.racers.race[to].flags & RacerCursed) && w.racers.powers[from].cursePassed &&
			!(w.racers.race[to].flags & RacerShielded)) {
			curseOn(w, to, w.racers.powers[from].curseMs);
			curseOff(w, from);
			break;
		}
	}
}

} // namespace lr
