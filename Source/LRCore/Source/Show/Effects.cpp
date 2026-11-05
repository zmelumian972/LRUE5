// What power-ups and hazards do to a racer: its own states (halted, ghost, turbo, shield, curse, shove, magnet hold)
// and the body's calls they make, each on a physics kart's body or on a route kart's playback.

#include "LRCore/Race/World.h"
#include "Kart/Steering.h"

namespace lr {

void World::halt(size_t i)
{
	Racer& r = racers.race[i];
	if (!(r.flags & RacerHalted)) {
		r.flags |= RacerHalted;
		if (racers.playback[i].active) {
			racers.karts[i].flags &= ~(KartSpinning | KartSpinFresh);
		}
		else {
			endSpin(racers.karts[i]);
		}
		hover(i, true);
	}
}

void World::resume(size_t i)
{
	Racer& r = racers.race[i];
	if (r.flags & RacerHalted) {
		r.flags &= ~RacerHalted;
		hover(i, false);
	}
}

void World::enterGhost(size_t i)
{
	skidEffects(i, false, RaceEvent::Ghost);
	racers.race[i].flags |= RacerGhost;
	curseOff(i);
	Kart& k = racers.karts[i];
	if (racers.playback[i].active) {
		k.flags &= ~(KartSpinning | KartSpinFresh);
	}
	else {
		endSpin(k);
	}
	if (racers.race[i].ai && racers.playback[i].active) {
		routeGhost(racers.playback[i], k, true);
	}
	else {
		k.flags |= KartGhost;
	}
}

void World::leaveGhost(size_t i)
{
	racers.race[i].flags &= ~RacerGhost;
	if (racers.race[i].ai && racers.playback[i].active) {
		routeGhost(racers.playback[i], racers.karts[i], false);
	}
	else {
		racers.karts[i].flags &= ~KartGhost;
	}
}

void World::turboOn(size_t i, int32_t level)
{
	racers.race[i].flags |= RacerTurbo;
	Driver& d = racers.drivers[i];
	d.flags = (d.flags & ~DriverTurboWeak) | DriverTurbo;
	snapHeading(racers.karts[i]); // (a route kart's heading is its route's: only its statistics are refreshed)
	boost(i, true);
	// A player's tyre smoke (QUIRK: if the turbo before was strong), dust and car smoke.
	if (!racers.race[i].ai) {
		if (racers.powers[i].turboLevel > 0) {
			skidEffects(i, true, RaceEvent::Turbo);
		}
		raise(RaceEvent::DustStart, {Owner::Racer, int16_t(i)});
		raise(RaceEvent::SmokeStart, {Owner::Racer, int16_t(i)});
	}
	racers.powers[i].turboLevel = uint8_t(level);
}

void World::turboOff(size_t i)
{
	racers.race[i].flags &= ~RacerTurbo;
	racers.drivers[i].flags &= ~(DriverTurbo | DriverTurboWeak);
	boost(i, false);
	skidEffects(i, false, RaceEvent::Turbo);
}

void World::shieldOn(size_t i, int32_t level)
{
	racers.race[i].flags |= RacerShielded;
	racers.powers[i].shieldLevel = uint8_t(level);
}

void World::curseOn(size_t i, int32_t ms)
{
	Power& pw = racers.powers[i];
	if (!(racers.race[i].flags & RacerCursed)) {
		RaceEvent& e = raise(RaceEvent::Sound, {Owner::Racer, int16_t(i), 1}); // its skull's loop
		e.sound = 8, e.looping = true, e.place = racers.visible[i].position;
	}
	pw.curseMs = ms;
	pw.cursePassed = true;
	racers.race[i].flags |= RacerCursed;
	racers.drivers[i].flags |= DriverCursed;
	markHit(i);
	// The curse slows the kart, ending any boost first.
	boost(i, false);
	if (racers.playback[i].active) {
		routeCurse(racers.playback[i], racers.karts[i], true);
	}
	else {
		racers.karts[i].flags |= KartCurseSlow;
	}
}

void World::curseOff(size_t i)
{
	if (racers.race[i].flags & RacerCursed) {
		RaceEvent& e = raise(RaceEvent::Sound, {Owner::Racer, int16_t(i), 1});
		e.sound = 8, e.stop = true;
	}
	racers.powers[i].curseMs = 0;
	racers.race[i].flags &= ~RacerCursed;
	racers.drivers[i].flags &= ~DriverCursed;
	if (racers.playback[i].active) {
		routeCurse(racers.playback[i], racers.karts[i], false);
	}
	else {
		racers.karts[i].flags &= ~KartCurseSlow;
	}
}

void World::shove(size_t i, Vec3 q)
{
	Racer& r = racers.race[i];
	if ((r.flags & RacerShoved) || !schedule({750, 0, Wait::Shove, uint8_t(i)})) {
		return;
	}
	Kart& k = racers.karts[i];
	startSteering(k, 2.5f, 0.89999998f, Pi); // (nothing to a route kart but the flag)
	Power& pw = racers.powers[i];
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
	racers.race[i].flags &= ~RacerShoved;
	Power& pw = racers.powers[i];
	if (pw.shoveSlot != 0) {
		endPush(i, pw.shoveSlot - 1);
	}
	pw.shoveSlot = 0;
}

void World::magnetHold(size_t i, bool on)
{
	Racer& r = racers.race[i];
	if (!r.ai) {
		skidEffects(i, on, RaceEvent::Magnet);
	}
	r.flags = on ? r.flags | RacerMagnetHeld : r.flags & ~RacerMagnetHeld;
	if (!r.ai) {
		racers.drivers[i].flags = on ? racers.drivers[i].flags | DriverBrakeToStop : racers.drivers[i].flags & ~DriverBrakeToStop;
	}
	if (racers.playback[i].active) {
		routePace(racers.playback[i], racers.karts[i], on ? 0.0f : 1.0f);
	}
	resume(i);
}

void World::spin(size_t i, float turns, float rate, float align)
{
	if (racers.playback[i].active) {
		routeSpin(racers.playback[i], racers.karts[i], turns, rate);
	}
	else {
		startSpin(racers.karts[i], racers.params[i], turns, rate, align);
	}
}

void World::spinOut(size_t i)
{
	if (racers.playback[i].active) {
		routeSpinOut(racers.playback[i], racers.karts[i]);
	}
	else {
		racers.karts[i].flags |= KartSpinOut;
		racers.karts[i].spinOutMs = 600;
	}
}

void World::endSpinOut(size_t i)
{
	if (!racers.playback[i].active) {
		racers.karts[i].flags &= ~KartSpinOut;
		racers.karts[i].spinOutMs = 0;
	}
}

void World::tilt(size_t i, float rate, int32_t ms)
{
	if (racers.playback[i].active) {
		routeTilt(racers.playback[i], racers.karts[i], rate, ms);
	}
	else {
		pitchKick(racers.karts[i], racers.params[i], rate, ms);
	}
}

void World::impulse(size_t i, Vec3 j)
{
	if (!racers.playback[i].active) {
		racers.karts[i].velocity += j * racers.params[i].invMass;
	}
}

void World::push(size_t i, int32_t which, Vec3 q)
{
	if (racers.playback[i].active) {
		routePush(racers.playback[i], racers.karts[i], q);
	}
	else {
		startPush(racers.karts[i], racers.params[i], which, q);
	}
}

void World::endPush(size_t i, int32_t which)
{
	if (racers.playback[i].active) {
		routeEndPush(racers.playback[i], racers.karts[i]);
	}
	else {
		racers.karts[i].flags &= ~(which ? KartPushB : KartPushA);
	}
}

void World::boost(size_t i, bool on)
{
	Kart& k = racers.karts[i];
	if (on && (k.flags & KartCurseSlow)) {
		return;
	}
	startBoost(k, racers.params[i], on);
	if (racers.playback[i].active) {
		routeBoost(racers.playback[i], k, on);
	}
}

void World::hover(size_t i, bool on)
{
	if (racers.playback[i].active) {
		routeHover(racers.playback[i], racers.karts[i], on);
	}
	else if (on) {
		enterHover(racers.karts[i], racers.params[i]);
	}
	else {
		leaveHover(racers.karts[i]);
	}
}

} // namespace lr
