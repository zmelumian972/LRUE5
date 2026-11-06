// The show: its load and its frame, which runs its parts (the effects, decals,
// sky and sounds) in the frame's order, and the presentation stream's draws.

#include "LRCore/Show/ShowState.h"

#include "LRCore/Race/World.h"
#include "LRData/Cache.h"
#include "LRData/Cars.h"

namespace lr {

namespace {

const std::string Common = "GAMEDATA/COMMON/";

} // namespace

uint16_t Show::draw(Draw why, int16_t racer)
{
	if (observer) {
		return observer->onDraw(*this, why, racer);
	}
	random = (random + 1) & 1023;
	return randomTable(random);
}

// The chassis' shadow and skid sizes, by racer.
void Show::loadChassis(const Cache& cache, const World& world)
{
	ChassisTable chassisTable;
	ChampionTable champions;
	DriverTable drivers;
	// (World::setup has read the three already.)
	(void)cache.get(Common + "CHASSIS.CMB", chassisTable);
	(void)cache.get(Common + "CHAMPS.CCB", champions);
	(void)cache.get(Common + "DRIVERS.DDB", drivers);
	const size_t n = world.racers.karts.size();
	chassis.assign(n, {});
	for (size_t i = 0; i < n && i < world.session.brief.slots.size(); i++) {
		const RaceBrief::Slot& slot = world.session.brief.slots[i];
		const DriverTable::Driver* driver = slot.driver.empty() ? nullptr : drivers.find(slot.driver);
		const ChampionTable::Champion* champion = driver ? champions.find(driver->champion) : nullptr;
		if (const ChassisTable::Chassis* c = chassisTable.find(champion ? champion->chassis : slot.chassis)) {
			Chassis& out = chassis[i];
			out.shadowWidth = c->shadowWidth;
			out.shadowLength = c->shadowLength;
			out.skidWidths = c->skidWidths;
			out.enginePitch = c->enginePitch;
			for (size_t w = 0; w < 4; w++) {
				out.skidPoints[w] = {c->skidPoints[w][0], c->skidPoints[w][1] * (world.track.mirrored ? -1.0f : 1.0f), c->skidPoints[w][2]};
			}
		}
	}
}

bool Show::load(const Cache& cache, const World& world)
{
	// A new race's parts (the stream, the observer and the effects volume run on).
	clockMs = 0;
	chassis.clear();
	effects = {};
	decals = {};
	sky = {};
	sounds = {};
	const std::string& track = world.session.brief.track;
	const std::string dir = "GAMEDATA/" + track + "/";
	RaceFile race;
	if (!cache.get(dir + track + ".RAB", race)) {
		return false;
	}
	effects.load(cache, dir, race, world.racers.karts.size());
	loadChassis(cache, world);
	decals.load(world);
	sky.load(cache, dir, race);
	sounds.load(*this, cache, world);
	return true;
}

// --- A frame ---

// An event to its part: the sky's, else the effects' (the sounds read the frame's events themselves).
void Show::apply(const World& world, const RaceEvent& e)
{
	if (e.kind == RaceEvent::Sky) {
		sky.event(e);
	}
	else {
		effects.apply(*this, world, e);
	}
}

void Show::advance(const World& world, int32_t ms)
{
	// The world's frame raised its events in its order; the effect sets update between its scenery and power-ups and
	// its track events (steps 3 and 7), so the events raised before go first.
	effects.beginFrame();
	clockMs += ms;
	const std::vector<RaceEvent>& events = world.raceView.raceEvents;
	size_t e = 0;
	for (; e < events.size(); e++) {
		const RaceEvent::Stage st = events[e].stage;
		if (st != RaceEvent::Setup && st != RaceEvent::Session && st != RaceEvent::Powers) {
			break;
		}
		apply(world, events[e]);
	}
	if (!world.session.paused()) {
		effects.update(*this, world, ms);
		effects.retries(world, false);
	}
	for (; e < events.size() && events[e].stage == RaceEvent::Events; e++) {
		apply(world, events[e]);
	}
	if (!world.session.paused()) {
		effects.retries(world, true);
	}
	for (; e < events.size(); e++) {
		apply(world, events[e]);
	}
	effects.endEvents(world);
	if (!world.session.paused()) {
		effects.carVisuals(*this, world);
		effects.moveParticles(*this, world);
		sky.update(ms);
	}
	decals.update(*this, world, world.session.paused() ? 0 : ms);
	sounds.update(*this, world, ms);
}

} // namespace lr
