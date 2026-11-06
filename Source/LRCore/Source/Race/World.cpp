// A race's setup, and the order of a frame and of a step over the world's parts.

#include "LRCore/Race/World.h"
#include "Powers/PowerParts.h"

#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Model.h"
#include "LRData/Race.h"

#include <algorithm>

namespace lr {

namespace {

const std::string Common = "GAMEDATA/COMMON/";

// The visible car's bounds: its first animated part's sphere, in the model's scale. (Both parts, forward and reverse,
// have the same bounds in every chassis, so which one plays doesn't matter.)
bool carShape(const Cache& cache, const ChassisTable::Chassis& chassis, Sphere& out)
{
	Model model;
	AnimationFile animation;
	if (!cache.get(Common + binaryName(chassis.model) + ".GDB", model) ||
		!cache.get(Common + binaryName(chassis.animation) + ".ADB", animation) || animation.parts.empty()) {
		return false;
	}
	const std::array<float, 4>& b = animation.parts[0].bounds;
	const float g = model.scale;
	out = {{b[0] * g, b[1] * g, b[2] * g}, b[3] * g};
	return true;
}

// A racer's car: a driver races as a champion, whose chassis it has; a built car is its chassis and its pieces. Its
// kart's source, its habits (a champion's stats and charge), its body model, its bounds; false if a table lacks it.
bool racerSource(World& w, const Cache& cache, size_t i, const RaceBrief::Slot& slot, const ChassisTable& chassisTable,
	const ChampionTable& champions, const DriverTable& driverTable, KartSource& src, std::array<int32_t, 8>& habits, std::string& body)
{
	const ChassisTable::Chassis* chassis;
	if (!slot.driver.empty()) {
		const DriverTable::Driver* driver = driverTable.find(slot.driver);
		const ChampionTable::Champion* champion = driver ? champions.find(driver->champion) : nullptr;
		chassis = champion ? chassisTable.find(champion->chassis) : nullptr;
		if (!chassis) {
			return false;
		}
		src.mass = champion->mass;
		src.centerOfMass = vec(champion->centerOfMass);
		Model model;
		if (cache.get(Common + binaryName(champion->model) + ".GDB", model)) {
			w.racers.bodyBounds[i] = modelBounds(model);
		}
		body = champion->model;
		const std::array<int32_t, 6>& st = driver->stats;
		habits = {st[0], st[1], st[2], st[3], st[4], st[5], driver->chargeColour, driver->chargeTarget};
	}
	else {
		chassis = chassisTable.find(slot.chassis);
		if (!chassis) {
			return false;
		}
		src.mass = chassis->mass + slot.pieces;
		src.pieceWeight = int32_t(slot.pieces);
		const Vec3 com = vec(chassis->centerOfMass);
		src.centerOfMass = {slot.centroidX * 0.25f + com.x, slot.centroidY * 0.25f + com.y, com.z};
	}
	src.frontLeftWheel = vec(chassis->wheels[1]);
	src.rearX = chassis->wheels[3][0];
	src.rightY = chassis->wheels[2][1];
	src.leftY = chassis->wheels[3][1];
	src.handling = chassis->handling;
	src.acceleration = chassis->acceleration;
	src.topSpeed = chassis->topSpeed;
	src.shadowWidth = chassis->shadowWidth;
	src.shadowLength = chassis->shadowLength;
	if (!carShape(cache, *chassis, w.racers.carBounds[i])) {
		return false;
	}
	if (slot.driver.empty()) {
		w.racers.bodyBounds[i] = w.racers.carBounds[i]; // (a built car's body is its pieces: the car's sphere stands in)
	}
	return true;
}

} // namespace

void setupEvents(World& w);
void setupEventTable(World& w);

std::vector<std::vector<std::string>> routeChoices(const Cache& cache, const RaceBrief& race)
{
	std::vector<std::vector<std::string>> choices(race.slots.size());
	for (size_t slot = 0; slot < race.slots.size(); slot++) {
		if (race.slots[slot].player && !(slot == 0 && race.autopilot)) {
			continue;
		}
		for (char variant : {'F', 'M', 'S'}) {
			for (int number = 0; number < 10; number++) {
				const std::string name = "GAMEDATA/" + race.track + "/R" + std::to_string(slot) + "_" + variant + "_" +
					std::to_string(number) + ".RRB";
				if (!cache.has(name)) {
					break;
				}
				choices[slot].push_back(name);
			}
		}
	}
	return choices;
}

void Racers::resize(size_t n, size_t volumes)
{
	race.resize(n);
	params.resize(n);
	karts.resize(n);
	drivers.resize(n);
	controls.resize(n);
	keys.resize(n);
	lastKeys.resize(n);
	visible.resize(n);
	playback.resize(n);
	powers.resize(n);
	carBounds.resize(n);
	bodyBounds.resize(n);
	groups.resize(n);
	worldMs.resize(n * volumes);
}

// A racer at its start place, on its route if it has one; its taunt's first delay.
void World::placeRacer(size_t i, const RaceBrief& race)
{
	// A time race's player starts in place 2 when there's a record ghost, else 1; in versus racer 0 takes 2, racer
	// 1 keeps 1 (places from 0).
	const bool timed = race.mode == RaceBrief::Mode::TimeRace, ghost = timed && !race.ghost.positions.empty();
	const size_t at = ghost ? 2 : timed ? 1 : race.twoPlayers && i < 2 ? 2 - i : i;
	const Pose& place = track.grid[at];
	placeKart(racers.karts[i], racers.params[i], place.position, place.rotation);
	racers.karts[i].boundsRadius = racers.carBounds[i].radius;
	racers.karts[i].worldCount = track.checkpointVolume >= 0 ? 2 : 1;
	racers.karts[i].worlds[0] = int32_t(track.trackVolume);
	racers.karts[i].worlds[1] = track.checkpointVolume;
	racers.visible[i] = place;
	Racer& r = racers.race[i];
	if (!routes[i].points.empty()) {
		attachAtStart(racers.playback[i], racers.karts[i], racers.visible[i], racers.params[i], routes[i], int32_t(i));
	}
	r.tauntMs = 2000 + 8 * draw(Draw::Taunt);
	racers.contactOrder.insert(racers.contactOrder.begin(), uint8_t(i));
	if (i == 0 && session.autopilot) {
		racers.handToAi(*this, 0);
	}
}

bool World::setup(const Cache& cache, const RaceBrief& race)
{
	if (observer.to) {
		observer.to->onSetup(*this);
	}
	const std::string dir = "GAMEDATA/" + race.track + "/";
	if (!session.setup(*this, cache, race) ||
		!track.load(cache, dir + race.track + ".RAB", race.mirrored, race.mode == RaceBrief::Mode::TimeRace) ||
		track.grid.size() < race.slots.size()) {
		return false;
	}
	ChassisTable chassisTable;
	ChampionTable champions;
	DriverTable driverTable;
	if (!cache.get(Common + "CHASSIS.CMB", chassisTable) || !cache.get(Common + "CHAMPS.CCB", champions) ||
		!cache.get(Common + "DRIVERS.DDB", driverTable)) {
		return false;
	}
	session.paceBonus = race.mirrored ? 0.05f : 0;
	session.watchingFlyby = track.introFrames > 0;
	triggers.touchInside.assign(track.touchSpheres.size(), 0);
	triggers.touchLast.assign(track.touchSpheres.size(), 0);

	// The routes: each AI slot plays one of its recorded lines, picked at random.
	const size_t n = race.slots.size();
	routes.resize(n);
	const std::vector<std::vector<std::string>> choices = routeChoices(cache, race);
	for (size_t slot = 0; slot < n; slot++) {
		const std::vector<std::string>& files = choices[slot];
		if (!files.empty() && !routes[slot].load(cache, files[draw(Draw::Route) % files.size()], race.mirrored)) {
			return false;
		}
	}

	racers.resize(n, track.volumes.size()); // (the dynamic worlds' timers, now the track is loaded)
	std::vector<std::array<int32_t, 8>> habits(n, {100, 100, 100, 100, 100, 100, 0, 0}); // a built car's
	std::vector<std::string> bodies(n); // a champion's body model
	for (size_t i = 0; i < n; i++) {
		KartSource src;
		if (!racerSource(*this, cache, i, race.slots[i], chassisTable, champions, driverTable, src, habits[i], bodies[i])) {
			return false;
		}
		racers.params[i] = makeKartParams(src);
		placeRacer(i, race);
	}
	// The track's moving parts, the event table and the trigger list's spheres, then the power-ups; the race view.
	setupEventTable(*this);
	scenery.setupHazards(*this);
	setupEvents(*this);
	setupPowers(*this, habits);
	raceView.setup(*this, cache, race, bodies);
	return true;
}

void World::frame(int32_t frameMs, int32_t sessionMs)
{
	sessionMs = sessionMs < 0 ? frameMs : sessionMs;
	if (replaying()) {
		session.advance(*this, frameMs, sessionMs);
		observer.to->simulate(*this, frameMs);
		return;
	}
	beginFrame(frameMs, sessionMs);
	for (size_t i = 0; i < racers.karts.size(); i++) {
		if (racers.race[i].player) {
			racers.playerControls(*this, i, frameMs);
		}
	}
	for (const PlayerCamera& c : raceView.cameras) {
		raceView.viewKeys(*this, size_t(c.racer));
	}
	for (int32_t left = frameMs; left > 0;) {
		const int32_t dt = std::min(left, 40);
		left -= dt;
		step(dt);
	}
	endFrame();
}

void World::beginFrame(int32_t frameMs, int32_t sessionMs)
{
	raceView.stage = RaceEvent::Session;
	session.advance(*this, frameMs, sessionMs < 0 ? frameMs : sessionMs);
	scenery.advanceClocks(*this, frameMs);
	raceView.advanceMaterials(*this, frameMs);
	powerUps.powerFrame(*this, frameMs);
	racers.countDownWorlds(*this, frameMs);
	triggers.endTouchFrame(*this);
	triggers.eventFrame(*this, frameMs);
}

void World::endFrame()
{
	// The snow's camera hook: no snow while player 1's racer has a colour change on (the show's particle let go).
	for (size_t index = 0; index < scenery.hazards.size(); index++) {
		Hazard& h = scenery.hazards[index];
		if (h.type == Hazard::Snow && h.state == Hazard::Active && !raceView.cameras.empty()) {
			const size_t r = size_t(raceView.cameras[0].racer);
			h.armed = !(r < raceView.tinted.size() && raceView.tinted[r]);
			if (!h.armed && h.on) {
				raceView.raise(RaceEvent::Finish, {Owner::Hazard, int16_t(index)});
				h.on = false;
			}
		}
	}
	// The scenery is drawn: what each camera has in view and within its distance. A hazard that reads a skeleton sees
	// the pose of its last drawing. (QUIRK: an entity out of sight keeps an old pose.)
	raceView.drawScenery(*this);
	// The racers drawn (a warping one is hidden): their skeletons sampled.
	for (size_t i = 0; i < raceView.looks.size(); i++) {
		if (racers.race[i].flags & RacerGhost) {
			continue;
		}
		RacerLook& look = raceView.looks[i];
		const RacerShape& shape = raceView.shapes[i];
		if (shape.carAnimation >= 0) {
			sampleLocals(raceView.skeletons[shape.carAnimation], &raceView.figures[shape.carAnimation], look.car.pose(), look.carNodes);
		}
		if (shape.variantAnimation >= 0) {
			sampleLocals(raceView.skeletons[shape.variantAnimation], &raceView.figures[shape.variantAnimation], look.variant.pose(),
				look.variantNodes);
		}
		sampleLocals(raceView.skeletons[0], &raceView.figures[0], look.figure.pose(), look.figureNodes);
	}
	// A warp shrinks its car to nothing over its last 250 ms before it vanishes: sin(pi * ms / 500), by the table.
	for (uint8_t index : powerUps.actionList) {
		const Action& a = powerUps.actions[index];
		if (a.kind == Action::Warp && a.state == WarpStarting && a.owner >= 0 && a.ms < 250) {
			const int32_t angle = int32_t((float(int32_t(a.ms)) * 0.002f * Pi) * -162.97466f);
			racers.powers[a.owner].carScale = cosEntry(int32_t(0xffffff00u - uint32_t(angle)));
		}
	}
}

void World::step(int32_t dt)
{
	for (size_t i = 0; i < racers.karts.size(); i++) {
		racers.driveRacer(*this, i, dt);
	}
	if (observer.to) {
		observer.to->onBodies(*this, false);
	}
	for (size_t i = 0; i < racers.karts.size(); i++) {
		racers.moveRacer(*this, i, dt);
	}
	if (observer.to) {
		observer.to->onBodies(*this, true);
	}
	for (size_t i = 0; i < racers.drivers.size(); i++) {
		raceView.driftMarks(*this, i); // (a powerslide the step ended)
		afterBody(racers.drivers[i]);
	}
	triggers.runWaits(*this, dt);
	racers.contactSweep(*this);
	raceView.racerTouches(*this);
	raceView.visuals(*this, dt);
	racers.racerTimers(*this, dt);
	racers.updateStandings(*this);
	racers.pace(*this, dt);
	raceView.updateCameras(*this, dt);
	session.recordRun(*this, dt);
}

Vec3 nodeWorld(const World& w, size_t model, int32_t node, Vec3 p, float time)
{
	const AnimatedModel& m = w.track.animated[model];
	const Animation* a = w.track.animationOf(m);
	const Clock& c = w.scenery.clocks[model];
	for (int32_t n = node; inRange(m.nodes, n); n = m.nodes[n].parent) {
		const Pose local = w.scenery.nodeLocal(m, a, size_t(n), c.part, time, c.loop);
		p = toWorld(local.rotation, p) + local.position;
	}
	return toWorld(m.pose.rotation, p * m.scale) + m.pose.position;
}

} // namespace lr
