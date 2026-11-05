// The menus' frame: the data they read, input (the queue, key repeat, the mouse), the dispatcher, dialogs, fades,
// music and the stack's flow.

#include "LRCore/Menus/Menus.h"
#include "LRData/Archive.h"
#include "LRCore/Common/Tables.h"

#include "LRData/Cache.h"
#include "LRData/Image.h"
#include "LRData/Look.h"
#include "LRData/Race.h"
#include "LRData/Saves.h"
#include "LRData/Scene.h"

#include <algorithm>
#include <cctype>

namespace lr {

namespace {

// A cinematic's world databases (through its folder, then MENUDATA/), with their skeletons,
// animations, material animations and models' scales.
bool loadCinemaWorlds(const Cache& cache, MenuData::Screen& screen, const std::string& folder)
{
	screen.cinemaFolder = folder;
	screen.cinemaWorlds.clear();
	const auto path = [&](const std::string& name, const char* extension) {
		const std::string own = folder + upper(name) + extension;
		return cache.has(own) ? own : "MENUDATA/" + upper(name) + extension;
	};
	screen.cinemaLibraries.clear();
	for (const std::string& world : screen.cinema.worlds) {
		MenuData::CinemaWorld& w = screen.cinemaWorlds.emplace_back();
		if (!cache.get(path(world, ".WDB"), w.scene)) {
			return false;
		}
		for (size_t i = 0; i < w.scene.materialLibraries.size(); i++) {
			const std::string mdb = path(w.scene.materialLibraries[i], ".MDB");
			if (std::none_of(screen.cinemaLibraries.begin(), screen.cinemaLibraries.end(), [&](const auto& l) { return l.materials == mdb; })) {
				MenuData::Screen::Library& l = screen.cinemaLibraries.emplace_back();
				l.materials = mdb;
				l.textures = path(i < w.scene.textureLists.size() ? w.scene.textureLists[i] : w.scene.materialLibraries[i], ".TDB");
				if (!cache.get(mdb, l.library)) {
					return false;
				}
			}
		}
		for (const std::string& name : w.scene.skeletons) {
			SkeletonFile f;
			(void)cache.get(path(name, ".SDB"), f); // (none: a skeleton without nodes)
			w.skeletons.push_back(nodesOf(f));
		}
		for (const std::string& name : w.scene.animations) {
			AnimationFile f;
			w.animations.push_back(cache.get(path(name, ".ADB"), f) ? animationOf(f, false) : Animation());
		}
		for (const std::string& name : w.scene.materialAnimations) {
			(void)cache.get(path(name, ".MAB"), w.materialAnimations.emplace_back()); // (none: no tracks)
		}
		for (const std::string& name : w.scene.models) {
			Model m;
			w.modelScales.push_back(cache.get(path(name, ".GDB"), m) ? m.scale : 1.0f);
		}
	}
	return true;
}

// The menus' tables and the quick builds.
bool loadTables(const Cache& cache, MenuData& m)
{
	bool ok = cache.get("MENUDATA/MENUNAME.SRF", m.names) && cache.get("MENUDATA/GSTYLES.MSB", m.styles) && cache.get("MENUDATA/DIALOG.MIB", m.dialog) &&
		cache.get("MENUDATA/LEGOMSC", m.music) && cache.get("MENUDATA/GENC0R0.SBK", m.sounds) && cache.get("MENUDATA/LEGORACE.CRB", m.circuits) &&
		cache.get("MENUDATA/LEGORACE.RCB", m.races) && cache.get("MENUDATA/LEGOMAN.ADB", m.minifig) && cache.get("MENUDATA/RSANIM.ADB", m.showcase) &&
		cache.get("MENUDATA/PIECEDB/LPIECEHI.LEB", m.pieces) && cache.get("MENUDATA/PIECEDB/CRSTMGR.LEB", m.sets) && cache.get("MENUDATA/PARTDB/BODYPART.PCB", m.parts) &&
		cache.get("MENUDATA/CBANIM.ADB", m.driverAnimations) && cache.get("MENUDATA/CMAMAN.ADB", m.photo);
	OriginalSave quick;
	ok = ok && cache.get("MENUDATA/QBUILD.LRS", quick);
	for (const OriginalSave::Record& r : quick.records) {
		MenuData::QuickBuild& q = m.quickBuilds.emplace_back();
		q.name = r.name;
		q.chassis = r.chassis;
		for (const OriginalSave::Piece& p : r.pieces) {
			q.pieces.push_back({p.type, p.x, p.y, p.rotation, p.colour, p.part});
		}
	}
	return ok;
}

// An image list's images (beside the list, or in MENUDATA), added to the menus' images; their ids in ids.
bool addImageList(const Cache& cache, MenuData& m, std::string_view path, std::vector<int16_t>& ids)
{
	ImageList list;
	if (!cache.get(path, list)) {
		return false;
	}
	const std::string folder(path.substr(0, path.rfind('/') + 1));
	for (const ImageList::Entry& e : list.images) {
		Image image;
		const std::string file = upper(e.name) + ".BMP";
		if (!cache.get(cache.has(folder + file) ? folder + file : "MENUDATA/" + file, image)) {
			return false;
		}
		ids.push_back(int16_t(m.images.size()));
		m.images.push_back({e.name, image.width, image.height, e.keyed, e.key, std::move(image.rgba)});
	}
	return true;
}

// Each screen's layout, styles and own images, then the screens that share a layout.
bool loadLayouts(const Cache& cache, MenuData& m)
{
	bool ok = true;
	m.screens.resize(145);
	for (size_t n = 0; ok && n < m.screens.size(); n++) {
		const std::string name = upper(m.names.narrow(n));
		if (name.empty()) {
			continue;
		}
		MenuData::Screen& s = m.screens[n];
		const std::string base = "MENUDATA/" + name;
		if (cache.has(base + ".MIB")) {
			ok = cache.get(base + ".MIB", s.layout);
		}
		if (ok && cache.has(base + ".MSB")) {
			ok = cache.get(base + ".MSB", s.styles);
		}
		if (ok && cache.has(base + ".IDB")) {
			ok = addImageList(cache, m, base + ".IDB", s.images);
		}
	}
	// The screens that share a layout.
	if (ok) {
		for (uint16_t n : {uint16_t(10), uint16_t(11)}) {
			m.screens[n] = m.screens[9];
		}
		for (uint16_t n : {uint16_t(45), uint16_t(46), uint16_t(47), uint16_t(48), uint16_t(49)}) {
			m.screens[n] = m.screens[50];
		}
		for (uint16_t n : {uint16_t(39), uint16_t(40)}) {
			m.screens[n] = m.screens[1];
		}
		// The builders' images (the sets' icons, the arrows, the pads).
		std::vector<int16_t> builder;
		ok = addImageList(cache, m, "MENUDATA/BUILDER.IDB", builder);
		for (uint16_t n : {uint16_t(ScreenEditCar), uint16_t(ScreenCarBuilder)}) {
			m.screens[n].images.insert(m.screens[n].images.end(), builder.begin(), builder.end());
		}
		// The screens that share a layout: the time race's is the single race's, racer select's three one.
		m.screens[29].layout = m.screens[6].layout;
		m.screens[29].styles = m.screens[6].styles;
		m.screens[29].images = m.screens[6].images;
		m.screens[20] = m.screens[21] = m.screens[19];
	}
	return ok;
}

// A screen's cinematic: its scene file, its effects, string tables (the first in each language) and image lists.
bool loadCinema(const Cache& cache, MenuData& m, MenuData::Screen& screen, const std::string& folder, std::string_view base)
{
	const std::string stem = folder + upper(base);
	CinemaFile& c = screen.cinema;
	if (!cache.get(stem + ".CDB", c) || !loadCinemaWorlds(cache, screen, folder)) { // (with its effects)
		return false;
	}
	for (size_t l = 0; l < 9; l++) {
		for (size_t t = 0; t < c.stringTables.size(); t++) {
			const std::string name = t == 0 && l > 0 ? cache.languages()[l] : upper(c.stringTables[t]);
			if (!cache.get(folder + name + ".SRF", screen.cinemaText[l].emplace_back())) {
				return false;
			}
		}
	}
	for (const std::string& list : c.imageLists) {
		if (!addImageList(cache, m, folder + upper(list) + ".IDB", screen.images)) {
			return false;
		}
	}
	return true;
}

// The cinematics of the legal splash, the credits and the screens with a scene view.
bool loadCinemas(const Cache& cache, MenuData& m)
{
	bool ok = loadCinema(cache, m, m.screens[ScreenLegal], "MENUDATA/LEGAL/", "splash1") && loadCinema(cache, m, m.screens[ScreenCredits], "MENUDATA/CREDITS/", "credits") &&
		loadCinema(cache, m, m.screens[ScreenSingleRace], "MENUDATA/SINGRACE/", "pst");
	for (uint16_t n = 22; ok && n <= 37; n++) {
		if (n == ScreenTimeRace || n == 30) {
			continue; // (the time race's layout is the single race's)
		}
		const MenuLayout& layout = m.screens[n].layout;
		const auto view = std::find_if(layout.bindings.begin(), layout.bindings.end(), [](const MenuLayout::Binding& b) { return b.kind == MenuLayout::SceneReference; });
		if (view != layout.bindings.end()) {
			ok = loadCinema(cache, m, m.screens[n], "MENUDATA/" + upper(m.names.narrow(n)) + "/", view->world);
		}
	}
	if (ok) {
		for (uint16_t n : {uint16_t(ScreenCircuitRace), uint16_t(ScreenTimeRace)}) {
			m.screens[n].cinema = m.screens[ScreenSingleRace].cinema;
			m.screens[n].cinemaFolder = m.screens[ScreenSingleRace].cinemaFolder;
			m.screens[n].cinemaWorlds = m.screens[ScreenSingleRace].cinemaWorlds;
			m.screens[n].cinemaLibraries = m.screens[ScreenSingleRace].cinemaLibraries;
		}
	}
	return ok;
}

// The drivers and the stock racers.
bool loadRacers(const Cache& cache, MenuData& m)
{
	OriginalSave stock;
	const bool ok = cache.get("GAMEDATA/COMMON/DRIVERS.DDB", m.drivers) && cache.get("MENUDATA/DEFAULT.LRS", stock);
	for (const OriginalSave::Record& r : stock.records) {
		RacerRecord& d = m.defaults.emplace_back();
		d.name = r.name, d.looks = r.looks, d.expression = r.expression, d.stockCar = r.stockCar, d.chassis = r.chassis, d.trophies = r.trophies;
		for (const OriginalSave::Piece& p : r.pieces) {
			d.pieces.push_back({p.type, p.x, p.y, p.rotation, p.colour, p.part});
		}
	}
	return ok;
}

// Each track's loading screen and the HUD's map.
bool loadLoadingScreens(const Cache& cache, MenuData& m)
{
	bool ok = true;
	for (const RaceTable::Race& race : m.races.races) {
		const std::string track = upper(race.folder);
		if (!ok || m.loading(track)) {
			continue;
		}
		MenuData::Loading& l = m.loadings.emplace_back();
		l.track = track;
		std::vector<int16_t> ids;
		ok = cache.get("GAMEDATA/" + track + "/LOADSCRN.LSB", l.screen);
		ImageList list;
		ok = ok && cache.get("GAMEDATA/" + track + "/LOADSCRN.IDB", list);
		for (const ImageList::Entry& e : list.images) {
			Image image;
			ok = ok && cache.get("GAMEDATA/" + track + "/" + upper(e.name) + ".BMP", image);
			l.tick = int16_t(m.images.size());
			m.images.push_back({e.name, image.width, image.height, e.keyed, e.key, std::move(image.rgba)});
		}
		// The HUD's map: the race file's bounds, and the texture of the map library's first material (every track's
		// local world has IGD_MAP.MDB as its second library). Stored as drawn: a flipped entry's rows turned over.
		RaceFile raceFile;
		ok = ok && cache.get("GAMEDATA/" + track + "/" + track + ".RAB", raceFile);
		l.mapBounds = raceFile.mapBounds;
		MaterialLibrary maps;
		TextureList textures;
		Texture texture;
		const std::string mapList = "GAMEDATA/" + track + "/IGD_MAP.TDB";
		if (ok && cache.get("GAMEDATA/" + track + "/IGD_MAP.MDB", maps) && !maps.materials.empty() && cache.get(mapList, textures) &&
			cache.texture(mapList, maps.materials[0].texture, texture)) {
			const int32_t entry = textures.find(maps.materials[0].texture);
			if (entry >= 0 && textures.textures[size_t(entry)].flip) {
				const size_t row = size_t(texture.width) * 4;
				for (size_t y = 0; y < texture.height / 2; y++) {
					std::swap_ranges(texture.rgba.begin() + std::ptrdiff_t(y * row), texture.rgba.begin() + std::ptrdiff_t((y + 1) * row),
						texture.rgba.begin() + std::ptrdiff_t((texture.height - 1 - y) * row));
				}
			}
			l.map = int16_t(m.images.size());
			m.images.push_back({maps.materials[0].texture, texture.width, texture.height, false, 0, std::move(texture.rgba)});
		}
	}
	return ok;
}

// Each language's texts and fonts.
bool loadLanguages(const Cache& cache, MenuData& m)
{
	bool ok = true;
	for (size_t l = 0; ok && l < 9; l++) {
		MenuData::Language& lang = m.languages[l];
		const std::string folder = "MENUDATA/" + cache.languages()[l] + "/";
		const std::string race = "GAMEDATA/COMMON/" + cache.languages()[l] + "/";
		FontList list, raceList;
		ok = cache.get(folder + "MENUTEXT.SRF", lang.text) && cache.get(folder + "CIRCUIT.SRF", lang.circuits) && cache.get(race + "GAME.SRF", lang.game) &&
			cache.get(race + "DRIVERS.SRF", lang.drivers) && cache.get(folder + "GFONTS.FDB", list) && cache.get(race + "LEGOFNTS.FDB", raceList);
		// The menus' fonts, then the race's.
		for (const auto& [fonts, dir] : {std::pair{&list, &folder}, std::pair{&raceList, &race}}) {
			if (fonts == &raceList) {
				lang.raceFonts = lang.fonts.size();
			}
			for (const FontList::Font& f : fonts->fonts) {
				Image image;
				MenuData::Font& font = lang.fonts.emplace_back();
				font.name = f.name;
				font.spacing = f.spacing;
				font.keyed = f.keyed;
				font.key = f.key;
				ok = ok && cache.get(*dir + upper(f.name) + ".BMP", image) && font.glyphs.scan(image, f.chars);
				font.width = image.width;
				font.rgba = std::move(image.rgba);
			}
		}
	}
	return ok;
}

// The scene views' worlds: each binding's world not yet found, through MENUDATA/'s folders, with
// its first camera and its lights.
void findMenuWorlds(const Cache& cache, MenuData& m)
{
	for (const MenuData::Screen& sc : m.screens) {
		for (const MenuLayout::Binding& b : sc.layout.bindings) {
			if (b.world.empty() || b.kind == MenuLayout::SceneReference || m.menuWorld(b.world)) {
				continue;
			}
			const std::string wanted = "/" + upper(b.world) + ".WDB";
			for (const std::string& f : cache.paths()) {
				SceneFile scene;
				if (!f.starts_with("MENUDATA/") || !f.ends_with(wanted) || !cache.get(f, scene)) {
					continue;
				}
				MenuData::MenuWorld& w = m.menuWorlds.emplace_back();
				w.name = upper(b.world);
				w.folder = f.substr(0, f.rfind('/') + 1);
				for (const SceneFile::Camera& c : scene.cameras) {
					if (!w.hasCamera) {
						w.hasCamera = true;
						w.camera = c;
					}
				}
				w.lit = !scene.ambients.empty() || !scene.lights.empty();
				const auto colour = [](const std::array<float, 3>& c) {
					return 0xff000000u | uint32_t(uint8_t(c[0])) << 16 | uint32_t(uint8_t(c[1])) << 8 | uint32_t(uint8_t(c[2]));
				};
				if (!scene.ambients.empty()) {
					w.ambient = colour(scene.ambients[0].colour);
				}
				for (const SceneFile::Light& l : scene.lights) {
					if (w.lights.size() < 7) {
						w.lights.push_back({normalize(vec(l.direction)), colour(l.colour)});
					}
				}
				break;
			}
		}
	}
}

} // namespace

const MaterialLibrary::Material* MenuData::Screen::cinemaMaterial(std::string_view name, size_t* library) const
{
	std::vector<const MaterialLibrary*> loaded;
	for (const Library& l : cinemaLibraries) {
		loaded.push_back(&l.library);
	}
	return findMaterial(loaded, name, library);
}

const MenuData::MenuWorld* MenuData::menuWorld(std::string_view name) const
{
	const std::string wanted = upper(name);
	for (const MenuWorld& w : menuWorlds) {
		if (w.name == wanted) {
			return &w;
		}
	}
	return nullptr;
}

// The menus' 3D's files: what the core poses things by.
bool MenuData::load3d(const Cache& cache)
{
	const auto scaled = [](const std::array<float, 4>& b, float scale) { return Sphere{Vec3{b[0], b[1], b[2]} * scale, b[3] * scale}; };
	const auto modelSphere = [&](const std::string& path, Sphere& out) {
		Model m;
		if (!cache.get(path, m)) {
			return false;
		}
		out = modelBounds(m);
		return true;
	};
	if (!cache.get("MENUDATA/PIECEDB/CHASSIS.CMB", menuChassis)) {
		return false;
	}
	for (const ChassisTable::Chassis& c : menuChassis.chassis) {
		// The chassis model's first part's bounds (its wheels turn in them), at the model's scale.
		AnimationFile a;
		Model m;
		const std::string base = "MENUDATA/PIECEDB/" + upper(c.model);
		const bool has = cache.get(base + ".GDB", m) && cache.get("MENUDATA/PIECEDB/" + upper(c.animation) + ".ADB", a) && !a.parts.empty();
		chassisBounds.push_back(has ? scaled(a.parts[0].bounds, m.scale) : Sphere{});
		chassisScales.push_back(has ? m.scale : 0.015625f);
	}
	// The seated body (body set 1's) in a car: its part's bounds as the menus' body set's file gives them
	// (the builder's large-car blend takes the group's radius as 6.0347 for one car; with MENUPART's leg_box part the
	// group comes out 6.0385, with GAMEPART's 5.961).
	AnimationFile seated;
	if (!cache.get("MENUDATA/PARTDB/MENUPART/LEG_BOX.ADB", seated) || seated.parts.empty()) {
		return false;
	}
	seatedBounds = scaled(seated.parts[0].bounds, 0.015625f);
	// The head libraries beside the two body sets' worlds; the ring items' models; the hats.
	if (!cache.get("MENUDATA/PARTDB/MENUPART/CBBODIES.GCB", heads[0]) || !cache.get("MENUDATA/PARTDB/GAMEPART/ICB_CHAR.GCB", heads[1])) {
		return false;
	}
	const char* RingModels[5] = {"HEADS", "CHESTS", "CHEST_HK", "LEGSS", "LEG-PEGS"};
	for (size_t i = 0; i < 5; i++) {
		if (!modelSphere(std::string("MENUDATA/PARTDB/MENUPART/") + RingModels[i] + ".GDB", ringBounds[i])) {
			return false;
		}
	}
	// (A hat ring's item is the hat-only entry of the hat's own name: BODYPART's K(0x29) names, "nohat", "helmet2s"...;
	// the worn hat is the K(0x2d) model with its head.)
	for (const PartCatalog::Part& hat : parts.rings[0]) {
		const HeadLibrary::Head* h = heads[0].find(hat.name);
		hatBounds.push_back(h ? modelBounds(h->model) : Sphere{});
	}
	// Racer select's turntable: CRSDOW's node and part, turned by its entity in BLENDED.WDB.
	SkeletonFile nodes;
	AnimationFile spin;
	SceneFile blended;
	if (!cache.get("MENUDATA/RS_SET/CRSDOW.SDB", nodes) || !cache.get("MENUDATA/RS_SET/CRSDOW.ADB", spin) || !cache.get("MENUDATA/RS_SET/BLENDED.WDB", blended)) {
		return false;
	}
	turntableNodes = nodesOf(nodes);
	turntable = animationOf(spin, false);
	// The bodies' skeletons, the champions (the win car's), the scene views' worlds.
	for (size_t b = 0; b < bodySkeletons.size(); b++) {
		SkeletonFile f;
		if (!cache.get("MENUDATA/PARTDB/MENUPART/" + upper(parts.bodies[b]) + ".SDB", f)) {
			return false;
		}
		bodySkeletons[b] = nodesOf(f);
	}
	if (!cache.get("GAMEDATA/COMMON/CHAMPS.CCB", champions)) {
		return false;
	}
	findMenuWorlds(cache, *this);
	for (const std::vector<SceneFile::Entity>* list : {&blended.entities, &blended.animated}) {
		for (const SceneFile::Entity& e : *list) {
			if (e.model >= 0 && size_t(e.model) < blended.models.size() && upper(blended.models[size_t(e.model)]) == "CRSDOW") {
				turntableFrame = basisFromForwardUp(normalize(vec(e.forward)), normalize(vec(e.up)));
			}
		}
	}
	return !turntableNodes.empty();
}

bool MenuData::load(const Cache& cache)
{
	*this = {};
	useExecutableTables(cache.executable());
	bool ok = cache.languages().size() == languages.size() && loadTables(cache, *this);
	// The images: the global list's, then each screen's own.
	ok = ok && addImageList(cache, *this, "MENUDATA/GIMAGES.IDB", globalImages) && addImageList(cache, *this, "GAMEDATA/COMMON/LEGOIMGS.IDB", hudImages);
	ok = ok && loadLayouts(cache, *this) && loadCinemas(cache, *this);
	// The race's: the drivers, the stock racers, each track's loading screen.
	ok = ok && loadRacers(cache, *this) && loadLoadingScreens(cache, *this) && loadLanguages(cache, *this);
	return ok && load3d(cache); // (after the screens: their views' worlds)
}

int16_t MenuData::image(std::string_view name, uint16_t screen) const
{
	for (int16_t i : globalImages) {
		if (images[size_t(i)].name == name) {
			return i;
		}
	}
	if (screen < screens.size()) {
		for (int16_t i : screens[screen].images) {
			if (images[size_t(i)].name == name) {
				return i;
			}
		}
	}
	return -1;
}

const MenuData::Loading* MenuData::loading(std::string_view track) const
{
	for (const Loading& l : loadings) {
		if (l.track == track) {
			return &l;
		}
	}
	return nullptr;
}

int16_t MenuData::raceFont(size_t language, std::string_view name) const
{
	const std::vector<Font>& fonts = languages[language].fonts;
	for (size_t i = languages[language].raceFonts; i < fonts.size(); i++) {
		if (fonts[i].name == name) {
			return int16_t(i);
		}
	}
	return -1;
}

int16_t MenuData::font(size_t language, std::string_view name) const
{
	const std::vector<Font>& fonts = languages[language].fonts;
	for (size_t i = 0; i < languages[language].raceFonts; i++) {
		if (fonts[i].name == name) {
			return int16_t(i);
		}
	}
	return -1;
}

uint32_t menuKey(uint32_t usb)
{
	// USB usages 4.. (a..z, 1..0, Enter, Esc, Backspace, Tab, Space, - = [ ] \ # ; ' ` , . /, Caps Lock, F1..F12,
	// Print Screen, Scroll Lock, Pause, Insert, Home, Page Up, Delete, End, Page Down, Right, Left, Down, Up, Num Lock,
	// the keypad's / * - + Enter 1..9 0 .) as DirectInput scan codes.
	static const uint8_t Codes[100] = {0, 0, 0, 0, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16,
		0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a, 0x1b, 0x2b, 0x2b, 0x27, 0x28,
		0x29, 0x33, 0x34, 0x35, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0xb7, 0x46, 0xc5, 0xd2, 0xc7, 0xc9, 0xd3, 0xcf, 0xd1, 0xcd, 0xcb,
		0xd0, 0xc8, 0x45, 0xb5, 0x37, 0x4a, 0x4e, 0x9c, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49, 0x52, 0x53};
	// The modifiers (224..231: left Ctrl, Shift, Alt, GUI, then the right ones) and the Menu key.
	static const uint8_t Modifiers[8] = {0x1d, 0x2a, 0x38, 0xdb, 0x9d, 0x36, 0xb8, 0xdc};
	const uint32_t code = usb < 100 ? Codes[usb] : usb >= 224 && usb < 232 ? Modifiers[usb - 224] : usb == 101 ? 0xdd : 0;
	return code ? SourceKeyboard | code : 0;
}

char16_t menuCharacter(uint32_t usb)
{
	static const char Punctuation[] = "-=[]\\;'`,./";
	return usb >= 4 && usb <= 29			  ? char16_t('A' + usb - 4)
		: usb >= 30 && usb <= 38			  ? char16_t('1' + usb - 30)
		: usb == 39							  ? u'0'
		: usb == 40							  ? u'\r'
		: usb == 42							  ? u'\b'
		: usb == 44							  ? u' '
		: usb >= 45 && usb <= 56 && usb != 50 ? char16_t(Punctuation[usb - 45 - (usb > 50)])
											  : 0;
}

void Menus::playTune(int32_t number, bool loops)
{
	tune = number;
	tuneLoops = loops;
	tuneStarted = true;
}

int32_t Menus::startFade(int32_t ms, bool in, uint32_t colour, MenuRect rect)
{
	for (size_t i = 0; i < fades.size(); i++) {
		if (!fades[i].active) {
			fades[i] = {ms, ms, in, true, false, colour, rect};
			return int32_t(i);
		}
	}
	return -1;
}

void Menus::navigate(int32_t where)
{
	pending = true;
	destination = where;
}

void Menus::saveSlots(Game& g, bool both)
{
	for (size_t slot : {size_t(1), size_t(0)}) {
		if (!both && slot != g.currentCard) {
			continue;
		}
		std::vector<uint8_t> bytes = g.saveSlot(slot);
		if (!bytes.empty()) {
			saves.push_back({uint8_t(slot), std::move(bytes)});
		}
	}
}

void Menus::start(const MenuData& menuData, Game& g, uint16_t next, bool save)
{
	data = &menuData;
	game = &g;
	if (g.defaults.empty()) {
		g.defaults = menuData.defaults; // racer select lists them
	}
	g.racerCount = g.settings.opponents; // the race setup screens start from the options' count
	depth = 0;
	push(next);
	if (save) {
		push(ScreenSaveAll);
	}
	pending = false;
	quit = raceStart = false;
	dialogCount = 0;
	queued = 0;
	held.clear();
	tune = -1;
	language = g.settings.language;
	build(g); // drawn from the next frame on
}

// Dialogs.

void Menus::openDialog(MenuDialog::Kind kind, int32_t message)
{
	if (dialogCount >= dialogs.size()) {
		return;
	}
	MenuDialog& d = dialogs[dialogCount++];
	d = {};
	d.kind = kind;
	d.message = message;
	makeRoot(d.tree);
	d.frame = make(d.tree, data->dialog, "dialfrme", "brickbox", 0);
	d.label = make(d.tree, data->dialog, "dialtext", "menufont", d.frame);
	d.tree[d.label].wrapping = true;
	setString(d.tree, d.label, message);
}

void Menus::giveButton(uint32_t binding, uint32_t style, int32_t textIndex)
{
	MenuDialog& d = dialogs[dialogCount - 1];
	WidgetTree& t = d.tree;
	const int16_t button = makeNamed(t, binding, style, d.frame, textIndex);
	if (button < 0) {
		return;
	}
	// Under the frame, in the dialog's icon tree (a binding that doesn't attach itself is attached here).
	if (t[button].parent < 0) {
		t.adopt(button, d.frame);
	}
	if (t[button].iconParent < 0) {
		Widget& root = t[0];
		t[button].iconParent = 0;
		if (root.iconLast >= 0) {
			t[root.iconLast].iconNext = button;
		}
		else {
			root.iconFirst = button;
		}
		root.iconLast = button;
	}
	d.buttons[d.buttons[0] < 0 ? 0 : 1] = button;
}

void Menus::showDialog(int32_t defaultButton)
{
	MenuDialog& d = dialogs[dialogCount - 1];
	WidgetTree& t = d.tree;
	Widget& m = t[d.label];
	const int32_t mw = m.rect.right - m.rect.left, mh = m.rect.bottom - m.rect.top;
	d.halfWidth = float(mw + 40) * 0.5f;
	d.halfHeight = float(mh + 40) * 0.5f;
	m.rect = {20, 20, mw + 20, mh + 20};
	int32_t top = m.rect.bottom + 20;
	for (int16_t b : d.buttons) {
		if (b < 0) {
			continue;
		}
		Widget& w = t[b];
		const int32_t bottom = w.rect.bottom, right = w.rect.right;
		d.halfHeight += float(bottom + 20) * 0.5f;
		w.rect = {25, top, right + 25, top + bottom};
		top = w.rect.bottom + 20;
		show(t, b, false);
	}
	show(t, d.label, false);
	if (defaultButton >= 0 && d.buttons[size_t(defaultButton)] >= 0) {
		select(t, d.buttons[size_t(defaultButton)], true, false, true);
	}
}

void Menus::closeDialog()
{
	MenuDialog& d = dialogs[dialogCount - 1];
	if (d.phase != MenuDialog::Open) {
		return;
	}
	d.phase = MenuDialog::Closing;
	d.animMs = 0;
	show(d.tree, d.label, false);
	// Its buttons leave the trees.
	WidgetTree& t = d.tree;
	for (int16_t& b : d.buttons) {
		if (b < 0) {
			continue;
		}
		for (int16_t* link = &t[d.frame].first; *link >= 0; link = &t[*link].next) {
			if (*link == b) {
				*link = t[b].next;
				break;
			}
		}
		t[d.frame].last = -1;
		for (int16_t c = t[d.frame].first; c >= 0; c = t[c].next) {
			t[d.frame].last = c;
		}
		t[b].parent = -1;
		b = -1;
	}
	t[0].iconFirst = t[0].iconLast = t[0].selected = -1;
	if (t[0].focus >= 0) {
		t[0].focus = -1;
	}
}

void Menus::updateDialog(int32_t dt)
{
	MenuDialog& d = dialogs[dialogCount - 1];
	d.ageMs += dt;
	if (d.kind == MenuDialog::TimedNotice && d.ageMs > 5000 && d.phase == MenuDialog::Open) {
		d.result = MenuDialog::TimedOut;
		closeDialog();
	}
	float scale = 1;
	if (d.phase == MenuDialog::Opening || d.phase == MenuDialog::Closing) {
		d.animMs += dt;
		scale = float(d.animMs) * 0.0033333334f;
		const bool closing = d.phase == MenuDialog::Closing;
		if (d.animMs > 300) {
			if (closing) {
				d.phase = MenuDialog::Closed;
			}
			else {
				d.phase = MenuDialog::Open;
				show(d.tree, d.label, true);
				for (int16_t b : d.buttons) {
					show(d.tree, b, true);
				}
			}
			d.animMs = 0;
		}
		if (closing) {
			scale = 1 - scale;
		}
		scale = std::clamp(scale, 0.0f, 1.0f);
	}
	if (d.phase == MenuDialog::Closed) {
		dialogCount--;
		return;
	}
	const int32_t hw = int32_t(d.halfWidth * scale), hh = int32_t(d.halfHeight * scale);
	d.tree[d.frame].rect = {320 - hw, 240 - hh, 320 + hw, 240 + hh};
	layoutFrame(d.tree, d.frame);
}

// The frame.

void Menus::frame(Game& g, int32_t dt, std::span<const MenuKey> keys, int32_t mx, int32_t my, uint32_t buttons)
{
	game = &g;
	sounds.clear();
	saves.clear();
	tuneStarted = false;
	const uint16_t before = top();

	// The widgets' timers (the dialog's when one is open), a cinematic's clock.
	WidgetTree& timed = active();
	if (!timed.widgets.empty()) {
		tick(timed, 0, dt);
	}
	if (!dialogCount) {
		tickScreen(dt);
	}

	// The frame's events and the repeats, then the dispatcher.
	queueInput(keys, buttons, dt);
	dispatchKeys(g);

	// The mouse: its motion is handled where the cursor was; the cursor moves at the frame's end.
	const int32_t dx = mx - mouseX, dy = my - mouseY;
	const bool moved = dx || dy;
	mouseX = mx;
	mouseY = my;
	if (moved) {
		moveMouse(dx, dy);
	}

	// The dialog's update, or the screen's.
	if (dialogCount) {
		updateDialog(dt);
	}
	else {
		updateScreen(g, dt);
	}
	if (quit) {
		return;
	}
	if (depth == 0) {
		leaveScreen();
		raceStart = true;
	}
	else if (top() != before) {
		leaveScreen();
		build(g);
	}
	stepFades(dt);
	if (!raceStart) {
		drawFrame();
	}
	if (moved) {
		// Kept a quarter of the arrow (32x32) from the right and bottom edges.
		cursorX = std::clamp(mx, 0, 640 - 8);
		cursorY = std::clamp(my, 0, 480 - 8);
	}
}

void Menus::queueKey(MenuKey k)
{
	if (queued < queue.size()) {
		queue[queued++] = k;
	}
}

// A key or button going down (held, its device's repeat restarted) or up.
void Menus::holdKey(uint32_t key, bool down)
{
	const int device = (key >> 28) == 2 ? 1 : 0;
	if (down) {
		repeatMs[size_t(device)] = 500;
		if (std::find(held.begin(), held.end(), key) == held.end()) {
			held.push_back(key);
		}
	}
	else {
		held.erase(std::remove(held.begin(), held.end(), key), held.end());
	}
	queueKey({key, down, false});
}

// The frame's events, then the repeats: each device's held keys again, in key order, 500 ms after a press and every
// 250 ms after that.
void Menus::queueInput(std::span<const MenuKey> keys, uint32_t buttons, int32_t dt)
{
	for (const MenuKey& k : keys) {
		if ((k.key >> 28) == (SourceCharacter >> 28)) {
			queueKey(k); // typed, not held
		}
		else {
			holdKey(k.key, k.down);
		}
	}
	for (uint32_t b = 0; b < 4; b++) {
		const bool was = mouseButtons & (1u << b), now = buttons & (1u << b);
		if (was != now) {
			holdKey(SourceMouse | b, now);
		}
	}
	mouseButtons = buttons;
	for (int device = 0; device < 2; device++) {
		repeatMs[size_t(device)] -= dt;
		if (repeatMs[size_t(device)] > 0) {
			continue;
		}
		std::vector<uint32_t> repeats;
		for (uint32_t key : held) {
			if (int((key >> 28) == 2) == device) {
				repeats.push_back(key);
			}
		}
		std::sort(repeats.begin(), repeats.end());
		for (uint32_t key : repeats) {
			queueKey({key, true, true});
		}
		repeatMs[size_t(device)] = repeats.empty() ? 500 : 250;
	}
}

// The dispatcher: the queued events in order, until one is taken by the screen or the focused widget.
void Menus::dispatchKeys(Game& g)
{
	size_t taken = 0;
	for (; taken < queued; taken++) {
		// The screen's own handler, the focused widget (either taking it ends the frame's events: the rest wait), then
		// the selection keys, then down the tree.
		const MenuKey k = queue[taken];
		if (screenKey(g, k)) {
			taken++;
			break;
		}
		WidgetTree& now = active();
		if (now.widgets.empty()) {
			continue;
		}
		const int16_t focused = now.focused();
		if (focused >= 0) {
			const MenuRect r = global(now, focused);
			if (k.down ? keyDown(now, focused, k, cursorX - r.left, cursorY - r.top) : keyUp(now, focused, k, cursorX - r.left, cursorY - r.top)) {
				taken++;
				break;
			}
		}
		if (k.key == MenuShift || k.key == MenuRightShift) {
			shift = k.down;
			continue;
		}
		if (k.key == MenuCtrl || k.key == MenuRightCtrl) {
			ctrl = k.down;
			continue;
		}
		const bool tab = k.key == MenuTab;
		const bool forward = (tab && !shift) || k.key == MenuDown || k.key == (SourceAxis | 2) || k.key == (SourcePad | 8);
		const bool back = (tab && shift) || k.key == MenuUp || k.key == (SourceAxis | 3) || k.key == (SourcePad | 6);
		if (forward || back || k.key == (SourcePad | 1)) {
			if (k.down && (forward || back)) {
				moveSelection(now, forward);
			}
			continue;
		}
		routeKey(now, 0, k, cursorX, cursorY);
	}
	std::copy(queue.begin() + taken, queue.begin() + queued, queue.begin());
	queued = uint8_t(queued - taken);
}

// The mouse's motion, handled where the cursor was: the builder's drag, the focused widget keeping it, else down the
// tree.
void Menus::moveMouse(int32_t dx, int32_t dy)
{
	WidgetTree& now = active();
	const int16_t focused = now.widgets.empty() ? int16_t(-1) : now.focused();
	// A focused text field keeps the motion only once the mouse has taken it there (hovering it); the focus it has
	// from its screen's opening doesn't hold the mouse (on the license, the first motion
	// hover-selects the button under the cursor, a later one after hovering the field is kept by it).
	const bool keeps = focused >= 0 && (now[focused].kind != Widget::TextField || now[focused].grabbed);
	const bool builder = !dialogCount && screen == ScreenCarBuilder && builderMotion(dx, dy);
	if (!builder && !(keeps && cursorOver(now, focused, dx)) && !now.widgets.empty()) {
		routeCursor(now, 0, cursorX, cursorY);
	}
}

// The fades: each one's time down; ended the frame after it reaches 0, let go the frame after that.
void Menus::stepFades(int32_t dt)
{
	for (Fade& f : fades) {
		if (!f.active) {
			continue;
		}
		if (f.leftMs == 0) {
			if (f.ended) {
				f.active = false;
				continue;
			}
			f.ended = true;
		}
		f.leftMs = std::max(0, f.leftMs - dt);
	}
}

} // namespace lr
