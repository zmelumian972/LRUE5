#pragma once

// What the builders' files share (Builders.cpp, CarBuilder.cpp, BuilderScenes.cpp, Scenes.cpp): the keys they take,
// each builder's items, the sets' images, and the rules for the car builder's view, its held piece and the things the
// builders' scenes place.

#include "LRCore/Common/Game.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Common/Tables.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Menus/Builders.h"
#include "LRCore/Menus/Widgets.h"
#include "LRData/Cars.h"
#include "Menus/MenuNames.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lr {

// The keyboard's (DirectInput codes).
enum : uint32_t {
	KeyA = SourceKeyboard | 0x1e,
	KeyD = SourceKeyboard | 0x20,
	KeyS = SourceKeyboard | 0x1f,
	KeyX = SourceKeyboard | 0x2d,
	KeyPlus = SourceKeyboard | 0x4e, // keypad +
	KeyMinus = SourceKeyboard | 0x4a, // keypad -
	KeyInsert = SourceKeyboard | 0xd2,
	KeyPageUp = SourceKeyboard | 0xc9,
	KeyDelete = SourceKeyboard | 0xd3,
	KeyPageDown = SourceKeyboard | 0xd1,
};

// The driver's rings, top to bottom, and where each keeps its part in a racer's looks (face, hat, legs, torso).
inline constexpr size_t RingLook[4] = {1, 0, 3, 2};

// Each builder's widgets in the screen's items (the driver builder's rings are items 0..3).
inline constexpr size_t DriverTopRing = 0, DriverMix = 4, DriverDone = 5, DriverCancel = 6;
inline constexpr size_t LicenseSnapshot = 0, LicenseNext = 1, LicenseBack = 2, LicenseName = 3, LicenseNameBox = 4;
inline constexpr size_t EditBuild = 0, EditRemoveBricks = 1, EditQuickBuild = 2, EditDone = 3, EditBack = 4, EditChassis = 5;
inline constexpr size_t BuilderSets = 0, BuilderPieces = 1, BuilderMovePad = 2, BuilderTurn = 3, BuilderAdd = 4, BuilderUndo = 5,
						BuilderViewPad = 6, BuilderBack = 7, BuilderPieceArea = 8, BuilderCarArea = 9;
inline const char* const DriverIdle[8] = {"breath1", "breath2", "sway", "check", "look", "kick", "watch", "hips"};

// The edit car's and builder's set images, by set.
inline const char* const SetImages[12] = {"bricks", "race", "space", "castle", "pirate", "islander", "magical", "adventur", "jungle",
	"alien", "rr", "vv"};

// A part's unlock value against the save: 0..2 always, 3..10 a part-unlock bit, 128 every time race won.
inline bool offered(uint8_t unlock, const Settings& s)
{
	return unlock <= 2 || (unlock <= 10 && (s.parts >> (unlock - 3) & 1)) || (unlock == 128 && (s.races & 0xfff) == 0xfff);
}

// A thing turned about z by an angle (its forward (cos a, -sin a, 0), as the builder draws the car), scaled.
inline Basis turned(float angle, float scale)
{
	const float c = std::cos(angle), s = std::sin(angle);
	return {Vec3{c, -s, 0} * scale, Vec3{s, c, 0} * scale, Vec3{0, 0, scale}};
}
// A thing facing forward with up, scaled.
inline Basis facing(Vec3 forward, Vec3 up, float scale) { return basisFromForwardUp(forward, up) * scale; }
inline constexpr float MinifigScale = 0.015625f; // the body models' scale

// The builder's camera: the large car's places (the small car's are the executable's). Its lens is
// GARAGE's "Cam1".
inline constexpr float LargeEyes[3][3] = {{0, -18, 10}, {0, -12, 17}, {0, -0.3f, 20}};

// `to` moved by a whole turn when it's more than half a turn from `from`: the shorter way there.
inline float shorterWay(float from, float to) { return to - from > Pi ? to - Tau : from - to > Pi ? to + Tau : to; }

// The game's cosine table read a quarter turn back (the sine), entry by entry (the drawn
// angle during a turn steps with it).
inline float tableSin(float theta) { return cosEntry(int32_t(0xffffff00u) - int32_t(theta * -162.97466f)); }

// The eight view slots' angles.
inline float slotAngle(int32_t slot)
{
	float a = 1.57f;
	for (int32_t i = 0; i < slot; i++) {
		a += 0.7853982f;
		if (a >= Tau) {
			a -= Tau;
		}
	}
	return a;
}

// The view turning from an angle to a slot's (the shorter way, over 150 ms).
inline void turnTo(CarBuilder& b, float from, int32_t slot)
{
	b.turnFrom = from;
	b.slot = slot;
	b.turnTo = shorterWay(from, slotAngle(slot));
	b.turnMs = 150;
	b.running |= CarBuilder::ViewTurn;
}

// The view's height changing to a place (over 300 ms).
inline void raiseTo(CarBuilder& b, int32_t place)
{
	b.pitchFrom = b.pitch;
	b.place = place;
	b.pitch = float(place);
	b.placeMs = b.placeTime = 300;
	b.running |= CarBuilder::ViewHeight;
}

// The held piece's extents on the grid's x and y (its size as turned).
inline std::array<int32_t, 2> extents(const CarBuilder& b)
{
	return b.rotation & 1 ? std::array{b.height, b.width} : std::array{b.width, b.height};
}

// A piece ring offering a set's first 30 choices, its items made again.
inline void offerChoices(Widget& ring, size_t choices)
{
	ring.values.clear();
	ring.fresh = true, ring.stepSpin = ring.fromSpin = -1;
	for (size_t i = 0; i < choices && i < 30; i++) {
		ring.values.push_back(int32_t(i));
	}
}

// The keypad's eight directions (keypad 8, 9, 6, 3, 2, 1, 4, 7); their grid steps from each view slot are the
// executable's table (ExecutableTables::builderStepX/Y).
inline int32_t keypadDirection(uint32_t key)
{
	static const uint32_t Codes[8] = {0x48, 0x49, 0x4d, 0x51, 0x50, 0x4f, 0x4b, 0x47};
	for (int32_t d = 0; d < 8; d++) {
		if (key == (SourceKeyboard | Codes[d])) {
			return d;
		}
	}
	return -1;
}

// The sets a car's screens offer: the four first, the unlocked ones, and any its pieces came from.
inline std::vector<int32_t> setsOffered(const CarSets& sets, const Settings& s, const Car& car)
{
	std::vector<int32_t> out;
	for (int32_t i = 0; i < int32_t(sets.sets.size()); i++) {
		bool used = false;
		for (const Car::Entry& e : car.entries) {
			used |= e.piece.part && sets.setOfPart(e.piece.part) == i;
		}
		if (i < 4 || (s.parts >> (i - 4) & 1) || used) {
			out.push_back(i);
		}
	}
	return out;
}

} // namespace lr
