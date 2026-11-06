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
// Menus holds the flow and the frame's output, and routes to its parts: Ui (the widget trees, dialogs and input), the
// screen families (Screens, RaceScreens, Builders, Cinema; each with the state its screens use, made afresh with each
// screen) and the race's HUD. The parts reach each other through the Menus they are handed.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Menus/Builders.h"
#include "LRCore/Menus/Cinema.h"
#include "LRCore/Menus/DrawList.h"
#include "LRCore/Menus/RaceHud.h"
#include "LRCore/Menus/MenuData.h"
#include "LRCore/Menus/RaceScreens.h"
#include "LRCore/Menus/Screens.h"
#include "LRCore/Menus/Widgets.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lr {

struct Pen;
struct World;

struct LRCORE_API Menus {
	const MenuData* data = nullptr; // the files, shared
	Game* game = nullptr; // what the screens change (start() and frame() bind it)

	// The stack and the screen on top (the built one) in the loaded language; the navigation chosen (pending, to the
	// destination: -1 the screen's own rule, -2 a race); what the program does next: quit, start the race the menus
	// describe, show the presentation's video settings.
	std::array<uint16_t, 10> stack = {};
	uint8_t depth = 0;
	uint16_t screen = 0;
	uint8_t language = 0;
	bool pending = false;
	int32_t destination = -1;
	bool quit = false, raceStart = false, videoSettings = false;
	// The widgets the built screen acts on (its items).
	std::array<int16_t, 16> items = {};

	// The parts.
	Ui ui{*this};
	Screens screens;
	RaceScreens race;
	Builders builders;
	Cinema cinema;
	RaceHud hud;

	// What the 3D views show besides their scenes: the car on racer select, edit car and the builder (rebuilt when it
	// changes), the builder's held piece (each frame), the car a cinematic swaps in for "carbody",
	// the device's lighting as the screens and views leave it.
	CarMesh carMesh, heldMesh, cinemaCar;
	MenuLighting lighting;
	// Fades (two slots).
	struct Fade {
		int32_t durationMs = 0, leftMs = 0;
		bool in = true, active = false, ended = false;
		uint32_t colour = 0;
		MenuRect rect = ScreenRect;
	};
	Array<Fade, 2> fades;
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

	Menus() = default;
	Menus(const Menus&) = delete; // (its parts hold it)
	Menus& operator=(const Menus&) = delete;

	// The menus starting with one screen (at the boot: legal; after a race: the race's choice).
	void start(const MenuData& menuData, Game& game, uint16_t next, bool save = false);
	// One frame: dt ms, the frame's key events in order, the mouse's position (640x480) and buttons.
	void frame(Game& game, int32_t dt, std::span<const MenuKey> keys, int32_t mouseX, int32_t mouseY, uint32_t buttons);
	// The top screen built; the frame drawn (Draw.cpp). The race's 2D (RaceDraw.cpp, by the menus' rules): its
	// loading screen, and while it runs the HUD, the standings, the fades and the dialog.
	void build();
	void drawFrame();
	void drawLoading(std::string_view track);
	void drawRace(const World& world);

	// What the parts share. The flow: the stack pushed and popped, a navigation chosen, whether w is the screen's item i,
	// the race setup screens and racer select; a widget's notice to its screen. The output: a sound, a fade started and
	// stepped (its slot, -1 none free), the tune, the save files; a pen on the draw list; MENUTEXT's text.
	void push(uint16_t n)
	{
		if (depth < stack.size()) {
			stack[depth++] = n;
		}
	}
	uint16_t top() const { return depth ? stack[depth - 1] : 0; }
	void pop(int32_t n) { depth = uint8_t(depth > n ? depth - n : 0); }
	void navigate(int32_t where);
	bool isItem(int16_t w, size_t i) const { return items[i] >= 0 && w == items[i]; }
	static bool raceScreen(uint16_t n)
	{
		return n == ScreenCircuitRace || n == ScreenSingleRace || n == ScreenTimeRace || n == ScreenRacerSelect || n == ScreenPlayer2Select;
	}
	bool cinematicScreen() const;
	void notice(WidgetTree& t, int16_t w, int32_t what);
	void play(int32_t sound)
	{
		if (sound > 0) {
			sounds.push_back(sound);
		}
	}
	int32_t startFade(int32_t ms, bool in, uint32_t colour, MenuRect rect = ScreenRect);
	void stepFades(int32_t dt);
	void playTune(int32_t number, bool loops);
	void stopTune() { tune = -1; }
	void saveSlots(bool both);
	Pen pen();
	std::u16string_view text(int32_t index) const { return data->languages[language].text.at(size_t(index)); }

	// The 3D views (Scenes.cpp): a scene view's scene; a ring's lights; the shared pieces the screens' scenes are made
	// of: the car group (the car's mesh, the chassis model, in the builders the seated minifig), a
	// minifig, a car's chassis (the builders' car's when null) and its model's scale, a minifig's catalog body
	// (MENUPART's 0..3 by the torso's and legs' variants; 4 seated); a piece carousel's item (the piece in
	// its colour, centred; made once each).
	void describeScene(const Widget& view, MenuScene& scene) const;
	void lightScene(MenuScene& scene, const Widget* ring);
	void carGroup(MenuScene& scene, Vec3 at, float angle, bool lit, const RacerRecord* racer = nullptr) const;
	void sceneMinifig(MenuScene& scene, Vec3 at, Vec3 forward, Vec3 up, const std::array<uint8_t, 5>& looks,
		MenuScene::Thing::PartFile parts, int32_t animation, float frame) const;
	const ChassisTable::Chassis* chassisOf(const std::vector<CarPiece>* pieces) const;
	float chassisScale(const ChassisTable::Chassis* c) const;
	int32_t bodyOf(const std::array<uint8_t, 5>& looks, bool seated) const;
	const CarMesh& carouselItem(int32_t type, int32_t colour) const;

private:
	mutable std::vector<std::pair<uint32_t, CarMesh>> m_carouselItems;
	// The frame's parts: the queue handled, the mouse's motion; the screen's timers, update, leave rule and its own look
	// at a key; a dialog's answer.
	void dispatchKeys();
	void moveMouse(int32_t dx, int32_t dy);
	void tickScreen(int32_t dt);
	void updateScreen(int32_t dt);
	void leave();
	void leaveScreen();
	bool screenKey(const MenuKey& key);
	void dialogNotice(MenuDialog& d);
	// describeScene's parts: the world's camera, the showcase (garage, racer select), the lights a screen opens with.
	void worldCamera(const MenuLayout::Binding& binding, MenuScene& scene) const;
	void showcaseScene(MenuScene& scene) const;
	void openLighting();
};

} // namespace lr
