#include "LRCore/Race/Track.h"

#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Collision.h"
#include "LRData/Model.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"

#include <algorithm>
#include <limits>
#include <cfloat>
#include <cmath>

namespace lr {

namespace {

bool samePrefix(std::string_view a, std::string_view b) { return a.substr(0, 8) == b.substr(0, 8); }

const std::string CommonFolder = "GAMEDATA/COMMON/";

// A file the race names, in the track's folder or else the common one.
std::string named(const Cache& cache, const std::string& dir, const std::string& name) { return cache.size(dir + name) ? dir + name : CommonFolder + name; }

// An entity's pose as placed (mirrored: y negated, the axes rebuilt from the flipped ones).
Pose placed(const SceneFile::Entity& e, bool mirrored)
{
	const float flip = mirrored ? -1.0f : 1.0f;
	return {{e.position[0], e.position[1] * flip, e.position[2]}, basisFromForwardUp({e.forward[0], e.forward[1] * flip, e.forward[2]}, {e.up[0], e.up[1] * flip, e.up[2]})};
}

// Each checkpoint's share of a lap: the main lap (next[0] from 0 back to 0) in even steps, then each branch's
// checkpoints evenly between where it leaves the lap and where it rejoins it.
void lapFractions(std::vector<Checkpoint>& cps)
{
	const size_t n = cps.size();
	for (Checkpoint& c : cps) {
		c.fraction = -1;
	}
	if (n == 0) {
		return;
	}
	auto next = [&](size_t c) { return size_t(cps[c].next[0]); };
	size_t lap = 0;
	for (size_t c = 0, i = 0; i < n && c < n; i++) {
		lap++;
		c = next(c);
		if (c == 0) {
			break;
		}
	}
	const float step = 1 / float(lap);
	float sum = 0;
	for (size_t c = 0; c < n && sum < 1;) {
		cps[c].fraction = sum;
		sum += step;
		c = next(c);
		if (c == 0) {
			break;
		}
	}
	for (size_t c = 0, i = 0; i < n && c < n; i++) {
		for (int b = 1; b < 4; b++) {
			const size_t start = cps[c].next[b];
			if (start == 255 || start >= n) {
				continue;
			}
			size_t target = start;
			uint32_t m = 0;
			for (size_t j = 0; j < n && target < n && cps[target].fraction < 0; j++) {
				target = next(target);
				m++;
			}
			if (target >= n) {
				continue;
			}
			// From the branch's first checkpoint up to the target (the target itself only when it is the first).
			const float bstep = (cps[target].fraction - cps[c].fraction) / float(m + 1);
			float f = cps[c].fraction;
			for (size_t k = start;; k = next(k)) {
				f += bstep;
				cps[k].fraction = f;
				if (k == target || next(k) == target || next(k) >= n) {
					break;
				}
			}
		}
		c = next(c);
		if (c == 0) {
			break;
		}
	}
}

} // namespace

Sphere modelBounds(const Model& m)
{
	const size_t n = m.positions.size() / 3;
	Vec3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
	for (size_t v = 0; v < n; v++) {
		const Vec3 p{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]};
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	}
	const Vec3 c = (hi + lo) * 0.5f;
	float r = 0;
	for (size_t v = 0; v < n; v++) {
		r = std::max(r, lengthZYX(Vec3{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]} - c));
	}
	return {c * m.scale, r * m.scale};
}

bool insideTriangle(Vec3 a3, Vec3 b3, Vec3 c3, Vec3 n, Vec3 x)
{
	// Project on the plane of the two axes the normal is least along.
	const float nx = std::fabs(n.x), ny = std::fabs(n.y), nz = std::fabs(n.z);
	int u, v;
	if (nx > ny) {
		u = nz > nx ? 0 : 1;
		v = nz > nx ? 1 : 2;
	}
	else {
		u = 0;
		v = nz > ny ? 1 : 2;
	}
	const float* A = &a3.x;
	const float* B = &b3.x;
	const float* C = &c3.x;
	const float* P = &x.x;
	const float au = A[u], av = A[v], bu = B[u], bv = B[v], cu = C[u], cv = C[v], pu = P[u], pv = P[v];
	// Each edge's line through the third corner's side: gXY(p) against gXY at a point on the edge.
	const float gAC = (au - cu) * pv + (cv - av) * pu, kAC = (au - cu) * cv + (cv - av) * cu;
	const float gAB = (bu - au) * pv + (av - bv) * pu, kAB = (bu - au) * av + (av - bv) * au;
	const float gCB = (cu - bu) * pv + (bv - cv) * pu, kCB = (cu - bu) * bv + (bv - cv) * bu;
	if ((cu - bu) * (av - bv) > (bv - cv) * (bu - au)) {
		return gAC >= kAC && gAB >= kAB && gCB >= kCB;
	}
	return gAC <= kAC && gAB <= kAB && gCB <= kCB;
}

int16_t Track::surfaceOf(std::string_view material) const
{
	for (size_t i = 0; i < surfaces.size(); i++) {
		if (samePrefix(surfaces[i].name, material)) {
			return int16_t(i);
		}
	}
	return -1;
}

// The volumes' meshes: one per entity (no track places a volume twice), its materials' surfaces and checkpoint gates.
bool Track::loadMeshes(const Cache& cache, const std::string& dir, const CollisionDatabase& db, bool mirror)
{
	for (size_t i = 0; i < db.entities.size(); i++) {
		BoundingVolume bvb;
		if (!cache.get(dir + binaryName(db.volumes[db.entities[i].volume]) + ".BVB", bvb)) {
			return false;
		}
		volumes[i].mesh = uint32_t(meshes.size());
		CollisionMesh& mesh = meshes.emplace_back();
		const bool checkpointMesh = int32_t(i) == checkpointVolume;
		std::vector<int16_t>& gates = mesh.materialCheckpoints;
		int16_t gateCount = 0;
		for (const std::string& name : bvb.materials) {
			mesh.materials.push_back(name);
			mesh.materialSurfaces.push_back(surfaceOf(name));
			// The k-th digit-named material is checkpoint k's gate.
			gates.push_back(checkpointMesh && !name.empty() && name[0] >= '0' && name[0] <= '9' ? gateCount++ : -1);
		}
		const float flip = mirror ? -1.0f : 1.0f;
		for (const BoundingVolume::Triangle& t : bvb.triangles) {
			CollisionMesh::Triangle& out = mesh.triangles.emplace_back();
			// Mirroring flips the winding back by swapping corners 1 and 2.
			const uint16_t order[3] = {t.v[0], mirror ? t.v[2] : t.v[1], mirror ? t.v[1] : t.v[2]};
			for (int c = 0; c < 3; c++) {
				const std::array<float, 3>& p = bvb.vertices[order[c]];
				out.v[c] = {p[0], p[1] * flip, p[2]};
			}
			out.surface = mesh.materialSurfaces[t.material];
			out.checkpoint = gates[t.material];
		}
		for (const BoundingVolume::Node& n : bvb.nodes) {
			CollisionMesh::Node& out = mesh.nodes.emplace_back();
			out.normal = {n.normal[0], n.normal[1] * flip, n.normal[2]};
			out.point = mesh.triangles[n.first].v[0];
			out.child[0] = n.child[0];
			out.child[1] = n.child[1];
			out.first = n.first;
			out.count = n.count;
		}
	}
	return true;
}

// The entities the track's sounds ride, the part animations play and the node transforms follow (the scenery is
// loaded after the events): a part animation's in the database it names, the others' the
// track's first, then the shared one; a model distance's model a plain one first, then an animated one.
void Track::findEventEntities()
{
	for (EventResource& r : events) {
		if (r.kind == EventResource::PartAnimation) {
			r.entity = r.entityName.empty() ? int16_t(-1) : animatedIn(r.entityName, r.plain);
			r.plain = false;
		}
		else if (r.kind == EventResource::ModelDistance) {
			const int32_t m = modelOf(r.entityName);
			r.plain = m >= 0;
			r.entity = m >= 0 ? int16_t(m) : animatedOf(r.entityName);
		}
		else {
			r.entity = r.entityName.empty() ? int16_t(-1) : animatedOf(r.entityName);
		}
	}
}

bool Track::load(const Cache& cache, std::string_view raceFile, bool mirror, bool timeRace)
{
	*this = {};
	const std::string dir(raceFile.substr(0, raceFile.rfind('/') + 1));
	RaceFile race;
	CollisionDatabase db;
	if (!cache.get(raceFile, race) || !cache.get(dir + binaryName(race.collisionFile), db)) {
		return false;
	}
	if (!race.surfaceFile.empty()) {
		SurfaceFile table;
		if (!cache.get(dir + binaryName(race.surfaceFile), table)) {
			return false;
		}
		for (const SurfaceFile::Surface& s : table.surfaces) {
			Vec3 push = vec(s.push);
			push.y = mirror ? -push.y : push.y;
			surfaces.push_back({s.name, s.flags, push, s.slopeHold, s.friction, s.grip, s.rolling, s.touchEvent, s.projectileEvent, {}, s.sound});
			std::copy_n(s.particle.begin(), std::min<size_t>(s.particle.size(), 8), surfaces.back().particle.begin());
		}
	}

	bool foundTrack = false;
	for (const CollisionDatabase::Entity& e : db.entities) {
		Volume& v = volumes.emplace_back();
		v.name = e.name;
		v.position = vec(e.position);
		v.rotation = basisFromForwardUp(vec(e.forward), vec(e.up));
		if (mirror) {
			v.position.y = -v.position.y;
			const Basis& r = v.rotation;
			v.rotation = basisFromForwardUp({r.f.x, -r.f.y, r.f.z}, {r.u.x, -r.u.y, r.u.z});
		}
		if (e.name == race.trackVolume) {
			trackVolume = uint32_t(volumes.size() - 1);
			foundTrack = true;
		}
		if (!race.checkpointVolume.empty() && e.name == race.checkpointVolume) {
			checkpointVolume = int32_t(volumes.size() - 1);
		}
	}
	if (!foundTrack) {
		return false;
	}

	if (!loadMeshes(cache, dir, db, mirror)) {
		return false;
	}
	this->mirrored = mirror;
	if (!loadRace(cache, dir, race, db, mirror) || !loadScenery(cache, dir, race) || !loadPowerups(cache, dir, race, timeRace)) {
		return false;
	}
	findEventEntities();
	return true;
}

namespace {

// A file's position or direction, as placed (mirrored: y negated).
Vec3 flipped(const std::array<float, 3>& p, float flip) { return Vec3{p[0], p[1] * flip, p[2]}; }

// The start grid and the checkpoints, with each one's share of a lap.
bool loadGrid(Track& t, const Cache& cache, const std::string& dir, const RaceFile& race, float flip)
{
	StartGrid start;
	if (!cache.get(dir + binaryName(race.startGridFile), start)) {
		return false;
	}
	for (const StartGrid::Place& p : start.places) {
		t.grid.push_back({flipped(p.position, flip), basisFromForwardUp(flipped(p.forward, flip), flipped(p.up, flip))});
	}

	if (!race.checkpointFile.empty()) {
		CheckpointFile file;
		if (!cache.get(dir + binaryName(race.checkpointFile), file)) {
			return false;
		}
		for (const CheckpointFile::Checkpoint& c : file.checkpoints) {
			t.checkpoints.push_back({flipped(c.normal, flip), c.offset, {c.next[0], c.next[1], c.next[2], c.next[3]}, -1, flipped(c.position, flip)});
		}
		lapFractions(t.checkpoints);
	}
	return true;
}

// The touch spheres (the racers', by xlo) and the positional ones.
bool loadSpheres(Track& t, const Cache& cache, const std::string& dir, const RaceFile& race, const CollisionDatabase& db, float flip)
{
	TouchFile touch;
	if (!cache.get(dir + binaryName(race.touchFile), touch)) {
		return false;
	}
	// Each added at the front, then sorted by xlo (keeping the order of equals).
	for (size_t i = touch.spheres.size(); i-- > 0;) {
		const TouchFile::Sphere& s = touch.spheres[i];
		if (s.projectilesOnly) {
			continue;
		}
		int32_t world = -1;
		if (!s.world.empty()) {
			world = int32_t(db.entities.size()); // a name not in the database: out of range (no track has one)
			for (size_t e = 0; e < db.entities.size(); e++) {
				if (db.entities[e].name == s.world) {
					world = int32_t(e);
					break;
				}
			}
		}
		const Vec3 c = flipped(s.centre, flip);
		t.touchSpheres.push_back({c, s.radius, c.x - s.radius, s.radius + c.x, int32_t(i), s.event, world, s.lapGated ? s.lap : -1, s.playersOnly});
	}
	std::stable_sort(t.touchSpheres.begin(), t.touchSpheres.end(), [](const TouchSphere& a, const TouchSphere& b) { return a.xlo < b.xlo; });

	if (!race.positionalFile.empty()) {
		TouchFile file;
		if (!cache.get(dir + binaryName(race.positionalFile), file)) {
			return false;
		}
		for (size_t i = 0; i < file.spheres.size(); i++) {
			const TouchFile::Sphere& s = file.spheres[i];
			const Vec3 c = flipped(s.centre, flip);
			t.places.push_back({c, s.radius, c.x - s.radius, s.radius + c.x, int32_t(i), s.event, -1, -1, s.playersOnly});
		}
	}
	return true;
}

// One event resource of the race's event file: a lap mark, or the track's own event kinds (the presentation's skipped).
void addEvent(Track& t, const EventFile::Resource& e, float flip)
{
	const int32_t* c = e.chains.data();
	switch (e.kind) {
	case EventFile::LapMarks: t.lapMarks.push_back({e.id, LapMark::Zone(e.zone), {c[0], c[1], c[2]}}); break;
	case EventFile::Timers:
	case EventFile::Forces:
	case EventFile::Links: {
		const EventResource::Kind kind = e.kind == EventFile::Timers ? EventResource::Timer : e.kind == EventFile::Forces ? EventResource::Force : EventResource::Link;
		t.events.push_back({kind, e.id, e.onEnd, e.noEnd, {c[0], c[1], c[2]}, e.durationMs, e.holdEvent, flipped(e.force, flip), e.channel, e.armingEvent, e.forced});
		break;
	}
	case EventFile::Colours: {
		EventResource& r = t.events.emplace_back();
		r = {EventResource::Colour, e.id, e.onEnd, e.noEnd, {c[0], c[1], c[2]}, 0, -1, {}, 0, -1, -1};
		r.clear = e.clear;
		r.onEntity = !e.entity.empty();
		break;
	}
	case EventFile::Sounds:
	case EventFile::Particles:
	case EventFile::SkyStates: {
		EventResource& r = t.events.emplace_back();
		r = {e.kind == EventFile::Sounds		 ? EventResource::Sound
				: e.kind == EventFile::Particles ? EventResource::Particle
												 : EventResource::Sky,
			e.id, e.onEnd, e.noEnd, {c[0], c[1], c[2]}, 0, -1, {}, 0, -1, -1};
		r.position = flipped(e.position, flip);
		r.atEvent = e.atEvent;
		const std::string& name = e.kind == EventFile::Particles ? e.emitter : e.sky;
		std::copy_n(name.begin(), std::min<size_t>(name.size(), 8), r.name.begin());
		r.sound = e.sound;
		r.volume = e.volume;
		r.pitch = e.pitch;
		r.nearDistance = e.nearDistance;
		r.farDistance = e.farDistance;
		r.looping = e.looping;
		r.chance = int32_t(e.probability * 255.0f);
		r.skyMs = e.skyMs;
		r.skyFlags = e.skyFlags;
		if (e.kind == EventFile::Sounds) {
			r.entityName = e.entity;
			r.node = e.node;
		}
		break;
	}
	case EventFile::PartAnimations:
	case EventFile::NodeTransforms:
	case EventFile::ModelDistances: {
		// (Their entities are found once the scenery is loaded.)
		EventResource& r = t.events.emplace_back();
		const EventResource::Kind kind = e.kind == EventFile::PartAnimations ? EventResource::PartAnimation
			: e.kind == EventFile::NodeTransforms							 ? EventResource::NodeTransform
																			 : EventResource::ModelDistance;
		r = {kind, e.id, e.onEnd, e.noEnd, {c[0], c[1], c[2]}, 0, -1, {}, 0, -1, -1};
		r.entityName = e.entity;
		r.node = e.node;
		r.looping = e.looping;
		r.parts = {e.parts[0], e.parts[1], e.parts[2], e.parts[3]};
		r.plain = e.shared; // (until the entity is found: its database)
		r.hide = e.hide;
		for (size_t v = 0; v < t.volumes.size() && !e.world.empty(); v++) {
			r.collision = samePrefix(t.volumes[v].name, e.world) ? int32_t(v) : r.collision;
		}
		break;
	}
	default: break; // the presentation's (their chains reach only the presentation's resources)
	}
}

// The timers and the targets.
bool loadTimersAndTargets(Track& t, const Cache& cache, const std::string& dir, const RaceFile& race, float flip)
{
	if (!race.timerFile.empty()) {
		TimerFile file;
		if (!cache.get(dir + binaryName(race.timerFile), file)) {
			return false;
		}
		for (const TimerFile::Timer& timer : file.timers) {
			t.timers.push_back({timer.onMs, timer.offMs, timer.randomOn, timer.randomOff, timer.event, timer.delayMs});
		}
	}

	if (!race.targetFile.empty()) {
		TargetFile file;
		if (!cache.get(dir + binaryName(race.targetFile), file)) {
			return false;
		}
		for (const TargetFile::Point& p : file.points) {
			t.targets.push_back({flipped(p.position, flip), p.disabledBy});
		}
	}
	return true;
}

// The cameras' place before the race, and the flyby.
bool loadCameras(Track& t, const Cache& cache, const std::string& dir, const RaceFile& race, float flip)
{
	// The cameras' place before the race.
	t.startCamera = flipped(race.startCamera, flip);
	t.startLook = flipped(race.startLook, flip);
	t.startDown = flipped(race.startDown, flip);

	// The flyby: the intro camera's animation, if the track has the camera.
	if (!race.introCamera.empty()) {
		AnimationFile camera;
		const std::string path = dir + binaryName(race.introCamera) + ".ADB";
		if (cache.size(path)) {
			if (!cache.get(path, camera) || camera.parts.empty()) {
				return false;
			}
			t.introFrames = camera.parts[0].frames;
			t.introRate = camera.parts[0].rate;
		}
	}
	return true;
}

} // namespace

bool Track::loadRace(const Cache& cache, const std::string& dir, const RaceFile& race, const CollisionDatabase& db, bool mirror)
{
	const float flip = mirror ? -1.0f : 1.0f;
	if (!loadGrid(*this, cache, dir, race, flip) || !loadSpheres(*this, cache, dir, race, db, flip)) {
		return false;
	}
	EventFile eventFile;
	if (!cache.get(dir + binaryName(race.eventFile), eventFile)) {
		return false;
	}
	for (const EventFile::Resource& e : eventFile.resources) {
		addEvent(*this, e, flip);
	}
	return loadTimersAndTargets(*this, cache, dir, race, flip) && loadCameras(*this, cache, dir, race, flip);
}

int16_t Track::animatedIn(std::string_view name, bool shared) const
{
	for (size_t i = shared ? sharedAnimated : 0; i < (shared ? animated.size() : sharedAnimated); i++) {
		if (samePrefix(animated[i].name, name)) {
			return int16_t(i);
		}
	}
	return -1;
}

int32_t Track::modelOf(std::string_view name) const
{
	for (size_t i = 0; i < models.size(); i++) {
		if (samePrefix(models[i].first, name)) {
			return int32_t(i);
		}
	}
	return -1;
}

int16_t Track::animatedOf(std::string_view name) const
{
	for (size_t i = 0; i < animated.size(); i++) {
		if (samePrefix(animated[i].name, name)) {
			return int16_t(i);
		}
	}
	return -1;
}

const Pose* Track::modelPlace(std::string_view name) const
{
	for (const auto& [n, pose] : models) {
		if (samePrefix(n, name)) {
			return &pose;
		}
	}
	return nullptr;
}

namespace {

} // namespace

// An animation's frames and tracks, mirrored like the scene (positions' y negated, rotations' y and w).
Animation animationOf(const AnimationFile& file, bool mirrored)
{
	const float flip = mirrored ? -1.0f : 1.0f;
	Animation a;
	for (const AnimationFile::Part& p : file.parts) {
		a.parts.push_back({p.frames, p.loopFrames, float(p.rate) / 1000, p.track, {{p.bounds[0], p.bounds[1] * flip, p.bounds[2]}, p.bounds[3]}});
	}
	for (const std::array<float, 3>& v : file.positions) {
		a.positions.push_back({v[0], v[1] * flip, v[2]});
	}
	for (const std::array<float, 4>& q : file.rotations) {
		a.rotations.push_back({q[0], q[1] * flip, q[2], q[3] * flip});
	}
	a.keys = file.keys;
	for (const AnimationFile::Track& t : file.tracks) {
		a.tracks.push_back({t.rotationFrame, t.rotationKey, t.rotationKeys, t.positionFrame, t.positionKey, t.positionKeys});
	}
	return a;
}

namespace {

// An entity's levels of detail's draw distances, squared: -1 for a level the file doesn't give; one
// not below FLT_MAX stays FLT_MAX ("always").
std::array<float, 3> squaredDistances(const SceneFile::Entity& e)
{
	std::array<float, 3> out = {-1, -1, -1};
	for (size_t l = 0; l < 3 && l < e.distances.size(); l++) {
		const float d = e.distances[l];
		out[l] = d >= std::numeric_limits<float>::max() ? std::numeric_limits<float>::max() : d * d;
	}
	if (e.distances.empty()) {
		out[0] = e.drawDistance * e.drawDistance;
	}
	return out;
}

// An animated entity of a scene: its pose, size, skeleton and animation (added to the track's once per name).
bool animatedModel(const Cache& cache, const std::string& dir, const SceneFile& scene, const SceneFile::Entity& e, bool mirrored, std::vector<Animation>& animations,
	std::vector<std::string>& animationNames, AnimatedModel& out)
{
	const float flip = mirrored ? -1.0f : 1.0f;
	out.name = e.name;
	out.pose = placed(e, mirrored);
	out.distances = squaredDistances(e);
	float modelScale = 1;
	if (e.model >= 0 && size_t(e.model) < scene.models.size()) {
		Model model;
		if (!cache.get(named(cache, dir, binaryName(scene.models[size_t(e.model)]) + ".GDB"), model)) {
			return false;
		}
		modelScale = model.scale;
		out.radius = modelBounds(model).radius;
		out.model = scene.models[size_t(e.model)];
		out.bounds = modelBounds(model);
		out.bounds.centre = out.bounds.centre * (1 / modelScale);
		out.bounds.radius /= modelScale;
		if (mirrored) {
			out.bounds.centre.y = -out.bounds.centre.y;
		}
	}
	out.scale = modelScale * e.scale;
	if (e.skeleton >= 0 && size_t(e.skeleton) < scene.skeletons.size()) {
		SkeletonFile skeleton;
		if (!cache.get(named(cache, dir, binaryName(scene.skeletons[size_t(e.skeleton)]) + ".SDB"), skeleton)) {
			return false;
		}
		for (const SkeletonFile::Node& n : skeleton.nodes) {
			int16_t parent = -1;
			for (size_t k = 0; k < skeleton.nodes.size(); k++) {
				parent = !n.parent.empty() && skeleton.nodes[k].name == n.parent ? int16_t(k) : parent;
			}
			Basis rows = basisOf({n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]});
			if (mirrored) {
				rows = basisFromForwardUp({rows.f.x, -rows.f.y, rows.f.z}, {rows.u.x, -rows.u.y, rows.u.z});
			}
			out.nodes.push_back({{n.position[0], n.position[1] * flip, n.position[2]}, rows, parent});
		}
	}
	if (e.animation >= 0 && size_t(e.animation) < scene.animations.size()) {
		const std::string& name = scene.animations[size_t(e.animation)];
		const auto known = std::find(animationNames.begin(), animationNames.end(), name);
		out.animation = int16_t(known - animationNames.begin());
		AnimationFile file;
		if (!cache.get(named(cache, dir, binaryName(name) + ".ADB"), file)) {
			return false;
		}
		if (known == animationNames.end()) {
			animationNames.push_back(name);
			animations.push_back(animationOf(file, mirrored));
		}
		out.part = int16_t(e.part);
		for (size_t k = 0; k < file.parts.size() && !e.partName.empty(); k++) {
			out.part = file.parts[k].name == e.partName ? int16_t(k) : out.part;
		}
		out.part = size_t(out.part) < file.parts.size() ? out.part : -1;
	}
	return true;
}

} // namespace

Sprite Sprite::of(const SceneFile::Sprite& s, bool mirrored)
{
	const float flip = mirrored ? -1.0f : 1.0f;
	Sprite out;
	out.position = {s.position[0], s.position[1] * flip, s.position[2]};
	out.axis = {s.axis[0], s.axis[1] * flip, s.axis[2]};
	out.width = s.width;
	out.height = s.height;
	out.reach = s.distance * s.distance;
	out.material = s.material;
	return out;
}

std::array<Vec3, 4> Sprite::corners(Vec3 right, Vec3 down, Vec3 look) const
{
	Vec3 side = right, up = down * -1.0f;
	if (lengthSquared(axis) > 0) {
		const Vec3 along = normalize(axis);
		const Vec3 across = cross(look, along);
		up = along;
		// (Looking along the axis the camera's own side stands in.)
		side = lengthSquared(across) > 1e-6f ? normalize(across) : right;
	}
	const Vec3 x = side * (width * 0.5f), y = up * (height * 0.5f);
	return {position - x + y, position + x + y, position + x - y, position - x - y};
}

namespace {

// A scene's material animation files' tracks, added to the track's; the first of each file's, then the end.
std::vector<int32_t> loadMaterialTracks(Track& t, const Cache& cache, const std::string& dir, const SceneFile& scene)
{
	std::vector<int32_t> firstTrack;
	for (const std::string& file : scene.materialAnimations) {
		MaterialAnimationFile mab;
		firstTrack.push_back(int32_t(t.materialTracks.size()));
		if (!cache.get(named(cache, dir, binaryName(file) + ".MAB"), mab)) {
			continue;
		}
		for (const MaterialAnimationFile::Track& track : mab.tracks) {
			Track::MaterialTrack& out = t.materialTracks.emplace_back();
			for (int32_t f = track.firstFrame; f < track.firstFrame + track.frames && f >= 0 && size_t(f) < mab.frames.size(); f++) {
				out.frames.push_back({mab.frames[size_t(f)].material, mab.frames[size_t(f)].start});
			}
			out.duration = std::max(1, track.duration);
			out.rate = float(track.rate) / 1000;
		}
	}
	firstTrack.push_back(int32_t(t.materialTracks.size()));
	return firstTrack;
}

// An entity's material animation bindings (a file index out of range skipped) and its texture scroll.
void bindMaterials(Track& t, const SceneFile& scene, const std::vector<int32_t>& firstTrack, const SceneFile::Entity& e, bool isAnimated, int32_t index)
{
	for (const std::array<int32_t, 4>& b : e.materialBindings) {
		// (A file index out of range is skipped.)
		if (b[0] < 0 || size_t(b[0]) >= scene.materialAnimations.size()) {
			continue;
		}
		const int32_t track = firstTrack[size_t(b[0])] + b[1];
		if (b[1] >= 0 && track < firstTrack[size_t(b[0]) + 1]) {
			t.materialBindings.push_back({isAnimated, index, track, b[2], b[3]});
		}
	}
	if (e.scroll[0] != 0 || e.scroll[1] != 0) {
		t.scrolls.push_back({isAnimated, index, int32_t(e.scroll[0] * 65.536f), int32_t(e.scroll[1] * 65.536f)});
	}
}

// One of the race's scenes (the track's, or the shared database): its material tracks, sprites, plain models, the intro's
// camera and the animated models; the shared one's also the sphinx's blow-up.
bool loadScene(Track& t, const Cache& cache, const std::string& dir, const RaceFile& race, const std::string* name, std::vector<std::string>& animationNames, bool& sharedSeen)
{
	if (name->empty()) {
		return true;
	}
	const std::string path = dir + binaryName(*name);
	SceneFile scene;
	if (!cache.size(path)) {
		return true; // (an empty shared database)
	}
	if (!cache.get(path, scene)) {
		return false;
	}
	const std::vector<int32_t> firstTrack = loadMaterialTracks(t, cache, dir, scene);
	for (const SceneFile::Sprite& s : scene.sprites) {
		Sprite& out = t.sprites.emplace_back(Sprite::of(s, t.mirrored));
		if (s.animationFile >= 0 && size_t(s.animationFile) < scene.materialAnimations.size()) {
			const int32_t track = firstTrack[size_t(s.animationFile)] + s.animationTrack;
			out.track = s.animationTrack >= 0 && track < firstTrack[size_t(s.animationFile) + 1] ? track : -1;
		}
	}
	for (const SceneFile::Entity& e : scene.entities) {
		bindMaterials(t, scene, firstTrack, e, false, int32_t(t.models.size()));
		t.models.emplace_back(e.name, placed(e, t.mirrored));
		t.modelDistances.push_back(squaredDistances(e));
	}
	if (name == &race.sharedScene) {
		t.sharedAnimated = t.animated.size();
		sharedSeen = true;
	}
	// The intro's camera: the scene camera of the race file's name, following a node of an animated entity.
	for (const SceneFile::Camera& c : scene.cameras) {
		if (!race.introCamera.empty() && binaryName(c.name) == binaryName(race.introCamera) && c.entity >= 0 && t.introEntity < 0) {
			t.introEntity = int16_t(t.animated.size() + size_t(c.entity));
			t.introNode = int16_t(c.node);
		}
	}
	for (const SceneFile::Entity& e : scene.animated) {
		bindMaterials(t, scene, firstTrack, e, true, int32_t(t.animated.size()));
		if (!animatedModel(cache, dir, scene, e, t.mirrored, t.animations, animationNames, t.animated.emplace_back())) {
			return false;
		}
	}
	if (name == &race.sharedScene) {
		// A sphinx's blow-up plays the material animation named "blowup".
		for (size_t i = 0; i < scene.materialAnimations.size(); i++) {
			MaterialAnimationFile file;
			if (samePrefix(scene.materialAnimations[i], "blowup") && cache.get(named(cache, dir, binaryName(scene.materialAnimations[i]) + ".MAB"), file) &&
				file.tracks.size() > 2) {
				t.blowupDuration = file.tracks[2].duration;
				t.blowupRate = file.tracks[2].rate;
			}
		}
	}
	return true;
}

bool loadPowerupModels(Track& t, const Cache& cache, const std::string& dir, const RaceFile& race)
{
	// The power-ups' models: the grapple (the first plain model), the dynamite stick, the homing missile. (Their files
	// are beside the power-ups' database: a track's own "barrel" is scenery.)
	const std::string pdir = cache.has(dir + binaryName(race.powerupScene)) ? dir : CommonFolder;
	SceneFile powerups;
	if (!cache.get(named(cache, pdir, binaryName(race.powerupScene)), powerups)) {
		return false;
	}
	auto bounds = [&](const SceneFile::Entity& e, Sphere& out) {
		Model model;
		if (e.model < 0 || size_t(e.model) >= powerups.models.size() || !cache.get(named(cache, pdir, binaryName(powerups.models[size_t(e.model)]) + ".GDB"), model)) {
			return false;
		}
		out = modelBounds(model);
		return true;
	};
	const auto stickEntity = std::find_if(powerups.entities.begin(), powerups.entities.end(), [](const auto& e) { return e.name == "barrel"; });
	const auto missileEntity = std::find_if(powerups.animated.begin(), powerups.animated.end(), [](const auto& e) { return e.name == "dmissil"; });
	std::vector<Animation> missileAnimation;
	std::vector<std::string> missileNames;
	if (powerups.entities.empty() || stickEntity == powerups.entities.end() || missileEntity == powerups.animated.end() || !bounds(powerups.entities[0], t.hook) ||
		!bounds(*stickEntity, t.stick) || !animatedModel(cache, pdir, powerups, *missileEntity, false, missileAnimation, missileNames, t.missile) || missileAnimation.empty()) {
		return false;
	}
	// (Its sphere is its model's bounds.) Its animation goes after the track's.
	t.missile.animation = int16_t(t.animations.size());
	t.animations.push_back(missileAnimation[0]);
	// The bricks' entities, their animations after it.
	static const char* const BrickNames[4] = {"gen", "genblen", "enh", "enhblen"};
	for (size_t b = 0; b < 4; b++) {
		const auto entity = std::find_if(powerups.animated.begin(), powerups.animated.end(), [&](const auto& e) { return e.name == BrickNames[b]; });
		std::vector<Animation> brickAnimation;
		std::vector<std::string> brickNames;
		AnimatedModel& m = t.brickModels[b];
		if (entity == powerups.animated.end() || !animatedModel(cache, pdir, powerups, *entity, false, brickAnimation, brickNames, m)) {
			return false;
		}
		m.animation = brickAnimation.empty() ? int16_t(-1) : int16_t(t.animations.size());
		m.part = brickAnimation.empty() ? int16_t(-1) : int16_t(0); // (part 0, looping from load)
		if (!brickAnimation.empty()) {
			t.animations.push_back(brickAnimation[0]);
		}
	}
	return true;
}

// The hazards (mirrored as the track).
bool loadHazards(Track& t, const Cache& cache, const std::string& dir, const RaceFile& race)
{
	HazardFile file;
	if (!race.hazardFile.empty() && !cache.get(dir + binaryName(race.hazardFile), file)) {
		return false;
	}
	const float flip = t.mirrored ? -1.0f : 1.0f;
	for (const HazardFile::Hazard& h : file.hazards) {
		HazardSpec& out = t.hazards.emplace_back();
		out.type = h.type;
		out.trigger = h.trigger;
		out.model = h.entity.empty() ? -1 : t.animatedOf(h.entity);
		for (size_t i = 0; i < h.entities.size() && i < 4; i++) {
			out.models[i] = t.animatedOf(h.entities[i]);
		}
		out.surface = h.surface.empty() ? -1 : t.surfaceOf(h.surface);
		out.source = flipped(h.source, flip);
		out.target = flipped(h.target, flip);
		out.triggerPoint = flipped(h.triggerPoint, flip);
		out.triggerRadius = h.triggerRadius;
		for (const auto* list : {&h.launches, &h.targets}) {
			auto& into = list == &h.launches ? out.launches : out.targets;
			for (const std::array<float, 4>& p : *list) {
				into.push_back({p[0], p[1] * flip, p[2], p[3]});
			}
		}
		out.startFrame = h.startFrame;
		out.size = vec(h.size);
		out.periodMs = h.periodMs;
		out.intervalMs = h.intervalMs;
		out.sweep = vec(h.sweep);
		out.strength = h.strength;
		out.low = h.low;
		out.high = h.high;
	}
	return true;
}

} // namespace

bool Track::loadScenery(const Cache& cache, const std::string& dir, const RaceFile& race)
{
	std::vector<std::string> animationNames;
	bool sharedSeen = false;
	for (const std::string* name : {&race.scene, &race.sharedScene}) {
		if (!loadScene(*this, cache, dir, race, name, animationNames, sharedSeen)) {
			return false;
		}
	}
	if (!sharedSeen) {
		sharedAnimated = animated.size();
	}
	return loadPowerupModels(*this, cache, dir, race) && loadHazards(*this, cache, dir, race);
}

bool Track::loadPowerups(const Cache& cache, const std::string& dir, const RaceFile& race, bool timeRace)
{
	PowerupFile file;
	std::string name = binaryName(race.powerupFile);
	if (timeRace) {
		name.insert(name.rfind('.'), "2");
	}
	if (!cache.get(dir + name, file)) {
		return false;
	}
	const float flip = mirrored ? -1.0f : 1.0f;
	for (const PowerupFile::Brick& b : file.colourBricks) {
		bricks.push_back({{b.position[0], b.position[1] * flip, b.position[2]}, b.colour, b.respawnMs});
	}
	for (const std::array<float, 3>& p : file.whiteBricks) {
		bricks.push_back({{p[0], p[1] * flip, p[2]}, 0, 0});
	}
	return true;
}

namespace {

struct Query {
	const CollisionMesh& mesh;
	Vec3 a, b, ab;
	SegmentHit& hit;

	bool visit(int32_t index) const
	{
		const CollisionMesh::Node& node = mesh.nodes[index];
		const float sa = dot(node.normal, a - node.point);
		const int32_t nearChild = node.child[sa > 0 ? 0 : 1];
		if (nearChild >= 0 && visit(nearChild)) {
			return true;
		}
		const float sb = dot(node.normal, b - node.point);
		if (!((sa > 0 && sb <= 0) || (sa <= 0 && sb > 0))) {
			return false; // not crossing this plane: nothing beyond it either
		}
		const float da = std::fabs(sa), db = std::fabs(sb);
		const float f = da / (da + db);
		const Vec3 x = a + ab * f;
		const CollisionMesh::Triangle* found = nullptr;
		Vec3 m;
		for (uint32_t i = node.first; i < node.first + node.count; i++) {
			const CollisionMesh::Triangle& t = mesh.triangles[i];
			if (!insideTriangle(t.v[0], t.v[1], t.v[2], node.normal, x)) {
				continue;
			}
			const Vec3 tm = cross(t.v[2] - t.v[1], t.v[0] - t.v[1]);
			found = &t;
			m = tm;
			if (dot(tm, ab) <= 0) {
				break; // facing the segment: taken at once; otherwise the last back face is kept
			}
		}
		if (found) {
			hit.point = x;
			hit.normal = normalize(m);
			const Vec3 v0 = found->v[0];
			hit.offset = -dotZYX(v0, hit.normal);
			hit.fraction = f;
			hit.corners[0] = found->v[0];
			hit.corners[1] = found->v[1];
			hit.corners[2] = found->v[2];
			hit.surface = found->surface;
			hit.checkpoint = found->checkpoint;
			return true;
		}
		const int32_t farChild = node.child[sb <= 0 ? 1 : 0];
		return farChild >= 0 && visit(farChild);
	}
};

} // namespace

bool Track::query(uint32_t volume, Vec3 a, Vec3 b, SegmentHit& hit) const
{
	const CollisionMesh& mesh = meshes[volumes[volume].mesh];
	return !mesh.nodes.empty() && Query{mesh, a, b, b - a, hit}.visit(0);
}

} // namespace lr
