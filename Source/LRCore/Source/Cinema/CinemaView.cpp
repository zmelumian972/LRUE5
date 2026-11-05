// The cinematics' and the setup previews' 3D: each cue's entity or camera found in the scene's worlds; a cue's begin
// and end placing an entity, starting or
// stopping its part, pushing a camera, setting the lights; each update's ticks moving the entities' clocks and the
// tracking cameras; what the scene shows (the camera at the bottom of the stack, the light cues, the model cues'
// entities, guy1 and the carbody swapped for the player's minifig and car). The presentation draws it as given.

#include "LRCore/Menus/Menus.h"
#include "Common/Assert.h"
#include "LRData/Archive.h"

#include <algorithm>
#include <cctype>

namespace lr {

namespace {

int32_t blinkMs(int32_t frames) { return int32_t(float(frames) * 33.333328f); }

// A part's clock moved on by ms (a looping part wraps.2's clock).
void advance(CinemaPlayer::Clock& c, const Animation& a, int32_t ms)
{
	if (!c.on || c.part < 0 || size_t(c.part) >= a.parts.size()) {
		return;
	}
	const Animation::Part& p = a.parts[size_t(c.part)];
	c.time += p.rate * float(ms);
	if (c.time >= float(p.frames - 1) && p.loopFrames > 0) {
		const float loop = float(p.loopFrames);
		c.time -= loop * float(int32_t(c.time / loop));
	}
}

// The face expression a material track's frame names.
uint8_t expressionOf(std::string_view material)
{
	static const char* const Names[6] = {"face", "angry", "blink", "happy", "sad", "suprz"};
	for (uint8_t i = 1; i < 6; i++) {
		if (sameName(material, Names[i])) {
			return i;
		}
	}
	return 0;
}

// A model cue's material tracks stepped by ms, in tuple order: each track's own cursor moves
// on (a track bound again goes on from where it stopped) and writes its frame's material into the slot of the
// entity's model's shared table (table 0: the entity's model; the data has no other), so the last tuple's wins.
void stepTracks(CinemaPlayer& c, const MenuData::Screen& s, const CinemaFile::Cue& q, const std::array<int16_t, 3>& e, int32_t ms)
{
	const MenuData::CinemaWorld& w = s.cinemaWorlds[size_t(e[0])];
	const SceneFile::Entity& ent = e[1] ? w.scene.animated[size_t(e[2])] : w.scene.entities[size_t(e[2])];
	for (const std::array<int32_t, 5>& t : q.tracks) {
		if (t[0] < 0 || size_t(t[0]) >= s.cinemaWorlds.size() || t[4] != 0 || ent.model < 0) {
			continue;
		}
		const MenuData::CinemaWorld& tw = s.cinemaWorlds[size_t(t[0])];
		if (t[1] < 0 || size_t(t[1]) >= tw.materialAnimations.size() || t[2] < 0 || size_t(t[2]) >= tw.materialAnimations[size_t(t[1])].tracks.size()) {
			continue;
		}
		const MaterialAnimationFile& m = tw.materialAnimations[size_t(t[1])];
		const MaterialAnimationFile::Track& tr = m.tracks[size_t(t[2])];
		const std::array<int16_t, 3> id = {int16_t(t[0]), int16_t(t[1]), int16_t(t[2])};
		auto cursor = std::find_if(c.cursors.begin(), c.cursors.end(), [&](const CinemaPlayer::Cursor& k) { return k.track == id; });
		if (cursor == c.cursors.end()) {
			cursor = c.cursors.insert(c.cursors.end(), {id, 0});
		}
		cursor->ms += ms;
		float at = float(tr.rate) / 1000.0f * float(cursor->ms);
		if (tr.duration > 0) {
			at -= float(tr.duration) * float(int32_t(at / float(tr.duration)));
		}
		const std::string* material = nullptr;
		for (int32_t f = tr.firstFrame; f < tr.firstFrame + tr.frames && f >= 0 && size_t(f) < m.frames.size(); f++) {
			if (float(m.frames[size_t(f)].start) <= at) {
				material = &m.frames[size_t(f)].material;
			}
		}
		if (!material) {
			continue;
		}
		const std::array<int16_t, 3> slot = {e[0], int16_t(ent.model), int16_t(t[3])};
		auto written = std::find_if(c.written.begin(), c.written.end(), [&](const CinemaPlayer::Written& k) { return k.slot == slot; });
		if (written == c.written.end()) {
			written = c.written.insert(c.written.end(), {slot, {}});
		}
		written->material = *material;
	}
}

// A camera cue's view: the tracked node's pose at its entity's time, through the entity's placement (no model scale).
void trackCamera(CinemaPlayer& c, const MenuData::Screen& s, const std::array<int16_t, 3>& e)
{
	LR_ASSERT(size_t(e[0]) < s.cinemaWorlds.size() && e[2] >= 0 && size_t(e[2]) < s.cinemaWorlds[size_t(e[0])].scene.cameras.size());
	const MenuData::CinemaWorld& w = s.cinemaWorlds[size_t(e[0])];
	const SceneFile::Camera& cam = w.scene.cameras[size_t(e[2])];
	if (cam.entity < 0 || cam.node < 0 || size_t(cam.entity) >= w.scene.animated.size()) {
		return;
	}
	const SceneFile::Entity& dummy = w.scene.animated[size_t(cam.entity)];
	if (dummy.skeleton < 0 || size_t(dummy.skeleton) >= w.skeletons.size()) {
		return;
	}
	const CinemaPlayer::Clock& clock = c.clocks[size_t(e[0])][size_t(cam.entity)];
	const Animation* a = dummy.animation >= 0 && size_t(dummy.animation) < w.animations.size() ? &w.animations[size_t(dummy.animation)] : nullptr;
	const CinemaPlayer::Placed& at = c.placed[size_t(e[0])][1][size_t(cam.entity)];
	std::vector<NodeMatrix> nodes;
	poseSkeleton(w.skeletons[size_t(dummy.skeleton)], a, {a ? clock.part : -1, clock.part >= 0 ? clock.time : -1, true}, Pose{at.position, at.rows}, 1, nodes);
	if (size_t(cam.node) < nodes.size()) {
		CinemaPlayer::View& v = c.views[size_t(e[0])][size_t(e[2])];
		v.eye = nodes[size_t(cam.node)].position;
		v.forward = nodes[size_t(cam.node)].rows.f;
		v.up = nodes[size_t(cam.node)].rows.u; // (the third row)
	}
}

// A blinking ambient or light cue: on and off by its blink's frames (at most 7 lights on, the oldest let go).
void blinkCue(CinemaPlayer& c, const CinemaFile::Cue& q, uint16_t cue, int32_t ms)
{
	std::pair<bool, int32_t>& b = c.blinks[cue];
	if (ms <= b.second) {
		b.second -= ms;
		return;
	}
	const bool ambient = q.kind == CinemaFile::Cue::Ambient;
	if (b.first) {
		if (ambient && c.ambient == int32_t(cue)) {
			c.ambient = -1;
		}
		if (!ambient) {
			c.lights.erase(std::remove(c.lights.begin(), c.lights.end(), cue), c.lights.end());
		}
		b = {false, blinkMs(q.blink[1])};
	}
	else {
		if (ambient) {
			c.ambient = int32_t(cue);
		}
		else if (std::find(c.lights.begin(), c.lights.end(), cue) == c.lights.end()) {
			if (c.lights.size() >= 7) {
				c.lights.erase(c.lights.begin());
			}
			c.lights.push_back(cue);
		}
		b = {true, blinkMs(q.blink[0])};
	}
}

} // namespace

void Menus::cinemaStart3d(CinemaPlayer& c, const MenuData::Screen& s, size_t sceneIndex) const
{
	const CinemaFile::Scene& scene = s.cinema.scenes[sceneIndex];
	c.scene = sceneIndex;
	const size_t n = scene.cues.size();
	c.byStart.resize(n);
	for (size_t i = 0; i < n; i++) {
		c.byStart[i] = uint16_t(i);
	}
	c.byEnd = c.byStart;
	std::stable_sort(c.byStart.begin(), c.byStart.end(), [&](uint16_t a, uint16_t b) { return scene.cues[a].start < scene.cues[b].start; });
	std::stable_sort(c.byEnd.begin(), c.byEnd.end(), [&](uint16_t a, uint16_t b) { return scene.cues[a].end < scene.cues[b].end; });
	c.active.assign(n, 0);
	c.begun.clear();
	c.starts = c.ends = 0;
	c.scaled = 0;
	c.frame = 0;
	c.entityOf.assign(n, {-1, -1, -1});
	c.cueMs.assign(n, 0);
	c.blinks.assign(n, {true, 0});
	const std::vector<MenuData::CinemaWorld>& worlds = s.cinemaWorlds;
	// Each cue's camera (world, 2, camera) or model entity (world, animated, index): the first match in world order.
	for (size_t i = 0; i < n; i++) {
		const CinemaFile::Cue& q = scene.cues[i];
		for (size_t w = 0; w < worlds.size() && c.entityOf[i][0] < 0; w++) {
			const SceneFile& f = worlds[w].scene;
			if (q.kind == CinemaFile::Cue::Camera) {
				for (size_t k = 0; k < f.cameras.size(); k++) {
					if (sameName(f.cameras[k].name, q.target)) {
						c.entityOf[i] = {int16_t(w), 2, int16_t(k)};
						break;
					}
				}
			}
			else if (q.kind == CinemaFile::Cue::Model && (q.entityKind == 0x2f || q.entityKind == 0x30)) {
				const std::vector<SceneFile::Entity>& list = q.entityKind == 0x30 ? f.animated : f.entities;
				for (size_t k = 0; k < list.size(); k++) {
					if (sameName(list[k].name, q.target)) {
						c.entityOf[i] = {int16_t(w), int16_t(q.entityKind == 0x30), int16_t(k)};
						break;
					}
				}
			}
		}
	}
	if (c.placed.size() == worlds.size()) {
		return; // (a preview switching scenes keeps its worlds' state)
	}
	// The worlds as loaded: entities where they stand, animated ones at rest, cameras as given (a camera's part starts
	// on its tracked entity).
	c.placed.assign(worlds.size(), {});
	c.clocks.assign(worlds.size(), {});
	c.views.assign(worlds.size(), {});
	c.cameras.clear();
	c.ambient = -1;
	c.lights.clear();
	c.viewed = false;
	c.cursors.clear();
	c.written.clear();
	c.colourings.clear();
	for (size_t w = 0; w < worlds.size(); w++) {
		const SceneFile& f = worlds[w].scene;
		for (size_t list = 0; list < 2; list++) {
			for (const SceneFile::Entity& e : list ? f.animated : f.entities) {
				c.placed[w][list].push_back({vec(e.position), basisFromForwardUp(vec(e.forward), vec(e.up))});
			}
		}
		c.clocks[w].assign(f.animated.size(), {});
		for (const SceneFile::Camera& cam : f.cameras) {
			CinemaPlayer::View& v = c.views[w].emplace_back();
			v.eye = vec(cam.position);
			v.forward = normalize(vec(cam.forward));
			v.up = normalize(vec(cam.up));
			v.fov = cam.fov, v.nearPlane = cam.nearPlane, v.farPlane = cam.farPlane;
			if (cam.part >= 0 && cam.entity >= 0 && size_t(cam.entity) < c.clocks[w].size()) {
				c.clocks[w][size_t(cam.entity)] = {cam.part, 0, true};
			}
		}
	}
}

void Menus::cue3d(CinemaPlayer& c, const MenuData::Screen& s, size_t cue, bool begins) const
{
	const CinemaFile::Cue& q = s.cinema.scenes[c.scene].cues[cue];
	const std::array<int16_t, 3> e = c.entityOf[cue];
	switch (q.kind) {
	case CinemaFile::Cue::Camera: {
		if (e[0] < 0) {
			return;
		}
		const std::array<int16_t, 2> cam = {e[0], e[2]};
		const auto at = std::find(c.cameras.begin(), c.cameras.end(), cam);
		if (!begins) {
			if (at != c.cameras.end()) {
				c.cameras.erase(at);
			}
			return;
		}
		if (at == c.cameras.end() && c.cameras.size() < 8) {
			c.cameras.push_back(cam);
		}
		const SceneFile::Camera& camera = s.cinemaWorlds[size_t(e[0])].scene.cameras[size_t(e[2])];
		if (q.part >= 0 && camera.entity >= 0 && size_t(camera.entity) < c.clocks[size_t(e[0])].size()) {
			c.clocks[size_t(e[0])][size_t(camera.entity)] = {q.part, 0, true};
		}
		return;
	}
	case CinemaFile::Cue::Model:
		if (e[0] < 0) {
			return;
		}
		if (begins) {
			c.placed[size_t(e[0])][size_t(e[1])][size_t(e[2])] = {vec(q.position), basisFromForwardUp(vec(q.forward), vec(q.up))};
			c.cueMs[cue] = 0;
			if (e[1] && q.part >= 0) {
				c.clocks[size_t(e[0])][size_t(e[2])] = {q.part, 0, true};
			}
		}
		else if (e[1] && q.part >= 0) {
			c.clocks[size_t(e[0])][size_t(e[2])].on = false; // (the pose freezes)
		}
		return;
	case CinemaFile::Cue::Ambient:
		if (begins) {
			c.ambient = int32_t(cue);
			c.blinks[cue] = {true, blinkMs(q.blink[0])};
		}
		else if (c.ambient == int32_t(cue)) {
			c.ambient = -1;
		}
		return;
	case CinemaFile::Cue::Light: {
		const auto at = std::find(c.lights.begin(), c.lights.end(), uint16_t(cue));
		if (begins) {
			if (at == c.lights.end()) {
				if (c.lights.size() >= 7) {
					c.lights.erase(c.lights.begin());
				}
				c.lights.push_back(uint16_t(cue));
			}
			c.blinks[cue] = {true, blinkMs(q.blink[0])};
		}
		else if (at != c.lights.end()) {
			c.lights.erase(at);
		}
		return;
	}
	default: return;
	}
}

void Menus::cueTicks(CinemaPlayer& c, const MenuData::Screen& s, int32_t ms) const
{
	const CinemaFile::Scene& scene = s.cinema.scenes[c.scene];
	const std::vector<uint16_t> order = c.begun; // (newest first; a blink may change the lights, never this list)
	for (const uint16_t cue : order) {
		const CinemaFile::Cue& q = scene.cues[cue];
		const std::array<int16_t, 3> e = c.entityOf[cue];
		switch (q.kind) {
		case CinemaFile::Cue::Camera:
			if (e[0] >= 0) {
				trackCamera(c, s, e);
			}
			break;
		case CinemaFile::Cue::Model:
			c.cueMs[cue] += ms;
			if (e[0] >= 0 && e[1]) {
				const MenuData::CinemaWorld& w = s.cinemaWorlds[size_t(e[0])];
				const SceneFile::Entity& ent = w.scene.animated[size_t(e[2])];
				if (ent.animation >= 0 && size_t(ent.animation) < w.animations.size()) {
					advance(c.clocks[size_t(e[0])][size_t(e[2])], w.animations[size_t(ent.animation)], ms);
				}
			}
			if (e[0] >= 0) {
				stepTracks(c, s, q, e, ms);
			}
			break;
		case CinemaFile::Cue::Ambient:
		case CinemaFile::Cue::Light:
			if (q.blink[0] != 0 || q.blink[1] != 0) {
				blinkCue(c, q, cue, ms);
			}
			break;
		default: break;
		}
	}
	if (!c.cameras.empty()) {
		c.view = c.views[size_t(c.cameras[0][0])][size_t(c.cameras[0][1])];
		c.viewed = true;
	}
}

// The cues in the window [a, b] in time order; links fire only on the cinematic screens.
namespace {
template <class Change> void window(CinemaPlayer& c, const CinemaFile::Scene& scene, int32_t a, int32_t b, Change&& change)
{
	const size_t n = scene.cues.size();
	for (;;) {
		bool stop = false;
		for (; c.starts < n; c.starts++) {
			const CinemaFile::Cue& cue = scene.cues[c.byStart[c.starts]];
			if (c.ends < n && cue.start > scene.cues[c.byEnd[c.ends]].end) {
				break;
			}
			if (cue.start < a || cue.start > b) {
				stop = true;
				break;
			}
			if (!c.active[c.byStart[c.starts]]) {
				change(c.byStart[c.starts], true);
			}
		}
		if (stop || c.ends >= n) {
			break;
		}
		const uint16_t e = c.byEnd[c.ends];
		if (scene.cues[e].end < a || scene.cues[e].end > b) {
			break;
		}
		if (c.active[e]) {
			change(e, false);
		}
		c.ends++;
	}
}
} // namespace

void Menus::playScene(CinemaPlayer& c, const MenuData::Screen& s, int32_t ms, int32_t ticks, bool loop, bool links)
{
	const CinemaFile::Scene& scene = s.cinema.scenes[c.scene];
	const auto change = [&](size_t cue, bool begins) {
		if (links) {
			cueChanged(cue, begins);
			return;
		}
		cue3d(c, s, cue, begins);
		c.active[cue] = begins;
		if (begins) {
			c.begun.insert(c.begun.begin(), uint16_t(cue));
		}
		else {
			c.begun.erase(std::remove(c.begun.begin(), c.begun.end(), uint16_t(cue)), c.begun.end());
		}
	};
	uint32_t scaled = c.scaled + uint32_t(ms * scene.rate);
	int32_t to = int32_t(scaled / 1000);
	if (to >= scene.frames) {
		window(c, scene, c.frame, to, change);
		if (!loop) {
			return;
		}
		// Rewind: every active cue ends; the clock and the cursors go back to the start.
		for (const uint16_t cue : std::vector<uint16_t>(c.begun)) {
			change(cue, false);
		}
		c.starts = c.ends = 0;
		c.frame = 0;
		scaled = 0, to = 0;
	}
	window(c, scene, c.frame, to, change);
	cueTicks(c, s, ticks);
	c.scaled = scaled;
	c.frame = to;
}

void Menus::cueWindow(CinemaPlayer& c, int32_t a, int32_t b)
{
	window(c, data->screens[screen].cinema.scenes[c.scene], a, b, [&](size_t cue, bool begins) { cueChanged(cue, begins); });
}

// The setup screens' preview: the pst scene looping on the frame's ms.
void Menus::updatePreview(int32_t dt)
{
	CinemaPlayer& c = state.cinema;
	const MenuData::Screen& s = data->screens[screen];
	if (state.scene < 0 || size_t(state.scene) >= s.cinema.scenes.size()) {
		return;
	}
	if (c.byStart.size() != s.cinema.scenes[size_t(state.scene)].cues.size() || c.scene != size_t(state.scene) || c.placed.empty()) {
		// (Switching: the old scene's active cues end first, its cameras and lights with them.)
		for (const uint16_t cue : std::vector<uint16_t>(c.placed.empty() ? std::vector<uint16_t>() : c.begun)) {
			cue3d(c, s, cue, false);
		}
		cinemaStart3d(c, s, size_t(state.scene));
	}
	playScene(c, s, dt, dt, true, false); // (the frame's ms from the first update on, as a cinematic's)
}

namespace {

// The camera at the bottom of the stack (the previous view while it is empty); the light cues: the ambient, the
// directional lights (none of either: the cleared device).
void sceneCameraAndLights(const CinemaPlayer& c, const CinemaFile::Scene& scene, MenuScene& out)
{
	out.camera = c.viewed;
	if (c.viewed) {
		const Vec3 t = c.view.eye + c.view.forward;
		out.eye = {c.view.eye.x, c.view.eye.y, c.view.eye.z};
		out.target = {t.x, t.y, t.z};
		out.up = {c.view.up.x, c.view.up.y, c.view.up.z};
		out.fov = c.view.fov, out.nearPlane = c.view.nearPlane, out.farPlane = c.view.farPlane;
	}
	out.lighting = {};
	if (c.ambient >= 0) {
		const std::array<int32_t, 3>& rgb = scene.cues[size_t(c.ambient)].colour;
		out.lighting.ambient = 0xff000000u | uint32_t(rgb[0] & 255) << 16 | uint32_t(rgb[1] & 255) << 8 | uint32_t(rgb[2] & 255);
	}
	for (const uint16_t l : c.lights) {
		const CinemaFile::Cue& q = scene.cues[l];
		out.lighting.lights.push_back(
			{normalize(vec(q.direction)), 0xff000000u | uint32_t(q.colour[0] & 255) << 16 | uint32_t(q.colour[1] & 255) << 8 | uint32_t(q.colour[2] & 255)});
	}
}

// A minifig's skeleton and part files: its entity's, in the screen's cinema folder.
void minifigFiles(const MenuData::Screen& s, const MenuData::CinemaWorld& w, const SceneFile::Entity& ent, MenuScene::Thing& t)
{
	const auto path = [&](const std::string& name, const char* extension) { return s.cinemaFolder + upper(name) + extension; };
	if (ent.skeleton >= 0 && size_t(ent.skeleton) < w.scene.skeletons.size()) {
		t.skeleton = path(w.scene.skeletons[size_t(ent.skeleton)], ".SDB");
	}
	if (ent.animation >= 0 && size_t(ent.animation) < w.scene.animations.size()) {
		t.partFile = path(w.scene.animations[size_t(ent.animation)], ".ADB");
	}
}

// An entity's model's shared material table and colour transform (every entity drawing the model shows them).
void entityMaterials(const CinemaPlayer& c, const MenuData::Screen& s, int16_t world, int32_t model, MenuScene::Thing& t)
{
	for (const CinemaPlayer::Written& k : c.written) {
		if (k.slot[0] == world && k.slot[1] == model) {
			t.materials.push_back({k.slot[2], k.material});
		}
	}
	for (const CinemaPlayer::Colouring& k : c.colourings) {
		if (k.model == std::array<int16_t, 2>{world, int16_t(model)}) {
			const CinemaFile::Effect& fx = s.cinema.effects[size_t(k.effect)];
			t.coloured = true;
			for (size_t ch = 0; ch < 3; ch++) {
				t.colourShift[ch] = fx.shift[ch];
				t.colourOffset[ch] = int32_t(k.offsets[ch]);
			}
		}
	}
}

} // namespace

void Menus::describeCinema(const CinemaPlayer& c, const MenuData::Screen& s, MenuScene& out, const std::array<uint8_t, 5>* looks, const RacerRecord* car) const
{
	using Thing = MenuScene::Thing;
	out.cinemaScreen = screen;
	out.lighting = {};
	if (s.cinema.scenes.empty() || c.entityOf.empty()) {
		out.camera = false;
		return;
	}
	const CinemaFile::Scene& scene = s.cinema.scenes[c.scene];
	out.world = scene.name;
	out.time = float(c.scaled) / 1000.0f;
	out.cues = c.begun;
	out.cinemaScreen = screen;
	sceneCameraAndLights(c, scene, out);
	// The model cues' entities, newest first.
	for (const uint16_t cue : c.begun) {
		const CinemaFile::Cue& q = scene.cues[cue];
		const std::array<int16_t, 3> e = c.entityOf[cue];
		if (q.kind != CinemaFile::Cue::Model || e[0] < 0) {
			continue;
		}
		const MenuData::CinemaWorld& w = s.cinemaWorlds[size_t(e[0])];
		const SceneFile::Entity& ent = e[1] ? w.scene.animated[size_t(e[2])] : w.scene.entities[size_t(e[2])];
		const CinemaPlayer::Placed& at = c.placed[size_t(e[0])][size_t(e[1])][size_t(e[2])];
		if (ent.model < 0 || size_t(ent.model) >= w.scene.models.size()) {
			continue; // (a camera's dummy: nothing to draw)
		}
		const float scale = ent.scale * (ent.model >= 0 && size_t(ent.model) < w.modelScales.size() ? w.modelScales[size_t(ent.model)] : 1.0f);
		const CinemaPlayer::Clock clock = e[1] ? c.clocks[size_t(e[0])][size_t(e[2])] : CinemaPlayer::Clock();
		// WINRRCAR's peg-leg swap models show only for leg piece 10.
		if (looks && screen == ScreenRocketCar && (sameName(ent.name, "swap") || sameName(ent.name, "pleg")) && (*looks)[2] != 10) {
			continue;
		}
		if (looks && e[1] && sameName(ent.name, "guy1")) {
			// The player's (or the mascot's) minifig on guy1's skeleton, playing guy1's part; its face by the cue's tracks.
			Thing& t = out.things.emplace_back();
			t.kind = Thing::Minifig;
			t.looks = *looks;
			t.body = bodyOf(*looks, false);
			minifigFiles(s, w, ent, t);
			t.animation = clock.part;
			t.frame = clock.time;
			t.world = {at.rows * scale, at.position};
			t.cue = cue, t.cueMs = c.cueMs[cue];
			// Its face: what the face tracks wrote into guy1's model's table (its first track's slot).
			for (const CinemaPlayer::Written& k : c.written) {
				if (!q.tracks.empty() && k.slot == std::array<int16_t, 3>{e[0], int16_t(ent.model), int16_t(q.tracks[0][3])}) {
					t.looks[4] = expressionOf(k.material);
				}
			}
			continue;
		}
		if (car && !car->pieces.empty() && !cinemaCar.vertices.empty() && sameName(ent.name, "carbody")) {
			// The player's (or the champion's) car in the carbody's place, its chassis model with it.
			Thing& t = out.things.emplace_back();
			t.kind = Thing::Car;
			t.model = "cinema";
			t.world = {at.rows, at.position};
			if (const ChassisTable::Chassis* ch = chassisOf(&car->pieces)) {
				Thing& wheels = out.things.emplace_back();
				wheels.kind = Thing::Chassis;
				wheels.model = ch->model;
				wheels.world = {at.rows * chassisScale(ch), at.position};
				wheels.frame = -1; // (its part animation off)
			}
			continue;
		}
		Thing& t = out.things.emplace_back();
		t.kind = Thing::Entity;
		t.entity = {e[0], e[1], e[2]};
		t.model = w.scene.models[size_t(ent.model)];
		t.animation = clock.part;
		t.frame = clock.time;
		t.world = {at.rows * scale, at.position};
		t.cue = cue, t.cueMs = c.cueMs[cue];
		entityMaterials(c, s, e[0], ent.model, t);
	}
}

// The menus' lighting: what the screen sets when it opens; a scene view's world replacing it each
// time it draws (not restored after); a ring's one light along the device's light 0, coloured per item.
void Menus::openLighting()
{
	uint32_t ambient = 0, light = 0;
	switch (screen) {
	case ScreenCarBuilder: ambient = 120, light = 255; break;
	case ScreenEditCar: ambient = 120, light = 120; break;
	case ScreenDriver: ambient = 128, light = 255; break;
	case ScreenLicense: ambient = 120, light = 180; break;
	case ScreenRacerSelect:
	case ScreenPlayer2Select:
	case ScreenGarage: ambient = 120, light = 255; break;
	default: return; // (the device keeps what it had)
	}
	const auto grey = [](uint32_t v) { return 0xff000000u | v << 16 | v << 8 | v; };
	lighting = {grey(ambient), {{normalize(Vec3{-1, -1, -1}), grey(light)}}};
}

void Menus::lightScene(MenuScene& scene, const Widget* ring)
{
	if (ring) {
		const Vec3 along = lighting.lights.empty() ? normalize(Vec3{-1, -1, -1}) : lighting.lights[0].first;
		scene.lighting = {0, {{along, 0xffffffff}}};
		for (const MenuScene::Thing& t : scene.things) {
			lighting = {t.primary | 0xff000000u, {{along, t.secondary | 0xff000000u}}};
		}
		if (scene.things.empty()) {
			lighting = scene.lighting;
		}
		return;
	}
	if (!scene.cues.empty() || scene.cinemaScreen) {
		lighting = scene.lighting; // (a cinematic's or a preview's light cues)
		return;
	}
	if (const MenuData::MenuWorld* w = data->menuWorld(scene.world); w && w->lit) {
		lighting = {w->ambient, w->lights};
	}
	scene.lighting = lighting;
}

// A cinematic entity's nodes (an animated one's skeleton at its clock) placed by its thing.
void Menus::entityNodes(const MenuScene& scene, const MenuScene::Thing& t, std::vector<NodeMatrix>& out) const
{
	out.clear();
	if (t.entity[0] < 0 || scene.cinemaScreen >= data->screens.size()) {
		return;
	}
	const MenuData::Screen& s = data->screens[scene.cinemaScreen];
	if (size_t(t.entity[0]) >= s.cinemaWorlds.size()) {
		return;
	}
	const MenuData::CinemaWorld& w = s.cinemaWorlds[size_t(t.entity[0])];
	const std::vector<SceneFile::Entity>& list = t.entity[1] ? w.scene.animated : w.scene.entities;
	if (size_t(t.entity[2]) >= list.size()) {
		return;
	}
	const SceneFile::Entity& ent = list[size_t(t.entity[2])];
	if (!t.entity[1] || ent.skeleton < 0 || size_t(ent.skeleton) >= w.skeletons.size()) {
		out.push_back(t.world);
		return;
	}
	const Animation* a = ent.animation >= 0 && size_t(ent.animation) < w.animations.size() ? &w.animations[size_t(ent.animation)] : nullptr;
	const float k = length(t.world.rows.f);
	const Basis unit = k > 0 ? Basis{t.world.rows.f * (1 / k), t.world.rows.s * (1 / k), t.world.rows.u * (1 / k)} : t.world.rows;
	poseSkeleton(w.skeletons[size_t(ent.skeleton)], a, {a ? t.animation : -1, t.animation >= 0 ? t.frame : -1, true}, Pose{t.world.position, unit}, k, out);
}

} // namespace lr
