// The racers' look: the body's lean, the chassis' and the driver's parts, the
// driver model's choices (from the look draws), and the group sphere the touch spheres test; the scenery's material
// tracks and texture scrolls.

#include "LRCore/Kart/Car.h"
#include "LRCore/Race/World.h"

#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Model.h"
#include "LRData/Pieces.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

const std::string Common = "GAMEDATA/COMMON/";

// The driver's parts (PELVIS.ADB's, in file order).
enum Figure : int32_t {
	VeloStop = 0,
	Boost = 1,
	Reverse = 2,
	RevHold = 3,
	RevBack = 4,
	SteerRight = 5,
	SteerLeft = 6,
	ShortLookRight = 7,
	LongLookRight = 8,
	Default = 9,
	Hit = 10,
	Loser = 11,
	LoserLoop = 12,
	Winner = 13,
	Winner2 = 14,
	Winner2Loop = 15,
	ShortLookLeft = 16,
	LongLookLeft = 17,
};

// A part's time moved on by `frames`: a looping one wraps, a one-shot that reaches its end goes on to its follow-up.
void advancePart(PartClock& c, const Animation& a, float rate, int32_t dt)
{
	if (c.part < 0 || size_t(c.part) >= a.parts.size()) {
		return;
	}
	const Animation::Part& p = a.parts[size_t(c.part)];
	c.time = rate * float(dt) + c.time;
	if (c.time < float(p.frames - 1)) {
		return;
	}
	if (c.next >= 0) {
		c.part = c.next;
		c.next = -1;
		c.loop = true;
		c.time = 0;
	}
	else if (c.loop) {
		c.time = wrapLoop(c.time, float(p.loopFrames));
	}
	else {
		c.time = float(p.frames - 1);
	}
}

void play(PartClock& c, int32_t part, int32_t next)
{
	c.part = part;
	c.time = 0;
	c.loop = next < 0;
	c.next = next;
	c.fromPart = -1;
	c.progress = 1;
}

// The driver's 300 ms blend into a looping part (shown at once for the rules).
void transition(PartClock& c, int32_t part)
{
	if (c.part == part) {
		return;
	}
	c.fromPart = c.part;
	c.fromTime = c.time;
	c.part = part;
	c.time = 0;
	c.loop = true;
	c.next = -1;
	c.progress = 0;
}

// The driver's animation over dt: a transition's old part plays on and its blend progresses; once done the time it
// overshot is taken off the new part's advance (QUIRK).
void advanceFigure(PartClock& c, const Animation& a, int32_t dt)
{
	if (c.progress < 1) {
		c.progress += float(dt) / 300;
		if (c.progress >= 1) {
			dt -= int32_t((c.progress - 1) * 300);
			c.progress = 1;
			c.fromPart = -1;
		}
		else if (c.fromPart >= 0 && size_t(c.fromPart) < a.parts.size()) {
			const Animation::Part& p = a.parts[size_t(c.fromPart)];
			c.fromTime = p.rate * float(dt) + c.fromTime;
			if (c.fromTime >= float(p.frames - 1)) {
				c.fromTime = wrapLoop(c.fromTime, float(p.loopFrames));
			}
		}
	}
	if (c.part >= 0 && size_t(c.part) < a.parts.size()) {
		advancePart(c, a, a.parts[size_t(c.part)].rate, dt);
	}
}

// Two spheres as one: centred between them, holding both (not the smallest).
Sphere merged(Sphere a, Sphere b)
{
	const Vec3 c = (a.centre + b.centre) * 0.5f;
	return {c, length(b.centre - a.centre) * 0.5f + std::max(a.radius, b.radius)};
}

Sphere partBounds(const Animation& a, int32_t part) { return part >= 0 && size_t(part) < a.parts.size() ? a.parts[size_t(part)].bounds : Sphere{}; }

// A model's scale (its .GDB's), 1 if it can't be read.
float modelScale(const Cache& cache, const std::string& path)
{
	Model m;
	return cache.get(path, m) ? m.scale : 1.0f;
}

} // namespace

namespace {

// The chassis' part by the forward speed, at a rate by it (the file's rate isn't used); the variant alike, part 0.
void carParts(const World& w, RacerLook& look, const RacerShape& shape, float fs, int32_t dt)
{
	const Animation& a = w.raceView.figures[size_t(shape.carAnimation)];
	const bool still = std::fabs(fs) < 9.9999997e-05f;
	const int32_t part = !still && fs < 0 && a.parts.size() > 1 ? 1 : 0;
	if (part != look.car.part) {
		play(look.car, part, -1);
	}
	advancePart(look.car, a, still ? 0.0f : std::fabs(fs) * 1.8f, dt);
	if (shape.variantAnimation >= 0) {
		advancePart(look.variant, w.raceView.figures[size_t(shape.variantAnimation)], still ? 0.0f : std::fabs(fs) * 1.8f, dt);
	}
}

// The body's lean: two springs (pitch and roll), for a kart of mass m, pulling the body's up toward the car's.
void leanBody(RacerLook& look, const Pose& carPose, float m, int32_t dt)
{
	const Basis& c = carPose.rotation;
	const Vec3 b2 = look.body.rotation.u;
	const Vec3 t0 = cross(c.s, b2), t1 = cross(b2, c.f);
	float pitchLen = length(c.f - t0), rollLen = length(c.s - t1);
	pitchLen = dot(t0, c.u) < 0 ? -pitchLen : pitchLen;
	rollLen = dot(t1, c.u) < 0 ? -rollLen : rollLen;
	auto spring = [&](float& v, float len, float rate, float damping) {
		v += (((-rate) * len) * float(dt)) / m;
		const float d = (float(dt) * damping) * v;
		if ((v > 0 && d < v) || (v < 0 && d > v)) {
			v -= d;
		}
		else {
			v = 0;
		}
	};
	spring(look.pitch, pitchLen, 30, 0.0099999998f);
	spring(look.roll, rollLen, 100, 0.029999999f);
	Vec3 r0 = look.pitch == 0 ? c.f : t0 + c.u * look.pitch;
	Vec3 r1 = look.roll == 0 ? c.s : t1 + c.u * look.roll;
	r0 = normalize(r0);
	r1 = normalize(r1 - r0 * dot(r1, r0));
	look.body = {carPose.position, {r0, r1, cross(r0, r1)}};
}

bool showing(int32_t shown, std::initializer_list<int32_t> parts) { return std::find(parts.begin(), parts.end(), shown) != parts.end(); }

// The driver at the race's end: the winner's part (one of two, drawn) or the loser's.
void finishFigure(World& w, size_t i, PartClock& f, int32_t shown)
{
	if (w.racers.race[i].standing == 1) {
		if (!showing(shown, {Winner, Winner2, Winner2Loop})) {
			if (w.lookDraw(i, false) & 1) {
				play(f, Winner2, Winner2Loop);
			}
			else {
				play(f, Winner, -1);
			}
		}
	}
	else if (!showing(shown, {Loser, LoserLoop})) {
		play(f, Loser, LoserLoop);
	}
}

// A look at the nearest other racer (-1 none), if it is beside or behind.
int32_t lookAtNearest(const World& w, size_t i, const Basis& c)
{
	const Vec3 own = w.racers.visible[i].position;
	float best = 0;
	int32_t nearest = -1;
	for (size_t j = 0; j < w.racers.karts.size(); j++) {
		const Vec3 d = w.racers.visible[j].position - own;
		const float d2 = dotZYX(d, d);
		if (d2 >= 2 && d2 <= 40000 && (nearest < 0 || d2 < best)) {
			best = d2;
			nearest = int32_t(j);
		}
	}
	int32_t part = -1;
	if (nearest >= 0) {
		const Vec3 u = normalize(w.racers.visible[size_t(nearest)].position - own);
		const float ahead = dotZYX(c.f, u), side = dotZYX(c.s, u);
		if (ahead > -0.2f && ahead < 0.2f) {
			part = side < 0 ? ShortLookRight : ShortLookLeft;
		}
		else if (ahead > -0.60000002f && ahead <= -0.2f) {
			part = side < 0 ? LongLookRight : LongLookLeft;
		}
	}
	return part;
}

// The driver model's part while racing: a hard stop, a boost or a hit, reversing, a look at a
// racer beside or behind, else steering.
void driverFigure(World& w, size_t i, int32_t shown, uint8_t marks, float fs)
{
	RacerLook& look = w.raceView.looks[i];
	PartClock& f = look.figure;
	if (look.lastSpeed > 0.050000001f && fs < 0.0099999998f && shown != VeloStop) {
		look.stoppedHard = true;
		w.voice(i, RaceEvent::Negative);
		play(f, VeloStop, Default);
		look.lastSpeed = 0;
		return;
	}
	look.lastSpeed = fs;
	if (marks & 1) {
		play(f, Boost, Default);
		return;
	}
	if (marks & 2) {
		play(f, Hit, Default);
		return;
	}
	if (showing(shown, {VeloStop, Boost, Hit, Winner, Winner2Loop, LoserLoop})) {
		return;
	}
	// The drive controller's thrust and turn radius (a route kart has none).
	const bool driven = !w.racers.playback[i].active;
	const float thrust = driven ? w.racers.drivers[i].thrust : 0.0f, turn = driven ? w.racers.drivers[i].radius : 0.0f;
	if (thrust < 0 && fs < 0) {
		if (!showing(shown, {Reverse, RevHold})) {
			play(f, Reverse, RevHold);
		}
		return;
	}
	if (shown == RevHold) {
		play(f, RevBack, Default);
		return;
	}
	if (showing(shown, {ShortLookRight, ShortLookLeft, LongLookRight, LongLookLeft})) {
		return;
	}
	if (look.lookMs == 0) {
		const int32_t part = lookAtNearest(w, i, w.racers.visible[i].rotation);
		if (part >= 0) {
			look.lookMs = int32_t(w.lookDraw(i, false) % 1000) + 5000;
			play(f, part, Default);
			return;
		}
	}
	if (turn < 0 && shown != SteerRight) {
		transition(f, SteerRight);
	}
	else if (turn > 0 && shown != SteerLeft) {
		transition(f, SteerLeft);
	}
	else if (turn == 0 && shown != Default) {
		transition(f, Default);
	}
}

} // namespace

uint16_t World::lookDraw(size_t racer, bool cooldown)
{
	if (observer.to) {
		observer.to->onLook(*this, racer, cooldown);
	}
	return randomTable(raceView.lookRandom = (raceView.lookRandom + 1) & 1023);
}

namespace {

// A part file (and the skeleton it moves) by name, loaded once.
int16_t figureOf(World& w, const Cache& cache, const std::string& animation, const std::string& skeleton)
{
	if (animation.empty()) {
		return -1;
	}
	for (size_t f = 0; f < w.raceView.figureNames.size(); f++) {
		if (w.raceView.figureNames[f] == animation) {
			return int16_t(f);
		}
	}
	AnimationFile file;
	SkeletonFile nodes;
	// (A missing file: a figure without parts or nodes.)
	(void)cache.get(Common + binaryName(animation) + ".ADB", file);
	(void)cache.get(Common + binaryName(skeleton.empty() ? animation : skeleton) + ".SDB", nodes);
	w.raceView.figures.push_back(animationOf(file, false));
	std::vector<SkeletonNode>& out = w.raceView.skeletons.emplace_back();
	for (const SkeletonFile::Node& node : nodes.nodes) {
		int16_t parent = -1;
		for (size_t k = 0; k < nodes.nodes.size(); k++) {
			parent = !node.parent.empty() && nodes.nodes[k].name == node.parent ? int16_t(k) : parent;
		}
		out.push_back({vec(node.position), basisOf({node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]}), parent});
	}
	w.raceView.figureNames.push_back(animation);
	return int16_t(w.raceView.figures.size() - 1);
}

// A racer's chassis: its part file and model's scale, the driver's mount, and its variant's.
void chassisShape(World& w, const Cache& cache, const ChassisTable::Chassis& chassis, RacerShape& shape)
{
	shape.carAnimation = figureOf(w, cache, chassis.animation, chassis.skeleton);
	shape.carScale = modelScale(cache, Common + binaryName(chassis.model) + ".GDB");
	shape.mount = vec(chassis.driverMount);
	if (!chassis.second.model.empty()) {
		shape.present[RacerShape::Variant] = true;
		shape.variantAnimation = figureOf(w, cache, chassis.second.animation, chassis.second.skeleton);
		shape.variantScale = modelScale(cache, Common + binaryName(chassis.second.model) + ".GDB");
	}
}

// A built car's body: the race's low build of its pieces, at scale 1; the fallback for none.
Sphere builtBody(const PieceLibrary& low, const std::vector<CarPiece>& pieces, Sphere body)
{
	Car car;
	car.load(low, pieces);
	car.chooseOffset(low);
	const CarMesh mesh = car.mesh(low, {}, {false, true, true, true}, car.entries.size());
	Model m;
	m.scale = 1; // (the built car is drawn at 1, not at the chassis model's scale)
	for (const CarMesh::Vertex& v : mesh.vertices) {
		m.positions.insert(m.positions.end(), v.position.begin(), v.position.end());
	}
	if (!mesh.vertices.empty()) {
		body = modelBounds(m);
	}
	return body;
}

// The cheats take members away.
void cheatShape(uint32_t cheats, RacerShape& shape)
{
	if (cheats & CheatNoWheels) {
		shape.present[RacerShape::Car] = false;
	}
	if (cheats & CheatNoBody) {
		shape.present[RacerShape::Body] = shape.present[RacerShape::Variant] = false;
	}
	if (cheats & CheatNoDriver) {
		shape.present[RacerShape::Driver] = false;
	}
}

// The players' cameras (the demo's: racer 0's), each at its saved view, updated once (the chase pose), then
// waiting at the race file's place for the flyby or the transition.
void setupCameras(World& w, const RaceBrief& race, size_t n)
{
	w.raceView.cameras.clear();
	for (size_t i = 0; i < n; i++) {
		if (race.slots[i].player && w.raceView.cameras.size() < 2) {
			w.raceView.cameras.emplace_back().racer = int8_t(i);
		}
	}
	if (w.raceView.cameras.empty() && n > 0) {
		w.raceView.cameras.emplace_back().racer = 0;
	}
	w.raceView.mapModes.assign(w.raceView.cameras.size(), GadgetFixedMap);
	w.raceView.huds.assign(w.raceView.cameras.size(), {});
	for (size_t p = 0; p < w.raceView.cameras.size(); p++) {
		PlayerCamera& c = w.raceView.cameras[p];
		c.split = w.raceView.cameras.size() > 1;
		c.fov = c.split ? 40.0f : 65.0f;
		c.viewport = !c.split ? std::array<int32_t, 4>{0, 0, 640, 480} : p == 0 ? std::array<int32_t, 4>{0, 0, 640, 240} : std::array<int32_t, 4>{0, 240, 640, 480};
		c.aspect = c.split ? 2.6666667f : 1.3333334f;
		w.raceView.mapModes[p] = race.maps[p];
		w.chooseView(p, race.views[p] & 3);
		w.updateCamera(p, 1);
		c.toEye = c.rawEye;
		c.toRotation = c.rawRotation;
		c.kind = PlayerCamera::Transition;
		c.clockMs = 0;
		c.eye = w.track.startCamera;
		Vec3 r, d, l;
		l = normalize(w.track.startLook);
		d = normalize(w.track.startDown - l * dot(w.track.startDown, l));
		r = cross(d, l);
		c.right = r, c.down = d, c.look = l;
		c.rotation = quaternionOf(Basis{r, d, l});
		if (w.session.watchingFlyby && w.track.introEntity >= 0) {
			c.kind = PlayerCamera::Flyby;
		}
	}
}

} // namespace

void World::setupView(const Cache& cache, const RaceBrief& race, const std::vector<std::string>& bodies)
{
	const size_t n = racers.karts.size();
	raceView.figures.clear();
	raceView.skeletons.clear();
	raceView.figureNames.clear();
	figureOf(*this, cache, "pelvis", "pelvis");

	ChassisTable chassisTable;
	ChampionTable champions;
	DriverTable driverTable;
	// (World::setup has read the three already.)
	(void)cache.get(Common + "CHASSIS.CMB", chassisTable);
	(void)cache.get(Common + "CHAMPS.CCB", champions);
	(void)cache.get(Common + "DRIVERS.DDB", driverTable);
	PieceLibrary low;
	const bool lowLoaded = cache.get("MENUDATA/PIECEDB/LPIECELO.LEB", low);

	raceView.shapes.assign(n, {});
	raceView.looks.assign(n, {});
	raceView.lookRandom = race.random;
	for (size_t i = 0; i < n; i++) {
		const RaceBrief::Slot& slot = race.slots[i];
		RacerShape& shape = raceView.shapes[i];
		const DriverTable::Driver* driver = slot.driver.empty() ? driverTable.find("BB") : driverTable.find(slot.driver);
		const ChampionTable::Champion* champion = !slot.driver.empty() && driver ? champions.find(driver->champion) : nullptr;
		const ChassisTable::Chassis* chassis = chassisTable.find(champion ? champion->chassis : slot.chassis);
		if (chassis) {
			chassisShape(*this, cache, *chassis, shape);
		}
		// (A player's slot races its record's built minifig, body set 1's seated leg box; DRIVERS.DDB's
		// "bb" only if that can't be had.)
		const std::string seated = "MENUDATA/PARTDB/GAMEPART/LEG_BOX.GDB";
		shape.driverScale = slot.driver.empty() && cache.has(seated) ? modelScale(cache, seated) : driver ? modelScale(cache, Common + binaryName(driver->model) + ".GDB") : 1.0f;
		// The body: the champion's model, or the built car (the race's build of its pieces).
		shape.body = racers.carBounds[i];
		if (i < bodies.size() && !bodies[i].empty()) {
			Model body;
			if (cache.get(Common + binaryName(bodies[i]) + ".GDB", body)) {
				shape.body = modelBounds(body);
			}
		}
		else if (!slot.car.empty() && lowLoaded) {
			shape.body = builtBody(low, slot.car, shape.body);
		}
		racers.bodyBounds[i] = shape.body;
		cheatShape(race.cheats, shape);

		RacerLook& look = raceView.looks[i];
		look.body = racers.visible[i];
		look.car.part = 0;
		look.variant.part = 0;
		look.figure.part = Default;
		look.lookMs = lookDraw(i, true) % 2000;
		look.driver = {look.body.position + toWorld(look.body.rotation, shape.mount), look.body.rotation};
		updateGroup(i);
	}
	racers.groupsStale = true;
	setupCameras(*this, race, n);
	raceView.drawnBy.assign(scenery.clocks.size(), 0);
	raceView.scrollOffsets.assign(track.scrolls.size(), {});
}

bool World::member(size_t i, RacerShape::Member which, Pose& pose, float& scale) const
{
	const RacerShape& shape = raceView.shapes[i];
	const RacerLook& look = raceView.looks[i];
	const float g = racers.powers.empty() ? 1.0f : racers.powers[i].carScale;
	switch (which) {
	case RacerShape::Body: pose = look.body, scale = g; break;
	case RacerShape::Variant: pose = look.body, scale = shape.variantScale * g; break;
	case RacerShape::Car: pose = racers.visible[i], scale = shape.carScale * g; break;
	case RacerShape::Driver: pose = look.driver, scale = shape.driverScale * g; break;
	}
	return shape.present[which];
}

void World::updateGroup(size_t i)
{
	const RacerShape& shape = raceView.shapes[i];
	const RacerLook& look = raceView.looks[i];
	Sphere spheres[4];
	int count = 0;
	for (int m = 0; m < 4; m++) {
		Pose pose;
		float g;
		if (!member(i, RacerShape::Member(m), pose, g)) {
			continue;
		}
		Sphere local;
		if (m == RacerShape::Body) {
			local = shape.body;
		}
		else if (m == RacerShape::Variant || m == RacerShape::Car) {
			const int16_t a = m == RacerShape::Car ? shape.carAnimation : shape.variantAnimation;
			if (a < 0) {
				continue;
			}
			local = partBounds(raceView.figures[size_t(a)], (m == RacerShape::Car ? look.car : look.variant).part);
		}
		else {
			const PartClock& c = look.figure;
			local = partBounds(raceView.figures[0], c.part);
			if (c.progress < 1 && c.fromPart >= 0) {
				local = merged(partBounds(raceView.figures[0], c.fromPart), local);
			}
		}
		spheres[count++] = {toWorld(pose.rotation, local.centre * g) + pose.position, local.radius * g};
	}
	if (count == 0) {
		racers.groups[i] = {racers.visible[i].position, 0};
		return;
	}
	// The members' mean centre (from the first's), and the radius that holds them all.
	const Vec3 c0 = spheres[0].centre;
	Vec3 sum;
	for (int k = 0; k < count; k++) {
		sum += spheres[k].centre - c0;
	}
	const Vec3 off = sum * (1.0f / float(count));
	float radius = 0;
	for (int k = 0; k < count; k++) {
		radius = std::max(radius, spheres[k].radius + lengthZYX(spheres[k].centre - c0 - off));
	}
	racers.groups[i] = {off + c0, radius};
}

void World::racerTouches()
{
	// (The first test after the setup places the members as set up with the car where its first step left it.)
	if (racers.groupsStale) {
		for (size_t i = 0; i < racers.karts.size(); i++) {
			updateGroup(i);
		}
		racers.groupsStale = false;
	}
	touchRacers(racers.groups.data());
}

void World::visuals(int32_t dt)
{
	raceView.stage = RaceEvent::Animation;
	for (size_t i = 0; i < racers.karts.size(); i++) {
		racerLook(i, dt);
	}
}

void World::racerLook(size_t i, int32_t dt)
{
	RacerLook& look = raceView.looks[i];
	const RacerShape& shape = raceView.shapes[i];
	const Kart& k = racers.karts[i];
	const float fs = k.headingSpeed;
	if (shape.carAnimation >= 0) {
		carParts(*this, look, shape, fs, dt);
	}
	const Pose& carPose = racers.visible[i];
	leanBody(look, carPose, racers.params[i].mass, dt);
	// (The seat isn't scaled: a shrinking car's members shrink about their own origins.)
	look.driver = {look.body.position + toWorld(look.body.rotation, shape.mount), look.body.rotation};

	// The driver model.
	PartClock& f = look.figure;
	const int32_t shown = f.part;
	advanceFigure(f, raceView.figures[0], dt);
	const uint8_t marks = look.marks;
	look.marks = 0;
	look.stoppedHard = false;
	look.lookMs = dt < look.lookMs ? look.lookMs - dt : 0;
	if (racers.race[i].laps >= session.raceLaps) {
		finishFigure(*this, i, f, shown);
	}
	else {
		driverFigure(*this, i, shown, marks, fs);
	}
	carVisuals(i);
	updateGroup(i);
}

void World::driftMarks(size_t i)
{
	RacerLook& look = raceView.looks[i];
	const Kart& k = racers.karts[i];
	const bool sliding = k.flags & KartPowerslide;
	if (sliding && !look.drifting) {
		look.drifting = true;
		if (racers.race[i].player && !(k.flags & KartHover)) {
			raise(RaceEvent::DustStart, {Owner::Racer, int16_t(i)});
			if (!look.skidding) {
				playSound(57, {Owner::Racer, int16_t(i)}, &racers.visible[i].position);
				skidEffects(i, true, RaceEvent::Drift);
			}
		}
	}
	else if (!sliding && look.drifting) {
		look.drifting = false;
		if (!(k.flags & KartHover) && !(racers.drivers[i].flags & DriverTurbo)) {
			skidEffects(i, false, RaceEvent::Drift);
		}
	}
}

void World::skidEffects(size_t i, bool on, RaceEvent::Reason why)
{
	RacerLook& look = raceView.looks[i];
	if (on && (racers.race[i].flags & RacerHalted)) {
		return; // (ignored while held)
	}
	look.skidding = on;
	raise(on ? RaceEvent::SkidStart : RaceEvent::SkidStop, {Owner::Racer, int16_t(i)}).value = why;
}

// The car's visuals the show's events need: the spin's tyre smoke, a held racer's skid
// stopped, the landing.
void World::carVisuals(size_t i)
{
	RacerLook& look = raceView.looks[i];
	const Kart& k = racers.karts[i];
	const Racer& r = racers.race[i];
	if (r.flags & RacerGhost) {
		return;
	}
	if ((k.flags & KartSpinning) && !look.spinSkid) {
		look.spinSkid = true;
		if (!r.ai) {
			skidEffects(i, true, RaceEvent::Spin);
		}
	}
	else if (!(k.flags & KartSpinning) && look.spinSkid) {
		look.spinSkid = false;
		skidEffects(i, false, RaceEvent::Spin);
	}
	if ((r.flags & RacerHalted) && look.skidding) {
		skidEffects(i, false, RaceEvent::Halted);
	}
	if (k.airborneMs > 400) {
		look.airborne = true;
	}
	if (look.airborne) {
		if (k.contacts > look.grounded) {
			look.airborne = false;
			const int32_t rise = k.contacts - look.grounded;
			RaceEvent& e = raise(RaceEvent::Landed, {Owner::Racer, int16_t(i)});
			e.value = rise;
			e.place = racers.visible[i].position;
			playSound(rise == 1 ? 63 : rise <= 3 ? 64 : 4, {Owner::Racer, int16_t(i)}, &racers.visible[i].position);
		}
		look.grounded = k.contacts;
	}
}

void World::showMaterial(size_t k)
{
	const Track::MaterialTrack& t = track.materialTracks[k];
	const int32_t f = int32_t(raceView.materials[k].cursor) % std::max(1, t.duration);
	int16_t shown = int16_t(t.frames.size()) - 1;
	for (size_t e = 0; e < t.frames.size(); e++) {
		shown = t.frames[e].second <= f ? int16_t(e) : shown;
	}
	raceView.materials[k].shown = shown;
}

void World::advanceMaterials(int32_t ms)
{
	// Each assigned track: its cursor runs; one not looping stops at its end, its slots keep what they show.
	raceView.materials.resize(track.materialTracks.size());
	for (size_t k = 0; k < raceView.materials.size(); k++) {
		RaceView::MaterialState& m = raceView.materials[k];
		if (!m.assigned) {
			continue;
		}
		m.cursor = m.rate * float(ms) + m.cursor;
		if (!m.looping && int32_t(m.cursor) >= track.materialTracks[k].duration) {
			m.assigned = false;
			m.cursor = 0;
			continue;
		}
		showMaterial(k);
	}
	raceView.scrollOffsets.resize(track.scrolls.size());
	for (size_t s = 0; s < track.scrolls.size(); s++) {
		const Track::Scroll& sc = track.scrolls[s];
		raceView.scrollOffsets[s][0] = uint16_t(raceView.scrollOffsets[s][0] + uint32_t(sc.u * ms));
		raceView.scrollOffsets[s][1] = uint16_t(raceView.scrollOffsets[s][1] + uint32_t(sc.v * ms));
	}
}

} // namespace lr
