#pragma once

// The builders: the driver builder, the license, edit car and the car builder; what each builds, does
// each frame and with its items, keys and the mouse, and what its 3D views show (Builders.cpp, CarBuilder.cpp,
// BuilderScenes.cpp). The car's own rules are Car's.

#include "LRCore/Common/Game.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Menus/DrawList.h"
#include "LRCore/Menus/Widgets.h"
#include "LRData/Cars.h"
#include "LRData/Menus.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lr {

struct Menus;

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
	Array<Sphere, 3> members;
	int32_t memberCount = 0;
	float eyeRadius = 0;
	// The keys holding a selector's arrow down (Insert, Page Up / Delete, Page Down).
	uint32_t setKey = 0, pieceKey = 0;
};

struct LRCORE_API Builders {
	// The screen's state (made afresh with each screen): the driver builder's queued animation, the exit's time, the
	// minifig's part and frame, whether anything changed; the license's expression; the car on edit car and the builder,
	// the sets offered, the quick builds' cursor, the car builder; what edit car's warning leads to; the screen's clock
	// (the photo's pose, the car's spin).
	struct State {
		int32_t queuedPart = -1, exitMs = -1;
		int32_t driverPart = -1;
		float driverFrame = 0;
		bool changed = false, saving = false; // saving: leaving by DONE / OK / FINISH (the save follows)
		uint8_t expression = 0;
		Car car;
		List<int32_t> sets;
		int32_t quickBuild = 0;
		CarBuilder builder;
		int32_t afterNotice = -1, animMs = 0;
	} state;

	// Building each screen; each frame's clocks (tickDrop: the held piece dropping or rising) and update; the keys the
	// car builder takes before the widgets, and its mouse; the items' notices and the dialogs' answers; the rule they
	// leave by.
	void buildDriver(Menus& m);
	void buildLicense(Menus& m);
	void buildEditCar(Menus& m);
	void buildCarBuilder(Menus& m);
	void tickBuilders(Menus& m, int32_t dt);
	void tickDrop(Menus& m, int32_t dt);
	void updateBuilders(Menus& m);
	bool builderKey(Menus& m, const MenuKey& key);
	bool builderMotion(Menus& m, int32_t dx, int32_t dy);
	void builderNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void builderDialog(Menus& m, MenuDialog& d);
	void leaveBuilder(Menus& m);
	// The 3D views: a builder's scene view (the driver builder's minifig, the license's photo, edit car's car, the car
	// builder's car and piece), a ring's items, the car builder's camera (shared by its draw and its mouse areas).
	void describeScene(const Menus& m, MenuScene& scene) const;
	void describeRing(const Menus& m, const Widget& view, const std::array<float, 9>& camera, MenuScene& scene) const;
	void builderCamera(const Menus& m, MenuScene& scene) const;

private:
	// Edit car's car: where it starts turning (kept: the builder's angle when it came back from there), its facing on
	// its last frame (the builder's opening group sphere takes it).
	bool m_spinKept = false;
	float m_editSpin = 0, m_editFacing = 0;
	struct RingSlots; // a ring's items' places (describeRing)
	// Their parts: back to the garage (asked first when changes would be lost), each builder's notices, a car builder
	// pad's region; the driver's next animation; edit car's chassis, car and buttons; the racer edited, as stored.
	void backToGarage(Menus& m, bool changed);
	void driverNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void licenseNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void editCarNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void carBuilderNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what);
	void builderPad(Menus& m, bool movePad, int32_t region);
	void driverPick(Menus& m);
	void changeChassis(Menus& m, int32_t set);
	void carChanged(Menus& m);
	void editCarButtons(Menus& m);
	RacerRecord* stored(Menus& m);
	void editCarAction(Menus& m, int32_t action);
	// The car builder's: the set selector's set; the held piece taken, changed, placed, taken back, moved and turned;
	// the view turned and raised.
	const CarSets::Set& builderSet(const Menus& m) const;
	void takeChoice(Menus& m);
	void heldChanged(Menus& m, bool chooseAnchor);
	void commit(Menus& m);
	void undo(Menus& m);
	void moveHeld(Menus& m, int32_t direction, bool refusalSound);
	void turnHeld(Menus& m);
	void turnView(int32_t step);
	void raiseView(int32_t step);
	// The builder by mouse: the two areas placed by projecting the held piece's and the car's bounds
	// with the scene's camera, a click, the views a click enters and leaves.
	void placeMouseAreas(Menus& m);
	bool builderMouse(Menus& m, const MenuKey& key);
	void enterView(Menus& m, CarBuilder::Mode mode);
	void leaveView(Menus& m);
	// The car group's cached sphere taken again: at the opening every member's afresh, the car and chassis at
	// edit car's facing, the seated driver at the mount through it; at a commit the driver's alone, at the angle shown.
	void takeGroupSphere(Menus& m, bool opening);
	// describeRing's parts (by its slots' places: the piece carousel's pieces, the driver builder's parts); the car
	// builder's car and piece.
	void pieceRing(const Menus& m, const Widget& view, const RingSlots& slots, const CarSets::Set& set, MenuScene& scene) const;
	void partRing(const Menus& m, const Widget& view, const RingSlots& slots, MenuScene& scene) const;
	void carBuilderScene(const Menus& m, MenuScene& scene) const;
};

} // namespace lr
