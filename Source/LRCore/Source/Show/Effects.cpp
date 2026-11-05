// What power-ups and hazards do to a racer: its own states (halted, ghost, turbo, shield, curse, shove, magnet hold)
// and the body's calls they make, each on a physics kart's body or on a route kart's playback.

#include "LRCore/Race/World.h"
#include "Kart/Steering.h"

namespace lr {

void World::halt(size_t i)
{
	Racer& r = racers[i];
	if (!(r.flags & RacerHalted)) {
		r.flags |= RacerHalted;
		if (playback[i].active) {
			karts[i].flags &= ~(KartSpinning | KartSpinFresh);
		}
		else {
			endSpin(karts[i]);
		}
		hover(i, true);
	}
}

void World::resume(size_t i)
{
	Racer& r = racers[i];
	if (r.flags & RacerHalted) {
		r.flags &= ~RacerHalted;
		hover(i, false);
	}
}

void World::enterGhost(size_t i)
{
	skidEffects(i, false, RaceEvent::Ghost);
	racers[i].flags |= RacerGhost;
	curseOff(i);
	Kart& k = karts[i];
	if (playback[i].active) {
		k.flags &= ~(KartSpinning | KartSpinFresh);
	}
	else {
		endSpin(k);
	}
	if (racers[i].ai && playback[i].active) {
		routeGhost(playback[i], k, true);
	}
	else {
		k.flags |= KartGhost;
	}
}

void World::leaveGhost(size_t i)
{
	racers[i].flags &= ~RacerGhost;
	if (racers[i].ai && playback[i].active) {
		routeGhost(playback[i], karts[i], false);
	}
	else {
		karts[i].flags &= ~KartGhost;
	}
}

void World::turboOn(size_t i, int32_t level)
{
	racers[i].flags |= RacerTurbo;
	Driver& d = drivers[i];
	d.flags = (d.flags & ~DriverTurboWeak) | DriverTurbo;
	snapHeading(karts[i]); // (a route kart's heading is its route's: only its statistics are refreshed)
	boost(i, true);
	// A player's tyre smoke (QUIRK: if the turbo before was strong), dust and car smoke.
	if (!racers[i].ai) {
		if (powers[i].turboLevel > 0) {
			skidEffects(i, true, RaceEvent::Turbo);
		}
		raise(RaceEvent::DustStart, {Owner::Racer, int16_t(i)});
		raise(RaceEvent::SmokeStart, {Owner::Racer, int16_t(i)});
	}
	powers[i].turboLevel = uint8_t(level);
}

void World::turboOff(size_t i)
{
	racers[i].flags &= ~RacerTurbo;
	drivers[i].flags &= ~(DriverTurbo | DriverTurboWeak);
	boost(i, false);
	skidEffects(i, false, RaceEvent::Turbo);
}

void World::shieldOn(size_t i, int32_t level)
{
	racers[i].flags |= RacerShielded;
	powers[i].shieldLevel = uint8_t(level);
}

void World::curseOn(size_t i, int32_t ms)
{
	Power& pw = powers[i];
	if (!(racers[i].flags & RacerCursed)) {
		RaceEvent& e = raise(RaceEvent::Sound, {Owner::Racer, int16_t(i), 1}); // its skull's loop
		e.sound = 8, e.looping = true, e.place = visible[i].position;
	}
	pw.curseMs = ms;
	pw.cursePassed = true;
	racers[i].flags |= RacerCursed;
	drivers[i].flags |= DriverCursed;
	markHit(i);
	// The curse slows the kart, ending any boost first.
	boost(i, false);
	if (playback[i].active) {
		routeCurse(playback[i], karts[i], true);
	}
	else {
		karts[i].flags |= KartCurseSlow;
	}
}

void World::curseOff(size_t i)
{
	if (racers[i].flags & RacerCursed) {
		RaceEvent& e = raise(RaceEvent::Sound, {Owner::Racer, int16_t(i), 1});
		e.sound = 8, e.stop = true;
	}
	powers[i].curseMs = 0;
	racers[i].flags &= ~RacerCursed;
	drivers[i].flags &= ~DriverCursed;
	if (playback[i].active) {
		routeCurse(playback[i], karts[i], false);
	}
	else {
		karts[i].flags &= ~KartCurseSlow;
	}
}

void World::shove(size_t i, Vec3 q)
{
	Racer& r = racers[i];
	if ((r.flags & RacerShoved) || !schedule({750, 0, Wait::Shove, uint8_t(i)})) {
		return;
	}
	Kart& k = karts[i];
	startSteering(k, 2.5f, 0.89999998f, Pi); // (nothing to a route kart but the flag)
	Power& pw = powers[i];
	if (!(k.flags & KartPushA)) {
		push(i, 0, q);
		pw.shoveSlot = 1;
	}
	else if (!(k.flags & KartPushB)) {
		push(i, 1, q);
		pw.shoveSlot = 2;
	}
	r.flags |= RacerShoved;
}

void World::endShove(size_t i)
{
	racers[i].flags &= ~RacerShoved;
	Power& pw = powers[i];
	if (pw.shoveSlot != 0) {
		endPush(i, pw.shoveSlot - 1);
	}
	pw.shoveSlot = 0;
}

void World::magnetHold(size_t i, bool on)
{
	Racer& r = racers[i];
	if (!r.ai) {
		skidEffects(i, on, RaceEvent::Magnet);
	}
	r.flags = on ? r.flags | RacerMagnetHeld : r.flags & ~RacerMagnetHeld;
	if (!r.ai) {
		drivers[i].flags = on ? drivers[i].flags | DriverBrakeToStop : drivers[i].flags & ~DriverBrakeToStop;
	}
	if (playback[i].active) {
		routePace(playback[i], karts[i], on ? 0.0f : 1.0f);
	}
	resume(i);
}

void World::spin(size_t i, float turns, float rate, float align)
{
	if (playback[i].active) {
		routeSpin(playback[i], karts[i], turns, rate);
	}
	else {
		startSpin(karts[i], params[i], turns, rate, align);
	}
}

void World::spinOut(size_t i)
{
	if (playback[i].active) {
		routeSpinOut(playback[i], karts[i]);
	}
	else {
		karts[i].flags |= KartSpinOut;
		karts[i].spinOutMs = 600;
	}
}

void World::endSpinOut(size_t i)
{
	if (!playback[i].active) {
		karts[i].flags &= ~KartSpinOut;
		karts[i].spinOutMs = 0;
	}
}

void World::tilt(size_t i, float rate, int32_t ms)
{
	if (playback[i].active) {
		routeTilt(playback[i], karts[i], rate, ms);
	}
	else {
		pitchKick(karts[i], params[i], rate, ms);
	}
}

void World::impulse(size_t i, Vec3 j)
{
	if (!playback[i].active) {
		karts[i].velocity += j * params[i].invMass;
	}
}

void World::push(size_t i, int which, Vec3 q)
{
	if (playback[i].active) {
		routePush(playback[i], karts[i], q);
	}
	else {
		startPush(karts[i], params[i], which, q);
	}
}

void World::endPush(size_t i, int which)
{
	if (playback[i].active) {
		routeEndPush(playback[i], karts[i]);
	}
	else {
		karts[i].flags &= ~(which ? KartPushB : KartPushA);
	}
}

void World::boost(size_t i, bool on)
{
	Kart& k = karts[i];
	if (on && (k.flags & KartCurseSlow)) {
		return;
	}
	startBoost(k, params[i], on);
	if (playback[i].active) {
		routeBoost(playback[i], k, on);
	}
}

void World::hover(size_t i, bool on)
{
	if (playback[i].active) {
		routeHover(playback[i], karts[i], on);
	}
	else if (on) {
		enterHover(karts[i], params[i]);
	}
	else {
		leaveHover(karts[i]);
	}
}

} // namespace lr
