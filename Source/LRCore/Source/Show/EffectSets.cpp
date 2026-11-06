// The show's effects: the effect sets' slots, handles and particles, the
// car visuals' handles, the spawns the rules' events ask for and those tried again each frame.

#include "LRCore/Show/ShowState.h"

#include "Common/Assert.h"
#include "LRCore/Race/World.h"
#include "LRData/Cache.h"
#include "Powers/PowerParts.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lr {

namespace {

constexpr size_t SetSlots[2] = {10, 4};
constexpr size_t MostParticles = 16;

bool named(const std::array<char, 9>& name) { return name[0] != 0; }

std::string folderOf(const std::string& path) { return path.substr(0, path.rfind('/') + 1); }

// A file of the track's folder, else of COMMON (the race files' rule).
std::string found(const Cache& cache, const std::string& dir, const std::string& name)
{
	return cache.has(dir + name) ? dir + name : "GAMEDATA/COMMON/" + name;
}

// A held effect's handle given up (h then empty), and the slot it had (nullptr for none).
EffectSlot* giveUp(Array<EffectSet, 2>& sets, EffectHandle& h)
{
	if (!h.held()) {
		return nullptr;
	}
	EffectSet& set = sets[h.set];
	const int16_t s = set.handles[h.index];
	set.handles[h.index] = -1;
	set.used[h.index] = false;
	h = {};
	return s >= 0 ? &set.slots[s] : nullptr;
}

// An effect slot's emitter stops; the slot stays until its particles are gone.
void finishSlot(EffectSlot& slot)
{
	slot.emitter = -1;
	if (slot.particles.empty()) {
		slot.active = false;
	}
	else {
		slot.finishPending = true;
	}
}

} // namespace

void Effects::load(const Cache& cache, const std::string& dir, const RaceFile& race, size_t racers)
{
	// The effect sets: the race's (EMITTER.EMB) and the track's, each with its world's material animation and libraries
	// (GLOBAL.WDB's, the track's LOCAL.WDB's).
	const std::string worlds[2] = {race.globalWorld, race.localWorld};
	for (size_t s = 0; s < 2; s++) {
		EffectSet& set = sets[s];
		set.slots.assign(SetSlots[s], {});
		set.handles.fill(-1);
		if (!race.emitterFiles[s].empty()) {
			(void)cache.get(found(cache, dir, binaryName(race.emitterFiles[s])), set.file); // (none: no emitters)
		}
		SceneFile scene;
		if (!worlds[s].empty() && cache.get(found(cache, dir, binaryName(worlds[s])), scene)) {
			const std::string folder = folderOf(found(cache, dir, binaryName(worlds[s])));
			if (!scene.materialAnimations.empty()) {
				(void)cache.get(folder + binaryName(scene.materialAnimations[0]) + ".MAB", set.tracks); // (none: no tracks)
			}
			for (size_t l = 0; l < scene.materialLibraries.size() && l < 2; l++) {
				set.libraries[l] = folder + binaryName(scene.materialLibraries[l]) + ".MDB";
			}
		}
	}
	visuals.assign(racers, {});
}

void Effects::beginFrame()
{
	spawned.clear();
	for (EffectSlot& slot : sets[0].slots) {
		slot.bubbled = false;
	}
}

// The sets' frame: a shot's smoke (a cannonball's, a hook's) finished in its power-up's frame once it has puffed three
// times (every one stops after its third emission), then the emitters and particles.
void Effects::update(Show& show, const World& world, int32_t ms)
{
	for (size_t k = 0; k < owned.size();) {
		EffectHandle& h = owned[k].second;
		const int16_t s = h.held() ? sets[h.set].handles[h.index] : -1;
		const EffectSlot* slot = s >= 0 ? &sets[h.set].slots[s] : nullptr;
		if (owned[k].first.kind == Owner::Action && slot && slot->emitter >= 0 && slot->emissions >= 3 &&
			sets[h.set].file.emitters[slot->emitter].name == "cannsmk") {
			finish(h);
			owned.erase(owned.begin() + std::ptrdiff_t(k));
			continue;
		}
		k++;
	}
	updateSets(show, world, ms);
}

// What the next frame looks back on once this frame's events are applied: the track events' particles active and the
// hazards on (retries), the oil slicks active (bubble).
void Effects::endEvents(const World& world)
{
	m_wanting.assign(world.scenery.events.size(), 0);
	for (size_t i = 0; i < world.track.events.size() && i < world.scenery.events.size(); i++) {
		m_wanting[i] = world.track.events[i].kind == EventResource::Particle && world.scenery.events[i].phase == EventState::Active;
	}
	m_hazardsOn.assign(world.scenery.hazards.size(), 0);
	for (size_t i = 0; i < world.scenery.hazards.size(); i++) {
		m_hazardsOn[i] = (world.scenery.hazards[i].type == Hazard::Snow || world.scenery.hazards[i].type == Hazard::Smoke) &&
			world.scenery.hazards[i].on;
	}
	for (EffectSlot& slot : sets[0].slots) {
		slot.slickWas = slot.place == EffectSlot::OilSlick && inRange(world.powerUps.actions, slot.racer) &&
			world.powerUps.actions[slot.racer].state == TrapActive;
	}
}

EffectHandle Effects::spawn(const World& world, int32_t s, const char* name, Owner owner, const Vec3* at, EffectSlot::Place where,
	int16_t racer, uint8_t wheel)
{
	// Either set (2): the race's first.
	if (s == 2) {
		s = sets[0].file.find(name) >= 0 ? 0 : 1;
	}
	EffectSet& set = sets[s];
	const int32_t e = set.file.find(name);
	if (e < 0) {
		spawned.push_back({name, -1, -1, false});
		return {};
	}
	const EmitterFile::Emitter& em = set.file.emitters[e];
	size_t freeHandle = EffectSet::Handles;
	for (size_t h = 0; h < EffectSet::Handles && freeHandle == EffectSet::Handles; h++) {
		freeHandle = set.used[h] ? freeHandle : h;
	}
	if (freeHandle == EffectSet::Handles) {
		return {}; // (every handle in use: never in practice)
	}
	// A free slot, else the lowest priority (a draining slot's is 0; ties: the last), replaced if the new one is above it.
	int32_t chosen = -1;
	for (size_t i = 0; i < set.slots.size() && chosen < 0; i++) {
		chosen = set.slots[i].active ? -1 : int32_t(i);
	}
	if (chosen < 0) {
		int32_t lowest = 1 << 30;
		for (size_t i = 0; i < set.slots.size(); i++) {
			const EffectSlot& sl = set.slots[i];
			const int32_t p = sl.emitter >= 0 ? set.file.emitters[sl.emitter].priority : 0;
			if (p <= lowest) {
				lowest = p, chosen = int32_t(i);
			}
		}
		if (em.priority <= lowest) {
			spawned.push_back({name, int8_t(s), -1, false});
			return {};
		}
		if (set.slots[chosen].handle >= 0) {
			set.handles[set.slots[chosen].handle] = -1; // (its handle now controls nothing)
		}
	}
	EffectSlot& slot = set.slots[chosen];
	spawned.push_back({name, int8_t(s), int16_t(chosen), slot.active});
	const Basis kept = slot.rotation;
	slot = {};
	slot.rotation = kept; // (QUIRK: a spawn without a basis keeps the slot's last)
	slot.active = true;
	slot.emitter = slot.look = int16_t(e);
	slot.timerMs = em.intervalMs;
	slot.place = where;
	slot.owner = owner;
	slot.racer = racer;
	slot.wheel = wheel;

	if (at) {
		slot.position = *at;
	}
	place(world, slot);
	EffectHandle h;
	if (em.durationMs == -1) {
		set.used[freeHandle] = true;
		set.handles[freeHandle] = int16_t(chosen);
		slot.handle = int16_t(freeHandle);
		h = {int8_t(s), int16_t(freeHandle)};
	}
	return h;
}

void Effects::finish(EffectHandle& h)
{
	if (EffectSlot* slot = giveUp(sets, h)) {
		slot->handle = -1;
		finishSlot(*slot);
	}
}

void Effects::release(EffectHandle& h)
{
	if (EffectSlot* slot = giveUp(sets, h)) {
		slot->active = false;
		slot->emitter = -1;
		slot->handle = -1;
		slot->particles.clear();
	}
}

bool Effects::controls(EffectHandle h, int32_t atLeast) const
{
	if (!h.held()) {
		return false;
	}
	const int16_t s = sets[h.set].handles[h.index];
	return s >= 0 && sets[h.set].slots[s].emissions >= atLeast;
}

void Effects::stopAll()
{
	for (EffectSet& set : sets) {
		for (EffectSlot& slot : set.slots) {
			slot.active = false;
			slot.emitter = -1;
			slot.particles.clear();
		}
	}
	// (The handles stay as their owners hold them: a restart's world raises their ends.)
}

// An oil slick's bubbles move each frame (in its power-up's frame): two draws, an angle and a distance about its
// centre.
void Effects::bubble(Show& show, EffectSlot& slot)
{
	const uint16_t x = show.draw(Show::Draw::OilBubble), y = show.draw(Show::Draw::OilBubble);
	const float a = float(x % 628) * 0.01f, d = float(y % 7);
	slot.offset = {std::sin(a) * d, std::cos(a) * d, 0};
	slot.bubbled = true;
}

// An emitter's interval up: a particle emitted at the slot's place unless its chance draw says no.
void Effects::emitParticle(Show& show, const World& world, EffectSlot& slot, const EmitterFile::Emitter& em)
{
	if (show.observer) {
		show.observer->onEmit(em.name, slot.owner);
	}
	const uint16_t x = show.draw(Show::Draw::ParticleChance);
	if ((x & 255) < em.chance) {
		return;
	}
	const uint16_t y = show.draw(Show::Draw::ParticlePoint);
	slot.emissions++;
	place(world, slot);
	ShowParticle p;
	p.position = slot.position;
	if (!em.launches.empty()) {
		const std::array<float, 3>& l = em.launches[y % em.launches.size()];
		p.velocity = toWorld(slot.rotation, vec(l)) + slot.velocity;
	}
	else {
		p.velocity = slot.velocity;
	}
	if (!slot.anchored) {
		slot.anchor = p.position;
		slot.anchored = true;
	}
	if (slot.particles.size() >= MostParticles) {
		// The oldest (the first with the greatest age, newest first) is reused.
		size_t oldest = 0;
		for (size_t k = 1; k < slot.particles.size(); k++) {
			oldest = slot.particles[k].age > slot.particles[oldest].age ? k : oldest;
		}
		slot.particles.erase(slot.particles.begin() + std::ptrdiff_t(oldest));
	}
	slot.particles.insert(slot.particles.begin(), p);
}

void Effects::updateSets(Show& show, const World& world, int32_t ms)
{
	for (EffectSlot& slot : sets[0].slots) {
		if (slot.active && slot.emitter >= 0 && slot.place == EffectSlot::OilSlick && !slot.bubbled) {
			bubble(show, slot);
		}
	}
	for (size_t s = 0; s < 2; s++) {
		EffectSet& set = sets[s];
		for (EffectSlot& slot : set.slots) {
			if (!slot.active) {
				continue;
			}
			if (slot.emitter >= 0) {
				LR_ASSERT(inRange(set.file.emitters, slot.emitter));
				const EmitterFile::Emitter& em = set.file.emitters[slot.emitter];
				slot.timerMs += ms;
				slot.ageMs += ms;
				if (em.durationMs >= 0 && slot.ageMs >= em.durationMs) {
					// It finishes by itself; its particles aren't aged this frame (QUIRK).
					if (slot.handle >= 0) {
						LR_ASSERT(inRange(set.handles, slot.handle));
						set.handles[slot.handle] = -1;
						slot.handle = -1;
					}
					finishSlot(slot);
					continue;
				}
				if (slot.timerMs >= em.intervalMs) {
					emitParticle(show, world, slot, em);
					slot.timerMs = 0;
				}
			}
			else {
				slot.timerMs += ms;
				slot.ageMs += ms;
			}
			if (slot.particles.empty() && slot.finishPending) {
				slot.active = false;
				continue;
			}
			LR_ASSERT(inRange(set.file.emitters, slot.look));
			const int32_t life = set.file.emitters[slot.look].lifeMs;
			const Vec3 a = vec(set.file.emitters[slot.look].acceleration);
			const float t = float(ms) / 1000;
			for (ShowParticle& p : slot.particles) {
				p.age += ms;
				p.position += p.velocity * t + a * (0.5f * t * t);
				p.velocity += a * t;
			}
			slot.particles.erase(
				std::remove_if(slot.particles.begin(), slot.particles.end(), [&](const ShowParticle& p) { return p.age > life; }),
				slot.particles.end());
		}
	}
}

// Where a slot sits this frame.
void Effects::place(const World& world, EffectSlot& slot)
{
	const size_t n = world.racers.karts.size();
	switch (slot.place) {
	case EffectSlot::AtPoint: break;
	case EffectSlot::Wheel:
	case EffectSlot::RearAxle:
	case EffectSlot::Exhaust:
	case EffectSlot::AboveOwner: {
		if (slot.racer < 0 || size_t(slot.racer) >= n) {
			break;
		}
		const size_t r = size_t(slot.racer);
		const Pose& car = world.racers.visible[r];
		const Kart& k = world.racers.karts[r];
		slot.rotation = car.rotation;
		if (slot.place == EffectSlot::Wheel) {
			slot.position = k.wheels[slot.wheel & 3].position;
			slot.velocity = k.velocity * 600.0f;
		}
		else if (slot.place == EffectSlot::RearAxle) {
			slot.position = (k.wheels[2].position + k.wheels[3].position) * 0.5f + Vec3{0, 0, 2};
			slot.velocity = k.velocity * 600.0f;
		}
		else if (slot.place == EffectSlot::Exhaust) {
			slot.position = car.position + toWorld(car.rotation, {-2, 0, 3});
			slot.velocity = k.velocity * 700.0f;
		}
		else {
			slot.position = car.position + Vec3{0, 0, 4};
			slot.velocity = {};
		}
		break;
	}
	case EffectSlot::OilSlick: {
		if (inRange(world.powerUps.actions, slot.racer)) {
			slot.position = world.powerUps.actions[slot.racer].point + slot.offset;
		}
		slot.rotation = {};
		slot.velocity = {};
		break;
	}
	case EffectSlot::Dynamite: {
		if (inRange(world.powerUps.actions, slot.racer)) {
			const Action& a = world.powerUps.actions[slot.racer];
			const float angle = a.phase;
			const float c = std::cos(angle), s = std::sin(angle);
			const Basis rows = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
			slot.rotation = rows;
			slot.position = a.shot.at + toWorld(rows, {-0.7277f, 0.1558f, 5.1785f});
		}
		slot.velocity = {};
		break;
	}
	}
}

// --- Events ---

void Effects::apply(Show& show, const World& world, const RaceEvent& e)
{
	const size_t n = visuals.size();
	const bool racer = e.owner.kind == Owner::Racer && e.owner.index >= 0 && size_t(e.owner.index) < n;
	const size_t i = racer ? size_t(e.owner.index) : 0;
	switch (e.kind) {
	case RaceEvent::Spawn: spawnFor(world, e); break;
	case RaceEvent::Finish:
	case RaceEvent::Release: letGoOf(show, e); break;
	case RaceEvent::StopEffects: stopAll(); break;
	case RaceEvent::DustStart: {
		if (!racer) {
			break;
		}
		Visuals& v = visuals[i];
		if (v.dust.held() || v.wheels[2].held() || v.wheels[3].held()) {
			break;
		}
		v.dust = spawn(world, 0, "dust", e.owner, nullptr, EffectSlot::Wheel, int16_t(i), 2);
		break;
	}
	case RaceEvent::SmokeStart:
		if (racer && !visuals[i].smoke.held()) {
			visuals[i].smoke = spawn(world, 0, "carsmke", e.owner, nullptr, EffectSlot::RearAxle, int16_t(i));
		}
		break;
	case RaceEvent::SkidStart:
		if (racer) {
			Visuals& v = visuals[i];
			if (!v.tyre.held()) {
				v.tyre = spawn(world, 0, "tiresmk", e.owner, nullptr, EffectSlot::Wheel, int16_t(i), 3);
			}
			v.skidding = true;
			v.spinning = e.value == RaceEvent::Spin;
			v.turbo = e.value == RaceEvent::Turbo;
		}
		break;
	case RaceEvent::SkidStop:
		if (racer) {
			Visuals& v = visuals[i];
			finish(v.tyre);
			v.skidding = false;
		}
		break;
	case RaceEvent::Landed:
		if (racer) {
			const Vec3 at = world.racers.visible[i].position;
			spawn(world, 0, "carland", e.owner, &at, EffectSlot::AtPoint, -1);
		}
		break;
	case RaceEvent::Sparks: spawn(world, 0, "carsprk", e.owner, &e.place, EffectSlot::AtPoint, -1); break;
	case RaceEvent::WheelParticle:
		if (racer) {
			wheelParticle(world, e, i);
		}
		break;
	default: break; // (the sky's, the sounds and voices: the show's other parts)
	}
}

// Spawn: the named effect at the event's place, some following their action or its owner (held while the owner is).
void Effects::spawnFor(const World& world, const RaceEvent& e)
{
	const char* name = e.name.data();
	EffectSlot::Place where = EffectSlot::AtPoint;
	int16_t follows = -1;
	const bool action = e.owner.kind == Owner::Action && inRange(world.powerUps.actions, e.owner.index);
	const Action* a = action ? &world.powerUps.actions[e.owner.index] : nullptr;
	if (std::strcmp(name, "oilbub") == 0 && a) {
		where = EffectSlot::OilSlick, follows = e.owner.index;
	}
	else if (std::strcmp(name, "dynsprk") == 0 && a) {
		where = EffectSlot::Dynamite, follows = e.owner.index;
	}
	else if (std::strcmp(name, "trbsmke") == 0 && a && a->owner >= 0) {
		where = EffectSlot::Exhaust, follows = a->owner;
	}
	else if (std::strcmp(name, "cannsmk") == 0 && a && a->owner >= 0 && a->emplacement < 0) {
		where = EffectSlot::AboveOwner, follows = a->owner;
	}
	Vec3 at = e.place;
	// A hazard's spawn without a place: at its model.
	if (e.owner.kind == Owner::Hazard && isZero(at) && inRange(world.scenery.hazards, e.owner.index)) {
		const int16_t m = world.track.hazards[e.owner.index].model;
		if (inRange(world.track.animated, m)) {
			at = world.track.animated[m].pose.position;
		}
	}
	const EffectHandle h = spawn(world, e.set, name, e.owner, &at, where, follows);
	if (h.held()) {
		owned.push_back({e.owner, h});
	}
}

// Finish or Release: the owner's held effects let go.
void Effects::letGoOf(Show& show, const RaceEvent& e)
{
	for (size_t k = 0; k < owned.size();) {
		if (owned[k].first == e.owner) {
			EffectHandle h = owned[k].second;
			// (An oil slick's frame moves its bubbles before its time runs out.)
			const int16_t s = sets[h.set].handles[h.index];
			if (s >= 0 && sets[h.set].slots[s].place == EffectSlot::OilSlick && !sets[h.set].slots[s].bubbled &&
				sets[h.set].slots[s].slickWas && e.stage == RaceEvent::Powers) {
				bubble(show, sets[h.set].slots[s]);
			}
			e.kind == RaceEvent::Finish ? finish(h) : release(h);
			owned.erase(owned.begin() + std::ptrdiff_t(k));
		}
		else {
			k++;
		}
	}
}

// A racer's wheel's particle (by surface): released when it changes or goes, spawned anew.
void Effects::wheelParticle(const World& world, const RaceEvent& e, size_t i)
{
	Visuals& v = visuals[i];
	const size_t w = size_t(e.value) & 3;
	if (!named(e.name)) {
		release(v.wheels[w]);
		v.wanted[w] = {};
		return;
	}
	if (v.wheels[w].held() && v.wanted[w] == e.name) {
		return;
	}
	release(v.wheels[w]);
	v.wanted[w] = e.name;
	v.wheels[w] = spawn(world, 2, e.name.data(), e.owner, nullptr, EffectSlot::Wheel, int16_t(i), uint8_t(w));
}

bool Effects::holds(Owner owner) const
{
	for (const auto& [o, h] : owned) {
		if (o == owner && h.held()) {
			return true;
		}
	}
	return false;
}

// The rules' particles still wanted without a handle are tried again each frame, as their part of the frame begins:
// a track event's particle active since last frame, a snowfall's or a smoke vent's on since then.
void Effects::retries(const World& world, bool hazards)
{
	if (!hazards) {
		for (size_t i = 0; i < world.track.events.size() && i < m_wanting.size(); i++) {
			const EventResource& r = world.track.events[i];
			const Owner o{Owner::TrackEvent, int16_t(i)};
			if (m_wanting[i] && !holds(o)) {
				const EffectHandle h = spawn(world, 2, r.name.data(), o, &world.scenery.events[i].at, EffectSlot::AtPoint, -1);
				if (h.held()) {
					owned.push_back({o, h});
				}
			}
		}
		return;
	}
	for (size_t i = 0; i < world.scenery.hazards.size() && i < m_hazardsOn.size(); i++) {
		const Hazard& hz = world.scenery.hazards[i];
		const Owner o{Owner::Hazard, int16_t(i)};
		if (m_hazardsOn[i] && hz.on && !holds(o)) {
			RaceEvent e;
			e.kind = RaceEvent::Spawn;
			e.owner = o;
			e.set = 2;
			const char* name = hz.type == Hazard::Snow ? "snow" : "smoke";
			std::memcpy(e.name.data(), name, std::strlen(name));
			spawnFor(world, e);
		}
	}
}

// The car visuals' per-step part, once a frame.
void Effects::carVisuals(Show& show, const World& world)
{
	for (size_t i = 0; i < visuals.size(); i++) {
		Visuals& v = visuals[i];
		// Wheel particles still wanted without a handle: tried again.
		for (size_t w = 0; w < 4; w++) {
			if (named(v.wanted[w]) && !v.wheels[w].held()) {
				v.wheels[w] = spawn(world, 2, v.wanted[w].data(), {Owner::Racer, int16_t(i), uint8_t(w)}, nullptr, EffectSlot::Wheel,
					int16_t(i), uint8_t(w));
			}
		}
		if (world.racers.race[i].flags & RacerGhost) {
			continue;
		}
		if (v.dust.held()) {
			if (controls(v.dust, 10)) {
				finish(v.dust);
			}
			else {
				const uint16_t x = show.draw(Show::Draw::DustCorner, int16_t(i));
				const int16_t s = sets[v.dust.set].handles[v.dust.index];
				if (s >= 0) {
					sets[v.dust.set].slots[s].wheel = (x & 2) ? 3 : 2;
				}
			}
		}
		if (v.smoke.held() && controls(v.smoke, 4)) {
			finish(v.smoke);
		}
		if (v.tyre.held()) {
			const uint16_t x = show.draw(Show::Draw::TyreWheel, int16_t(i));
			const int16_t s = sets[v.tyre.set].handles[v.tyre.index];
			if (s >= 0) {
				// The smoke at one of the wheels laying marks.
				uint8_t wheels[4], count = 0;
				for (uint8_t w = 0; w < 4; w++) {
					if (v.skidWheels & (1u << w)) {
						wheels[count++] = w;
					}
				}
				sets[v.tyre.set].slots[s].wheel = count ? wheels[x % count] : uint8_t(2 + (x & 1));
			}
		}
	}
}

void Effects::moveParticles(Show& show, const World& world)
{
	for (EffectSet& set : sets) {
		for (EffectSlot& slot : set.slots) {
			if (slot.active && slot.emitter >= 0 && slot.place != EffectSlot::OilSlick) {
				place(world, slot);
			}
		}
	}
	// A held snowfall's or smoke vent's particle: one draw each frame and its place.
	for (const auto& [o, h] : owned) {
		const int16_t s = o.kind == Owner::Hazard && h.held() ? sets[h.set].handles[h.index] : -1;
		if (s < 0 || !inRange(world.scenery.hazards, o.index)) {
			continue;
		}
		EffectSlot& slot = sets[h.set].slots[s];
		if (world.scenery.hazards[o.index].type == Hazard::Snow && !world.raceView.cameras.empty()) {
			// 100 ft ahead of camera 0, 40 ft up (z up: the camera's down row's z
			// taken away; the flakes fall 42-48 ft in their life, so they cross the view), (x % 200) - 100 ft
			// sideways.
			const int32_t x = int16_t(show.draw(Show::Draw::Hazard));
			Vec3 right, down, look;
			world.raceView.cameras[0].shownRows(right, down, look);
			slot.position = world.raceView.cameras[0].shownEye() + look * 100.0f + right * float(x % 200 - 100);
			slot.position.z -= 40.0f * down.z;
		}
		else if (world.scenery.hazards[o.index].type == Hazard::Smoke) {
			// One of four points of "dp_def" (the executable's, not the hazard file's entity), in bone 0's frame.
			static const Vec3 Offsets[4] = {{-24.45f, 26.74f, -19.56f}, {-35.72f, 9.41f, -18.41f}, {-6.9f, -9.13f, -15.49f},
				{4.37f, 8.54f, -16.65f}};
			const uint16_t x = show.draw(Show::Draw::Hazard);
			const int16_t m = world.track.hazardEntities.smokeStack;
			if (m >= 0) {
				const AnimatedModel& model = world.track.animated[m];
				const Clock& c = world.scenery.clocks[m];
				const Animation* a = world.track.animationOf(model);
				const Pose bone = world.scenery.nodeLocal(model, a, 0, c.part, c.drawn, c.loop);
				Vec3 at = Offsets[x & 3];
				at.y = world.track.mirrored ? -at.y : at.y;
				at = toWorld(bone.rotation, at * (1.0f / model.scale)) + bone.position;
				slot.position = toWorld(model.pose.rotation, at * model.scale) + model.pose.position;
			}
		}
	}
}

bool Effects::particleLook(size_t s, const EffectSlot& slot, const ShowParticle& p, float& width, float& height,
	std::string& material) const
{
	const EffectSet& set = sets[s];
	if (!inRange(set.file.emitters, slot.look)) {
		return false;
	}
	const EmitterFile::Emitter& em = set.file.emitters[slot.look];
	const float grown = em.lifeMs > 0 ? float(p.age) / float(em.lifeMs) : 0;
	width = em.width, height = em.height;
	if (em.growWidth != 0 || em.growHeight != 0) {
		width += em.growWidth * grown;
		height += em.growHeight * grown;
	}
	if (width <= 0 || height <= 0) {
		return false;
	}
	material = em.material;
	if (inRange(set.tracks.tracks, em.track)) {
		// The track's frame at the particle's age (the age as the cursor).
		const MaterialAnimationFile::Track& t = set.tracks.tracks[em.track];
		const int32_t f = int32_t(float(t.rate) / 1000 * float(p.age)) % std::max(1, t.duration);
		const int32_t first = std::max(0, t.firstFrame), last = std::min<int32_t>(first + t.frames, int32_t(set.tracks.frames.size()));
		int32_t shown = last - 1;
		for (int32_t k = first; k < last; k++) {
			shown = set.tracks.frames[k].start <= f ? k : shown;
		}
		if (shown >= 0) {
			material = set.tracks.frames[shown].material;
		}
	}
	return !material.empty();
}

} // namespace lr
