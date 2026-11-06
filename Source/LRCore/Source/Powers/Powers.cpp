// The power-up manager: holding and using a colour (a player's key, the AI's chances), making an action from its
// kind's pool (or taking over the furthest-along active one), the effect slots some kinds need, the traps' touches,
// and the manager's frame (bricks, actions newest first, explosions, the done released). Each action kind's own
// behaviour is its ActionKind (ActionParts.h).

#include "Powers/ActionParts.h"
#include "Common/Assert.h"
#include "Show/SoundIds.h"

#include <algorithm>

namespace lr {

namespace {

constexpr float RedConeMin = 100, RedConeMax = 62500, RedConeCos = 0.96f;
// A racer's AI: how often it thinks of using its colour after deciding not to; it waits while this many effect slots
// are used.
constexpr int32_t AiWaitMs = 1000;
constexpr int32_t AiEffectsBusy = 20;
constexpr int32_t CursedMs = 10000; // a racer the curse's trap curses

// The kinds in Action::Kind's order.
const ActionKind* const Kinds[Action::Kinds] = {&MagnetAction, &OilAction, &DynamiteAction, &CurseAction, &RedAction, &RedAction,
	&RedAction, &RedAction, &ShieldAction, &TurboAction, &WarpAction};

void giveSlotsBack(World& w, Action& a)
{
	for (int8_t& s : a.slots) {
		if (s >= 0) {
			w.powerUps.effectMask &= ~(1u << s);
			w.powerUps.effectsUsed--;
			s = -1;
		}
	}
}

// The release: the slots and the touch entry given back, and what the kind undoes.
void release(World& w, Action& a)
{
	const ActionKind& k = *Kinds[a.kind];
	w.raceView.raise(RaceEvent::Finish, ownerOf(w, a)); // (its particle, if it has one, winds down)
	if (k.loop >= 0) {
		w.raceView.stopSound(k.loop, ownerOf(w, a));
	}
	giveSlotsBack(w, a);
	w.triggers.killMember(a.member);
	if (k.release) {
		k.release(w, a);
	}
}

// A pool's action to reuse: the free list's head, else the furthest-along active one its kind may take over.
int16_t takeAction(World& w, Action::Kind kind)
{
	std::vector<uint8_t>& free = w.powerUps.freeActions[kind];
	if (!free.empty()) {
		const uint8_t a = free.front();
		free.erase(free.begin());
		return a;
	}
	// (A turbo takes over any green of level 0 to 2, a shield any blue, the others their own colour and level: each
	// its own kind.)
	int best = -1;
	uint8_t bestState = 0;
	uint32_t bestMs = UINT32_MAX;
	for (size_t i = 0; i < w.powerUps.actionList.size(); i++) {
		const Action& a = w.powerUps.actions[w.powerUps.actionList[i]];
		if (a.kind == kind && (a.state > bestState || (a.state == bestState && a.ms < bestMs))) {
			best = int(i);
			bestState = a.state;
			bestMs = a.ms;
		}
	}
	if (best < 0) {
		return -1;
	}
	const uint8_t a = w.powerUps.actionList[best];
	w.powerUps.actionList.erase(w.powerUps.actionList.begin() + best);
	release(w, w.powerUps.actions[a]);
	return a;
}

} // namespace

bool takeSlots(World& w, Action& a, int n)
{
	bool all = true;
	for (int s = 0; s < n; s++) {
		if (w.powerUps.effectsUsed >= EffectSlots) {
			all = false;
			continue;
		}
		int free = 0;
		while (w.powerUps.effectMask & (1u << free)) {
			free++;
		}
		w.powerUps.effectMask |= 1u << free;
		w.powerUps.effectsUsed++;
		a.slots[s] = int8_t(free);
	}
	return all;
}

void PowerUps::useHeld(World& w, size_t i)
{
	Power& pw = w.racers.powers[i];
	if (pw.held == NoColour) {
		const Vec3 at = w.racers.visible[i].position; // a taunt
		w.triggers.fireAt(w, 999, 999, &at);
		return;
	}
	if (pw.held == Yellow || pw.held == Blue) {
		w.raceView.voice(w, i, RaceEvent::Positive);
	}
	make(w, int8_t(i), pw.held, pw.whiteCount);
	pw.held = NoColour;
	for (int16_t& slot : pw.whites) {
		if (slot >= 0) {
			sendHome(w, size_t(slot));
			slot = -1;
		}
	}
	pw.whiteCount = 0;
}

void PowerUps::consider(World& w, size_t i)
{
	Power& pw = w.racers.powers[i];
	if (effectsUsed >= AiEffectsBusy || (pw.chargeColour && pw.held == pw.chargeColour && pw.whiteCount < pw.chargeTarget)) {
		pw.aiIntervalMs = AiWaitMs;
		return;
	}
	const uint8_t chance = pw.chances[pw.held - 1];
	if (pw.held == Green) {
		if ((w.draw(Draw::UseGreen) & 255) >= chance) {
			pw.aiIntervalMs = AiWaitMs;
			return;
		}
		if (((w.racers.race[i].flags & RacerFinished) && pw.whiteCount == 3) || (w.racers.karts[i].flags & KartSpinning)) {
			return;
		}
		useHeld(w, i);
		return;
	}
	if (pw.held == Red) {
		const Pose& v = w.racers.visible[i];
		if (nearestInCone(w, v.position, v.rotation.f, RedConeMin, RedConeMax, RedConeCos) < 0) {
			return;
		}
	}
	if ((w.draw(Draw::Use) & 255) >= chance) {
		pw.aiIntervalMs = AiWaitMs;
		return;
	}
	useHeld(w, i);
}

int16_t PowerUps::make(World& w, int8_t racer, uint8_t colour, int32_t level, const Aim* aim)
{
	const int32_t free = EffectSlots - effectsUsed;
	Action::Kind kind;
	switch (colour) {
	case Red: kind = level == 0 ? Action::Cannon : level == 1 ? Action::Hook : level == 2 ? Action::Bolt : Action::Missile; break;
	case Yellow:
		kind = level == 0 ? Action::Oil : level == 1 ? Action::Dynamite : level == 2 ? Action::Magnet : Action::Curse;
		if (level >= 2 && free < 3) {
			return -1;
		}
		break;
	case Blue:
		kind = Action::Shield;
		if (free < 2) {
			return -1;
		}
		break;
	case Green:
		kind = level >= 3 ? Action::Warp : Action::Turbo;
		if (level < 3 && free < 3) {
			return -1;
		}
		break;
	default: return -1;
	}
	int16_t first = -1;
	for (uint8_t m = 0; m < (kind == Action::Missile ? 3 : 1); m++) {
		const int16_t index = takeAction(w, kind);
		if (index < 0) {
			break;
		}
		first = first < 0 ? index : first;
		actionList.insert(actionList.begin(), uint8_t(index));
		Action& a = actions[index];
		a = {};
		a.kind = kind;
		a.owner = racer;
		a.missile = m;
		a.emplacement = aim ? aim->emplacement : -1;
		Kinds[kind]->start(w, a, level, aim);
		a.level = uint8_t(level);
	}
	return first;
}

void PowerUps::cancelTurbo(size_t i)
{
	for (uint8_t index : actionList) {
		Action& a = actions[index];
		if (a.kind == Action::Turbo && a.owner == int8_t(i) && a.state > Action::Ready) {
			a.state = Action::Done;
			a.owner = -1;
		}
	}
}

void PowerUps::cancelShield(size_t i)
{
	for (uint8_t index : actionList) {
		Action& a = actions[index];
		if (a.kind == Action::Shield && a.owner == int8_t(i) && a.state > Action::Ready) {
			a.state = Action::Done;
		}
	}
}

void PowerUps::cancelMagnetHold(size_t i)
{
	for (uint8_t index : actionList) {
		Action& a = actions[index];
		if (a.kind == Action::Magnet && a.held == int8_t(i) && a.state > Action::Ready) {
			a.state = Action::Done;
		}
	}
}

void PowerUps::touchTrap(World& w, size_t index, size_t i)
{
	LR_ASSERT(index < actions.size());
	Action& a = actions[index];
	if (a.owner == int8_t(i) || a.state == Action::Done) {
		return;
	}
	if (w.racers.race[i].flags & RacerShielded) {
		RaceEvent& absorb = w.raceView.raise(RaceEvent::Absorb, {Owner::Racer, int16_t(i)});
		absorb.place = w.racers.visible[i].position;
		if (a.level < 3) {
			a.state = Action::Done; // used up on the shield (a curse stays)
		}
		return;
	}
	if (a.kind == Action::Curse) {
		if (!(w.racers.race[i].flags & RacerCursed)) {
			curseOn(w, i, CursedMs); // (the manager's own)
		}
	}
	else if (Kinds[a.kind]->touch) {
		Kinds[a.kind]->touch(w, a, i);
	}
}

// Each action's frame by its kind, newest first: a turbo's even when done, the others' only before.
void PowerUps::actionsFrame(World& w, int32_t ms)
{
	for (size_t n = 0; n < actionList.size(); n++) {
		Action& a = actions[actionList[n]];
		const ActionKind& k = *Kinds[a.kind];
		if (k.framesWhenDone || a.state != Action::Done) {
			k.frame(w, a, ms);
		}
	}
}

// The done actions and the idle explosions back to their pools' free lists.
void PowerUps::releaseDone(World& w)
{
	for (size_t n = 0; n < actionList.size();) {
		Action& a = actions[actionList[n]];
		if (a.state != Action::Done) {
			n++;
			continue;
		}
		const uint8_t index = actionList[n];
		actionList.erase(actionList.begin() + long(n));
		release(w, a);
		freeActions[a.kind].insert(freeActions[a.kind].begin(), index);
	}
	for (int pool = 0; pool < 2; pool++) {
		std::vector<uint8_t>& list = explosionList[pool];
		for (size_t n = 0; n < list.size();) {
			if (explosions[list[n]].state != Explosion::Idle) {
				n++;
				continue;
			}
			freeExplosions[pool].insert(freeExplosions[pool].begin(), list[n]);
			list.erase(list.begin() + long(n));
		}
	}
}

void PowerUps::powerFrame(World& w, int32_t ms)
{
	w.raceView.stage = RaceEvent::Powers;
	// The bricks: colour bricks then white bricks, in file order.
	for (size_t index = 0; index < bricks.size(); index++) {
		brickFrame(w, index, ms);
	}

	// The actions, newest first, then the explosions; then the done are released back to their pools.
	actionsFrame(w, ms);
	hooksMoved(w);
	for (int pool = 0; pool < 2; pool++) {
		for (uint8_t e : explosionList[pool]) {
			explosionFrame(w, explosions[e], ms);
		}
	}
	releaseDone(w);
}

// The race's power-ups at load: every racer's chances (from its driver's habits, 255 for a built car), the pools,
// the bricks waiting to grow (white ones growing) in the trigger list.
void setupPowers(World& w, const std::vector<std::array<int32_t, 8>>& habits)
{
	w.racers.powers.assign(w.racers.race.size(), {});
	for (size_t i = 0; i < w.racers.race.size(); i++) {
		Power& pw = w.racers.powers[i];
		const std::array<int32_t, 8>& h = habits[i];
		// The stats in the file's order: red, yellow, green, blue.
		static constexpr uint8_t colours[4] = {Red, Yellow, Green, Blue};
		for (size_t s = 0; s < 4; s++) {
			const float c = std::min(1.0f, ((float(h[s]) * 0.8888889f) * 0.01f) + 0.2f);
			pw.chances[colours[s] - 1] = uint8_t(int32_t(c * 255));
		}
		pw.chargeColour = h[6] >= 1 && h[6] <= 4 ? uint8_t(h[6]) : 0;
		pw.chargeTarget = uint8_t(h[7]);
	}
	w.powerUps.actions.clear();
	for (size_t k = 0; k < Action::Kinds; k++) {
		w.powerUps.freeActions[k].clear();
		for (int32_t n = 0; n < PoolSizes[k]; n++) {
			w.powerUps.freeActions[k].push_back(uint8_t(w.powerUps.actions.size()));
			w.powerUps.actions.emplace_back().kind = Action::Kind(k);
		}
	}
	for (size_t pool = 0; pool < 2; pool++) {
		w.powerUps.freeExplosions[pool].clear();
		for (int n = 0; n < (pool ? 2 : 6); n++) {
			const uint8_t e = uint8_t(pool ? 6 + n : n);
			w.powerUps.freeExplosions[pool].push_back(e);
			w.powerUps.explosions[e] = {};
			w.powerUps.explosions[e].blast = pool ? 10.0f : 5.0f;
		}
	}
	w.powerUps.targetsOn.assign(w.track.targets.size(), 1);
	for (size_t index = 0; index < w.track.bricks.size(); index++) {
		const BrickPlace& place = w.track.bricks[index];
		Brick& b = w.powerUps.bricks.emplace_back();
		b.position = b.home = place.position;
		b.colour = b.assigned = place.colour;
		b.nextColour = place.colour ? Green : 0; // (a brick's next colour is green until it is taken)
		b.respawnMs = place.respawnMs;
		b.state = place.colour ? Brick::Wait : Brick::Growing;
		b.ms = 3000;
		b.member = w.triggers.addMember(w, {b.position, 0, Member::Brick, false, false, uint16_t(index)});
	}
}

} // namespace lr
