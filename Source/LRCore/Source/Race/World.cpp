// A race's setup, the order of a frame and of a step, the session's phases and the players' controls.

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
				const std::string name =
					"GAMEDATA/" + race.track + "/R" + std::to_string(slot) + "_" + variant + "_" + std::to_string(number) + ".RRB";
				if (!cache.has(name)) {
					break;
				}
				choices[slot].push_back(name);
			}
		}
	}
	return choices;
}

bool World::setup(const Cache& cache, const RaceBrief& race)
{
	if (setupWatch) {
		setupWatch(*this, setupContext);
	}
	const std::string dir = "GAMEDATA/" + race.track + "/";
	if (!setupSession(cache, race) || !track.load(cache, dir + race.track + ".RAB", race.mirrored, race.mode == RaceBrief::Mode::TimeRace) ||
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
	touchInside.assign(track.touchSpheres.size(), 0);
	touchLast.assign(track.touchSpheres.size(), 0);

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

	params.resize(n);
	karts.resize(n);
	drivers.resize(n);
	controls.resize(n);
	keys.resize(n);
	visible.resize(n);
	playback.resize(n);
	racers.resize(n);
	carBounds.resize(n);
	bodyBounds.resize(n);
	lastKeys.assign(n, 0);
	worldMs.assign(n * track.volumes.size(), 0);
	std::vector<std::array<int32_t, 8>> habits(n, {100, 100, 100, 100, 100, 100, 0, 0}); // a built car's
	std::vector<std::string> bodies(n); // a champion's body model
	for (size_t i = 0; i < n; i++) {
		const RaceBrief::Slot& slot = race.slots[i];
		// The car: a driver races as a champion, whose chassis it has; a built car is its chassis and its pieces.
		KartSource src;
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
			Model body;
			if (cache.get(Common + binaryName(champion->model) + ".GDB", body)) {
				bodyBounds[i] = modelBounds(body);
			}
			bodies[i] = champion->model;
			const std::array<int32_t, 6>& st = driver->stats;
			habits[i] = {st[0], st[1], st[2], st[3], st[4], st[5], driver->chargeColour, driver->chargeTarget};
		}
		else {
			chassis = chassisTable.find(slot.chassis);
			if (!chassis) {
				return false;
			}
			src.mass = chassis->mass + slot.pieces;
			src.pieceWeight = int(slot.pieces);
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
		if (!carShape(cache, *chassis, carBounds[i])) {
			return false;
		}
		if (slot.driver.empty()) {
			bodyBounds[i] = carBounds[i]; // (a built car's body is its pieces: the car's sphere stands in)
		}

		params[i] = makeKartParams(src);
		// A time race's player starts in place 2 when there's a record ghost, else 1; in versus racer 0 takes 2, racer
		// 1 keeps 1 (places from 0).
		const bool timed = race.mode == RaceBrief::Mode::TimeRace, ghost = timed && !race.ghost.positions.empty();
		const size_t at = ghost ? 2 : timed ? 1 : race.twoPlayers && i < 2 ? 2 - i : i;
		const Pose& place = track.grid[at];
		placeKart(karts[i], params[i], place.position, place.rotation);
		karts[i].boundsRadius = carBounds[i].radius;
		karts[i].worldCount = track.checkpointVolume >= 0 ? 2 : 1;
		karts[i].worlds[0] = int32_t(track.trackVolume);
		karts[i].worlds[1] = track.checkpointVolume;
		visible[i] = place;
		Racer& r = racers[i];
		if (!routes[i].points.empty()) {
			attachAtStart(playback[i], karts[i], visible[i], params[i], routes[i], int32_t(i));
		}
		r.tauntMs = 2000 + 8 * draw(Draw::Taunt);
		contactOrder.insert(contactOrder.begin(), uint8_t(i));
		if (i == 0 && autopilot) {
			handToAi(0);
		}
	}
	powers.resize(n);
	// The track's moving parts, the event table and the trigger list's spheres, then the power-ups; the race view.
	setupEventTable(*this);
	setupHazards();
	setupEvents(*this);
	setupPowers(*this, habits);
	setupView(cache, race, bodies);
	return true;
}

void World::frame(int32_t frameMs, int32_t sessionMs)
{
	sessionMs = sessionMs < 0 ? frameMs : sessionMs;
	if (simulate) {
		advanceSession(frameMs, sessionMs);
		simulate(*this, frameMs, simulateContext);
		return;
	}
	beginFrame(frameMs, sessionMs);
	for (size_t i = 0; i < karts.size(); i++) {
		if (racers[i].player) {
			playerControls(i, frameMs);
		}
	}
	for (const PlayerCamera& c : cameras) {
		viewKeys(size_t(c.racer));
	}
	for (int32_t left = frameMs; left > 0;) {
		const int32_t dt = std::min(left, 40);
		left -= dt;
		step(dt);
	}
	endFrame();
}

void World::advanceSession(int32_t frameMs, int32_t sessionMs)
{
	session.ms += clockHeld ? 0 : sessionMs;
	updateSession();
	if (session.watchingFlyby) {
		session.flyby += (float(track.introRate) / 1000) * float(frameMs);
	}
}

void World::beginFrame(int32_t frameMs, int32_t sessionMs)
{
	stage = RaceEvent::Session;
	advanceSession(frameMs, sessionMs < 0 ? frameMs : sessionMs);
	advanceClocks(frameMs);
	advanceMaterials(frameMs);
	powerFrame(frameMs);
	countDownWorlds(frameMs);
	endTouchFrame();
	eventFrame(frameMs);
}

void World::playerControls(size_t i, int32_t frameMs)
{
	stage = RaceEvent::Controls;
	// Nothing is sent before the start or once the AI drives.
	Racer& r = racers[i];
	Controls& c = controls[i];
	const bool blocked = (r.flags & RacerOnGrid) || r.ai;
	updateControls(c, keys[i], blocked, frameMs, drivers[i], karts[i], params[i]);
	driftMarks(i);
	// The start boost: a throttle held more than 2 s on the grid opens a 100 ms window; the race starting in it (or
	// the throttle pressed just after it) makes a turbo. Braking or letting the throttle go starts the 2 s again.
	const uint16_t k = keys[i];
	const bool throttle = k & KeyThrottle, pressed = throttle && !c.throttleDown;
	if (k & KeyBrake) {
		c.throttleHeldMs = 0;
	}
	else {
		c.throttleHeldMs = !throttle && c.throttleDown ? frameMs : c.throttleHeldMs + frameMs;
	}
	c.boostWindowMs = std::max(0, c.boostWindowMs - frameMs);
	if ((r.flags & RacerOnGrid) && throttle && c.throttleHeldMs > 2000) {
		c.boostWindowMs = 100;
		c.throttleHeldMs = 0;
	}
	if (!blocked && pressed && c.boostWindowMs > 0) {
		make(int8_t(i), Green, c.boostWindowMs >= 60 ? 1 : 0);
		c.boostWindowMs = 0;
	}
	if (!blocked && (k & KeyUse) && !c.useDown) {
		useHeld(i);
	}
	c.throttleDown = throttle;
	c.useDown = k & KeyUse;
}

void World::endFrame()
{
	// The snow's camera hook: no snow while player 1's racer has a colour change on (the show's particle let go).
	for (size_t index = 0; index < hazards.size(); index++) {
		Hazard& h = hazards[index];
		if (h.type == Hazard::Snow && h.state == Hazard::Active && !cameras.empty()) {
			const size_t r = size_t(cameras[0].racer);
			h.armed = !(r < tinted.size() && tinted[r]);
			if (!h.armed && h.on) {
				raise(RaceEvent::Finish, {Owner::Hazard, int16_t(index)});
				h.on = false;
			}
		}
	}
	// The scenery is drawn: what each camera has in view and within its distance. A hazard that reads a skeleton sees
	// the pose of its last drawing. (QUIRK: an entity out of sight keeps an old pose.)
	drawScenery();
	// The racers drawn (a warping one is hidden): their skeletons sampled.
	for (size_t i = 0; i < looks.size(); i++) {
		if (racers[i].flags & RacerGhost) {
			continue;
		}
		RacerLook& look = looks[i];
		const RacerShape& shape = shapes[i];
		if (shape.carAnimation >= 0) {
			sampleLocals(skeletons[size_t(shape.carAnimation)], &figures[size_t(shape.carAnimation)], look.car.pose(), look.carNodes);
		}
		if (shape.variantAnimation >= 0) {
			sampleLocals(skeletons[size_t(shape.variantAnimation)], &figures[size_t(shape.variantAnimation)], look.variant.pose(), look.variantNodes);
		}
		sampleLocals(skeletons[0], &figures[0], look.figure.pose(), look.figureNodes);
	}
	// A warp shrinks its car to nothing over its last 250 ms before it vanishes: sin(pi * ms / 500), by the table.
	for (uint8_t index : actionList) {
		const Action& a = actions[index];
		if (a.kind == Action::Warp && a.state == 2 && a.owner >= 0 && a.ms < 250) {
			const int32_t angle = int32_t((float(int32_t(a.ms)) * 0.002f * 3.1415927f) * -162.97466f);
			powers[size_t(a.owner)].carScale = cosEntry(int32_t(0xffffff00u - uint32_t(angle)));
		}
	}
}

void World::step(int32_t dt)
{
	for (size_t i = 0; i < karts.size(); i++) {
		driveRacer(i, dt);
	}
	if (bodiesWatch) {
		bodiesWatch(*this, false, bodiesContext);
	}
	for (size_t i = 0; i < karts.size(); i++) {
		moveRacer(i, dt);
	}
	if (bodiesWatch) {
		bodiesWatch(*this, true, bodiesContext);
	}
	for (size_t i = 0; i < drivers.size(); i++) {
		driftMarks(i); // (a powerslide the step ended)
		afterBody(drivers[i]);
	}
	runWaits(dt);
	contactSweep();
	racerTouches();
	visuals(dt);
	racerTimers(dt);
	updateStandings();
	pace(dt);
	updateCameras(dt);
	recordRun(dt);
}

void World::updateSession()
{
	const RaceBrief::Mode mode = brief.mode;
	switch (session.phase) {
	case Session::Intro:
		if (session.watchingFlyby && session.flyby >= float(track.introFrames - 1)) {
			session.watchingFlyby = false;
			session.ms = 0;
		}
		if (session.ms < 2000 || session.watchingFlyby) {
			return;
		}
		session.phase = Session::Countdown;
		session.ms = 0;
		// The cameras take their views again: each snaps to its pose.
		for (size_t p = 0; p < cameras.size(); p++) {
			cameras[p].kind = PlayerCamera::Chase;
			chooseView(p, cameras[p].view);
		}
		for (size_t i = 0; i < racers.size(); i++) {
			if (racers[i].player) {
				racers[i].flags |= RacerEngine;
				playSound(62, {Owner::Racer, int16_t(i)}, &visible[i].position); // (its engine starts: the show's)
			}
		}
		// The HUDs' countdown (a test drive has none).
		for (Hud& h : huds) {
			h.banner = mode == RaceBrief::Mode::TestDrive ? h.banner : 1;
		}
		[[fallthrough]];
	case Session::Countdown:
		// (A test drive's ends at once.)
		if (session.ms < 3000 && mode != RaceBrief::Mode::TestDrive) {
			return;
		}
		session.phase = Session::Racing;
		session.ms = 0;
		// The start.
		for (size_t i = 0; i < racers.size(); i++) {
			Racer& r = racers[i];
			r.flags &= ~RacerOnGrid;
			playback[i].paused = false;
			r.zones[0] = LapMark::Open;
			r.zones[1] = LapMark::Past;
			r.zones[2] = LapMark::Line;
			if (r.ai && (draw(Draw::StartGreen) & 255) < powers[i].chances[Green - 1]) {
				make(int8_t(i), Green, 0); // an AI may start with a turbo
			}
		}
		// A player's start boost: the race starts in the window its throttle opened.
		for (size_t i = 0; i < racers.size(); i++) {
			Controls& c = controls[i];
			if (racers[i].player && !racers[i].ai && c.boostWindowMs > 0) {
				make(int8_t(i), Green, c.boostWindowMs >= 60 ? 1 : 0);
			}
			c.boostWindowMs = 0;
		}
		racingStarted();
		session.finishers = 0;
		[[fallthrough]];
	case Session::Racing:
		for (size_t i = 0; i < racers.size(); i++) {
			if (racers[i].laps < raceLaps || (racers[i].flags & RacerFinished)) {
				continue;
			}
			finish(i);
			if (!racers[i].ai || (autopilot && i == 0)) {
				// The first player over the line: the others race on for a while (a test drive's for 1 s).
				session.phase = Session::Finished;
				session.ms = mode == RaceBrief::Mode::TestDrive ? 9000 : 0;
				racingEnded(i);
				handToAi(i);
				break;
			}
		}
		if (session.phase != Session::Finished) {
			return;
		}
		[[fallthrough]];
	case Session::Finished:
		// Outside a circuit the screen fades to black from 9 s; a time race then asks to race again (and goes no
		// further).
		if (mode != RaceBrief::Mode::Circuit && session.ms > 9000) {
			startFade();
		}
		if (mode == RaceBrief::Mode::TimeRace && session.ms > 9500 && !timeRaceAsked) {
			timeRaceAsked = true;
			openDialog(RaceDialog::TimeRaceEnd);
			return;
		}
		if (session.ms < 10000 || mode == RaceBrief::Mode::TimeRace) {
			for (size_t i = 0; i < racers.size(); i++) {
				if (racers[i].laps >= raceLaps && !(racers[i].flags & RacerFinished)) {
					finish(i);
					if (racers[i].player) {
						handToAi(i);
					}
				}
			}
			return;
		}
		// Time up: the rest are placed in racer order (not by standing), not finished.
		for (size_t i = 0; i < racers.size(); i++) {
			if (!(racers[i].flags & RacerFinished)) {
				racers[i].place = session.finishers++;
				setStanding(i, session.finishers);
				finished(i);
			}
		}
		session.phase = Session::Results;
		session.ms = 0;
		[[fallthrough]];
	case Session::Results:
		// A circuit shows its standings for 5 s, fading to black in the last.
		if (mode == RaceBrief::Mode::Circuit) {
			if (session.ms > 4000) {
				startFade();
			}
			if (session.ms < 5000) {
				return;
			}
		}
		sessionEnded();
		break;
	case Session::Over: break;
	}
}

void World::countDownWorlds(int32_t frameMs)
{
	const size_t volumes = track.volumes.size();
	for (size_t i = 0; i < karts.size(); i++) {
		Kart& k = karts[i];
		for (int32_t left = frameMs; left > 0;) {
			const int32_t chunk = std::min(left, 255);
			left -= chunk;
			for (size_t v = 0; v < volumes; v++) {
				int32_t& ms = worldMs[i * volumes + v];
				if (ms == 0) {
					continue;
				}
				if (chunk < ms) {
					ms -= chunk;
					continue;
				}
				ms = 0;
				for (int32_t slot = 2; slot < k.worldCount; slot++) {
					if (k.worlds[slot] == int32_t(v)) {
						std::copy(k.worlds + slot + 1, k.worlds + k.worldCount, k.worlds + slot);
						k.worldCount--;
						break;
					}
				}
			}
		}
	}
}

} // namespace lr
