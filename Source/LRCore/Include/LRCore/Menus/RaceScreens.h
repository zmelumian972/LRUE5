#pragma once

// The race setup screens and racer select, RaceScreens.cpp: circuit race, single race
// and time race, each a circuit carousel over a looping preview; the racers' strip with the chosen one's car and
// minifig.

#include "LRCore/Common/Game.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Menus/Cinema.h"
#include "LRCore/Menus/DrawList.h"
#include "LRCore/Menus/Widgets.h"

#include <cstdint>
#include <string>

namespace lr {

struct Menus;

struct LRCORE_API RaceScreens {
	// The screen's state (made afresh with each screen). The setup screens: the circuit shown, the preview's place in
	// it, its timer, its scene and player (the preview cinematic's) and mascot, its change mark and the screen's own
	// fade slot; the racer count found at the open; the time race's record panel (lap or race, its timer, the race whose
	// record it shows); the screen's clock. Racer select: the racer under the cursor, its models changed, the minifig
	// playing (its part and frame), the "chosen" blend's ms (-1 none), the button whose stack change waits for the
	// minifig, the turntable's clock, the racer's car.
	struct State {
		int32_t circuit = 0, place = 0, scene = -1, fade = -1, racersFound = 0;
		uint32_t previewMs = 0;
		bool previewChanged = false;
		CinemaPlayer preview;
		std::string mascot;
		bool showingLap = true;
		int32_t toggleMs = 2500, recordRace = 0, animMs = 0;
		RacerRef racer;
		bool racerChanged = false, animating = false, held = false; // held: a stack change waits for the minifig
		int32_t driverPart = -1;
		float driverFrame = 0;
		int32_t blendMs = -1, action = -1, spinMs = 0;
		bool oldEnded = false; // the part the chosen blend left had ended (racer select's leave gate)
		Car car;
	} state;

	void buildRaceSetup(Menus& m);
	void buildRacerSelect(Menus& m);
	// Each frame: the clocks (with the widgets'), the screen's update, the preview's; the items' notices; the way out.
	void tickRaceScreens(Menus& m, int32_t dt);
	void updateRaceScreens(Menus& m, int32_t dt);
	void updatePreview(Menus& m, int32_t dt);
	void raceScreenNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void leaveRaceScreen(Menus& m);
	// The setup screens' parts: a circuit chosen, the preview's place and race shown, the time race's record.
	void chooseCircuit(Menus& m, int32_t circuit);
	void showPlace(Menus& m);
	void showRace(Menus& m, int32_t race, int32_t theme);
	void refreshRecord(Menus& m);
	// Racer select's: the strip's names, the racer before or after one, the racer's car shown, an idle animation.
	void fillPickNames(Menus& m);
	RacerRef stepRacer(const Menus& m, RacerRef from, bool forward) const;
	void showRacer(Menus& m);
	void startIdle(Menus& m);
};

} // namespace lr
