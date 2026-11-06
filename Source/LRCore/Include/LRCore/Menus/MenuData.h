#pragma once

// The menus' files (MENUDATA, the race's 2D and the menus' 3D), read once and shared, and the screen numbers this front
// end builds.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Race/Track.h"
#include "LRCore/Show/View.h"
#include "LRData/Cars.h"
#include "LRData/Look.h"
#include "LRData/Model.h"
#include "LRData/Menus.h"
#include "LRData/Pieces.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

class Cache;

// The menus' files, read once and shared.
struct LRCORE_API MenuData {
	struct ImageInfo {
		std::string name;
		uint32_t width = 0, height = 0;
		bool keyed = false;
		uint32_t key = 0;
		std::vector<uint8_t> rgba; // the pixels (paint() reads them)
	};
	struct Font {
		std::string name;
		FontGlyphs glyphs;
		int32_t spacing = 0;
		bool keyed = false;
		uint32_t key = 0;
		uint32_t width = 0; // its image's, and the pixels
		std::vector<uint8_t> rgba;
	};
	struct Language {
		StringTable text, circuits, game, drivers; // MENUTEXT, CIRCUIT; the race's GAME and DRIVERS
		List<Font> fonts; // GFONTS', then the race's (LEGOFNTS) from raceFonts on
		size_t raceFonts = 0;
	};
	// A cinematic's world database with what the core poses from it: its skeletons and
	// animations (the cameras' dummies, guy1), its material animations (the face tracks), its models' scales.
	struct CinemaWorld {
		SceneFile scene;
		List<std::vector<SkeletonNode>> skeletons;
		List<Animation> animations;
		List<MaterialAnimationFile> materialAnimations;
		List<float> modelScales;
	};
	struct Screen {
		MenuLayout layout;
		MenuStyles styles;
		std::vector<int16_t> images; // its image list's, in images
		CinemaFile cinema; // a cinematic screen's (or a setup screen's preview)
		std::array<List<StringTable>, 9> cinemaText; // its string tables, by language
		std::string cinemaFolder; // "MENUDATA/C_AWARD1/"
		List<CinemaWorld> cinemaWorlds; // the cinema's worlds, in its list's order
		// The material libraries its worlds load (each once), in load order with the texture list beside each (cache
		// paths): a model's or a .MAB frame's material resolves by name across them, newest first.
		struct Library {
			std::string materials, textures;
			MaterialLibrary library;
		};
		std::vector<Library> cinemaLibraries;
		const MaterialLibrary::Material* cinemaMaterial(std::string_view name, size_t* library = nullptr) const;
	};
	// A menu scene view's world: its first camera, first ambient and directional lights.
	struct MenuWorld {
		std::string name; // upper case, as the layouts name it
		std::string folder; // "MENUDATA/GARAGE/"
		bool hasCamera = false;
		SceneFile::Camera camera;
		bool lit = false; // any ambient or directional record
		uint32_t ambient = 0; // 0xAARRGGBB (alpha 0: none)
		std::vector<std::pair<Vec3, uint32_t>> lights; // direction (normalised), colour
	};
	std::vector<MenuWorld> menuWorlds;
	const MenuWorld* menuWorld(std::string_view name) const;
	// A track's loading screen and its dot image (in images).
	// A track's HUD map too: its image (in images, rows as drawn: -1 none) and bounds (min x,
	// max y, max x, min y).
	struct Loading {
		std::string track; // the folder: "RACEC0R1"
		LoadingScreen screen;
		int16_t tick = -1;
		int16_t map = -1;
		std::array<float, 4> mapBounds = {1023, 1023, -1023, -1023};
	};
	StringTable names; // MENUNAME.SRF: screens, bindings and styles by number
	std::array<Language, 9> languages;
	List<ImageInfo> images; // the global list's, then every screen's
	std::vector<int16_t> globalImages;
	std::vector<int16_t> hudImages; // the race's (GAMEDATA/COMMON/LEGOIMGS.IDB), in its order
	MenuStyles styles; // GSTYLES.MSB
	MenuLayout dialog; // DIALOG.MIB
	std::vector<Screen> screens; // by number
	NameList music, sounds;
	CircuitTable circuits;
	RaceTable races;
	DriverTable drivers;
	std::vector<RacerRecord> defaults; // DEFAULT.LRS's racers
	std::vector<Loading> loadings;
	AnimationFile minifig, showcase; // the main menu's and the garage's minifig animations
	// The builders': the HI piece library, the car sets, the driver parts, the quick-build cars, the driver builder's
	// minifig animations and the license photo's pose.
	PieceLibrary pieces;
	CarSets sets;
	PartCatalog parts;
	struct QuickBuild {
		std::u16string name;
		std::string chassis;
		std::vector<CarPiece> pieces;
	};
	List<QuickBuild> quickBuilds;
	AnimationFile driverAnimations, photo;
	// The menus' 3D: the menus' chassis table and each chassis model's first part bounds (scaled),
	// the minifig's skeleton, the bodies' head libraries (sets 0 and 1), the ring items' models' bounds (heads, chests,
	// hooked chests, legs, pegged legs), the hats', racer select's turntable (CRSDOW: its
	// skeleton and part, the entity's orientation) and the seated body's part bounds.
	ChassisTable menuChassis;
	ChampionTable champions; // GAMEDATA/COMMON/CHAMPS.CCB (the win car's champion's chassis)
	std::vector<Sphere> chassisBounds;
	std::vector<float> chassisScales; // each menus' chassis model's own .GDB scale (dcjmw 0.004129, bvbjmw 0.005479)
	std::vector<SkeletonNode> turntableNodes;
	Animation turntable;
	Basis turntableFrame;
	std::array<HeadLibrary, 2> heads;
	// The skeletons the menus' minifigs pose on: each catalog body's (MENUPART's rr, hr, rp, hp,
	// leg_box) by body index.
	std::array<std::vector<SkeletonNode>, 5> bodySkeletons;
	std::array<Sphere, 5> ringBounds;
	List<Sphere> hatBounds; // the hat ring's items' (the hat-only entries of BODYPART's hat names)
	Sphere seatedBounds;

	// From the data cache: false if a file is missing or malformed. Hands the core the executable's
	// tables (Tables.h).
	[[nodiscard]] bool load(const Cache& cache);
	[[nodiscard]] bool load3d(const Cache& cache);
	int16_t image(std::string_view name, uint16_t screen) const; // -1 none
	int16_t font(size_t language, std::string_view name) const;
	int16_t raceFont(size_t language, std::string_view name) const; // LEGOFNTS'
	const Loading* loading(std::string_view track) const;
};

// Screen numbers this front end builds (MENUNAME.SRF's).
enum Screen : uint16_t {
	ScreenMainMenu = 2,
	ScreenGarage = 3,
	ScreenOptions = 8,
	ScreenControl1 = 10,
	ScreenControl2 = 11,
	ScreenPickCard = 12,
	ScreenCarBuilder = 14,
	ScreenDriver = 15,
	ScreenLicense = 16,
	ScreenEditCar = 17,
	ScreenCircuitRace = 5,
	ScreenSingleRace = 6,
	ScreenRacerSelect = 19, // one player's
	ScreenPlayer2Select = 21,
	ScreenAward1 = 22, // 1st..3rd, then lost (25)
	ScreenWinCar = 26,
	ScreenRocketCar = 27,
	ScreenVeronicaCar = 28,
	ScreenTimeRace = 29,
	ScreenCircuitIntro = 31, // circuit 1's; 37 the Rocket Racer's
	ScreenCredits = 39,
	ScreenLegal = 40,
	ScreenPickLanguage = 44,
	ScreenLoadAll = 45,
	ScreenLoadCard = 46,
	ScreenOpenCard = 47,
	ScreenSaveAll = 48,
	ScreenSaveCard = 49,
	ScreenNewRacer = 144,
};

// A driver table entry's minifig looks in a MenuScene::Thing's order (face, hat, legs, torso, expression).
LRCORE_API std::array<uint8_t, 5> driverLooks(const DriverTable::Driver& d);

} // namespace lr
