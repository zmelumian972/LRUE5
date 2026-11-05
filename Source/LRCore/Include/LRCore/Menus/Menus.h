#pragma once

// The front end: a stack of screen numbers (the names of MENUNAME.SRF), the top one built as a tree of widgets from
// its layout and the styles, input (a queue, key repeat, the mouse), up to two dialogs over it, fades, sounds, and what
// it draws: a list of 2D elements in the game's 640x480 screen and the 3D views among them. Screens leave by a rule
// that pops and pushes the stack; when the stack empties the menus end and a race starts.
//
// One frame: the widgets' timers, the queued input (the screen's handler, the focused widget, Tab/Up/Down, then down
// the tree), the mouse, then the top dialog's update or the screen's own (which may navigate: the new top is built at
// the frame's end), the fades, and the draw list.
//
// Widgets live in flat arrays (a tree per screen and per dialog), linked by index. Keys are the original's: source << 28
// | code, the keyboard's DirectInput scan codes (menuKey() maps the USB codes the presentation sends).

#include "LRCore/Common/Api.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Common/Game.h"
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
#include <span>
#include <string>
#include <vector>

namespace lr {

class Cache;
struct World;

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
		std::vector<Font> fonts; // GFONTS', then the race's (LEGOFNTS) from raceFonts on
		size_t raceFonts = 0;
	};
	// A cinematic's world database with what the core poses from it: its skeletons and
	// animations (the cameras' dummies, guy1), its material animations (the face tracks), its models' scales.
	struct CinemaWorld {
		SceneFile scene;
		std::vector<std::vector<SkeletonNode>> skeletons;
		std::vector<Animation> animations;
		std::vector<MaterialAnimationFile> materialAnimations;
		std::vector<float> modelScales;
	};
	struct Screen {
		MenuLayout layout;
		MenuStyles styles;
		std::vector<int16_t> images; // its image list's, in images
		CinemaFile cinema; // a cinematic screen's (or a setup screen's preview)
		std::array<std::vector<StringTable>, 9> cinemaText; // its string tables, by language
		std::string cinemaFolder; // "MENUDATA/C_AWARD1/"
		std::vector<CinemaWorld> cinemaWorlds; // the cinema's worlds, in its list's order
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
	std::vector<ImageInfo> images; // the global list's, then every screen's
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
	std::vector<QuickBuild> quickBuilds;
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
	std::vector<Sphere> hatBounds; // the hat ring's items' (the hat-only entries of BODYPART's hat names)
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

// An input event: a key (source << 28 | code) pressed or released.
struct MenuKey {
	uint32_t key = 0;
	bool down = false;
	bool repeat = false;
};

enum : uint32_t {
	SourceKeyboard = 1u << 28,
	SourceMouse = 2u << 28,
	SourcePad = 3u << 28, // every pad button is code 4 in the menus
	SourceAxis = 4u << 28, // 0 right, 1 left, 2 down, 3 up
	SourceCharacter = 7u << 28, // a typed character (for a text field), before its key
	// The keys the menus give a meaning.
	MenuEnter = SourceKeyboard | 0x1c,
	MenuEscape = SourceKeyboard | 0x01,
	MenuSpace = SourceKeyboard | 0x39,
	MenuTab = SourceKeyboard | 0x0f,
	MenuUp = SourceKeyboard | 0xc8,
	MenuDown = SourceKeyboard | 0xd0,
	MenuLeft = SourceKeyboard | 0xcb,
	MenuRight = SourceKeyboard | 0xcd,
	MenuShift = SourceKeyboard | 0x2a,
	MenuRightShift = SourceKeyboard | 0x36,
	MenuCtrl = SourceKeyboard | 0x1d,
	MenuRightCtrl = SourceKeyboard | 0x9d,
	MenuPadButton = SourcePad | 4,
};

// The keyboard's DirectInput code for a USB key code (0 if the menus don't know it).
LRCORE_API uint32_t menuKey(uint32_t usb);
// The character a USB key types into a text field (letters as capitals, digits, Enter, Backspace,
// Space, the US punctuation); 0 none. The presentation sends it (SourceCharacter) before the key, while a text field's
// screen (the license) is up.
LRCORE_API char16_t menuCharacter(uint32_t usb);

// What the presentation draws, in order, in 640x480 space.
struct MenuElement {
	enum Kind : uint8_t { Image, Glyphs, Quad, View3D, Triangle };
	Kind kind = Image;
	int16_t image = -1; // MenuData::images; a Glyphs' font (the language's)
	uint32_t colour = 0; // 0xAARRGGBB: an image's and the text's tint (alpha ignored), a quad's top-left
	std::array<float, 4> dest = {}; // x0 y0 x1 y1
	std::array<float, 4> source = {}; // u0 v0 u1 v1 in image pixels
	std::array<uint32_t, 3> corners = {}; // a quad's other colours: top-right, bottom-left, bottom-right
	int32_t alpha = -1; // a quad blended at this alpha (0..255); -1 opaque
	// Glyphs: the glyph quads (dest, source) and the text with its box, for a renderer that lays text out itself.
	uint32_t firstGlyph = 0, glyphCount = 0;
	std::u16string text;
	std::array<int32_t, 4> box = {}; // x y w h
	bool wrapped = false;
	bool scaled = false; // drawn at its own size times `scale` from (x, y), not fitted to the box
	std::array<float, 2> scale = {1, 1};
	int32_t widget = -1; // View3D: the scene view's (or ring's) widget id, and its scene in Menus::scenes
	int32_t scene = -1;
	// Triangle: its corners (x, y, u, v; u and v 0..1 of the image, or -1: flat), coloured `colour`, corners[0] and
	// corners[1] (blended by their alpha, in between interpolated), drawn inside the clip rect (left, top, right,
	// bottom).
	std::array<std::array<float, 4>, 3> vertices = {};
	std::array<int32_t, 4> clip = {0, 0, 640, 480};
};

// The device's lighting as the menus leave it: an ambient (0xAARRGGBB, alpha 0 none:
// the material's own ambient, `state` 255) and up to 7 directional lights (the way they travel, normalised; colour).
struct MenuLighting {
	uint32_t ambient = 0;
	std::vector<std::pair<Vec3, uint32_t>> lights;
	bool operator==(const MenuLighting&) const = default;
};

// What a 3D view shows, for the presentation to pose and draw: its world, camera and light, and what stands in it.
struct MenuScene {
	std::string world; // the world's name ("garage", "cbset"); empty for a ring
	// The camera (the world's first camera when it has one; a cinematic's from its camera stack):
	// eye, a point ahead, screen up; the lens (vertical, degrees; model units); aspect = the viewport's width / height.
	std::array<float, 3> eye = {}, target = {}, up = {0, 0, 1};
	float fov = 45, nearPlane = 1, farPlane = 1000, aspect = 4.0f / 3;
	bool camera = true; // false: a cinematic with no camera yet (nothing drawn)
	MenuLighting lighting; // what its models are lit with (a ring's items: per item, below)
	// A cinematic's (the world is its scene's name; a setup preview's too): its clock in scene frames and the cues
	// begun, newest first; the screen (its MenuData::Screen holds the cinema and its worlds).
	float time = 0;
	std::vector<uint16_t> cues;
	uint16_t cinemaScreen = 0;
	// A scene view's flags (MU): its world drawn; its folder's BLENDED.WDB drawn after it, its materials found by name in
	// the libraries its world loaded (the driver builder's spotlight cone).
	bool drawWorld = true, blendedWorld = false;
	float blendedFrame = 0; // the frame its blended world's animated entities play their part at (the turntable's clock)
	bool sliding = false; // a ring's items sliding between slots (it scrolls)
	struct Thing {
		enum Kind : uint8_t {
			Car, // Menus::carMesh, its chassis and the seated driver (at the chassis' driver mount)
			Piece, // a library piece: a ring's item (in its slot, spun), or the builder's held piece
			Minifig, // looks and expression; standing or seated; an animation part (CBANIM's, CMAMAN's) and its frame
			Part, // a driver ring's item: the ring (hat, face, torso, legs) and the part
			Mark, // a cell under the held piece where it hits something (one 1x1 quad, car frame)
			Chassis, // the menus' chassis model (`model`) under a car, playing its first part at `frame`
			Entity, // a cinematic's entity (cinema world `entity[0]`, animated `entity[1]` 1 or plain 0, index
			// `entity[2]`) at its cue's place, playing `animation` at `frame` (-1 at rest); `cue` its model cue
		};
		Kind kind = Car;
		std::array<float, 3> at = {};
		float angle = 0; // about z; a ring's item: its spin about the tilted axis
		int32_t type = 0, colour = 0, rotation = 0, slot = 0; // a piece's; a ring item's slot; a part's ring
		uint8_t alpha = 255;
		bool lit = false; // brighter (hovered)
		std::array<uint8_t, 5> looks = {}; // face, hat, legs, torso, expression
		bool seated = false;
		int32_t animation = -1;
		float frame = 0;
		// Where it is drawn: its world matrix, the model's scale in the rows (a car's mesh, a
		// mark's quad: scale 1; a ring item's and a minifig's: their models'). A minifig's nodes are posed from it by the
		// shared sampler (View.h) with its part file's part and frame.
		NodeMatrix world;
		enum PartFile : uint8_t { NoParts, Legoman, Showcase, Builder, Photo }; // LEGOMAN, RSANIM, CBANIM, CMAMAN
		PartFile parts = NoParts;
		std::string model; // a Part item's model (a ring's body part, or a hat's head library entry); a Chassis' model
		// A minifig's skeleton (its .SDB) and, in a cinematic or preview, its part file (guy1's
		// .ADB) in place of `parts`; its body (MENUPART's catalog body 0..4).
		std::string skeleton, partFile;
		int32_t body = 0;
		std::array<int32_t, 3> entity = {-1, -1, -1};
		int32_t cue = -1, cueMs = 0; // a cinematic thing's model cue and the ms it has run
		// A cinematic entity's model's shared material table as the material tracks left it (slot, material; the other
		// slots the model's own), and its colour transform while a colour effect runs on the
		// model (per channel red, green, blue: (byte >> shift) + offset, capped at 255).
		std::vector<std::pair<int32_t, std::string>> materials;
		bool coloured = false;
		std::array<int32_t, 3> colourShift = {}, colourOffset = {};
		// A ring item's lighting: ambient = primary, its one light's colour = secondary,
		// along the scene's first light.
		uint32_t primary = 0xffffff, secondary = 0xffffff;
	};
	std::vector<Thing> things;
};

struct MenuGlyph {
	std::array<int32_t, 4> dest;
	std::array<float, 4> source;
};

// A widget (one per element of a screen's tree; dialogs have their own trees).
struct Widget {
	enum Kind : uint8_t {
		Root,
		Region,
		Image,
		ImageButton,
		Label,
		TextButton,
		Frame,
		Quad,
		Selector,
		Carousel,
		Options,
		SceneView,
		SceneReference,
		Ring, // a 3D carousel: its items are models the scene draws
		Hotspot, // an image button with regions
		TextField,
	};
	enum Flag : uint8_t { Visible = 1, Focused = 2, DrawsItself = 4 };
	enum IconState : uint8_t { Enabled = 1, Selected = 2, Pressed = 4 };
	Kind kind = Region;
	uint8_t flags = Visible | DrawsItself;
	int32_t id = 0;
	MenuRect rect;
	uint32_t colour = 0xffffffff;
	float scaleX = 1, scaleY = 1;
	int32_t offsetX = 0, offsetY = 0;
	int16_t parent = -1, first = -1, last = -1, next = -1, focus = -1; // the tree; focus: the child on the focus path
	// An icon's (buttons, selectors, options rows, the root).
	bool icon = false;
	uint8_t state = 0; // IconState
	int16_t iconParent = -1, iconFirst = -1, iconLast = -1, iconNext = -1, selected = -1;
	uint32_t holding = 0; // the key holding it pressed
	std::array<int32_t, 5> sounds = {}; // selected, deselected, pressed, released (the bank's numbers)
	std::array<uint32_t, 6> stateColours = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
	// The kind's own.
	int16_t image = -1; // an image's (MenuData::images)
	std::array<int16_t, 6> images = {-1, -1, -1, -1, -1, -1}; // a button's state images
	std::array<uint32_t, 6> imageColours = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
	std::array<int16_t, 6> fonts = {-1, -1, -1, -1, -1, -1}; // a text button's
	std::u16string text; // a label's (upper case)
	int16_t font = -1;
	int32_t wrap = 0, maxWidth = 0;
	bool wrapping = false, centred = false; // the dialog's message wraps
	int16_t child = -1, label = -1; // a button's image; a text button's label
	std::array<int16_t, 2> buttons = {-1, -1}; // a selector's or options row's previous and next
	int16_t carousel = -1; // a selector's
	bool unselectedInput = false;
	std::array<uint32_t, 8> itemColours = {}; // a selector's style's item and focused-item colours (a 3D ring's lights)
	// A carousel's.
	int32_t index = 0; // the selection; an options row's value
	int32_t scrollMs = 0, scrollLeft = 0;
	float scrollSpeed = 0, scrollAt = 0;
	bool pageMode = false, wrapAround = false, scrolled = false;
	std::array<int32_t, 3> carouselSounds = {};
	// An options row's.
	int32_t steps = 0;
	std::array<int32_t, 2> stepSounds = {};
	int16_t track = -1, thumb = -1;
	bool grabbed = false; // its thumb is being dragged
	// A ring's items (the screen's values: parts, choices), its slots; a piece ring's spin.
	std::vector<int32_t> values;
	int32_t slots = 0, focusSlot = 0;
	std::vector<MenuRect> slotRects;
	float spin = 0, spinRate = 0;
	// A piece ring's items: made this frame (not turned before the ring's next tick); the spin their
	// places were recentred at, by the last step and the one before (-1: unturned, as made).
	bool fresh = false;
	float stepSpin = -1, fromSpin = -1;
	// A hotspot's regions (in its space) and the lit one (0 none); its highlight is `image`.
	std::vector<int32_t> regions;
	std::vector<MenuRect> regionRects;
	int32_t lit = 0;
	// A text field's: its longest text, its characters (MENUTEXT's), its sounds (typed, added, erased, refused), its caret.
	int32_t maxLength = 31, charset = -1;
	std::array<int32_t, 4> editSounds = {};
	int16_t caret = -1;
	// A blinking colour box: on and off times, the time to its next toggle, whether it is off now.
	int32_t blinkOn = 0, blinkOff = 0, blinkLeft = 0;
	bool blinkedOff = false;

	// 0 disabled, 2 normal, 4 selected; +1 pressed.
	uint8_t visualState() const { return uint8_t((state & Enabled ? (state & Selected ? 4 : 2) : 0) + (state & Pressed ? 1 : 0)); }
};

struct WidgetTree {
	std::vector<Widget> widgets;
	int16_t add(const Widget& w, int16_t parent); // appended last among the parent's children
	// A widget made without a parent made its child: the last, or after a sibling; and taken out again.
	void adopt(int16_t w, int16_t parent, int16_t after = -1);
	void detach(int16_t w);
	Widget& operator[](int16_t i) { return widgets[size_t(i)]; }
	const Widget& operator[](int16_t i) const { return widgets[size_t(i)]; }
	int16_t find(int32_t id) const; // the first widget with that id; -1 none
	int16_t focused() const; // the end of the focus path (the root if it alone has it); -1 none
};

struct MenuDialog {
	enum Phase : uint8_t { Opening, Open, Closing, Closed };
	enum Kind : uint8_t { TimedNotice, OneButton, YesNo };
	enum Result : uint8_t { Pending, TimedOut, Pressed, Yes, No }; // Pressed: a one-button dialog's button
	WidgetTree tree;
	Kind kind = TimedNotice;
	int32_t message = 0; // MENUTEXT's
	int32_t result = Pending;
	int32_t ageMs = 0, animMs = 0;
	Phase phase = Opening;
	float halfWidth = 0, halfHeight = 0;
	int16_t frame = -1, label = -1;
	std::array<int16_t, 2> buttons = {-1, -1};
};

// A cinematic screen's player: the scene's cues sorted by their start and end frames and the
// cursors through them, the cues begun (the presentation poses what they show), the 2D overlays they started (laid
// out at their first draw), the two fade slots, and this frame's effects started and stopped for the presentation
// (sounds, particles, colour flashes).
struct CinemaPlayer {
	std::vector<uint16_t> byStart, byEnd;
	size_t starts = 0, ends = 0;
	std::vector<uint8_t> active; // per cue
	std::vector<uint16_t> begun; // the active cues, newest first
	struct Overlay {
		bool shown = false, pending = false, disabled = false;
		std::array<float, 4> place = {}; // left, top, width, height: fractions of the screen
		int32_t width = 0, height = 0; // its content's
	};
	std::vector<Overlay> overlays; // per effect
	struct Fade {
		int16_t effect = -1;
		int32_t left = 0;
		bool expired = false;
	};
	std::array<Fade, 2> fades;
	std::vector<std::pair<int16_t, bool>> effects; // this frame's: started (true) or stopped
	bool first = true; // no update yet (a skippable screen is skippable after its first)
	// The 3D side: the scene playing (a preview's switches), its clock when the screen
	// doesn't keep it (previews: scaled ms and frame), each cue's entity (world, animated 1 / plain 0, index; -1 none)
	// and ms run, each world's entities' placements and animated entities' clocks, each world camera's pose as last
	// updated, the camera stack (world, camera), the ambient cue, the directional light cues (oldest first) and each
	// light cue's blink (lit, ms to its next change); the view drawn last.
	size_t scene = 0;
	uint32_t scaled = 0;
	int32_t frame = 0;
	struct Clock {
		int32_t part = -1;
		float time = 0;
		bool on = false;
	};
	struct Placed {
		Vec3 position;
		Basis rows;
	};
	struct View {
		Vec3 eye, forward{1, 0, 0}, up{0, 0, 1};
		float fov = 65, nearPlane = 1, farPlane = 100;
	};
	std::vector<std::array<int16_t, 3>> entityOf;
	std::vector<int32_t> cueMs;
	std::vector<std::array<std::vector<Placed>, 2>> placed; // per world: plain, animated
	std::vector<std::vector<Clock>> clocks; // per world, per animated entity
	std::vector<std::vector<View>> views; // per world, per camera
	std::vector<std::array<int16_t, 2>> cameras;
	int32_t ambient = -1;
	std::vector<uint16_t> lights;
	std::vector<std::pair<bool, int32_t>> blinks;
	bool viewed = false;
	View view;
	// The model cues' material tracks: each track's cursor (world, animation, track; the ms it
	// has stepped while bound, never rewound), and the models' shared material tables as the tracks wrote them (world,
	// model, slot; the material: it stays when the cue ends).
	struct Cursor {
		std::array<int16_t, 3> track;
		int32_t ms = 0;
	};
	std::vector<Cursor> cursors;
	struct Written {
		std::array<int16_t, 3> slot;
		std::string material;
	};
	std::vector<Written> written;
	// The colour effects running: the effect, the model it transforms (world, model; shared by
	// every entity drawing it), its offsets.
	struct Colouring {
		int16_t effect = -1;
		std::array<int16_t, 2> model = {-1, -1};
		std::array<float, 3> offsets = {};
	};
	std::vector<Colouring> colourings;
};

// The car builder: the piece held over the car, the view, and what's running. Busy while a piece
// drops into place or rises back into the hand.
struct CarBuilder {
	enum Mode : uint8_t { Browse = 1, CarView = 2, PieceView = 3, Busy = 6 };
	enum Running : uint8_t { ViewTurn = 1, ViewHeight = 2, PieceDrop = 4, PieceRise = 8 };
	Mode mode = Browse;
	uint8_t running = 0;
	// The held piece: the choice's piece, its unrotated size, its rotation and grid place (the low corner), the corner
	// it keeps (0 low x low y, 1 low x high y, 2 high x low y, 3 high x high y) and that corner on the grid.
	int32_t type = 0, colour = 0, part = 0;
	int32_t width = 0, height = 0, rotation = 0, x = 0, y = 0, anchor = 0, anchorX = 0, anchorY = 0;
	bool holding = false;
	// The marks under it: the highest top it covers; the cells it covers and those where it hits something.
	int32_t overlayHeight = 0;
	std::vector<std::array<int8_t, 2>> covered, hits;
	// Its heights (model units): bobbing over where it would land, the landing height, the drop's or rise's; the
	// bob's phase and time left, the 1 s clock its pulse runs on.
	enum Bob : uint8_t { Still, Lowering, Holding, Raising };
	Bob bob = Still;
	int32_t bobMs = 0, bobClock = 1000, dropMs = 0;
	float bobHeight = 0, restHeight = 0, dropHeight = 0;
	// The view: the slot (0..7) the move keys use and the turn to it (from, to, the angle shown, ms left); the camera's
	// height place (0..2), the one it comes from and the change's ms left of its whole.
	int32_t slot = 1, turnMs = 0;
	float turnFrom = 0, turnTo = 0, angle = 2.3553982f;
	// The height as a fraction of the places (what the eye is made from; the target place while a change runs, the
	// free height while the car is dragged) and the change's start.
	int32_t place = 0, placeMs = 0, placeTime = 300;
	float pitch = 0, pitchFrom = 0;
	// The mouse: the car view's free slot (-1 not turning freely), the hovered area (0 none, 1 the
	// held piece, 2 the car), the ms since the last click on the held piece, a piece-view drag's motion since its last
	// step and that step's ms; the held piece's and the car's screen rectangles, projected each frame; the car group's
	// cached sphere (the group standing at the origin; the car's mouse area: taken at the opening and at
	// each commit, held in between), the members' cached spheres it is made of (car, chassis, seated driver), and the
	// radius the camera's large-car blend uses (the group's as it was at the opening or the last height step).
	float freeSlot = -1;
	uint8_t pane = 0;
	int32_t clickMs = 1000, dragX = 0, dragY = 0, dragMs = 0;
	MenuRect pieceRect, carRect;
	Sphere group;
	std::array<Sphere, 3> members;
	int32_t memberCount = 0;
	float eyeRadius = 0;
	// The keys holding a selector's arrow down (Insert, Page Up / Delete, Page Down).
	uint32_t setKey = 0, pieceKey = 0;
};

struct LRCORE_API Menus {
	const MenuData* data = nullptr; // the files, shared
	Game* game = nullptr; // only during start() and frame(): what the screens change

	// The stack and the screen on top.
	std::array<uint16_t, 10> stack = {};
	uint8_t depth = 0;
	uint16_t screen = 0; // the built one
	WidgetTree tree;
	uint8_t language = 0; // the loaded one
	// Navigation: the destination chosen (-1 none: the screen's own rule; -2 a race).
	int32_t destination = -1;
	// What the program does next: quit, start the race the menus describe, show the presentation's video settings.
	bool quit = false, raceStart = false, videoSettings = false;
	// The screen's own state (what its kind uses).
	struct ScreenState {
		int32_t page = 0, nextPage = -1, afterNotice = -1; // options: the page the warning leads to
		// A cinematic's clock and player.
		uint32_t scaled = 0;
		int32_t cinemaFrame = 0, cinemaFrames = 0, rate = 30;
		bool complete = false, finished = false, skippable = false;
		CinemaPlayer cinema;
		// Its model swaps: whether it swaps, the minifig's looks, the racer whose car it shows.
		bool cinemaSwap = false;
		std::array<uint8_t, 5> cinemaLooks = {};
		RacerRecord cinemaRacer;
		// The main menu's timers and minifig.
		int32_t idleMs = 0, attractMs = 0;
		int32_t animPart = 0, animMs = 0;
		bool tapArmed = true, quitting = false;
		// The garage's racers, its cursor, the racer shown and the driver's idle animation.
		std::vector<RacerRef> racers;
		int32_t cursor = 0, shown = -2;
		int32_t driverPart = -1;
		float driverFrame = 0;
		// The controls screen's player, devices (binding sets) and the row waiting for a key.
		size_t player = 0;
		std::vector<uint8_t> entries;
		int32_t waiting = -1;
		std::array<int16_t, 9> rows = {};
		// Widgets the screen acts on.
		std::array<int16_t, 16> items = {};
		// The builders: the driver builder's queued animation and the exit's time, whether anything changed; the
		// license's expression; the car on edit car and the builder, the sets offered, the quick builds' cursor.
		int32_t queuedPart = -1, exitMs = -1;
		bool changed = false, saving = false; // saving: leaving by DONE / OK / FINISH (the save follows)
		uint8_t expression = 0;
		Car car;
		std::vector<int32_t> sets;
		int32_t quickBuild = 0;
		CarBuilder builder;
		// The race setup screens: the circuit shown, the preview's place in it, its timer, its scene (the preview
		// cinematic's) and mascot, its change mark and the screen's own fade slot; the racer count found at the open;
		// the time race's record panel (lap or race, its timer, the race whose record it shows).
		int32_t circuit = 0, place = 0, scene = -1, fade = -1, racersFound = 0;
		uint32_t previewMs = 0;
		bool previewChanged = false;
		std::string mascot;
		bool showingLap = true;
		int32_t toggleMs = 2500, recordRace = 0;
		// Racer select: the racer under the cursor, its models changed, the minifig playing (its part and frame are
		// the garage's driverPart, driverFrame), the "chosen" blend's ms (-1 none), the button whose stack change waits
		// for the minifig, the turntable's clock.
		RacerRef racer;
		bool racerChanged = false, animating = false, held = false; // held: a stack change waits for the minifig
		int32_t blendMs = -1, action = -1, spinMs = 0;
		bool oldEnded = false; // the part the chosen blend left had ended (racer select's leave gate)
	} state;
	// The car on edit car and the builder, as drawn (rebuilt when it changes); the builder's held piece (each frame).
	CarMesh carMesh, heldMesh;
	// Dialogs over the screen.
	std::array<MenuDialog, 2> dialogs;
	uint8_t dialogCount = 0;
	// Input.
	uint8_t queued = 0;
	std::vector<uint32_t> held; // keys held down
	bool shift = false, ctrl = false;
	int32_t cursorX = 0, cursorY = 0, mouseX = 0, mouseY = 0;
	uint32_t mouseButtons = 0;
	// Fades.
	struct Fade {
		int32_t durationMs = 0, leftMs = 0;
		bool in = true, active = false, ended = false;
		uint32_t colour = 0;
		MenuRect rect = {0, 0, 640, 480};
	};
	std::array<Fade, 2> fades;
	// Music: the menus' tune playing (MENUDATA/LEGOMSC's, -1 none) and whether it started this frame.
	int32_t tune = -1;
	bool tuneLoops = false, tuneStarted = false;
	// This frame's output.
	std::vector<MenuElement> elements;
	std::vector<MenuGlyph> glyphs;
	std::vector<MenuScene> scenes; // the View3D elements
	std::vector<int32_t> sounds; // the sound bank's numbers, from 1
	struct SaveFile {
		uint8_t slot;
		std::vector<uint8_t> bytes;
	};
	std::vector<SaveFile> saves; // our save files to write

	// The menus starting with one screen (at the boot: legal; after a race: the race's choice).
	void start(const MenuData& menuData, Game& game, uint16_t next, bool save = false);
	// One frame: dt ms, the frame's key events in order, the mouse's position (640x480) and buttons.
	void frame(Game& game, int32_t dt, std::span<const MenuKey> keys, int32_t mouseX, int32_t mouseY, uint32_t buttons);

	// Making widgets (Widgets.cpp): from a binding (by name) and a style; shown or hidden; an icon released; the icon
	// walk's first, last and next; a widget's rect on the screen.
	int16_t make(WidgetTree& t, const MenuLayout& layout, std::string_view binding, std::string_view style, int16_t parent, int32_t text = -1);
	void show(WidgetTree& t, int16_t w, bool visible);
	void release(WidgetTree& t, int16_t w, bool local, bool quiet, bool silent);
	int16_t first(WidgetTree& t, int16_t icon);
	int16_t last(WidgetTree& t, int16_t icon);
	int16_t next(WidgetTree& t, int16_t icon);
	MenuRect global(const WidgetTree& t, int16_t w) const;
	// Drawing (Draw.cpp): the frame, a widget and what it holds, an image, a line of text and its width, a text's box.
	void drawFrame();
	void draw(WidgetTree& t, int16_t w, MenuRect view, MenuRect screen);
	void drawImage(int16_t image, MenuRect dest, MenuRect source, const MenuRect* clip, uint32_t colour);
	void drawLine(int16_t font, std::u16string_view text, int32_t x, int32_t y, uint32_t colour);
	int32_t lineWidth(int16_t font, std::u16string_view line) const;
	MenuRect measure(int16_t font, std::u16string_view text, int32_t wrap) const;
	// The race's 2D (drawn by the menus' rules): its loading screen, and while it runs the standings, the fade
	// and the dialog.
	void drawLoading(std::string_view track);
	void drawRace(const World& world);
	// The HUDs, one layer per player's viewport: [first, end) of elements each.
	void drawHud(const World& world);
	std::array<std::pair<uint32_t, uint32_t>, 2> hudLayers = {};
	struct HudColumns; // the HUD's racing rows' columns (Draw.cpp)
	// Dialogs (Menus.cpp): opened by a screen, handed its buttons, then shown; the top one's tree, else the screen's.
	void openDialog(MenuDialog::Kind kind, int32_t message);
	WidgetTree& active() { return dialogCount ? dialogs[dialogCount - 1].tree : tree; }
	// The flow (Menus.cpp, Screens.cpp).
	void push(uint16_t n)
	{
		if (depth < stack.size()) {
			stack[depth++] = n;
		}
	}
	uint16_t top() const { return depth ? stack[depth - 1] : 0; }
	void build(Game& game);
	void leave(Game& game);
	// The cinematic screens (Cinema.cpp): the player started from the screen's scene, its update, an effect started or
	// stopped. The cinema's 3D (CinemaView.cpp): what the scene shows, an entity's nodes, the device's lighting.
	void startCinema(Game& game);
	void updateCinema(int32_t dt);
	void describeCinema(const CinemaPlayer& c, const MenuData::Screen& s, MenuScene& scene, const std::array<uint8_t, 5>* looks, const RacerRecord* car) const;
	void entityNodes(const MenuScene& scene, const MenuScene::Thing& t, std::vector<NodeMatrix>& out) const;
	MenuLighting lighting; // the device's, as the screens and views leave it
	CarMesh cinemaCar; // the car a cinematic swaps in for "carbody"
	void effect(size_t effect, bool start, int32_t cue = -1); // cue: the model cue whose link starts it (channels 0x54/0x55)
	// The builders (Builders.cpp): a scene view's scene; the builder's camera (shared by its draw and its mouse areas).
	void describeScene(const Widget& view, MenuScene& scene) const;
	void builderCamera(MenuScene& scene) const;
	struct RingSlots; // a ring's items' places (describeRing)
	// A piece carousel's item: the piece in its colour, centred; made once each.
	const CarMesh& carouselItem(int32_t type, int32_t colour) const;
	mutable std::vector<std::pair<uint32_t, CarMesh>> carouselItems;
	void play(int32_t sound)
	{
		if (sound > 0) {
			sounds.push_back(sound);
		}
	}
	int32_t startFade(int32_t ms, bool in, uint32_t colour, MenuRect rect = {0, 0, 640, 480}); // its slot (-1 none free)
	std::u16string_view text(int32_t index) const { return data->languages[language].text.at(size_t(index)); }

	// The input queue and its handling, the root widget, the selection's walk and the fades: the frame's parts the
	// data-free tests drive on their own.
	std::array<MenuKey, 32> queue;
	void queueInput(std::span<const MenuKey> keys, uint32_t buttons, int32_t dt);
	void stepFades(int32_t dt);
	int16_t makeRoot(WidgetTree& t);
	void moveSelection(WidgetTree& t, bool forward);

private:
	// What only the menus' own files use (Menus.cpp, Widgets.cpp, Draw.cpp, Screens.cpp, Cinema.cpp, CinemaView.cpp,
	// RaceScreens.cpp, Builders.cpp). Their state: a navigation pending (to destination), edit car's spin, the input
	// queue (queued: its length) and the key repeats, whether the cursor is drawn.
	bool pending = false;
	bool spinKept = false;
	float editSpin = 0; // where edit car's car starts turning (the builder's angle when it came back from it)
	float editFacing = 0; // edit car's car's facing on its last frame (the builder's opening group sphere takes it)
	std::array<int32_t, 2> repeatMs = {500, 500}; // keyboard, mouse
	bool cursorOn = false; // drawn (item screens)
	// The frame's parts: the frame's key and button events queued, then the repeats; the queue handled; the mouse's
	// motion; the fades.
	void queueKey(MenuKey k);
	void holdKey(uint32_t key, bool down);
	void dispatchKeys(Game& game);
	void moveMouse(int32_t dx, int32_t dy);
	// Making widgets: from MENUNAME's numbers; make's kinds (each widget w, its id, rect and colour set, made from its
	// binding and style, and its own children); their texts, items, layouts and values.
	int16_t makeNamed(WidgetTree& t, uint32_t binding, uint32_t style, int16_t parent, int32_t text = -1);
	int16_t makeImage(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, bool styleColour);
	int16_t makeLabel(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, int32_t text);
	int16_t makeFrame(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeImageButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeTextButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, int32_t text);
	int16_t makeSelector(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeOptions(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeSceneView(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, int16_t parent, int16_t attachTo);
	int16_t makeRing(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeHotspot(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeTextField(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	void setText(WidgetTree& t, int16_t w, std::u16string_view text);
	void setString(WidgetTree& t, int16_t w, int32_t text); // MENUTEXT's
	void relabel(WidgetTree& t, int16_t w, int32_t text); // and measured again
	void alignRight(WidgetTree& t, int16_t w, int32_t margin);
	void addItem(WidgetTree& t, int16_t carousel, int16_t item);
	void selectItem(WidgetTree& t, int16_t carousel, int32_t index); // at once
	void layoutCarousel(WidgetTree& t, int16_t carousel);
	void layoutFrame(WidgetTree& t, int16_t frame);
	void setValue(WidgetTree& t, int16_t row, int32_t value);
	void setCharset(WidgetTree& t, int16_t field, int32_t text); // a text field's (MENUTEXT's)
	// The icons: enabling, selecting, pressing; the selection's walk; keys and the cursor on widgets; the timers.
	void enable(WidgetTree& t, int16_t w, bool on);
	bool select(WidgetTree& t, int16_t w, bool quiet, bool local, bool silent);
	void deselect(WidgetTree& t, int16_t w, bool local, bool silent);
	void press(WidgetTree& t, int16_t w, bool local, bool quiet, bool silent);
	void recompute(WidgetTree& t, int16_t w);
	void focusOn(WidgetTree& t, int16_t w);
	void unfocus(WidgetTree& t, int16_t w);
	int16_t previous(WidgetTree& t, int16_t icon);
	bool keyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool stepperKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y); // selectors, option rows
	bool ringKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool textFieldKeyDown(WidgetTree& t, int16_t w, const MenuKey& key);
	bool hotspotKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool keyUp(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool routeKey(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool routeCursor(WidgetTree& t, int16_t w, int32_t x, int32_t y);
	bool cursorOver(WidgetTree& t, int16_t w, int32_t dx); // the mouse over an icon (dx: its motion, dragging a thumb)
	void stepSelector(WidgetTree& t, int16_t selector, bool forward);
	void stepRow(WidgetTree& t, int16_t row, bool forward);
	void tick(WidgetTree& t, int16_t w, int32_t dt);
	// Drawing's parts: an image stretched to its rect, a hotspot's lit region, a text field, a label, a wrapping
	// carousel's items again, the 3D views and a 3D view's element; text, quads.
	void drawStretched(Widget& n, MenuRect view, MenuRect screen);
	void drawLitRegion(const Widget& n, MenuRect view, MenuRect screen);
	void drawTextField(WidgetTree& t, const Widget& n, MenuRect view, MenuRect screen);
	void drawLabel(const Widget& n, MenuRect view, MenuRect screen);
	void drawWrapAround(WidgetTree& t, const Widget& n, MenuRect view, MenuRect screen);
	void drawView(WidgetTree& t, int16_t w);
	void addView(const Widget& n, std::array<float, 4> dest, const Widget* ring);
	void drawText(int16_t font, std::u16string_view text, MenuRect box, MenuRect clip, uint32_t colour);
	void drawScaled(int16_t font, std::u16string_view text, int32_t x, int32_t y, float sx, float sy, uint32_t colour);
	void drawWrapped(int16_t font, std::u16string_view text, int32_t x, int32_t y, int32_t wrap, bool centred, MenuRect clip, uint32_t colour);
	void drawQuad(MenuRect rect, uint32_t colour, int32_t alpha);
	// drawRace's parts: the race's standings, the circuit's, the dialog.
	void drawRaceStandings(const World& world);
	void drawCircuitStandings(const World& world);
	void drawRaceDialog(const World& world);
	// drawHud's parts: player p's banner (centred at x on row y), the racer's power-up frame, the speedometer, a map's dot, the
	// whole map and the rotating one, each in p's viewport.
	void drawHudBanner(const World& world, size_t p, int32_t x, int32_t y);
	void drawPowerFrame(const World& world, size_t racer, MenuRect view);
	void drawSpeedometer(const World& world, size_t p, MenuRect view);
	void drawMapDot(const World& world, int32_t x, int32_t y, bool red, MenuRect view);
	void drawFixedMap(const World& world, size_t p, const MenuData::Loading& track, MenuRect view);
	void drawRotatingMap(const World& world, size_t p, const MenuData::Loading& track, MenuRect view);
	// And the HUD's racing rows, at a player's columns, and a time drawn in them.
	void drawHudTimes(const World& world, size_t p, const HudColumns& at);
	int32_t hudTime(const HudColumns& at, int32_t t, int32_t y);
	void drawTriangle(int16_t image, const std::array<std::array<float, 4>, 3>& corners, const std::array<uint32_t, 3>& colours, MenuRect clip);
	// The dialogs' buttons, their showing, update and close (the top one's).
	void giveButton(uint32_t binding, uint32_t style, int32_t text);
	void showDialog(int32_t defaultButton);
	void updateDialog(int32_t dt);
	void closeDialog();
	// The flow (Menus.cpp, Screens.cpp): the navigation; build's parts (a screen item by MENUNAME's numbers, the banner, a
	// selector over a carousel and a label in it, and the screens built here); the screens' updates, keys and notices.
	void navigate(int32_t where);
	int16_t screenItem(uint32_t binding, uint32_t style, int32_t text);
	int16_t makeBanner(int32_t text);
	int16_t makeLabelSelector(uint32_t binding, uint32_t style, uint32_t box);
	int16_t addLabel(int16_t selector, std::u16string_view text);
	void buildStorage(Game& game);
	void buildMainMenu(Game& game);
	void buildOptions(Game& game);
	void buildControls(Game& game);
	void buildPickLanguage(Game& game);
	void leaveScreen();
	void tickScreen(int32_t dt);
	void updateScreen(Game& game, int32_t dt);
	void updateMainMenu(Game& game, int32_t dt);
	void updateGarage(Game& game);
	void leaveGarage(Game& game);
	bool screenKey(Game& game, const MenuKey& key);
	void notice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void mainMenuNotice(Game& game, int16_t w, int32_t what);
	void optionsNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void controlsNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void garageNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void dialogNotice(Game& game, MenuDialog& d);
	void showPage(int32_t page);
	void refreshControls(Game& game);
	void buildGarage(Game& game);
	void fillRacerNames(Game& game);
	void garagePage(int32_t page);
	void newRacerStep(Game& game);
	void idleAnimation(Game& game);
	// The cinematic screens: a cue begun or ended (its links fired), the overlays drawn, the award screens' flow. Their
	// 3D: the player made ready for a scene of a screen's cinema, a cue begun or ended and the cues' ticks, a preview's
	// update (looping, no links), a scene's lights.
	bool cinematicScreen() const;
	void cueChanged(size_t cue, bool begins);
	void cinemaStart3d(CinemaPlayer& c, const MenuData::Screen& s, size_t scene) const;
	void cue3d(CinemaPlayer& c, const MenuData::Screen& s, size_t cue, bool begins) const;
	void cueTicks(CinemaPlayer& c, const MenuData::Screen& s, int32_t ms) const;
	void playScene(CinemaPlayer& c, const MenuData::Screen& s, int32_t ms, int32_t ticks, bool loop, bool links);
	void cueWindow(CinemaPlayer& c, int32_t a, int32_t b);
	void updatePreview(int32_t dt);
	void lightScene(MenuScene& scene, const Widget* ring);
	void openLighting();
	void drawCinema();
	void leaveCinema(Game& game);
	// The race setup screens and racer select (RaceScreens.cpp).
	static bool raceScreen(uint16_t n) { return n == ScreenCircuitRace || n == ScreenSingleRace || n == ScreenTimeRace || n == ScreenRacerSelect || n == ScreenPlayer2Select; }
	void buildRaceSetup(Game& game);
	void buildRacerSelect(Game& game);
	void tickRaceScreens(int32_t dt);
	void updateRaceScreens(Game& game, int32_t dt);
	void raceScreenNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void leaveRaceScreen(Game& game);
	void chooseCircuit(Game& game, int32_t circuit);
	void showPlace(Game& game);
	void showRace(Game& game, int32_t race, int32_t theme);
	void refreshRecord(Game& game);
	void fillPickNames(Game& game);
	RacerRef stepRacer(const Game& game, RacerRef from, bool forward) const;
	void showRacer(Game& game);
	void startIdle(Game& game);
	// The builders (Builders.cpp).
	void buildDriver(Game& game);
	void buildLicense(Game& game);
	void buildEditCar(Game& game);
	void buildCarBuilder(Game& game);
	void tickBuilders(int32_t dt);
	void tickDrop(int32_t dt); // the car builder's held piece dropping or rising
	void updateBuilders(Game& game);
	bool builderKey(Game& game, const MenuKey& key);
	void builderNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	// Its parts: whether w is the screen's item i, a yes/no question, each builder's notices, a car builder pad's region.
	bool isItem(int16_t w, size_t i) const;
	void ask(int32_t message, int32_t yes, int32_t no);
	void driverNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void licenseNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void editCarNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void carBuilderNotice(Game& game, WidgetTree& t, int16_t w, int32_t what);
	void builderPad(bool movePad, int32_t region);
	void builderDialog(Game& game, MenuDialog& d);
	void leaveBuilder();
	void driverPick(Game& game);
	void changeChassis(Game& game, int32_t set);
	void carChanged(Game& game);
	void editCarButtons(Game& game);
	RacerRecord* stored(Game& game);
	void editCarAction(Game& game, int32_t action);
	void takeChoice();
	void heldChanged(bool chooseAnchor);
	void commit(Game& game);
	void undo(Game& game);
	void moveHeld(int32_t direction, bool refusalSound);
	void turnHeld();
	void turnView(int32_t step);
	void raiseView(int32_t step);
	// The builder by mouse: the two areas placed by projecting the held piece's and the car's bounds
	// with the scene's camera, the views a click enters and leaves, a drag's motion.
	void placeMouseAreas();
	bool builderMouse(Game& game, const MenuKey& key);
	bool builderMotion(int32_t dx, int32_t dy);
	void enterView(CarBuilder::Mode mode);
	void leaveView();
	void carGroup(MenuScene& scene, Vec3 at, float angle, bool lit, const RacerRecord* racer = nullptr) const;
	// describeScene's parts: a ring's items (by its slots' places: the piece carousel's pieces, the driver builder's
	// parts), the world's camera, a minifig, the showcase, the main menu's champion, the car builder's car and piece.
	void describeRing(const Widget& view, const std::array<float, 9>& camera, MenuScene& scene) const;
	void pieceRing(const Widget& view, const RingSlots& slots, const CarSets::Set& set, MenuScene& scene) const;
	void partRing(const Widget& view, const RingSlots& slots, MenuScene& scene) const;
	void worldCamera(const MenuLayout::Binding& binding, MenuScene& scene) const;
	void sceneMinifig(MenuScene& scene, Vec3 at, Vec3 forward, Vec3 up, const std::array<uint8_t, 5>& looks, MenuScene::Thing::PartFile parts, int32_t animation,
		float frame) const;
	void showcaseScene(MenuScene& scene) const;
	void mainMenuScene(MenuScene& scene) const;
	void carBuilderScene(MenuScene& scene) const;
	const ChassisTable::Chassis* chassisOf(const std::vector<CarPiece>* pieces) const; // the builders' car's when null
	float chassisScale(const ChassisTable::Chassis* c) const;
	// The car group's cached sphere taken again: at the opening every member's afresh, the car and chassis at
	// edit car's facing, the seated driver at the mount through it; at a commit the driver's alone, at the angle shown.
	void takeGroupSphere(bool opening);
	// A minifig's catalog body (MENUPART's 0..3 by the torso's and legs' variants; 4 seated).
	int32_t bodyOf(const std::array<uint8_t, 5>& looks, bool seated) const;
	// The menus' tune, the save files.
	void playTune(int32_t number, bool loops);
	void stopTune() { tune = -1; }
	void saveSlots(Game& game, bool both);
};

// The draw list's 2D elements painted into a 640x480 target (0xAARRGGBB, rows top first) as the original's device
// draws them: pixel centres at +0.5, the top and left edges in, point sampling, keyed texels left
// out, texels times the colour; quads opaque or blended at their alpha. A pixel painted is opaque; one blended over a
// clear pixel keeps the quad's alpha (a layer over a 3D view). View3D elements paint nothing.
LRCORE_API void paint(const MenuData& data, size_t language, std::span<const MenuElement> elements, std::span<const MenuGlyph> glyphs, uint32_t* target);

// A driver table entry's minifig looks in a MenuScene::Thing's order (face, hat, legs, torso, expression).
LRCORE_API std::array<uint8_t, 5> driverLooks(const DriverTable::Driver& d);

// What a widget tells its screen.
enum MenuNotice : int32_t { NoticeSelected, NoticePressed, NoticeReleased, NoticeValue, NoticeSettled, NoticeHover };

} // namespace lr
