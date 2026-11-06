// The menus' 3D views: a scene view's scene by its screen, the world's camera, the showcase, the
// pieces the screens' scenes are made of (the car group, a minifig, a car's chassis), a piece carousel's items, and the
// lights.

#include "LRCore/Menus/Menus.h"
#include "LRCore/Common/Tables.h"
#include "LRData/Archive.h"

#include "Show/Animation.h"
#include "Menus/BuilderParts.h"
#include "Menus/Layout.h"
#include "Menus/MenuNames.h"
#include "Menus/Pen.h"

#include <algorithm>
#include <cmath>

namespace lr {

// A driver table entry's minifig (its K(0x35)..K(0x39): hat, face, torso, legs, expression) in a thing's order
// (face, hat, legs, torso, expression).
std::array<uint8_t, 5> driverLooks(const DriverTable::Driver& d)
{
	return {uint8_t(d.looks[1]), uint8_t(d.looks[0]), uint8_t(d.looks[3]), uint8_t(d.looks[2]), uint8_t(d.looks[4])};
}

void Menus::describeScene(const Widget& view, MenuScene& scene) const
{
	const MenuLayout& layout = data->screens[screen].layout;
	const auto binding = std::find_if(layout.bindings.begin(), layout.bindings.end(),
		[&](const MenuLayout::Binding& b) { return b.id == view.id; });
	if (binding == layout.bindings.end()) {
		return;
	}
	if (view.kind == Widget::Ring) {
		builders.describeRing(*this, view, binding->camera, scene);
		return;
	}
	worldCamera(*binding, scene);
	switch (screen) {
	case ScreenCircuitRace:
	case ScreenSingleRace:
	case ScreenTimeRace: {
		// The preview: its PST scene (looping) as a cinematic draws it, the mascot in place of its
		// "guy1".
		const DriverTable::Driver* d = race.state.scene >= 0 ? data->drivers.find(race.state.mascot) : nullptr;
		const std::array<uint8_t, 5> mascot = d ? driverLooks(*d) : std::array<uint8_t, 5>{};
		cinema.describeCinema(*this, race.state.preview, data->screens[screen], scene, d ? &mascot : nullptr, nullptr);
		break;
	}
	case ScreenGarage:
	case ScreenRacerSelect:
	case ScreenPlayer2Select: showcaseScene(scene); break;
	case ScreenMainMenu: screens.mainMenuScene(*this, scene); break;
	default: builders.describeScene(*this, scene); break;
	}
}

// The world's camera when it has one, else the layout's.
void Menus::worldCamera(const MenuLayout::Binding& binding, MenuScene& scene) const
{
	const std::array<float, 9>& c = binding.camera;
	scene.world = binding.world;
	scene.drawWorld = binding.numbers[0] != 0;
	scene.blendedWorld = scene.drawWorld && binding.numbers[1] != 0;
	if (const MenuData::MenuWorld* w = data->menuWorld(binding.world); w && w->hasCamera) {
		const SceneFile::Camera& cam = w->camera;
		scene.eye = cam.position;
		scene.target = {cam.position[0] + cam.forward[0], cam.position[1] + cam.forward[1], cam.position[2] + cam.forward[2]};
		scene.up = cam.up;
		scene.fov = cam.fov, scene.nearPlane = cam.nearPlane, scene.farPlane = cam.farPlane;
		if (screen == ScreenEditCar && builders.state.animMs > 0) {
			// (From its second frame the view looks with the world's up, not GARAGE Cam1's slightly rolled one.)
			scene.up = {0, 0, 1};
		}
	}
	else {
		scene.eye = {c[0], c[1], c[2]};
		scene.target = {c[3], c[4], c[5]};
		scene.fov = c[6], scene.nearPlane = c[7], scene.farPlane = c[8];
	}
}

// The showcase: the racer's car on the turntable (turned as its shadow's node at the turntable's
// clock), the minifig beside it.
void Menus::showcaseScene(MenuScene& scene) const
{
	using Thing = MenuScene::Thing;
	const Game& g = *game;
	const int32_t n = int32_t(screens.state.racers.size());
	const RacerRecord* r = screen == ScreenGarage
		? (n && screens.state.shown >= 0 ? g.racer(screens.state.racers[screens.state.shown % n]) : nullptr)
		: g.racer(race.state.racer);
	// (A racer just shown is placed from the next frame)
	if (!r || (screen != ScreenGarage && race.state.racerChanged)) {
		return;
	}
	// (The garage's turntable stands still; its minifig is the garage's, racer select's its own.)
	const int32_t spinMs = screen == ScreenGarage ? 0 : race.state.spinMs;
	const int32_t part = screen == ScreenGarage ? screens.state.driverPart : race.state.driverPart;
	const float frame = screen == ScreenGarage ? screens.state.driverFrame : race.state.driverFrame;
	std::vector<NodeMatrix> spun;
	scene.blendedFrame = float(spinMs) * 0.03f; // (the turntable's shadows in BLENDED.WDB)
	poseSkeleton(data->turntableNodes, &data->turntable, {0, float(spinMs) * 0.03f, true}, Pose{{}, data->turntableFrame}, 1, spun);
	const Basis& turn = spun.empty() ? data->turntableFrame : spun[0].rows;
	carGroup(scene, {-11.52f, -6.767f, 0}, 0, false, r);
	for (Thing& t : scene.things) {
		const float k = t.kind == Thing::Chassis ? length(t.world.rows.f) : 1.0f; // (the chassis model's own scale)
		t.world.rows = facing(turn.f, turn.u, k);
		t.frame = float(spinMs) * 0.03f;
	}
	sceneMinifig(scene, {-0.938f, -0.898f, 1.487f}, {0.963631f, -0.267238f, 0}, {0, 0, 1},
		{r->looks[0], r->looks[1], r->looks[2], r->looks[3], r->expression}, Thing::Showcase, part, frame);
}

// The car group: the car's mesh, the menus' chassis model and, in the builders, the seated minifig at
// the chassis' driver mount; all turned by the angle at the place.
void Menus::carGroup(MenuScene& scene, Vec3 at, float angle, bool lit, const RacerRecord* racer) const
{
	using Thing = MenuScene::Thing;
	Thing& car = scene.things.emplace_back();
	car.kind = Thing::Car;
	car.at = {at.x, at.y, at.z};
	car.angle = angle;
	car.lit = lit;
	car.world = {turned(angle, 1), at};
	const ChassisTable::Chassis* c = chassisOf(racer ? &racer->pieces : nullptr);
	if (c) {
		Thing& wheels = scene.things.emplace_back();
		wheels.kind = Thing::Chassis;
		wheels.model = c->model;
		wheels.lit = lit;
		wheels.world = {turned(angle, chassisScale(c)), at};
	}
	if (!racer && c) {
		const Game& g = *game;
		Thing& man = scene.things.emplace_back();
		man.kind = Thing::Minifig;
		man.seated = true;
		man.body = 4;
		man.skeleton = "MENUDATA/PARTDB/MENUPART/" + upper(data->parts.bodies[4]) + ".SDB";
		man.partFile = "MENUDATA/PARTDB/MENUPART/" + upper(data->parts.bodies[4]) + ".ADB";
		man.animation = 0; // ("velostop", one frame)
		man.looks = {g.working.looks[0], g.working.looks[1], g.working.looks[2], g.working.looks[3], g.working.expression};
		man.world = {turned(angle, MinifigScale), at + toWorld(turned(angle, 1), vec(c->driverMount))};
	}
}

// (The main menu's, the driver builder's and the license's minifig poses on catalog body 0's skeleton, the others on
// their own body's)
void Menus::sceneMinifig(MenuScene& scene, Vec3 at, Vec3 forward, Vec3 up, const std::array<uint8_t, 5>& looks,
	MenuScene::Thing::PartFile parts, int32_t animation, float frame) const
{
	MenuScene::Thing& t = scene.things.emplace_back();
	t.kind = MenuScene::Thing::Minifig;
	t.body = bodyOf(looks, false);
	const bool firstBody = screen == ScreenMainMenu || screen == ScreenDriver || screen == ScreenLicense;
	t.skeleton = "MENUDATA/PARTDB/MENUPART/" + upper(data->parts.bodies[size_t(firstBody ? 0 : t.body)]) + ".SDB";
	t.at = {at.x, at.y, at.z};
	t.looks = looks;
	t.parts = parts;
	t.animation = animation;
	t.frame = frame;
	t.world = {facing(forward, up, MinifigScale), at};
}

const ChassisTable::Chassis* Menus::chassisOf(const std::vector<CarPiece>* pieces) const
{
	// The car's first piece is its chassis (the table's names are lower case).
	const int32_t type = pieces ? (pieces->empty() ? -1 : int32_t((*pieces)[0].type))
								: (builders.state.car.entries.empty() ? -1 : int32_t(builders.state.car.entries[0].piece.type));
	const PieceLibrary::Piece* p = type >= 0 ? data->pieces.find(type) : nullptr;
	if (!p) {
		return nullptr;
	}
	std::string name = p->name;
	for (char& ch : name) {
		ch = char(std::tolower(uint8_t(ch)));
	}
	return data->menuChassis.find(name);
}

float Menus::chassisScale(const ChassisTable::Chassis* c) const
{
	// Each menus' chassis model at its own .GDB scale: not all are 0.015625.
	const size_t i = c ? size_t(c - data->menuChassis.chassis.data()) : data->chassisScales.size();
	return i < data->chassisScales.size() ? data->chassisScales[i] : 0.015625f;
}

int32_t Menus::bodyOf(const std::array<uint8_t, 5>& looks, bool seated) const
{
	// Body 2·legs variant + torso variant of the catalog's; the seated body is catalog body 4.
	const PartCatalog& cat = data->parts;
	const bool hooked = looks[3] < cat.rings[2].size() && cat.rings[2][looks[3]].variant;
	const bool pegs = looks[2] < cat.rings[3].size() && cat.rings[3][looks[2]].variant;
	return seated ? 4 : 2 * int32_t(pegs) + int32_t(hooked);
}

const CarMesh& Menus::carouselItem(int32_t type, int32_t colour) const
{
	const uint32_t key = uint32_t(type) << 8 | uint32_t(colour & 255);
	for (const auto& [k, m] : m_carouselItems) {
		if (k == key) {
			return m;
		}
	}
	return m_carouselItems.emplace_back(key, Car::itemMesh(data->pieces, data->sets.materials, type, colour)).second;
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

} // namespace lr
