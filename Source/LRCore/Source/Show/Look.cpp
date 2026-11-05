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

float wrapLoop(float t, float loop) { return t >= loop ? t - loop * float(int32_t(t / loop)) : t; }

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

Sphere partBounds(const Animation& a, int32_t part)
{
	return part >= 0 && size_t(part) < a.parts.size() ? a.parts[size_t(part)].bounds : Sphere{};
}

// A model's scale (its .GDB's), 1 if it can't be read.
float modelScale(const Cache& cache, const std::string& path)
{
	Model m;
	return cache.get(path, m) ? m.scale : 1.0f;
}

} // namespace

uint16_t World::lookDraw(size_t racer, bool cooldown)
{
	if (lookWatch) {
		lookWatch(*this, racer, cooldown, lookContext);
	}
	return randomTable(lookRandom = (lookRandom + 1) & 1023);
}

void World::setupView(const Cache& cache, const RaceBrief& race, const std::vector<std::string>& bodies)
{
	const size_t n = karts.size();
	figures.clear();
	skeletons.clear();
	figureNames.clear();
	// A part file (and the skeleton it moves) by name, loaded once.
	auto figureOf = [&](const std::string& animation, const std::string& skeleton) -> int16_t {
		if (animation.empty()) {
			return -1;
		}
		for (size_t f = 0; f < figureNames.size(); f++) {
			if (figureNames[f] == animation) {
				return int16_t(f);
			}
		}
		AnimationFile file;
		SkeletonFile nodes;
		cache.get(Common + binaryName(animation) + ".ADB", file);
		cache.get(Common + binaryName(skeleton.empty() ? animation : skeleton) + ".SDB", nodes);
		figures.push_back(animationOf(file, false));
		std::vector<SkeletonNode>& out = skeletons.emplace_back();
		for (const SkeletonFile::Node& node : nodes.nodes) {
			int16_t parent = -1;
			for (size_t k = 0; k < nodes.nodes.size(); k++) {
				parent = !node.parent.empty() && nodes.nodes[k].name == node.parent ? int16_t(k) : parent;
			}
			out.push_back({vec(node.position), basisOf({node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]}), parent});
		}
		figureNames.push_back(animation);
		return int16_t(figures.size() - 1);
	};
	figureOf("pelvis", "pelvis");

	ChassisTable chassisTable;
	ChampionTable champions;
	DriverTable driverTable;
	cache.get(Common + "CHASSIS.CMB", chassisTable);
	cache.get(Common + "CHAMPS.CCB", champions);
	cache.get(Common + "DRIVERS.DDB", driverTable);
	PieceLibrary low;
	const bool lowLoaded = cache.get("MENUDATA/PIECEDB/LPIECELO.LEB", low);

	shapes.assign(n, {});
	looks.assign(n, {});
	groups.assign(n, {});
	lookRandom = race.random;
	for (size_t i = 0; i < n; i++) {
		const RaceBrief::Slot& slot = race.slots[i];
		RacerShape& shape = shapes[i];
		const DriverTable::Driver* driver = slot.driver.empty() ? driverTable.find("BB") : driverTable.find(slot.driver);
		const ChampionTable::Champion* champion = !slot.driver.empty() && driver ? champions.find(driver->champion) : nullptr;
		const ChassisTable::Chassis* chassis = chassisTable.find(champion ? champion->chassis : slot.chassis);
		if (chassis) {
			shape.carAnimation = figureOf(chassis->animation, chassis->skeleton);
			shape.carScale = modelScale(cache, Common + binaryName(chassis->model) + ".GDB");
			shape.mount = vec(chassis->driverMount);
			if (!chassis->second.model.empty()) {
				shape.present[RacerShape::Variant] = true;
				shape.variantAnimation = figureOf(chassis->second.animation, chassis->second.skeleton);
				shape.variantScale = modelScale(cache, Common + binaryName(chassis->second.model) + ".GDB");
			}
		}
		// (A player's slot races its record's built minifig, body set 1's seated leg box; DRIVERS.DDB's
		// "bb" only if that can't be had.)
		const std::string seated = "MENUDATA/PARTDB/GAMEPART/LEG_BOX.GDB";
		shape.driverScale = slot.driver.empty() && cache.has(seated) ? modelScale(cache, seated)
			: driver                                                 ? modelScale(cache, Common + binaryName(driver->model) + ".GDB")
																	 : 1.0f;
		// The body: the champion's model, or the built car (the race's build of its pieces).
		shape.body = carBounds[i];
		if (i < bodies.size() && !bodies[i].empty()) {
			Model body;
			if (cache.get(Common + binaryName(bodies[i]) + ".GDB", body)) {
				shape.body = modelBounds(body);
			}
		}
		else if (!slot.car.empty() && lowLoaded) {
			Car car;
			car.load(low, slot.car);
			car.chooseOffset(low);
			const CarMesh mesh = car.mesh(low, {}, {false, true, true, true}, car.entries.size());
			Model m;
			m.scale = 1; // (the built car is drawn at 1, not at the chassis model's scale)
			for (const CarMesh::Vertex& v : mesh.vertices) {
				m.positions.insert(m.positions.end(), v.position.begin(), v.position.end());
			}
			if (!mesh.vertices.empty()) {
				shape.body = modelBounds(m);
			}
		}
		bodyBounds[i] = shape.body;
		// The cheats take members away.
		if (race.cheats & CheatNoWheels) {
			shape.present[RacerShape::Car] = false;
		}
		if (race.cheats & CheatNoBody) {
			shape.present[RacerShape::Body] = shape.present[RacerShape::Variant] = false;
		}
		if (race.cheats & CheatNoDriver) {
			shape.present[RacerShape::Driver] = false;
		}

		RacerLook& look = looks[i];
		look.body = visible[i];
		look.car.part = 0;
		look.variant.part = 0;
		look.figure.part = Default;
		look.lookMs = lookDraw(i, true) % 2000;
		look.driver = {look.body.position + toWorld(look.body.rotation, shape.mount), look.body.rotation};
		updateGroup(i);
	}
	groupsStale = true;

	// The players' cameras (the demo's: racer 0's), each at its saved view, updated once (the chase pose), then
	// waiting at the race file's place for the flyby or the transition.
	cameras.clear();
	for (size_t i = 0; i < n; i++) {
		if (race.slots[i].player && cameras.size() < 2) {
			cameras.emplace_back().racer = int8_t(i);
		}
	}
	if (cameras.empty() && n > 0) {
		cameras.emplace_back().racer = 0;
	}
	mapModes.assign(cameras.size(), 1);
	huds.assign(cameras.size(), {});
	for (size_t p = 0; p < cameras.size(); p++) {
		PlayerCamera& c = cameras[p];
		c.split = cameras.size() > 1;
		c.fov = c.split ? 40.0f : 65.0f;
		c.viewport = !c.split ? std::array<int32_t, 4>{0, 0, 640, 480} : p == 0 ? std::array<int32_t, 4>{0, 0, 640, 240} : std::array<int32_t, 4>{0, 240, 640, 480};
		c.aspect = c.split ? 2.6666667f : 1.3333334f;
		mapModes[p] = race.maps[p];
		chooseView(p, race.views[p] & 3);
		updateCamera(p, 1);
		c.toEye = c.rawEye;
		c.toRotation = c.rawRotation;
		c.kind = PlayerCamera::Transition;
		c.clockMs = 0;
		c.eye = track.startCamera;
		Vec3 r, d, l;
		l = normalize(track.startLook);
		d = normalize(track.startDown - l * dot(track.startDown, l));
		r = cross(d, l);
		c.right = r, c.down = d, c.look = l;
		c.rotation = quaternionOf(Basis{r, d, l});
		if (session.watchingFlyby && track.introEntity >= 0) {
			c.kind = PlayerCamera::Flyby;
		}
	}
	drawnBy.assign(clocks.size(), 0);
	scrollOffsets.assign(track.scrolls.size(), {});
}

bool World::member(size_t i, RacerShape::Member which, Pose& pose, float& scale) const
{
	const RacerShape& shape = shapes[i];
	const RacerLook& look = looks[i];
	const float g = powers.empty() ? 1.0f : powers[i].carScale;
	switch (which) {
	case RacerShape::Body: pose = look.body, scale = g; break;
	case RacerShape::Variant: pose = look.body, scale = shape.variantScale * g; break;
	case RacerShape::Car: pose = visible[i], scale = shape.carScale * g; break;
	case RacerShape::Driver: pose = look.driver, scale = shape.driverScale * g; break;
	}
	return shape.present[which];
}

void World::updateGroup(size_t i)
{
	const RacerShape& shape = shapes[i];
	const RacerLook& look = looks[i];
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
			local = partBounds(figures[size_t(a)], (m == RacerShape::Car ? look.car : look.variant).part);
		}
		else {
			const PartClock& c = look.figure;
			local = partBounds(figures[0], c.part);
			if (c.progress < 1 && c.fromPart >= 0) {
				local = merged(partBounds(figures[0], c.fromPart), local);
			}
		}
		spheres[count++] = {toWorld(pose.rotation, local.centre * g) + pose.position, local.radius * g};
	}
	if (count == 0) {
		groups[i] = {visible[i].position, 0};
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
	groups[i] = {off + c0, radius};
}

void World::racerTouches()
{
	// (The first test after the setup places the members as set up with the car where its first step left it.)
	if (groupsStale) {
		for (size_t i = 0; i < karts.size(); i++) {
			updateGroup(i);
		}
		groupsStale = false;
	}
	touchRacers(groups.data());
}

void World::visuals(int32_t dt)
{
	stage = RaceEvent::Animation;
	for (size_t i = 0; i < karts.size(); i++) {
		racerLook(i, dt);
	}
}

void World::racerLook(size_t i, int32_t dt)
{
	RacerLook& look = looks[i];
	const RacerShape& shape = shapes[i];
	const Kart& k = karts[i];
	const float fs = k.headingSpeed;

	// The chassis' part by the forward speed, at a rate by it (the file's rate isn't used); the variant alike, part 0.
	if (shape.carAnimation >= 0) {
		const Animation& a = figures[size_t(shape.carAnimation)];
		const bool still = std::fabs(fs) < 9.9999997e-05f;
		const int32_t part = !still && fs < 0 && a.parts.size() > 1 ? 1 : 0;
		if (part != look.car.part) {
			play(look.car, part, -1);
		}
		advancePart(look.car, a, still ? 0.0f : std::fabs(fs) * 1.8f, dt);
		if (shape.variantAnimation >= 0) {
			advancePart(look.variant, figures[size_t(shape.variantAnimation)], still ? 0.0f : std::fabs(fs) * 1.8f, dt);
		}
	}

	// The body's lean: two springs (pitch and roll) pulling the body's up toward the car's.
	const Pose& carPose = visible[i];
	const Basis& c = carPose.rotation;
	const Vec3 b2 = look.body.rotation.u;
	const Vec3 t0 = cross(c.s, b2), t1 = cross(b2, c.f);
	float pitchLen = length(c.f - t0), rollLen = length(c.s - t1);
	pitchLen = dot(t0, c.u) < 0 ? -pitchLen : pitchLen;
	rollLen = dot(t1, c.u) < 0 ? -rollLen : rollLen;
	const float m = params[i].mass;
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
	// (The seat isn't scaled: a shrinking car's members shrink about their own origins.)
	look.driver = {look.body.position + toWorld(look.body.rotation, shape.mount), look.body.rotation};

	// The driver model.
	PartClock& f = look.figure;
	const int32_t shown = f.part;
	advanceFigure(f, figures[0], dt);
	const uint8_t marks = look.marks;
	look.marks = 0;
	look.stoppedHard = false;
	look.lookMs = dt < look.lookMs ? look.lookMs - dt : 0;
	const Racer& r = racers[i];
	auto one = [&](std::initializer_list<int32_t> parts) { return std::find(parts.begin(), parts.end(), shown) != parts.end(); };
	[&] {
		if (r.laps >= raceLaps) {
			if (r.standing == 1) {
				if (!one({Winner, Winner2, Winner2Loop})) {
					if (lookDraw(i, false) & 1) {
						play(f, Winner2, Winner2Loop);
					}
					else {
						play(f, Winner, -1);
					}
				}
			}
			else if (!one({Loser, LoserLoop})) {
				play(f, Loser, LoserLoop);
			}
			return;
		}
		if (look.lastSpeed > 0.050000001f && fs < 0.0099999998f && shown != VeloStop) {
			look.stoppedHard = true;
			voice(i, RaceEvent::Negative);
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
		if (one({VeloStop, Boost, Hit, Winner, Winner2Loop, LoserLoop})) {
			return;
		}
		// The drive controller's thrust and turn radius (a route kart has none).
		const bool driven = !playback[i].active;
		const float thrust = driven ? drivers[i].thrust : 0.0f, turn = driven ? drivers[i].radius : 0.0f;
		if (thrust < 0 && fs < 0) {
			if (!one({Reverse, RevHold})) {
				play(f, Reverse, RevHold);
			}
			return;
		}
		if (shown == RevHold) {
			play(f, RevBack, Default);
			return;
		}
		if (one({ShortLookRight, ShortLookLeft, LongLookRight, LongLookLeft})) {
			return;
		}
		if (look.lookMs == 0) {
			// The nearest other racer, if it is beside or behind: look at it.
			const Vec3 own = visible[i].position;
			float best = 0;
			int32_t nearest = -1;
			for (size_t j = 0; j < karts.size(); j++) {
				const Vec3 d = visible[j].position - own;
				const float d2 = d.z * d.z + d.y * d.y + d.x * d.x;
				if (d2 >= 2 && d2 <= 40000 && (nearest < 0 || d2 < best)) {
					best = d2;
					nearest = int32_t(j);
				}
			}
			int32_t part = -1;
			if (nearest >= 0) {
				const Vec3 u = normalize(visible[size_t(nearest)].position - own);
				const float ahead = dotZYX(c.f, u), side = dotZYX(c.s, u);
				if (ahead > -0.2f && ahead < 0.2f) {
					part = side < 0 ? ShortLookRight : ShortLookLeft;
				}
				else if (ahead > -0.60000002f && ahead <= -0.2f) {
					part = side < 0 ? LongLookRight : LongLookLeft;
				}
			}
			if (part >= 0) {
				look.lookMs = int32_t(lookDraw(i, false) % 1000) + 5000;
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
	}();
	carVisuals(i);
	updateGroup(i);
}

void World::driftMarks(size_t i)
{
	RacerLook& look = looks[i];
	const Kart& k = karts[i];
	const bool sliding = k.flags & KartPowerslide;
	if (sliding && !look.drifting) {
		look.drifting = true;
		if (racers[i].player && !(k.flags & KartHover)) {
			raise(RaceEvent::DustStart, {Owner::Racer, int16_t(i)});
			if (!look.skidding) {
				playSound(57, {Owner::Racer, int16_t(i)}, &visible[i].position);
				skidEffects(i, true, RaceEvent::Drift);
			}
		}
	}
	else if (!sliding && look.drifting) {
		look.drifting = false;
		if (!(k.flags & KartHover) && !(drivers[i].flags & DriverTurbo)) {
			skidEffects(i, false, RaceEvent::Drift);
		}
	}
}

void World::skidEffects(size_t i, bool on, RaceEvent::Reason why)
{
	RacerLook& look = looks[i];
	if (on && (racers[i].flags & RacerHalted)) {
		return; // (ignored while held)
	}
	look.skidding = on;
	raise(on ? RaceEvent::SkidStart : RaceEvent::SkidStop, {Owner::Racer, int16_t(i)}).value = why;
}

// The car's visuals the show's events need: the spin's tyre smoke, a held racer's skid
// stopped, the landing.
void World::carVisuals(size_t i)
{
	RacerLook& look = looks[i];
	const Kart& k = karts[i];
	const Racer& r = racers[i];
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
			e.place = visible[i].position;
			playSound(rise == 1 ? 63 : rise <= 3 ? 64 : 4, {Owner::Racer, int16_t(i)}, &visible[i].position);
		}
		look.grounded = k.contacts;
	}
}

void World::showMaterial(size_t k)
{
	const Track::MaterialTrack& t = track.materialTracks[k];
	const int32_t f = int32_t(materials[k].cursor) % std::max(1, t.duration);
	int16_t shown = int16_t(t.frames.size()) - 1;
	for (size_t e = 0; e < t.frames.size(); e++) {
		shown = t.frames[e].second <= f ? int16_t(e) : shown;
	}
	materials[k].shown = shown;
}

void World::advanceMaterials(int32_t ms)
{
	// Each assigned track: its cursor runs; one not looping stops at its end, its slots keep what they show.
	materials.resize(track.materialTracks.size());
	for (size_t k = 0; k < materials.size(); k++) {
		MaterialState& m = materials[k];
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
	scrollOffsets.resize(track.scrolls.size());
	for (size_t s = 0; s < track.scrolls.size(); s++) {
		const Track::Scroll& sc = track.scrolls[s];
		scrollOffsets[s][0] = uint16_t(scrollOffsets[s][0] + uint32_t(sc.u * ms));
		scrollOffsets[s][1] = uint16_t(scrollOffsets[s][1] + uint32_t(sc.v * ms));
	}
}

} // namespace lr
