// The menus' files: MENUDATA's layouts, styles, images, texts and fonts, the cinematics, the tables, the race's 2D and
// the menus' 3D.

#include "LRCore/Menus/MenuData.h"
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
			if (std::none_of(screen.cinemaLibraries.begin(), screen.cinemaLibraries.end(),
					[&](const auto& l) { return l.materials == mdb; })) {
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

// A save file record's pieces as a car's.
std::vector<CarPiece> piecesOf(const OriginalSave::Record& r)
{
	std::vector<CarPiece> out;
	for (const OriginalSave::Piece& p : r.pieces) {
		out.push_back({p.type, p.x, p.y, p.rotation, p.colour, p.part});
	}
	return out;
}

// The menus' tables and the quick builds.
bool loadTables(const Cache& cache, MenuData& m)
{
	bool ok = cache.get("MENUDATA/MENUNAME.SRF", m.names) && cache.get("MENUDATA/GSTYLES.MSB", m.styles) &&
		cache.get("MENUDATA/DIALOG.MIB", m.dialog) && cache.get("MENUDATA/LEGOMSC", m.music) &&
		cache.get("MENUDATA/GENC0R0.SBK", m.sounds) && cache.get("MENUDATA/LEGORACE.CRB", m.circuits) &&
		cache.get("MENUDATA/LEGORACE.RCB", m.races) && cache.get("MENUDATA/LEGOMAN.ADB", m.minifig) &&
		cache.get("MENUDATA/RSANIM.ADB", m.showcase) && cache.get("MENUDATA/PIECEDB/LPIECEHI.LEB", m.pieces) &&
		cache.get("MENUDATA/PIECEDB/CRSTMGR.LEB", m.sets) && cache.get("MENUDATA/PARTDB/BODYPART.PCB", m.parts) &&
		cache.get("MENUDATA/CBANIM.ADB", m.driverAnimations) && cache.get("MENUDATA/CMAMAN.ADB", m.photo);
	OriginalSave quick;
	ok = ok && cache.get("MENUDATA/QBUILD.LRS", quick);
	for (const OriginalSave::Record& r : quick.records) {
		MenuData::QuickBuild& q = m.quickBuilds.emplace_back();
		q.name = r.name;
		q.chassis = r.chassis;
		q.pieces = piecesOf(r);
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
	bool ok = loadCinema(cache, m, m.screens[ScreenLegal], "MENUDATA/LEGAL/", "splash1") &&
		loadCinema(cache, m, m.screens[ScreenCredits], "MENUDATA/CREDITS/", "credits") &&
		loadCinema(cache, m, m.screens[ScreenSingleRace], "MENUDATA/SINGRACE/", "pst");
	for (uint16_t n = 22; ok && n <= 37; n++) {
		if (n == ScreenTimeRace || n == 30) {
			continue; // (the time race's layout is the single race's)
		}
		const MenuLayout& layout = m.screens[n].layout;
		const auto view = std::find_if(layout.bindings.begin(), layout.bindings.end(),
			[](const MenuLayout::Binding& b) { return b.kind == MenuLayout::SceneReference; });
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
		d.name = r.name, d.looks = r.looks, d.expression = r.expression, d.stockCar = r.stockCar, d.chassis = r.chassis,
		d.trophies = r.trophies;
		d.pieces = piecesOf(r);
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
			if (entry >= 0 && textures.textures[entry].flip) {
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
		ok = cache.get(folder + "MENUTEXT.SRF", lang.text) && cache.get(folder + "CIRCUIT.SRF", lang.circuits) &&
			cache.get(race + "GAME.SRF", lang.game) && cache.get(race + "DRIVERS.SRF", lang.drivers) &&
			cache.get(folder + "GFONTS.FDB", list) && cache.get(race + "LEGOFNTS.FDB", raceList);
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
	if (!cache.get("MENUDATA/RS_SET/CRSDOW.SDB", nodes) || !cache.get("MENUDATA/RS_SET/CRSDOW.ADB", spin) ||
		!cache.get("MENUDATA/RS_SET/BLENDED.WDB", blended)) {
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
			if (inRange(blended.models, e.model) && upper(blended.models[e.model]) == "CRSDOW") {
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
	ok = ok && addImageList(cache, *this, "MENUDATA/GIMAGES.IDB", globalImages) &&
		addImageList(cache, *this, "GAMEDATA/COMMON/LEGOIMGS.IDB", hudImages);
	ok = ok && loadLayouts(cache, *this) && loadCinemas(cache, *this);
	// The race's: the drivers, the stock racers, each track's loading screen.
	ok = ok && loadRacers(cache, *this) && loadLoadingScreens(cache, *this) && loadLanguages(cache, *this);
	return ok && load3d(cache); // (after the screens: their views' worlds)
}

int16_t MenuData::image(std::string_view name, uint16_t screen) const
{
	static const std::vector<int16_t> None;
	for (const std::vector<int16_t>* list : {&globalImages, screen < screens.size() ? &screens[screen].images : &None}) {
		for (int16_t i : *list) {
			if (images[i].name == name) {
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

namespace {

// The font of that name among fonts [from, to); -1 none.
int16_t findFont(const std::vector<MenuData::Font>& fonts, size_t from, size_t to, std::string_view name)
{
	for (size_t i = from; i < to; i++) {
		if (fonts[i].name == name) {
			return int16_t(i);
		}
	}
	return -1;
}

} // namespace

int16_t MenuData::raceFont(size_t language, std::string_view name) const
{
	return findFont(languages[language].fonts, languages[language].raceFonts, languages[language].fonts.size(), name);
}

int16_t MenuData::font(size_t language, std::string_view name) const
{
	return findFont(languages[language].fonts, 0, languages[language].raceFonts, name);
}

} // namespace lr
