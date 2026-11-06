#pragma once

// The front end's own screens (Screens.cpp): the main menu, options, controls, the language, the garage, the load and
// save steps and a new racer's card; what each builds, does each frame and with its items.

#include "LRCore/Common/Game.h"
#include "LRCore/Menus/DrawList.h"
#include "LRCore/Menus/Widgets.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

struct Menus;

struct LRCORE_API Screens {
	// The screen's state (made afresh with each screen): options' and the garage's page, the page asked for and the one
	// options' warning leads to; the main menu's timers and minifig, whether it quits; the garage's racers, its cursor,
	// the racer shown and the driver's idle animation; the controls screen's player, devices (binding sets), the row
	// waiting for a key and the rows.
	struct State {
		int32_t page = 0, nextPage = -1, afterNotice = -1;
		int32_t idleMs = 0, attractMs = 0;
		int32_t animPart = 0, animMs = 0;
		bool tapArmed = true, quitting = false;
		List<RacerRef> racers;
		int32_t cursor = 0, shown = -2;
		int32_t driverPart = -1;
		float driverFrame = 0;
		size_t player = 0;
		List<uint8_t> entries;
		int32_t waiting = -1;
		Array<int16_t, 9> rows = {};
	} state;

	// Building each screen.
	void buildStorage(Menus& m);
	void buildMainMenu(Menus& m);
	void buildOptions(Menus& m);
	void buildControls(Menus& m);
	void buildPickLanguage(Menus& m);
	void buildGarage(Menus& m);
	void newRacerStep(Menus& m);
	// Each frame: the main menu's, the garage's; the garage's way out.
	void updateMainMenu(Menus& m, int32_t dt);
	void updateGarage(Menus& m);
	void leaveGarage(Menus& m);
	// The items' notices.
	void mainMenuNotice(Menus& m, int16_t w, int32_t what);
	void optionsNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void controlsNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void garageNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	// Options' and the garage's pages, the controls' rows, the garage's names.
	void showPage(Menus& m, int32_t page);
	void refreshControls(Menus& m);
	void fillRacerNames(Menus& m);
	void garagePage(Menus& m, int32_t page);
	// The main menu's 3D view: the champion's minifig.
	void mainMenuScene(const Menus& m, MenuScene& scene) const;
};

// A random idle animation for the showcase's minifig (the garage's, racer select's), never the part playing: its part,
// from its first frame.
void showcaseIdle(const Menus& m, int32_t& part, float& frame);

} // namespace lr
