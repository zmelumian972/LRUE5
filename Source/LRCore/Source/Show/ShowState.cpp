// The show: the effect sets, the car visuals' handles, particles,
// decals and the sky, run from the world's events and race view after each race frame.

#include "LRCore/Show/ShowState.h"

#include "LRCore/Race/World.h"
#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Scene.h"
#include "Powers/PowerParts.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lr {

namespace {

const std::string Common = "GAMEDATA/COMMON/";
constexpr size_t SetSlots[2] = {10, 4};
constexpr size_t MostParticles = 16;

bool named(const std::array<char, 9>& name) { return name[0] != 0; }

std::string folderOf(const std::string& path) { return path.substr(0, path.rfind('/') + 1); }

// A file of the track's folder, else of COMMON (the race files' rule).
std::string found(const Cache& cache, const std::string& dir, const std::string& name)
{
	return cache.has(dir + name) ? dir + name : Common + name;
}

} // namespace

uint16_t Show::draw(Draw why, int16_t racer)
{
	if (drawWatch) {
		return drawWatch(*this, why, racer, drawContext);
	}
	random = (random + 1) & 1023;
	return randomTable(random);
}

bool Show::load(const Cache& cache, const World& world)
{
	const uint32_t keep = random;
	const float volume = effectsVolume;
	auto* watch = drawWatch;
	void* context = drawContext;
	*this = {};
	random = keep, drawWatch = watch, drawContext = context, effectsVolume = volume;
	const std::string& track = world.brief.track;
	const std::string dir = "GAMEDATA/" + track + "/";
	RaceFile race;
	if (!cache.get(dir + track + ".RAB", race)) {
		return false;
	}
	// The effect sets: the race's (EMITTER.EMB) and the track's, each with its world's material animation and libraries
	// (GLOBAL.WDB's, the track's LOCAL.WDB's).
	const std::string worlds[2] = {race.globalWorld, race.localWorld};
	for (size_t s = 0; s < 2; s++) {
		EffectSet& set = sets[s];
		set.slots.assign(SetSlots[s], {});
		set.handles.fill(-1);
		if (!race.emitterFiles[s].empty()) {
			cache.get(found(cache, dir, binaryName(race.emitterFiles[s])), set.file);
		}
		SceneFile scene;
		if (!worlds[s].empty() && cache.get(found(cache, dir, binaryName(worlds[s])), scene)) {
			const std::string folder = folderOf(found(cache, dir, binaryName(worlds[s])));
			if (!scene.materialAnimations.empty()) {
				cache.get(folder + binaryName(scene.materialAnimations[0]) + ".MAB", set.tracks);
			}
			for (size_t l = 0; l < scene.materialLibraries.size() && l < 2; l++) {
				set.libraries[l] = folder + binaryName(scene.materialLibraries[l]) + ".MDB";
			}
		}
	}
	// The chassis' shadow and skid sizes, by racer.
	ChassisTable chassisTable;
	ChampionTable champions;
	DriverTable drivers;
	cache.get(Common + "CHASSIS.CMB", chassisTable);
	cache.get(Common + "CHAMPS.CCB", champions);
	cache.get(Common + "DRIVERS.DDB", drivers);
	const size_t n = world.karts.size();
	m_chassis.assign(n, {});
	for (size_t i = 0; i < n && i < world.brief.slots.size(); i++) {
		const RaceBrief::Slot& slot = world.brief.slots[i];
		const DriverTable::Driver* driver = slot.driver.empty() ? nullptr : drivers.find(slot.driver);
		const ChampionTable::Champion* champion = driver ? champions.find(driver->champion) : nullptr;
		if (const ChassisTable::Chassis* c = chassisTable.find(champion ? champion->chassis : slot.chassis)) {
			Chassis& out = m_chassis[i];
			out.shadowWidth = c->shadowWidth;
			out.shadowLength = c->shadowLength;
			out.skidWidths = c->skidWidths;
			out.enginePitch = c->enginePitch;
			for (size_t w = 0; w < 4; w++) {
				out.skidPoints[w] = {c->skidPoints[w][0], c->skidPoints[w][1] * (world.track.mirrored ? -1.0f : 1.0f), c->skidPoints[w][2]};
			}
		}
	}
	visuals.assign(n, {});
	shadowAlpha.assign(n, {});
	scarMs.assign(world.explosions.size(), -1);
	// The ground the decals lie on: the track's collision triangles in the world, in cells of 16 ft.
	if (world.track.trackVolume < world.track.volumes.size()) {
		const Track::Volume& v = world.track.volumes[world.track.trackVolume];
		if (v.mesh < world.track.meshes.size()) {
			for (const CollisionMesh::Triangle& t : world.track.meshes[v.mesh].triangles) {
				Triangle g;
				for (int c = 0; c < 3; c++) {
					g.v[c] = toWorld(v.rotation, t.v[c]) + v.position;
				}
				m_ground.push_back(g);
			}
		}
	}
	if (!m_ground.empty()) {
		float maxX = -1e30f, maxY = -1e30f;
		m_minX = m_minY = 1e30f;
		for (const Triangle& t : m_ground) {
			for (const Vec3& p : t.v) {
				m_minX = std::min(m_minX, p.x), m_minY = std::min(m_minY, p.y);
				maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y);
			}
		}
		m_columns = int32_t((maxX - m_minX) / m_cell) + 1;
		m_rows = int32_t((maxY - m_minY) / m_cell) + 1;
		m_cells.assign(size_t(m_columns) * size_t(m_rows), {});
		for (uint32_t i = 0; i < m_ground.size(); i++) {
			const Triangle& t = m_ground[i];
			const float x0 = std::min({t.v[0].x, t.v[1].x, t.v[2].x}), x1 = std::max({t.v[0].x, t.v[1].x, t.v[2].x});
			const float y0 = std::min({t.v[0].y, t.v[1].y, t.v[2].y}), y1 = std::max({t.v[0].y, t.v[1].y, t.v[2].y});
			for (int32_t cy = int32_t((y0 - m_minY) / m_cell); cy <= int32_t((y1 - m_minY) / m_cell); cy++) {
				for (int32_t cx = int32_t((x0 - m_minX) / m_cell); cx <= int32_t((x1 - m_minX) / m_cell); cx++) {
					m_cells[size_t(cy) * size_t(m_columns) + size_t(cx)].push_back(i);
				}
			}
		}
	}
	// The sky: its first state.
	if (!race.skyFile.empty() && cache.get(dir + binaryName(race.skyFile), sky) && !sky.states.empty()) {
		hasSky = true;
		skyState = std::max(0, sky.find(sky.initial));
		updateSky(0);
	}
	loadSounds(cache, world);
	return true;
}

// --- The effect sets ---

EffectHandle Show::spawn(const World& world, int s, const char* name, Owner owner, const Vec3* at, EffectSlot::Place where, int16_t racer,
	uint8_t wheel)
{
	// Either set (2): the race's first.
	if (s == 2) {
		s = sets[0].file.find(name) >= 0 ? 0 : 1;
	}
	EffectSet& set = sets[size_t(s)];
	const int32_t e = set.file.find(name);
	if (e < 0) {
		spawned.push_back({name, -1, -1, false});
		return {};
	}
	const EmitterFile::Emitter& em = set.file.emitters[size_t(e)];
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
			const int32_t p = sl.emitter >= 0 ? set.file.emitters[size_t(sl.emitter)].priority : 0;
			if (p <= lowest) {
				lowest = p, chosen = int32_t(i);
			}
		}
		if (em.priority <= lowest) {
			spawned.push_back({name, int8_t(s), -1, false});
			return {};
		}
		if (set.slots[size_t(chosen)].handle >= 0) {
			set.handles[size_t(set.slots[size_t(chosen)].handle)] = -1; // (its handle now controls nothing)
		}
	}
	EffectSlot& slot = set.slots[size_t(chosen)];
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

void Show::finish(EffectHandle& h)
{
	if (!h.held()) {
		return;
	}
	EffectSet& set = sets[size_t(h.set)];
	const int16_t s = set.handles[size_t(h.index)];
	if (s >= 0) {
		EffectSlot& slot = set.slots[size_t(s)];
		slot.emitter = -1;
		slot.handle = -1;
		if (slot.particles.empty()) {
			slot.active = false;
		}
		else {
			slot.finishPending = true;
		}
	}
	set.handles[size_t(h.index)] = -1;
	set.used[size_t(h.index)] = false;
	h = {};
}

void Show::release(EffectHandle& h)
{
	if (!h.held()) {
		return;
	}
	EffectSet& set = sets[size_t(h.set)];
	const int16_t s = set.handles[size_t(h.index)];
	if (s >= 0) {
		EffectSlot& slot = set.slots[size_t(s)];
		slot.active = false;
		slot.emitter = -1;
		slot.handle = -1;
		slot.particles.clear();
	}
	set.handles[size_t(h.index)] = -1;
	set.used[size_t(h.index)] = false;
	h = {};
}

bool Show::controls(EffectHandle h, int32_t atLeast) const
{
	if (!h.held()) {
		return false;
	}
	const int16_t s = sets[size_t(h.set)].handles[size_t(h.index)];
	return s >= 0 && sets[size_t(h.set)].slots[size_t(s)].emissions >= atLeast;
}

void Show::stopAll()
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
void Show::bubble(EffectSlot& slot)
{
	const uint16_t x = draw(Draw::OilBubble), y = draw(Draw::OilBubble);
	const float a = float(x % 628) * 0.01f, d = float(y % 7);
	slot.offset = {std::sin(a) * d, std::cos(a) * d, 0};
	slot.bubbled = true;
}

void Show::updateSets(const World& world, int32_t ms)
{
	for (EffectSlot& slot : sets[0].slots) {
		if (slot.active && slot.emitter >= 0 && slot.place == EffectSlot::OilSlick && !slot.bubbled) {
			bubble(slot);
		}
	}
	for (size_t s = 0; s < 2; s++) {
		EffectSet& set = sets[s];
		for (EffectSlot& slot : set.slots) {
			if (!slot.active) {
				continue;
			}
			bool emitted = false;
			if (slot.emitter >= 0) {
				const EmitterFile::Emitter& em = set.file.emitters[size_t(slot.emitter)];
				slot.timerMs += ms;
				slot.ageMs += ms;
				if (em.durationMs >= 0 && slot.ageMs >= em.durationMs) {
					// It finishes by itself; its particles aren't aged this frame (QUIRK).
					slot.emitter = -1;
					if (slot.handle >= 0) {
						set.handles[size_t(slot.handle)] = -1;
						slot.handle = -1;
					}
					if (slot.particles.empty()) {
						slot.active = false;
					}
					else {
						slot.finishPending = true;
					}
					continue;
				}
				if (slot.timerMs >= em.intervalMs) {
					drawingFor = em.name + " " + std::to_string(slot.owner.kind) + "/" + std::to_string(slot.owner.index);
					const uint16_t x = draw(Draw::ParticleChance);
					if ((x & 255) >= em.chance) {
						const uint16_t y = draw(Draw::ParticlePoint);
						slot.emissions++;
						emitted = true;
						place(world, slot);
						ShowParticle p;
						p.position = slot.position;
						if (!em.launches.empty()) {
							const std::array<float, 3>& l = em.launches[y % em.launches.size()];
							p.velocity = slot.rotation.f * l[0] + slot.rotation.s * l[1] + slot.rotation.u * l[2] + slot.velocity;
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
					slot.timerMs = 0;
				}
			}
			else {
				slot.timerMs += ms;
				slot.ageMs += ms;
			}
			(void) emitted;
			if (slot.particles.empty() && slot.finishPending) {
				slot.active = false;
				continue;
			}
			const int32_t life = set.file.emitters[size_t(slot.look)].lifeMs;
			const Vec3 a = vec(set.file.emitters[size_t(slot.look)].acceleration);
			const float t = float(ms) / 1000;
			for (ShowParticle& p : slot.particles) {
				p.age += ms;
				p.position += p.velocity * t + a * (0.5f * t * t);
				p.velocity += a * t;
			}
			slot.particles.erase(std::remove_if(slot.particles.begin(), slot.particles.end(), [&](const ShowParticle& p) { return p.age > life; }),
				slot.particles.end());
		}
	}
}

// Where a slot sits this frame.
void Show::place(const World& world, EffectSlot& slot)
{
	const size_t n = world.karts.size();
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
		const Pose& car = world.visible[r];
		const Kart& k = world.karts[r];
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
		if (slot.racer >= 0 && size_t(slot.racer) < world.actions.size()) {
			slot.position = world.actions[size_t(slot.racer)].point + slot.offset;
		}
		slot.rotation = {};
		slot.velocity = {};
		break;
	}
	case EffectSlot::Dynamite: {
		if (slot.racer >= 0 && size_t(slot.racer) < world.actions.size()) {
			const Action& a = world.actions[size_t(slot.racer)];
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

void Show::apply(const World& world, const RaceEvent& e)
{
	const size_t n = visuals.size();
	const bool racer = e.owner.kind == Owner::Racer && e.owner.index >= 0 && size_t(e.owner.index) < n;
	const size_t i = racer ? size_t(e.owner.index) : 0;
	switch (e.kind) {
	case RaceEvent::Spawn: {
		const char* name = e.name.data();
		EffectSlot::Place where = EffectSlot::AtPoint;
		int16_t follows = -1;
		const bool action = e.owner.kind == Owner::Action && e.owner.index >= 0 && size_t(e.owner.index) < world.actions.size();
		const Action* a = action ? &world.actions[size_t(e.owner.index)] : nullptr;
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
		if (e.owner.kind == Owner::Hazard && isZero(at) && e.owner.index >= 0 && size_t(e.owner.index) < world.hazards.size()) {
			const int16_t m = world.track.hazards[size_t(e.owner.index)].model;
			if (m >= 0 && size_t(m) < world.track.animated.size()) {
				at = world.track.animated[size_t(m)].pose.position;
			}
		}
		const EffectHandle h = spawn(world, e.set, name, e.owner, &at, where, follows);
		if (h.held()) {
			owned.push_back({e.owner, h});
		}
		break;
	}
	case RaceEvent::Finish:
	case RaceEvent::Release:
		for (size_t k = 0; k < owned.size();) {
			if (owned[k].first == e.owner) {
				EffectHandle h = owned[k].second;
				// (An oil slick's frame moves its bubbles before its time runs out.)
				const int16_t s = sets[size_t(h.set)].handles[size_t(h.index)];
				if (s >= 0 && sets[size_t(h.set)].slots[size_t(s)].place == EffectSlot::OilSlick && !sets[size_t(h.set)].slots[size_t(s)].bubbled &&
					sets[size_t(h.set)].slots[size_t(s)].slickWas && e.stage == RaceEvent::Powers) {
					bubble(sets[size_t(h.set)].slots[size_t(s)]);
				}
				e.kind == RaceEvent::Finish ? finish(h) : release(h);
				owned.erase(owned.begin() + std::ptrdiff_t(k));
			}
			else {
				k++;
			}
		}
		break;
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
			const Vec3 at = world.visible[i].position;
			spawn(world, 0, "carland", e.owner, &at, EffectSlot::AtPoint, -1);
		}
		break;
	case RaceEvent::Sparks: spawn(world, 0, "carsprk", e.owner, &e.place, EffectSlot::AtPoint, -1); break;
	case RaceEvent::WheelParticle: {
		if (!racer) {
			break;
		}
		Visuals& v = visuals[i];
		const size_t w = size_t(e.value) & 3;
		if (!named(e.name)) {
			release(v.wheels[w]);
			v.wanted[w] = {};
			break;
		}
		if (v.wheels[w].held() && v.wanted[w] == e.name) {
			break;
		}
		release(v.wheels[w]);
		v.wanted[w] = e.name;
		v.wheels[w] = spawn(world, 2, e.name.data(), e.owner, nullptr, EffectSlot::Wheel, int16_t(i), uint8_t(w));
		break;
	}
	case RaceEvent::Sky: {
		if (!hasSky) {
			break;
		}
		if (e.flags & 1) {
			domeShown = false;
		}
		if (e.flags & 2) {
			domeShown = true;
		}
		if (e.flags & 4) {
			worldShown = false;
		}
		if (e.flags & 8) {
			worldShown = true;
		}
		const int32_t to = named(e.name) ? sky.find(e.name.data()) : -1;
		if (to >= 0 && to != skyState) {
			blendFrom = skyState;
			blendMs = 0;
			blendLength = std::max(0, e.value);
			skyState = to;
			skyKey = 0;
			skyMs = 0;
		}
		break;
	}
	default: break; // (sounds and voices: the show's sound half)
	}
}

bool Show::holds(Owner owner) const
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
void Show::retries(const World& world, bool hazards)
{
	if (!hazards) {
		for (size_t i = 0; i < world.track.events.size() && i < m_wanting.size(); i++) {
			const EventResource& r = world.track.events[i];
			const Owner o{Owner::TrackEvent, int16_t(i)};
			if (m_wanting[i] && !holds(o)) {
				const EffectHandle h = spawn(world, 2, r.name.data(), o, &world.events[i].at, EffectSlot::AtPoint, -1);
				if (h.held()) {
					owned.push_back({o, h});
				}
			}
		}
		return;
	}
	for (size_t i = 0; i < world.hazards.size() && i < m_hazardsOn.size(); i++) {
		const Hazard& hz = world.hazards[i];
		const Owner o{Owner::Hazard, int16_t(i)};
		if (m_hazardsOn[i] && hz.on && !holds(o)) {
			RaceEvent e;
			e.kind = RaceEvent::Spawn;
			e.owner = o;
			e.set = 2;
			const char* name = hz.type == Hazard::Snow ? "snow" : "smoke";
			std::memcpy(e.name.data(), name, std::strlen(name));
			apply(world, e);
		}
	}
}

// The car visuals' per-step part, once a frame.
void Show::carVisuals(const World& world)
{
	for (size_t i = 0; i < visuals.size(); i++) {
		Visuals& v = visuals[i];
		// Wheel particles still wanted without a handle: tried again.
		for (size_t w = 0; w < 4; w++) {
			if (named(v.wanted[w]) && !v.wheels[w].held()) {
				v.wheels[w] = spawn(world, 2, v.wanted[w].data(), {Owner::Racer, int16_t(i), uint8_t(w)}, nullptr, EffectSlot::Wheel, int16_t(i),
					uint8_t(w));
			}
		}
		if (world.racers[i].flags & RacerGhost) {
			continue;
		}
		if (v.dust.held()) {
			if (controls(v.dust, 10)) {
				finish(v.dust);
			}
			else {
				const uint16_t x = draw(Draw::DustCorner, int16_t(i));
				const int16_t s = sets[size_t(v.dust.set)].handles[size_t(v.dust.index)];
				if (s >= 0) {
					sets[size_t(v.dust.set)].slots[size_t(s)].wheel = (x & 2) ? 3 : 2;
				}
			}
		}
		if (v.smoke.held() && controls(v.smoke, 4)) {
			finish(v.smoke);
		}
		if (v.tyre.held()) {
			const uint16_t x = draw(Draw::TyreWheel, int16_t(i));
			const int16_t s = sets[size_t(v.tyre.set)].handles[size_t(v.tyre.index)];
			if (s >= 0) {
				// The smoke at one of the wheels laying marks.
				uint8_t wheels[4], count = 0;
				for (uint8_t w = 0; w < 4; w++) {
					if (v.skidWheels & (1u << w)) {
						wheels[count++] = w;
					}
				}
				sets[size_t(v.tyre.set)].slots[size_t(s)].wheel = count ? wheels[x % count] : uint8_t(2 + (x & 1));
			}
		}
	}
}

void Show::moveParticles(const World& world, int32_t ms)
{
	(void) ms;
	for (EffectSet& set : sets) {
		for (EffectSlot& slot : set.slots) {
			if (slot.active && slot.emitter >= 0 && slot.place != EffectSlot::OilSlick) {
				place(world, slot);
			}
		}
	}
	// A held snowfall's or smoke vent's particle: one draw each frame and its place.
	for (const auto& [o, h] : owned) {
		const int16_t s = o.kind == Owner::Hazard && h.held() ? sets[size_t(h.set)].handles[size_t(h.index)] : -1;
		if (s < 0 || size_t(o.index) >= world.hazards.size()) {
			continue;
		}
		EffectSlot& slot = sets[size_t(h.set)].slots[size_t(s)];
		if (world.hazards[size_t(o.index)].type == Hazard::Snow && !world.cameras.empty()) {
			// 100 ft ahead of camera 0, 40 ft up (z up: the camera's down row's z
			// taken away; the flakes fall 42-48 ft in their life, so they cross the view), (x % 200) - 100 ft
			// sideways.
			const int32_t x = int16_t(draw(Draw::Hazard));
			Vec3 right, down, look;
			world.cameras[0].shownRows(right, down, look);
			slot.position = world.cameras[0].shownEye() + look * 100.0f + right * float(x % 200 - 100);
			slot.position.z -= 40.0f * down.z;
		}
		else if (world.hazards[size_t(o.index)].type == Hazard::Smoke) {
			// One of four points of "dp_def" (the executable's, not the hazard file's entity), in bone 0's frame.
			static const Vec3 Offsets[4] = {{-24.450001f, 26.74f, -19.559999f}, {-35.720001f, 9.4099998f, -18.41f},
				{-6.9000001f, -9.1300001f, -15.49f}, {4.3699999f, 8.54f, -16.65f}};
			const uint16_t x = draw(Draw::Hazard);
			const int16_t m = world.track.animatedOf("dp_def");
			if (m >= 0) {
				const AnimatedModel& model = world.track.animated[size_t(m)];
				const Clock& c = world.clocks[size_t(m)];
				const Animation* a = model.animation >= 0 ? &world.track.animations[size_t(model.animation)] : nullptr;
				const Pose bone = world.nodeLocal(model, a, 0, c.part, c.drawn, c.loop);
				Vec3 at = Offsets[x & 3];
				at.y = world.track.mirrored ? -at.y : at.y;
				at = toWorld(bone.rotation, at * (1.0f / model.scale)) + bone.position;
				slot.position = toWorld(model.pose.rotation, at * model.scale) + model.pose.position;
			}
		}
	}
}

bool Show::particleLook(size_t s, const EffectSlot& slot, const ShowParticle& p, float& width, float& height, std::string& material) const
{
	const EffectSet& set = sets[s];
	if (slot.look < 0 || size_t(slot.look) >= set.file.emitters.size()) {
		return false;
	}
	const EmitterFile::Emitter& em = set.file.emitters[size_t(slot.look)];
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
	if (em.track >= 0 && size_t(em.track) < set.tracks.tracks.size()) {
		// The track's frame at the particle's age (the age as the cursor).
		const MaterialAnimationFile::Track& t = set.tracks.tracks[size_t(em.track)];
		const int32_t f = int32_t(float(t.rate) / 1000 * float(p.age)) % std::max(1, t.duration);
		const int32_t first = t.firstFrame, last = std::min<int32_t>(first + t.frames, int32_t(set.tracks.frames.size()));
		int32_t shown = last - 1;
		for (int32_t k = first; k < last; k++) {
			shown = set.tracks.frames[size_t(k)].start <= f ? k : shown;
		}
		if (shown >= 0) {
			material = set.tracks.frames[size_t(shown)].material;
		}
	}
	return !material.empty();
}

// --- Decals ---

void Show::project(Decal& d, Vec3 centre, Vec3 along, float width, float length) const
{
	d.points.clear();
	d.uvs.clear();
	if (m_cells.empty()) {
		return;
	}
	along.z = 0;
	along = isZero(along) ? Vec3{1, 0, 0} : normalize(along);
	const Vec3 across = {-along.y, along.x, 0};
	const float hl = length * 0.5f, hw = width * 0.5f;
	const float reach = std::sqrt(hl * hl + hw * hw);
	const float top = centre.z, bottom = centre.z - 15; // the box: centred above the ground point, 15 deep
	// The triangles under the box, clipped to its four sides (in its own frame: u along the length, v across).
	std::vector<uint32_t> seen;
	const int32_t cx0 = std::max(0, int32_t((centre.x - reach - m_minX) / m_cell)), cx1 = std::min(m_columns - 1, int32_t((centre.x + reach - m_minX) / m_cell));
	const int32_t cy0 = std::max(0, int32_t((centre.y - reach - m_minY) / m_cell)), cy1 = std::min(m_rows - 1, int32_t((centre.y + reach - m_minY) / m_cell));
	for (int32_t cy = cy0; cy <= cy1; cy++) {
		for (int32_t cx = cx0; cx <= cx1; cx++) {
			for (uint32_t t : m_cells[size_t(cy) * size_t(m_columns) + size_t(cx)]) {
				if (std::find(seen.begin(), seen.end(), t) != seen.end()) {
					continue;
				}
				seen.push_back(t);
				const Triangle& tri = m_ground[t];
				const Vec3 nrm = cross(tri.v[1] - tri.v[0], tri.v[2] - tri.v[0]);
				if (std::fabs(nrm.z) <= 0.1f * std::sqrt(lengthSquared(nrm))) {
					continue; // (a wall: nothing lies on it; the collision mesh winds either way)
				}
				// In the box's frame.
				std::vector<std::array<float, 3>> poly; // u, v, z
				for (const Vec3& p : tri.v) {
					const Vec3 o = p - centre;
					poly.push_back({dot(o, along), dot(o, across), p.z});
				}
				const auto clip = [&](int axis, float limit, float sign) {
					std::vector<std::array<float, 3>> out;
					for (size_t k = 0; k < poly.size(); k++) {
						const std::array<float, 3>& a = poly[k];
						const std::array<float, 3>& b = poly[(k + 1) % poly.size()];
						const float da = sign * a[size_t(axis)] - limit, db = sign * b[size_t(axis)] - limit;
						if (da <= 0) {
							out.push_back(a);
						}
						if ((da < 0) != (db < 0) && da != db) {
							const float f = da / (da - db);
							out.push_back({a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f});
						}
					}
					poly = out;
				};
				clip(0, hl, 1), clip(0, hl, -1), clip(1, hw, 1), clip(1, hw, -1);
				if (poly.size() < 3) {
					continue;
				}
				bool inside = false;
				for (const std::array<float, 3>& q : poly) {
					inside = inside || (q[2] <= top + 1e-3f && q[2] >= bottom);
				}
				if (!inside) {
					continue;
				}
				for (size_t k = 1; k + 1 < poly.size(); k++) {
					for (const std::array<float, 3>* q : {&poly[0], &poly[k], &poly[k + 1]}) {
						d.points.push_back(centre + along * (*q)[0] + across * (*q)[1] + Vec3{0, 0, (*q)[2] - centre.z});
						d.uvs.push_back({(*q)[1] / width + 0.5f, (*q)[0] / length + 0.5f});
					}
				}
			}
		}
	}
}

void Show::updateDecals(const World& world, int32_t ms)
{
	decals.clear();
	const size_t n = world.karts.size();
	// The cars' shadows: at the car's bounds centre, 6 up, 8 x 9 (the chassis') times its scale, along its -row 1;
	// alpha per camera by the eye's distance.
	for (size_t i = 0; i < n; i++) {
		shadowAlpha[i] = {0, 0};
		if (world.racers[i].flags & RacerGhost) {
			continue;
		}
		const Pose& car = world.visible[i];
		const float scale = world.powers.empty() ? 1.0f : world.powers[i].carScale;
		const Vec3 centre = toWorld(car.rotation, world.carBounds[i].centre) + car.position;
		bool any = false;
		for (size_t c = 0; c < world.cameras.size() && c < 2; c++) {
			const Vec3 d = world.cameras[c].shownEye() - centre;
			const float d2 = lengthSquared(d);
			float a = 0;
			if (d2 < 2500) {
				a = 191;
			}
			else if (d2 < 10000) {
				a = 191 - ((d2 - 2500) / 7500) * 191;
			}
			shadowAlpha[i][c] = uint8_t(std::clamp(a, 0.0f, 255.0f));
			any = any || shadowAlpha[i][c] > 0;
		}
		if (!any) {
			continue;
		}
		Decal& dcl = decals.emplace_back();
		dcl.kind = Decal::Shadow;
		dcl.owner = int16_t(i);
		dcl.material = "carshad" + std::to_string(i % 6);
		const Chassis& ch = m_chassis[i];
		// The shadow's length along the car's forward (its -row 1 is its width's direction).
		project(dcl, centre + Vec3{0, 0, 6}, car.rotation.f, ch.shadowWidth * scale, ch.shadowLength * scale);
	}
	// Skid marks (players only): which wheels, laying pieces, the trails' fading.
	for (size_t i = 0; i < n; i++) {
		Visuals& v = visuals[i];
		const Kart& k = world.karts[i];
		const Pose& car = world.visible[i];
		uint8_t wheels = 0;
		if (v.skidding && !(world.racers[i].flags & RacerGhost)) {
			if (v.spinning) {
				wheels = 15;
			}
			else if (v.turbo) {
				wheels = 4 | 8;
			}
			else {
				const Vec3 dir = k.velocityDirection;
				wheels = dot(car.rotation.s, dir) > 0 ? (2 | 8) : (1 | 4);
			}
			for (size_t w = 0; w < 4; w++) {
				v.liftedMs[w] = k.wheels[w].contact ? 0 : v.liftedMs[w] + ms;
				if (v.liftedMs[w] > 150) {
					wheels &= uint8_t(~(1u << w));
				}
			}
		}
		v.skidWheels = wheels;
		for (uint8_t w = 0; w < 4; w++) {
			// The trail of this racer's wheel, if one lays it.
			Trail* t = nullptr;
			for (Trail& x : trails) {
				t = x.racer == int16_t(i) && x.wheel == w && !x.fading ? &x : t;
			}
			const bool on = wheels & (1u << w);
			const bool turbo = v.turbo && world.racers[i].player;
			if (t && (!on || t->spinning != v.spinning || t->turbo != turbo)) {
				if (on) {
					*t = {}; // (the mode changed: released at once)
				}
				else {
					t->fading = true; // (skid effects stopped: it fades)
					t->fadeMs = 0;
				}
				t = nullptr;
			}
			if (!on) {
				continue;
			}
			const Vec3 at = toWorld(car.rotation, m_chassis[i].skidPoints[w]) + car.position;
			if (!t) {
				for (Trail& x : trails) {
					if (x.racer < 0) {
						t = &x;
						break;
					}
				}
				if (!t) {
					continue;
				}
				*t = {};
				t->racer = int16_t(i), t->wheel = w, t->spinning = v.spinning, t->turbo = turbo;
				t->anchor = at;
			}
			const int32_t length = t->spinning ? 250 : 1000;
			for (Trail::Piece& p : t->pieces) {
				p.ageMs += ms;
			}
			t->pieces.erase(std::remove_if(t->pieces.begin(), t->pieces.end(), [&](const Trail::Piece& p) { return p.ageMs > length; }), t->pieces.end());
			if (lengthSquared(at - t->anchor) >= 0.01f) {
				// A new piece from the anchor to the wheel; the live one is re-laid each frame.
				if (t->live) {
					t->pieces.push_back(t->current);
					if (t->pieces.size() > 3) {
						t->pieces.erase(t->pieces.begin());
					}
				}
				t->current = {t->anchor, at, 0};
				t->live = true;
				t->anchor = at;
			}
		}
	}
	for (Trail& t : trails) {
		if (t.racer < 0) {
			continue;
		}
		uint8_t alpha = 127;
		if (t.fading) {
			t.fadeMs += ms;
			if (t.fadeMs >= 1000) {
				t = {};
				continue;
			}
			alpha = uint8_t((1 - float(t.fadeMs) / 1000) * 127);
		}
		const float width = m_chassis[size_t(t.racer)].skidWidths[t.wheel < 2 ? 0 : 1];
		std::vector<Trail::Piece> pieces = t.pieces;
		if (t.live) {
			pieces.push_back(t.current);
		}
		for (const Trail::Piece& p : pieces) {
			const Vec3 step = p.to - p.from;
			const float length = std::sqrt(step.x * step.x + step.y * step.y);
			if (length <= 0) {
				continue;
			}
			Decal& dcl = decals.emplace_back();
			dcl.kind = Decal::Skid;
			dcl.owner = int16_t(&t - trails.data());
			dcl.material = t.turbo ? "trbskid" : "skid";
			dcl.alpha = alpha;
			project(dcl, (p.from + p.to) * 0.5f + Vec3{0, 0, 6}, step, width, length);
		}
	}
	// Explosion scars: the flash's width, 180 while exploding, then down to 0 over 5 s.
	for (size_t e = 0; e < world.explosions.size(); e++) {
		const Explosion& x = world.explosions[e];
		if (!x.scar || x.state == Explosion::Idle) {
			continue;
		}
		const float g = x.state == Explosion::Exploding ? x.growth : 1.0f;
		const float size = (15 - 0.1f) * g + 0.1f;
		const float alpha = x.state == Explosion::Exploding ? 180 : 180 * std::clamp(float(x.ms) / 5000, 0.0f, 1.0f);
		Decal& dcl = decals.emplace_back();
		dcl.kind = Decal::Scar;
		dcl.owner = int16_t(e);
		dcl.material = "exscar";
		dcl.alpha = uint8_t(alpha);
		project(dcl, x.centre + Vec3{0, 0, 6}, {1, 0, 0}, size, size);
	}
	// Oil slicks while active.
	for (size_t a = 0; a < world.actions.size(); a++) {
		const Action& x = world.actions[a];
		if (x.kind != Action::Oil || x.state != TrapActive) {
			continue;
		}
		Decal& dcl = decals.emplace_back();
		dcl.kind = Decal::Slick;
		dcl.owner = int16_t(a);
		dcl.material = "oilslck";
		project(dcl, x.point + Vec3{0, 0, 5}, {1, 0, 0}, 15, 15);
	}
}

// --- The sky ---

void Show::updateSky(int32_t ms)
{
	if (!hasSky) {
		return;
	}
	const auto colours = [&](int32_t state, int32_t key, int32_t elapsed, std::array<std::array<float, 3>, 3>& out) {
		const SkyFile::State& s = sky.states[size_t(state)];
		if (s.keyframes.empty()) {
			return;
		}
		const SkyFile::Keyframe& a = s.keyframes[size_t(key) % s.keyframes.size()];
		const SkyFile::Keyframe& b = s.keyframes[size_t(key + 1) % s.keyframes.size()];
		const float f = a.durationMs > 0 ? std::clamp(float(elapsed) / float(a.durationMs), 0.0f, 1.0f) : 0;
		for (size_t r = 0; r < 3; r++) {
			for (size_t c = 0; c < 3; c++) {
				out[r][c] = float(a.colours[r][c]) + (float(b.colours[r][c]) - float(a.colours[r][c])) * f;
			}
		}
	};
	const SkyFile::State& s = sky.states[size_t(skyState)];
	skyMs += ms;
	// A keyframe advances once its elapsed time exceeds its duration.
	while (!s.keyframes.empty() && skyMs > s.keyframes[size_t(skyKey) % s.keyframes.size()].durationMs) {
		skyMs -= s.keyframes[size_t(skyKey) % s.keyframes.size()].durationMs;
		skyKey = int32_t((size_t(skyKey) + 1) % s.keyframes.size());
	}
	colours(skyState, skyKey, skyMs, skyColours);
	if (blendFrom >= 0) {
		blendMs += ms;
		if (blendMs >= blendLength) {
			blendFrom = -1;
		}
		else {
			std::array<std::array<float, 3>, 3> from = {};
			colours(blendFrom, 0, 0, from);
			const float f = float(blendMs) / float(std::max(1, blendLength));
			for (size_t r = 0; r < 3; r++) {
				for (size_t c = 0; c < 3; c++) {
					skyColours[r][c] = from[r][c] + (skyColours[r][c] - from[r][c]) * f;
				}
			}
		}
	}
}

// --- A frame ---

void Show::advance(const World& world, int32_t ms)
{
	// The world's frame raised its events in its order; the effect sets update between its scenery and power-ups and
	// its track events (steps 3 and 7), so the events raised before go first.
	spawned.clear();
	clockMs += ms;
	for (EffectSlot& slot : sets[0].slots) {
		slot.bubbled = false;
	}
	const std::vector<RaceEvent>& events = world.raceEvents;
	size_t e = 0;
	for (; e < events.size(); e++) {
		const RaceEvent::Stage st = events[e].stage;
		if (st != RaceEvent::Setup && st != RaceEvent::Session && st != RaceEvent::Powers) {
			break;
		}
		apply(world, events[e]);
	}
	if (!world.paused()) {
		// A shot's smoke (a cannonball's, a hook's) is finished in its power-up's frame once it has puffed three times
		// (every one stops after its third emission).
		for (size_t k = 0; k < owned.size();) {
			EffectHandle& h = owned[k].second;
			const int16_t s = h.held() ? sets[size_t(h.set)].handles[size_t(h.index)] : -1;
			const EffectSlot* slot = s >= 0 ? &sets[size_t(h.set)].slots[size_t(s)] : nullptr;
			if (owned[k].first.kind == Owner::Action && slot && slot->emitter >= 0 && slot->emissions >= 3 &&
				sets[size_t(h.set)].file.emitters[size_t(slot->emitter)].name == "cannsmk") {
				finish(h);
				owned.erase(owned.begin() + std::ptrdiff_t(k));
				continue;
			}
			k++;
		}
		updateSets(world, ms);
	}
	if (!world.paused()) {
		retries(world, false);
	}
	for (; e < events.size() && events[e].stage == RaceEvent::Events; e++) {
		apply(world, events[e]);
	}
	if (!world.paused()) {
		retries(world, true);
	}
	for (; e < events.size(); e++) {
		apply(world, events[e]);
	}
	m_wanting.assign(world.events.size(), 0);
	for (size_t i = 0; i < world.track.events.size() && i < world.events.size(); i++) {
		m_wanting[i] = world.track.events[i].kind == EventResource::Particle && world.events[i].phase == EventState::Active;
	}
	m_hazardsOn.assign(world.hazards.size(), 0);
	for (size_t i = 0; i < world.hazards.size(); i++) {
		m_hazardsOn[i] = (world.hazards[i].type == Hazard::Snow || world.hazards[i].type == Hazard::Smoke) && world.hazards[i].on;
	}
	for (EffectSlot& slot : sets[0].slots) {
		slot.slickWas = slot.place == EffectSlot::OilSlick && slot.racer >= 0 && size_t(slot.racer) < world.actions.size() &&
			world.actions[size_t(slot.racer)].state == TrapActive;
	}
	if (!world.paused()) {
		carVisuals(world);
		moveParticles(world, ms);
		updateSky(ms);
	}
	updateDecals(world, world.paused() ? 0 : ms);
	updateSounds(world, ms);
}

} // namespace lr
