// The builders: the driver builder, the license, edit car and the car builder; what each builds, does
// each frame and with its items and keys, and the rule it leaves by. The car's own rules are Car's.

#include "LRCore/Menus/Menus.h"
#include "LRCore/Common/Tables.h"

#include "Show/Animation.h"
#include "Menus/MenuNames.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace lr {

namespace {

// MENUTEXT's.
enum : int32_t {
	TextCharset = 1,
	TextBuildDriver = 9,
	TextMakeLicense = 10,
	TextBuildCar = 11,
	TextFinish = 30,
	TextBuild = 37,
	TextMix = 56,
	TextPlayer = 58,
	TextSnapshot = 59,
	TextRemoveBricks = 60,
	TextQuickBuild = 61,
	TextLoseChanges = 123,
	TextCancelNewRacer = 119,
	TextBuildingLimit = 186,
};

// The keyboard's (DirectInput codes).
enum : uint32_t {
	KeyA = SourceKeyboard | 0x1e,
	KeyD = SourceKeyboard | 0x20,
	KeyS = SourceKeyboard | 0x1f,
	KeyX = SourceKeyboard | 0x2d,
	KeyPlus = SourceKeyboard | 0x4e,  // keypad +
	KeyMinus = SourceKeyboard | 0x4a, // keypad -
	KeyInsert = SourceKeyboard | 0xd2,
	KeyPageUp = SourceKeyboard | 0xc9,
	KeyDelete = SourceKeyboard | 0xd3,
	KeyPageDown = SourceKeyboard | 0xd1,
};

// The driver's rings, top to bottom, and where each keeps its part in a racer's looks (face, hat, legs, torso).
constexpr size_t RingLook[4] = {1, 0, 3, 2};
const char* const DriverIdle[8] = {"breath1", "breath2", "sway", "check", "look", "kick", "watch", "hips"};

// The edit car's and builder's set images, by set.
const char* const SetImages[12] = {"bricks", "race", "space", "castle", "pirate", "islander", "magical", "adventur", "jungle", "alien", "rr", "vv"};

// A part's unlock value against the save: 0..2 always, 3..10 a part-unlock bit, 128 every time race won.
bool offered(uint8_t unlock, const Settings& s)
{
	return unlock <= 2 || (unlock <= 10 && (s.parts >> (unlock - 3) & 1)) || (unlock == 128 && (s.races & 0xfff) == 0xfff);
}

// A thing turned about z by an angle (its forward (cos a, -sin a, 0), as the builder draws the car), scaled.
Basis turned(float angle, float scale)
{
	const float c = std::cos(angle), s = std::sin(angle);
	return {Vec3{c, -s, 0} * scale, Vec3{s, c, 0} * scale, Vec3{0, 0, scale}};
}
// A thing facing forward with up, scaled.
Basis facing(Vec3 forward, Vec3 up, float scale)
{
	const Basis b = basisFromForwardUp(forward, up);
	return {b.f * scale, b.s * scale, b.u * scale};
}
constexpr float MinifigScale = 0.015625f; // the body models' scale

std::string upperName(std::string s)
{
	for (char& ch : s) {
		ch = char(std::toupper(uint8_t(ch)));
	}
	return s;
}

} // namespace

// A driver table entry's minifig (its K(0x35)..K(0x39): hat, face, torso, legs, expression) in a thing's order
// (face, hat, legs, torso, expression).
std::array<uint8_t, 5> driverLooks(const DriverTable::Driver& d)
{
	return {uint8_t(d.looks[1]), uint8_t(d.looks[0]), uint8_t(d.looks[3]), uint8_t(d.looks[2]), uint8_t(d.looks[4])};
}

namespace {

// The builder's camera: the large car's places (the small car's are the executable's). Its lens is
// GARAGE's "Cam1".
constexpr float LargeEyes[3][3] = {{0, -18, 10}, {0, -12, 17}, {0, -0.3f, 20}};

// The game's cosine table read a quarter turn back (the sine), entry by entry (the drawn
// angle during a turn steps with it).
float tableSin(float theta)
{
	const int32_t i = (int32_t(0xffffff00u) - int32_t(theta * -162.97466f)) & 1023;
	return float(std::cos(double(i) * 6.283185307179586 / 1024.0));
}

// The eight view slots' angles.
float slotAngle(int32_t slot)
{
	float a = 1.57f;
	for (int32_t i = 0; i < slot; i++) {
		a += 0.78539819f;
		if (a >= 6.2831855f) {
			a -= 6.2831855f;
		}
	}
	return a;
}

// The keypad's eight directions (keypad 8, 9, 6, 3, 2, 1, 4, 7); their grid steps from each view slot are the
// executable's table (ExecutableTables::builderStepX/Y).
int32_t keypadDirection(uint32_t key)
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
std::vector<int32_t> setsOffered(const CarSets& sets, const Settings& s, const Car& car)
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

} // namespace

// Building each screen.

void Menus::buildDriver(Game& g)
{
	WidgetTree& t = tree;
	const MenuLayout& layout = data->screens[screen].layout;
	const bool editing = !g.newRacer;
	const RacerRecord* kept = stored(g);
	make(t, layout, "backdrp", "backdrp", -1);
	static const char* const Selectors[4] = {"hatsel", "facesel", "torsosel", "legsel"};
	for (size_t r = 0; r < 4; r++) {
		// Each ring offers the parts the stored racer wears and the ones unlocked, in the catalog's order.
		const int16_t sel = make(t, layout, Selectors[r], "partcont", -1);
		const int16_t ring = make(t, layout, r == 1 ? "headbox" : "partbox", "picker", sel);
		t.adopt(ring, sel);
		t[sel].carousel = ring;
		t[ring].colour = t[sel].colour;
		const std::vector<PartCatalog::Part>& parts = data->parts.rings[r];
		const uint8_t worn = kept ? kept->looks[RingLook[r]] : 0;
		for (int32_t i = 0; i < int32_t(parts.size()); i++) {
			if (i == worn || offered(parts[size_t(i)].unlock, g.settings)) {
				t[ring].values.push_back(i);
			}
			if (i == g.working.looks[RingLook[r]] && !t[ring].values.empty() && t[ring].values.back() == i) {
				t[ring].index = int32_t(t[ring].values.size()) - 1;
			}
		}
		state.items[r] = sel;
	}
	make(t, layout, "platform", "", -1);
	state.items[4] = make(t, layout, "mix", "nubutton", -1, TextMix);
	state.items[5] = make(t, layout, "gonext", editing ? "buttonch" : "buttonra", -1, editing ? TextOk : TextMakeLicense);
	state.items[6] = make(t, layout, "goback", "buttonca", -1, TextCancel);
	alignRight(t, make(t, layout, "banner", "banner", -1, TextBuildDriver), 20);
	select(t, state.items[0], true, false, true);
	driverPick(g);
}

void Menus::buildLicense(Game& g)
{
	WidgetTree& t = tree;
	const MenuLayout& layout = data->screens[screen].layout;
	const bool editing = !g.newRacer;
	make(t, layout, "backdrp", "backdrp", -1);
	state.items[0] = make(t, layout, "swapface", "nubutton", -1, TextSnapshot);
	state.items[1] = make(t, layout, "gonext", editing ? "buttonch" : "buttonra", -1, editing ? TextOk : TextBuildCar);
	state.items[2] = make(t, layout, "goback", editing ? "buttonca" : "buttonla", -1, editing ? TextCancel : TextBuildDriver);
	make(t, layout, "license", "license", -1);
	// The trophies won: gold, silver or bronze in each circuit's place.
	static const char* const Trophies[3] = {"gold", "silver", "bronze"};
	for (size_t k = 0; k < g.working.trophies.size(); k++) {
		if (const uint8_t trophy = g.working.trophies[k]; trophy >= 1 && trophy <= 3) {
			make(t, layout, "trophy" + std::to_string(k + 1), Trophies[trophy - 1], -1);
		}
	}
	state.items[4] = make(t, layout, "firstbox", "roundbox", -1);
	const int16_t field = make(t, layout, "ftext", "textedit", -1);
	state.items[3] = field;
	setCharset(t, field, TextCharset);
	setText(t, field, g.working.name.empty() ? text(TextPlayer) : std::u16string_view(g.working.name));
	make(t, layout, "platform", "", -1);
	alignRight(t, make(t, layout, "banner", "banner", -1, TextMakeLicense), 20);
	state.expression = g.working.expression;
	select(t, field, true, false, true);
}

void Menus::buildEditCar(Game& g)
{
	editSpin = std::exchange(spinKept, false) ? editSpin : 0;
	WidgetTree& t = tree;
	const MenuLayout& layout = data->screens[screen].layout;
	const bool editing = !g.newRacer;
	make(t, layout, "backdrp", "backdrp", -1);
	make(t, layout, "garage", "", -1);
	alignRight(t, make(t, layout, "banner", "banner", -1, TextBuildCar), 20);
	state.items[0] = make(t, layout, "build", "nubutton", -1, TextBuild);
	state.items[1] = make(t, layout, "ripapart", "nubutton", -1, TextRemoveBricks);
	state.items[2] = make(t, layout, "qbuild", "nubutton", -1, TextQuickBuild);
	state.items[3] = make(t, layout, "gonext", "buttonch", -1, editing ? TextOk : TextFinish);
	state.items[4] = make(t, layout, "goback", editing ? "buttonca" : "buttonla", -1, editing ? TextCancel : TextMakeLicense);
	const int16_t sel = make(t, layout, "selector", "imagecon", -1);
	const int16_t car = make(t, layout, "imagebox", "picker", sel);
	t[sel].carousel = car;
	t[car].colour = t[sel].colour;
	state.items[5] = sel;
	state.car.load(data->pieces, g.working.pieces);
	state.sets = setsOffered(data->sets, g.settings, state.car);
	for (int32_t set : state.sets) {
		addItem(t, car, make(t, layout, "setimage", SetImages[set], -1));
	}
	if (!state.car.entries.empty()) {
		state.car.chooseOffset(data->pieces);
		carMesh = state.car.menuMesh(data->pieces, data->sets.materials);
		const PieceLibrary::Piece* chassis = data->pieces.find(g.working.chassis);
		for (size_t i = 0; chassis && i < state.sets.size(); i++) {
			if (data->sets.sets[size_t(state.sets[i])].part == chassis->type) {
				selectItem(t, car, int32_t(i));
			}
		}
	}
	else {
		changeChassis(g, state.sets[0]); // a new racer's: the first set's (a draw for its colour)
	}
	editCarButtons(g);
	select(t, state.items[0], true, false, true);
}

void Menus::buildCarBuilder(Game& g)
{
	WidgetTree& t = tree;
	const MenuLayout& layout = data->screens[screen].layout;
	const int16_t backdrop = make(t, layout, "backdrp", "backdrp", -1);
	make(t, layout, "garage", "", -1);
	state.car.load(data->pieces, g.working.pieces);
	state.car.chooseOffset(data->pieces);
	carMesh = state.car.menuMesh(data->pieces, data->sets.materials);
	state.sets = setsOffered(data->sets, g.settings, state.car);
	const int16_t sets = make(t, layout, "sets", "imagecon", -1);
	const int16_t box = make(t, layout, "imagebox", "pieceset", sets);
	t[sets].carousel = box;
	t[box].colour = t[sets].colour;
	for (int32_t set : state.sets) {
		addItem(t, box, make(t, layout, "setimage", SetImages[set], -1));
	}
	const int16_t pieces = make(t, layout, "pieces", "partcont", -1);
	const int16_t ring = make(t, layout, "piecesel", "picker", pieces);
	t[pieces].carousel = ring;
	t[ring].colour = t[pieces].colour;
	state.items[0] = sets;
	state.items[1] = pieces;
	// The move pad, the rotate / place / undo buttons, the view pad, the held piece's and the car's mouse areas
	// (placed by the presentation over them: hidden here), the door.
	const auto at = [&](int16_t w, int32_t x, int32_t y) {
		Widget& n = t[w];
		n.rect = {x, y, x + n.rect.width(), y + n.rect.height()};
	};
	state.items[2] = make(t, layout, "procker", "procker", backdrop);
	at(state.items[2], 51, 40);
	state.items[3] = make(t, layout, "rotbrik", "icon", -1);
	state.items[4] = make(t, layout, "addbrik", "icon", -1);
	state.items[5] = make(t, layout, "undobrik", "icon", -1);
	state.items[6] = make(t, layout, "crocker", "crocker", backdrop);
	at(state.items[6], 51, 301);
	state.items[8] = make(t, layout, "piece", "icon", -1);
	state.items[9] = make(t, layout, "car", "icon", -1);
	show(t, state.items[8], false);
	show(t, state.items[9], false);
	state.items[7] = make(t, layout, "goback", "icon", -1);
	CarBuilder& b = state.builder;
	b = {};
	t[ring].values.clear();
	t[ring].fresh = true, t[ring].stepSpin = t[ring].fromSpin = -1;
	for (size_t i = 0; i < data->sets.sets[size_t(state.sets[0])].choices.size() && i < 30; i++) {
		t[ring].values.push_back(int32_t(i));
	}
	takeChoice();
	// The view: slot 1, the camera jumping to its middle height.
	b.place = 1;
	b.pitch = 1;
	b.placeMs = b.placeTime = 1;
	b.pitchFrom = 0;
	b.running = CarBuilder::ViewHeight;
	takeGroupSphere(true);
	b.eyeRadius = b.group.radius;
	select(t, sets, true, false, true);
}

// Each frame, before the input: the driver's animation, the builder's view and held piece.

void Menus::tickBuilders(int32_t dt)
{
	if (screen == ScreenLicense || screen == ScreenEditCar) {
		state.animMs += dt; // the photo's pose, the car's spin
	}
	if (screen == ScreenEditCar) {
		editFacing = std::fmod(editSpin + float(state.animMs) * 0.001f, 6.2831855f); // (as its scene draws it)
	}
	if (screen == ScreenDriver) {
		if (state.exitMs >= 0) {
			state.exitMs += dt;
		}
		else {
			state.driverFrame = advance(data->driverAnimations, state.driverPart, state.driverFrame, dt);
		}
		return;
	}
	if (screen != ScreenCarBuilder) {
		return;
	}
	CarBuilder& b = state.builder;
	const PieceLibrary& lib = data->pieces;
	b.clickMs = std::min(b.clickMs + dt, 100000);
	b.dragMs = std::min(b.dragMs + dt, 100000);
	// The view's turn and height change.
	if (b.running & CarBuilder::ViewTurn) {
		b.turnMs = std::max(0, b.turnMs - dt);
		const float e = tableSin(float(b.turnMs) / 150.0f * 1.5707964f);
		b.angle = b.turnTo + (b.turnFrom - b.turnTo) * e;
		if (b.turnMs == 0) {
			b.running &= uint8_t(~CarBuilder::ViewTurn);
		}
	}
	if (b.running & CarBuilder::ViewHeight) {
		if (b.placeMs == b.placeTime) {
			// (A height change's places take the group's cached radius as it is now, from its first tick on: the frame
			// the step was keyed still shows the old eye.)
			b.eyeRadius = b.group.radius;
		}
		b.placeMs = std::max(0, b.placeMs - dt);
		if (b.placeMs == 0) {
			b.running &= uint8_t(~CarBuilder::ViewHeight);
		}
	}
	b.bobClock = dt >= b.bobClock ? b.bobClock - dt + 1000 : b.bobClock - dt;
	// A drop ends (the piece placed) the frame after it reaches where it lands; a rise likewise at the hover height.
	if (b.running & (CarBuilder::PieceDrop | CarBuilder::PieceRise)) {
		const bool drop = b.running & CarBuilder::PieceDrop;
		if (drop ? b.dropHeight <= b.restHeight || b.dropMs >= 2500 : b.dropHeight >= 1.0f || b.dropMs >= 2000) {
			b.running &= uint8_t(~(CarBuilder::PieceDrop | CarBuilder::PieceRise));
			if (drop) {
				Game& g = *game;
				state.car.place(lib, {uint16_t(b.type), uint8_t(b.x), uint8_t(b.y), uint8_t(b.rotation), uint8_t(b.colour), uint16_t(b.part)}, g);
				carMesh = state.car.menuMesh(lib, data->sets.materials);
				if (state.car.status) {
					openDialog(1, TextBuildingLimit);
					giveButton(NameDialog, StyleCheck, TextOk);
					showDialog(0);
					state.car.remove(lib, state.car.entries.size() - 1);
					carMesh = state.car.menuMesh(lib, data->sets.materials);
				}
				else {
					play(10);
				}
				// (The piece carousel's items are made again, before its tick turns them.)
				Widget& ring = tree[tree[state.items[1]].carousel];
				ring.stepSpin = ring.fromSpin = -1;
				takeChoice();
			}
			b.mode = CarBuilder::Browse;
		}
		else {
			b.dropMs += dt;
			b.dropHeight = drop ? std::max(b.restHeight, b.dropHeight - 0.01f * float(dt)) : std::min(1.0f, b.dropHeight + 0.006f * float(dt));
		}
		return;
	}
	// The held piece bobs over where it would land while it fits; else it sits there.
	const PieceLibrary::Piece* p = lib.find(b.type);
	if (!p) {
		return;
	}
	const bool fits = state.car.test(lib, *p, b.x, b.y, b.rotation) >= 0;
	if (fits && b.bob == CarBuilder::Still) {
		b.bob = CarBuilder::Lowering;
		b.bobMs = 2500;
	}
	else if (!fits) {
		b.bob = CarBuilder::Still;
		b.bobHeight = b.restHeight;
	}
	switch (b.bob) {
	case CarBuilder::Lowering:
		if (dt >= b.bobMs) {
			b.bob = CarBuilder::Holding;
			b.bobMs = 0;
		}
		else {
			b.bobMs -= dt;
		}
		break;
	case CarBuilder::Holding:
	case CarBuilder::Raising:
		if (dt >= b.bobMs) {
			b.bobMs = 2500 - (dt - b.bobMs);
			b.bob = b.bob == CarBuilder::Holding ? CarBuilder::Raising : CarBuilder::Lowering;
		}
		else {
			b.bobMs -= dt;
		}
		break;
	default: break;
	}
	b.restHeight = float(b.overlayHeight) * 0.4f + (1.0f - 8.4f);
	switch (b.bob) {
	case CarBuilder::Lowering: {
		const float speed = std::clamp(std::trunc(b.bobHeight - b.restHeight) * 0.33333334f, 0.3f, 1.0f);
		b.bobHeight -= speed * 0.01f * float(dt);
		if (b.bobHeight <= b.restHeight) {
			b.bobHeight = b.restHeight;
			b.bobMs = 0;
		}
		break;
	}
	case CarBuilder::Raising:
		b.bobHeight += 0.01f * float(dt);
		if (b.bobHeight >= 1.0f - 2 * 1.2f) {
			b.bobHeight = 1.0f - 2 * 1.2f;
			b.bobMs = 0;
		}
		break;
	case CarBuilder::Still:
		b.bobMs = 2500;
		b.bobHeight = b.restHeight;
		break;
	default: break;
	}
}

// The screen's own update.

void Menus::updateBuilders(Game& g)
{
	if (screen == ScreenCarBuilder) {
		placeMouseAreas();
	}
	if (screen == ScreenLicense) {
		show(tree, state.items[4], tree[state.items[3]].state & Widget::Selected); // the name's frame marks it selected
	}
	if (screen == ScreenDriver) {
		if (state.exitMs >= 0) {
			if (partEnded(data->driverAnimations, state.driverPart, blendedFrame(data->driverAnimations, state.driverPart, state.exitMs, true))) {
				state.exitMs = -1;
				navigate(g.newRacer ? ScreenLicense : ScreenGarage);
			}
		}
		else if (partEnded(data->driverAnimations, state.driverPart, state.driverFrame) && !pending) {
			driverPick(g);
		}
	}
}

void Menus::driverPick(Game& g)
{
	// The part queued by a ring's change, else a random idle one; never the one that just played.
	int32_t part = state.queuedPart;
	state.queuedPart = -1;
	while (part < 0 || part == state.driverPart) {
		part = partIndex(data->driverAnimations, DriverIdle[g.draw() % 8]);
	}
	state.driverPart = part;
	state.driverFrame = 0;
}

RacerRecord* Menus::stored(Game& g)
{
	const RacerRef r = g.chosen[0];
	std::vector<RacerRecord>& list = g.list(r.list);
	return r.list != RacerList::None && r.list != RacerList::Defaults && r.index < list.size() ? &list[r.index] : nullptr;
}

// Edit car.

void Menus::editCarAction(Game& g, int32_t action)
{
	if (action == 1) {
		// REMOVE BRICKS: the bare chassis again.
		changeChassis(g, state.sets[size_t(tree[tree[state.items[5]].carousel].index)]);
		return;
	}
	// QUICK BUILD: the next stored car on this chassis, from the one after the cursor on.
	const auto& builds = data->quickBuilds;
	for (int32_t n = 0; n < 25 + 5 && !builds.empty(); n++) {
		state.quickBuild = (state.quickBuild + 1) % int32_t(builds.size());
		if (builds[size_t(state.quickBuild)].chassis == g.working.chassis) {
			state.car.load(data->pieces, builds[size_t(state.quickBuild)].pieces);
			g.working.stockCar = true;
			carChanged(g);
			return;
		}
	}
}

void Menus::changeChassis(Game& g, int32_t set)
{
	// The car is the set's bare chassis, in a random colour.
	const CarSets::Set& s = data->sets.sets[size_t(set)];
	state.car.clear();
	g.working.chassis = s.chassis.substr(0, 8);
	if (const PieceLibrary::Piece* p = data->pieces.find(s.chassis)) {
		state.car.place(data->pieces, {uint16_t(p->type), 0, 0, 0, 3, 0}, g);
	}
	g.working.stockCar = true;
	carChanged(g);
}

void Menus::carChanged(Game& g)
{
	g.working.pieces = state.car.pieces();
	state.car.chooseOffset(data->pieces);
	carMesh = state.car.menuMesh(data->pieces, data->sets.materials);
	editCarButtons(g);
}

void Menus::editCarButtons(Game& g)
{
	WidgetTree& t = tree;
	const bool bricks = state.car.entries.size() > 1;
	enable(t, state.items[5], !bricks || g.working.stockCar);
	enable(t, state.items[1], bricks);
}

// The car builder's held piece.

void Menus::takeChoice()
{
	// The held piece takes the piece selector's choice: the first time in the middle of the grid, after that keeping
	// the corner it was by (turned if it doesn't fit the grid as it was).
	CarBuilder& b = state.builder;
	const Widget& ring = tree[tree[state.items[1]].carousel];
	const CarSets::Set& set = data->sets.sets[size_t(state.sets[size_t(tree[tree[state.items[0]].carousel].index)])];
	const CarSets::Choice& choice = set.choices[size_t(ring.values[size_t(ring.index)])];
	const PieceLibrary::Piece* p = data->pieces.find(choice.type);
	if (!p) {
		return;
	}
	b.type = choice.type;
	b.colour = choice.colour;
	b.part = set.part;
	b.width = p->width;
	b.height = p->height;
	const auto extent = [&](bool y) { return (b.rotation & 1) != y ? b.height : b.width; };
	if (!b.holding) {
		b.anchor = 0;
		b.rotation = 0;
		b.x = (Car::Width - b.width) >> 1;
		b.y = (Car::Depth - b.height) >> 1;
		if (b.x < 0 || b.y < 0) {
			b.rotation = 1;
			b.x = (Car::Width - b.height) >> 1;
			b.y = (Car::Depth - b.width) >> 1;
		}
		b.holding = true;
	}
	else {
		if (b.rotation & 1 ? b.width > Car::Depth || b.height > Car::Width : b.width > Car::Width || b.height > Car::Depth) {
			b.rotation = (b.rotation + 1) & 3;
		}
		b.x = b.anchorX - (b.anchor & 2 ? extent(false) : 0);
		b.y = b.anchorY - (b.anchor & 1 ? extent(true) : 0);
		b.x = std::min(b.x, Car::Width - extent(false));
		b.y = std::min(b.y, Car::Depth - extent(true));
	}
	b.x = std::max(b.x, 0);
	b.y = std::max(b.y, 0);
	heldChanged(true);
}

void Menus::heldChanged(bool chooseAnchor)
{
	// The corner nearest the grid's edge the piece is by is kept (when chosen again); then the marks under it.
	CarBuilder& b = state.builder;
	const int32_t ex = b.rotation & 1 ? b.height : b.width, ey = b.rotation & 1 ? b.width : b.height;
	const auto point = [&] {
		b.anchorX = b.x + (b.anchor & 2 ? ex : 0);
		b.anchorY = b.y + (b.anchor & 1 ? ey : 0);
	};
	point();
	if (chooseAnchor) {
		b.anchor = (b.anchorX >= 5 ? 2 : 0) | (b.anchorY >= 3 ? 1 : 0);
		point();
	}
	const PieceLibrary& lib = data->pieces;
	const PieceLibrary::Piece& p = *lib.find(b.type);
	const Car& car = state.car;
	b.overlayHeight = 0;
	b.covered.clear();
	b.hits.clear();
	for (int32_t x = 0; x < ex; x++) {
		for (int32_t y = 0; y < ey; y++) {
			if (lib.cells[p.cellAt(x, y, b.rotation)].solid()) {
				b.covered.push_back({int8_t(b.x + x), int8_t(b.y + y)});
				b.overlayHeight = std::max<int32_t>(b.overlayHeight, car.cell(b.x + x, b.y + y).height);
			}
		}
	}
	b.overlayHeight = std::min(b.overlayHeight, int32_t(Car::HighestTop));
	// Where it hits something: everywhere when it has nothing to rest on.
	int32_t r = car.rest(lib, p, b.x, b.y, b.rotation, p.chassis());
	if (r >= 0 && car.collides(lib, p, b.x, b.y, b.rotation, r)) {
		r = car.rest(lib, p, b.x, b.y, b.rotation, false);
	}
	for (const auto& c : b.covered) {
		const PieceLibrary::Cell& cell = lib.cells[p.cellAt(c[0] - b.x, c[1] - b.y, b.rotation)];
		const Car::Cell& g = car.cell(c[0], c[1]);
		const int32_t above = g.height - r;
		if (r < 0 || above > cell.base() || (above == cell.base() && g.studded && !cell.rests && !cell.flush)) {
			b.hits.push_back(c);
		}
	}
}

void Menus::moveHeld(int32_t direction, bool refusalSound)
{
	CarBuilder& b = state.builder;
	const ExecutableTables& steps = executableTables();
	const int32_t dx = steps.builderStepX[b.slot][direction], dy = steps.builderStepY[b.slot][direction];
	const bool independent = direction & 1;
	const int32_t ex = b.rotation & 1 ? b.height : b.width, ey = b.rotation & 1 ? b.width : b.height;
	// One stud at a time; a step it can't take does nothing.
	const auto along = [&](int32_t d, bool y) {
		bool moved = false;
		for (int32_t i = 0; i < std::abs(d); i++) {
			int32_t& c = y ? b.y : b.x;
			const int32_t limit = (y ? Car::Depth : Car::Width) - (y ? ey : ex);
			if (d > 0 ? c < limit : c > 0) {
				c += d > 0 ? 1 : -1;
				moved = true;
				heldChanged(true);
			}
		}
		return moved;
	};
	bool moved = false;
	if (dx && dy && !independent) {
		// Both or neither. QUIRK: when x didn't move, it still steps back the other way.
		const bool x = along(dx, false);
		moved = x && along(dy, true);
		if (!moved) {
			along(-dx, false);
		}
	}
	else {
		const bool x = along(dx, false), y = along(dy, true);
		moved = x || y;
	}
	if (moved || refusalSound) {
		play(moved ? 16 : 8);
	}
}

void Menus::turnHeld()
{
	// A turn that wouldn't fit the grid is skipped; the corner kept stays where it was.
	CarBuilder& b = state.builder;
	if (b.rotation & 1 ? b.height > Car::Depth || b.width > Car::Width : b.width > Car::Depth || b.height > Car::Width) {
		b.rotation++;
	}
	b.rotation = (b.rotation + 1) & 3;
	const int32_t ex = b.rotation & 1 ? b.height : b.width, ey = b.rotation & 1 ? b.width : b.height;
	b.x = std::clamp(b.anchorX - (b.anchor & 2 ? ex : 0), 0, Car::Width - ex);
	b.y = std::clamp(b.anchorY - (b.anchor & 1 ? ey : 0), 0, Car::Depth - ey);
	heldChanged(false);
	play(22);
}

void Menus::turnView(int32_t step)
{
	CarBuilder& b = state.builder;
	if (b.running & CarBuilder::ViewTurn) {
		return;
	}
	b.turnFrom = slotAngle(b.slot);
	b.slot = (b.slot + step + 8) & 7;
	b.turnTo = slotAngle(b.slot);
	if (b.turnTo - b.turnFrom > 3.1415927f) {
		b.turnTo -= 6.2831855f;
	}
	else if (b.turnFrom - b.turnTo > 3.1415927f) {
		b.turnTo += 6.2831855f;
	}
	b.turnMs = 150;
	b.running |= CarBuilder::ViewTurn;
}

void Menus::raiseView(int32_t step)
{
	CarBuilder& b = state.builder;
	if ((b.running & CarBuilder::ViewHeight) || b.place + step < 0 || b.place + step > 2) {
		return;
	}
	b.pitchFrom = b.pitch;
	b.place += step;
	b.pitch = float(b.place);
	b.placeMs = b.placeTime = 300;
	b.running |= CarBuilder::ViewHeight;
}

void Menus::commit(Game& g)
{
	CarBuilder& b = state.builder;
	const PieceLibrary::Piece* p = data->pieces.find(b.type);
	const int32_t fit = p ? state.car.test(data->pieces, *p, b.x, b.y, b.rotation) : Car::NothingBelow;
	if (fit == Car::TooMany) {
		openDialog(1, TextBuildingLimit);
		giveButton(NameDialog, StyleCheck, TextOk);
		showDialog(0);
		return;
	}
	if (fit < 0) {
		play(18);
		return;
	}
	g.working.stockCar = false;
	takeGroupSphere(false); // (before the piece goes into the car)
	b.dropHeight = 1.0f;
	b.dropMs = 0;
	b.running |= CarBuilder::PieceDrop;
	b.mode = CarBuilder::Busy;
}

void Menus::undo(Game& g)
{
	CarBuilder& b = state.builder;
	const PieceLibrary& lib = data->pieces;
	if (state.car.entries.size() <= 1) {
		play(18);
		return;
	}
	const CarPiece last = state.car.entries.back().piece;
	state.car.remove(lib, state.car.entries.size() - 1);
	carMesh = state.car.menuMesh(lib, data->sets.materials);
	// It comes back to the hand where it was, in its set.
	WidgetTree& t = tree;
	const int32_t set = data->sets.setOfPart(last.part);
	const auto it = std::find(state.sets.begin(), state.sets.end(), set);
	if (last.part && it != state.sets.end()) {
		Widget& ring = t[t[state.items[1]].carousel];
		selectItem(t, t[state.items[0]].carousel, int32_t(it - state.sets.begin()));
		const std::vector<CarSets::Choice>& choices = data->sets.sets[size_t(set)].choices;
		ring.values.clear();
		ring.fresh = true, ring.stepSpin = ring.fromSpin = -1;
		ring.index = 0;
		for (size_t i = 0; i < choices.size() && i < 30; i++) {
			ring.values.push_back(int32_t(i));
		}
		for (size_t i = ring.values.size(); i-- > 0;) {
			if (choices[i].type == last.type && choices[i].colour == last.colour) {
				ring.index = int32_t(i);
			}
		}
	}
	b.anchor = 0;
	b.anchorX = last.x;
	b.anchorY = last.y;
	b.rotation = last.rotation;
	takeChoice();
	g.working.stockCar = false;
	play(13);
	b.dropHeight = b.restHeight;
	b.dropMs = 0;
	b.running |= CarBuilder::PieceRise;
	b.mode = CarBuilder::Busy;
}

// Keys the screens take before the widgets.

bool Menus::builderKey(Game& g, const MenuKey& k)
{
	if (screen != ScreenCarBuilder) {
		return false;
	}
	CarBuilder& b = state.builder;
	WidgetTree& t = tree;
	if (b.mode == CarBuilder::Busy) {
		return true;
	}
	if ((k.key >> 28) == (SourceMouse >> 28)) {
		return builderMouse(g, k);
	}
	// A set or piece key holds its selector's arrow down.
	const bool setKey = k.key == KeyInsert || k.key == KeyPageUp, pieceKey = k.key == KeyDelete || k.key == KeyPageDown;
	if (setKey || pieceKey) {
		const int16_t sel = state.items[setKey ? 0 : 1];
		const int16_t arrow = t[sel].buttons[k.key == KeyPageUp || k.key == KeyPageDown ? 1 : 0];
		uint32_t& holding = setKey ? b.setKey : b.pieceKey;
		if (!k.down) {
			if (holding == k.key) {
				release(t, arrow, true, true, true);
				holding = 0;
			}
			return true;
		}
		if (!k.repeat && !holding) {
			holding = k.key;
			press(t, arrow, true, true, true);
			stepSelector(t, sel, k.key == KeyPageUp || k.key == KeyPageDown);
		}
		return true;
	}
	if (!k.down || k.repeat || t.focused() >= 0) {
		return false;
	}
	if (const int32_t d = keypadDirection(k.key); d >= 0) {
		moveHeld(d, true);
	}
	else if (k.key == (SourceKeyboard | 0x4c)) {
		turnHeld();
	}
	else if (k.key == KeyA || k.key == KeyD) {
		turnView(k.key == KeyA ? -1 : 1);
	}
	else if (k.key == KeyS || k.key == KeyX) {
		raiseView(k.key == KeyS ? 1 : -1);
	}
	else if (k.key == KeyPlus) {
		commit(g);
	}
	else if (k.key == KeyMinus) {
		undo(g);
	}
	else {
		return false;
	}
	return true;
}

// The builder by mouse.

namespace {
// A sphere's screen rectangle through a scene's camera into a viewport (settled from the original): the
// points where planes parallel to the frustum's sides touch the sphere, slid along the screen axes through its centre,
// projected; truncated toward zero. Empty (all 0) when the sphere is wholly outside the frustum.
MenuRect screenRect(const MenuScene& scene, MenuRect viewport, Sphere s)
{
	const Vec3 eye = vec(scene.eye);
	const Vec3 ahead = normalize(vec(scene.target) - eye);
	const Vec3 right = normalize(cross(ahead, vec(scene.up)));
	const Vec3 down = cross(ahead, right);
	const float tv = std::tan(scene.fov * 0.0087266462f), aspect = float(viewport.width()) / float(std::max(1, viewport.height()));
	const float th = aspect * tv;
	const float cosV = 1 / std::sqrt(1 + tv * tv), cosH = 1 / std::sqrt(1 + th * th);
	const Vec3 p = s.centre - eye;
	const float z = dot(p, ahead), x = dot(p, right), y = dot(p, down);
	if (z + s.radius < scene.nearPlane || z - s.radius > scene.farPlane || std::fabs(x) * cosH - z * th * cosH > s.radius ||
		std::fabs(y) * cosV - z * tv * cosV > s.radius) {
		return {};
	}
	const float f = float(viewport.height()) * 0.5f / tv;
	const auto project = [&](Vec3 q) {
		const Vec3 d = q - eye;
		const float qz = dot(d, ahead);
		return std::array<float, 2>{float(viewport.left) + float(viewport.width()) * 0.5f + f * dot(d, right) / qz,
			float(viewport.top) + float(viewport.height()) * 0.5f + f * dot(d, down) / qz};
	};
	const float rh = s.radius / cosH, rv = s.radius / cosV;
	return {int32_t(project(s.centre - right * rh)[0]), int32_t(project(s.centre - down * rv)[1]), int32_t(project(s.centre + right * rh)[0]),
		int32_t(project(s.centre + down * rv)[1])};
}
bool inside(const MenuRect& r, int32_t x, int32_t y) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }
} // namespace

void Menus::placeMouseAreas()
{
	CarBuilder& b = state.builder;
	WidgetTree& t = tree;
	heldMesh = state.car.heldMesh(data->pieces, data->sets.materials, {uint16_t(b.type), uint8_t(b.x), uint8_t(b.y), uint8_t(b.rotation), uint8_t(b.colour), 0}, 21);
	MenuScene scene;
	builderCamera(scene);
	const int16_t view = t.find(24); // the scene view
	if (view < 0 || state.items[8] < 0 || state.items[9] < 0) {
		return;
	}
	const MenuRect viewport = global(t, view);
	// The held piece at the hover height (its mesh is built 21 plates up), the car group at the stand point, both
	// the piece turned with the car, the car group's cached sphere as taken (it doesn't follow the group's turns).
	const Basis turn = turned(b.angle, 1);
	Sphere piece = meshBounds(heldMesh);
	piece = {toWorld(turn, piece.centre) + Vec3{0, 0, 1}, piece.radius};
	const Sphere car = {b.group.centre + Vec3{0, 0, 1}, b.group.radius};
	b.pieceRect = screenRect(scene, viewport, piece);
	b.carRect = screenRect(scene, viewport, car);
	for (const auto& [w, r] : {std::pair{state.items[8], b.pieceRect}, std::pair{state.items[9], b.carRect}}) {
		Widget& n = t[w];
		const MenuRect parent = global(t, n.parent);
		n.rect = {r.left - parent.left, r.top - parent.top, r.right - parent.left, r.bottom - parent.top};
	}
	// Hovering lights the area the cursor is on (the held piece's first, only inside the view), its button marked selected.
	const bool over = inside(viewport, cursorX, cursorY);
	const uint8_t pane = b.mode == CarBuilder::CarView ? 2 : b.mode == CarBuilder::PieceView ? 1 : over && inside(b.pieceRect, cursorX, cursorY) ? 1
		: over && inside(b.carRect, cursorX, cursorY)                                                                                      ? 2
																																	: 0;
	if (pane != b.pane) {
		for (const auto& [w, on] : {std::pair{state.items[8], pane == 1}, std::pair{state.items[9], pane == 2}}) {
			t[w].state = uint8_t(on ? t[w].state | Widget::Selected : t[w].state & ~Widget::Selected);
		}
		b.pane = pane;
	}
}

void Menus::enterView(CarBuilder::Mode mode)
{
	CarBuilder& b = state.builder;
	WidgetTree& t = tree;
	// The selectors, the pads and buttons the view doesn't use go off; the view's pad is selected.
	// (A selector's arrows keep their selected mark through it.)
	const auto off = [&](int16_t w) {
		std::array<uint8_t, 2> marks = {};
		for (size_t i = 0; i < 2; i++) {
			marks[i] = t[w].buttons[i] >= 0 ? t[t[w].buttons[i]].state & Widget::Selected : 0;
		}
		enable(t, w, false);
		for (size_t i = 0; i < 2; i++) {
			if (t[w].buttons[i] >= 0) {
				t[t[w].buttons[i]].state |= marks[i];
			}
		}
	};
	play(mode == CarBuilder::CarView ? 5 : 17);
	// (The other view's pad first: the order the selection sounds come out in.)
	off(state.items[mode == CarBuilder::CarView ? 2 : 6]);
	off(state.items[0]);
	off(state.items[1]);
	if (mode == CarBuilder::CarView) {
		off(state.items[3]), off(state.items[4]), off(state.items[5]);
	}
	select(t, state.items[mode == CarBuilder::CarView ? 6 : 2], false, false, false);
	focusOn(t, 0);
	b.mode = mode;
	b.freeSlot = float(b.slot);
	b.dragX = b.dragY = 0;
	cursorOn = mode != CarBuilder::CarView;
}

void Menus::leaveView()
{
	CarBuilder& b = state.builder;
	WidgetTree& t = tree;
	const bool car = b.mode == CarBuilder::CarView;
	for (size_t i : {0, 1, 2, 3, 4, 5, 6}) {
		enable(t, state.items[i], true);
		for (int16_t a : t[state.items[i]].buttons) {
			if (a >= 0) {
				enable(t, a, true);
			}
		}
	}
	unfocus(t, 0);
	play(car ? 5 : 17);
	b.mode = CarBuilder::Browse;
	cursorOn = true;
	if (!car) {
		return;
	}
	// The view snaps to the nearest slot and height.
	const int32_t k = int32_t(b.freeSlot + 0.5f) & 7;
	if (k != b.slot || b.freeSlot != std::floor(b.freeSlot)) {
		b.turnFrom = b.angle;
		b.slot = k;
		b.turnTo = slotAngle(k);
		if (b.turnTo - b.turnFrom > 3.1415927f) {
			b.turnTo -= 6.2831855f;
		}
		else if (b.turnFrom - b.turnTo > 3.1415927f) {
			b.turnTo += 6.2831855f;
		}
		b.turnMs = 150;
		b.running |= CarBuilder::ViewTurn;
	}
	b.freeSlot = -1;
	MenuScene now;
	builderCamera(now);
	int32_t nearest = 0;
	float best = 1e30f;
	for (int32_t i = 0; i < 3; i++) {
		CarBuilder probe = b;
		probe.pitch = float(i);
		probe.running = 0;
		std::swap(state.builder, probe);
		MenuScene at;
		builderCamera(at);
		std::swap(state.builder, probe);
		const float d = lengthSquared(vec(at.eye) - vec(now.eye));
		if (d < best) {
			best = d, nearest = i;
		}
	}
	if (b.pitch != float(nearest)) {
		b.pitchFrom = b.pitch;
		b.place = nearest;
		b.pitch = float(nearest);
		b.placeMs = b.placeTime = 300;
		b.running |= CarBuilder::ViewHeight;
	}
	b.place = nearest;
}

bool Menus::builderMouse(Game& g, const MenuKey& k)
{
	CarBuilder& b = state.builder;
	const uint32_t button = k.key & 0xfffffff;
	if (!k.down) {
		if (button == 0 && (b.mode == CarBuilder::CarView || b.mode == CarBuilder::PieceView)) {
			leaveView();
			return true;
		}
		return false;
	}
	if (k.repeat || b.mode != CarBuilder::Browse) {
		return b.mode != CarBuilder::Browse;
	}
	if (inside(b.pieceRect, cursorX, cursorY)) {
		if (button == 1) {
			turnHeld();
			return true;
		}
		if (button != 0) {
			return false;
		}
		// A second click soon after the first places it.
		if (b.clickMs < 500) {
			b.clickMs = 1000;
			commit(g);
			return true;
		}
		b.clickMs = 0;
		enterView(CarBuilder::PieceView);
		return true;
	}
	if (button == 0 && inside(b.carRect, cursorX, cursorY)) {
		enterView(CarBuilder::CarView);
		return true;
	}
	return false;
}

bool Menus::builderMotion(int32_t dx, int32_t dy)
{
	CarBuilder& b = state.builder;
	if (b.mode == CarBuilder::CarView) {
		// Turning and raising freely, each refused while its stepping change runs.
		if (!(b.running & CarBuilder::ViewTurn) && dx) {
			b.freeSlot = std::fmod(b.freeSlot - float(dx) * 0.01f + 8, 8.0f);
			const float whole = std::floor(b.freeSlot);
			b.angle = slotAngle(int32_t(whole)) + (b.freeSlot - whole) * 0.78539819f;
			b.turnFrom = b.angle;
		}
		const float delta = float(dy) * 0.01f;
		if (!(b.running & CarBuilder::ViewHeight) && dy && !(b.pitch >= 2 && delta > 0) && !(b.pitch <= 0 && delta < 0)) {
			b.pitch = std::clamp(b.pitch + delta, 0.0f, 2.0f);
		}
		return true;
	}
	if (b.mode == CarBuilder::PieceView) {
		// A drag steps the piece once the cursor has gone 5 px on an axis (100 ms apart).
		b.dragX += dx, b.dragY += dy;
		if ((std::abs(b.dragX) >= 5 || std::abs(b.dragY) >= 5) && b.dragMs >= 100) {
			const int32_t x = b.dragX, y = b.dragY;
			const int32_t d = x >= 5 ? (y >= 5 ? 3 : y <= -5 ? 1 : 2) : x <= -5 ? (y >= 5 ? 5 : y <= -5 ? 7 : 6) : (y >= 5 ? 4 : 0);
			moveHeld(d, false);
			b.dragX = b.dragY = 0;
			b.dragMs = 0;
		}
		return true;
	}
	return false;
}

// The items' notices.

void Menus::builderNotice(Game& g, WidgetTree& t, int16_t w, int32_t what)
{
	const auto is = [&](size_t i) { return state.items[i] >= 0 && w == state.items[i]; };
	const auto ask = [&](int32_t message, int32_t yes, int32_t no) {
		openDialog(2, message);
		giveButton(NameDialog, StyleCheck, yes);
		giveButton(NameDialog, StyleCancel, no);
		showDialog(1);
	};
	RacerRecord* const kept = stored(g);
	switch (screen) {
	case ScreenDriver:
		if (what == NoticeValue) {
			for (size_t r = 0; r < 4; r++) {
				if (!is(r)) {
					continue;
				}
				// The part worn changes, and the minifig shows it off.
				const Widget& ring = t[t[w].carousel];
				g.working.looks[RingLook[r]] = uint8_t(ring.values[size_t(ring.index)]);
				state.changed = true;
				const char* next = r < 2 ? "head1" : r == 2 ? "chest" : g.draw() & 1 ? "shaking" : "leg";
				state.queuedPart = partIndex(data->driverAnimations, next);
			}
			return;
		}
		if (what != NoticeReleased || state.exitMs >= 0) {
			return;
		}
		if (is(4)) {
			// MIX: a random part on every ring.
			for (size_t r = 0; r < 4; r++) {
				const int16_t ring = t[state.items[r]].carousel;
				selectItem(t, ring, int32_t(g.draw() % t[ring].values.size()));
			}
			for (size_t r = 0; r < 4; r++) {
				const Widget& ring = t[t[state.items[r]].carousel];
				g.working.looks[RingLook[r]] = uint8_t(ring.values[size_t(ring.index)]);
			}
			state.changed = true;
		}
		else if (is(5)) {
			if (!g.newRacer && kept) {
				kept->looks = g.working.looks;
				g.markRacers(g.chosen[0].list);
			}
			// The exit animation, then off.
			state.saving = true;
			state.driverPart = partIndex(data->driverAnimations, g.draw() % 2 ? "kiss" : "jump");
			state.exitMs = 0;
		}
		else if (is(6)) {
			if (g.newRacer) {
				ask(TextCancelNewRacer, TextYes, TextNo);
			}
			else if (kept && kept->looks != g.working.looks) {
				ask(TextLoseChanges, TextContinue, TextCancel);
			}
			else {
				navigate(ScreenGarage);
			}
		}
		return;
	case ScreenLicense: {
		if (what != NoticeReleased) {
			return;
		}
		const std::u16string& name = t[state.items[3]].text;
		if (is(0)) {
			state.expression = uint8_t((state.expression + 1) % 6);
		}
		else if (is(1) || (is(2) && g.newRacer)) {
			if (name.empty()) {
				return;
			}
			g.working.name = name;
			g.working.expression = state.expression;
			if (!g.newRacer && kept) {
				kept->name = name;
				kept->expression = state.expression;
				g.markRacers(g.chosen[0].list);
			}
			g.applyCheat(name);
			state.saving = true;
			navigate(is(2) ? ScreenDriver : g.newRacer ? ScreenEditCar : ScreenGarage);
		}
		else if (is(2)) {
			if (kept && (kept->name != name || kept->expression != state.expression)) {
				ask(TextLoseChanges, TextContinue, TextCancel);
			}
			else {
				navigate(ScreenGarage);
			}
		}
		return;
	}
	case ScreenEditCar: {
		if (what == NoticeValue && is(5)) {
			changeChassis(g, state.sets[size_t(t[t[w].carousel].index)]);
			return;
		}
		if (what != NoticeReleased) {
			return;
		}
		if (is(0)) {
			navigate(ScreenCarBuilder);
		}
		else if (is(1) || is(2)) {
			// Changes to a car of the player's own are lost: asked first.
			state.afterNotice = is(1) ? 1 : 2;
			if (g.working.stockCar) {
				editCarAction(g, state.afterNotice);
			}
			else {
				ask(TextLoseChanges, TextContinue, TextCancel);
			}
		}
		else if (is(3)) {
			// (Only a new racer marks its card to be saved: an edited car waits for the garage's next save.)
			if (kept) {
				*kept = g.working;
			}
			if (std::exchange(g.newRacer, false)) {
				g.settings.picks[0] = g.chosen[0];
				g.dirty = true;
				g.markRacers(g.chosen[0].list);
			}
			state.saving = true;
			navigate(ScreenGarage);
		}
		else if (is(4)) {
			if (g.newRacer) {
				navigate(ScreenLicense);
			}
			else if (kept && kept->pieces != g.working.pieces) {
				state.afterNotice = 3;
				ask(TextLoseChanges, TextContinue, TextCancel);
			}
			else {
				navigate(ScreenGarage);
			}
		}
		return;
	}
	case ScreenCarBuilder:
		if (what == NoticeValue && (is(2) || is(6)) && t[w].lit && state.builder.mode != CarBuilder::Busy) {
			// A pad's region clicked: the move pad's eight directions; the view pad's turns, heights and its centre's
			// reset (back to slot 1, the middle height).
			const int32_t region = t[w].lit;
			if (is(2)) {
				moveHeld(region - 1, false);
			}
			else if (region == 3 || region == 7) {
				// (A step that starts sounds 6.)
				if (!(state.builder.running & CarBuilder::ViewTurn)) {
					play(6);
				}
				turnView(region == 3 ? 1 : -1);
			}
			else if (region == 1 || region == 5) {
				const int32_t to = state.builder.place + (region == 1 ? 1 : -1);
				if (!(state.builder.running & CarBuilder::ViewHeight) && to >= 0 && to <= 2) {
					play(6);
				}
				raiseView(region == 1 ? 1 : -1);
			}
			else if (region == 9) {
				turnView((9 - state.builder.slot) & 7);
				raiseView(1 - state.builder.place);
			}
			return;
		}
		if (what != NoticeReleased) {
			if (what == NoticeValue && (is(0) || is(1))) {
				// A new set offers its pieces from the first; either way the hand takes the choice.
				if (is(0)) {
					Widget& ring = t[t[state.items[1]].carousel];
					const CarSets::Set& set = data->sets.sets[size_t(state.sets[size_t(t[t[w].carousel].index)])];
					ring.values.clear();
					ring.fresh = true, ring.stepSpin = ring.fromSpin = -1;
					for (size_t i = 0; i < set.choices.size() && i < 30; i++) {
						ring.values.push_back(int32_t(i));
					}
					selectItem(t, t[state.items[1]].carousel, 0);
				}
				takeChoice();
			}
			return;
		}
		if (state.builder.mode == CarBuilder::Busy) {
			return;
		}
		if (is(3)) {
			turnHeld();
		}
		else if (is(4)) {
			commit(g);
		}
		else if (is(5)) {
			undo(g);
		}
		else if (is(7)) {
			g.working.pieces = state.car.pieces();
			navigate(ScreenEditCar);
		}
		return;
	default: return;
	}
}

void Menus::builderDialog(Game& g, MenuDialog& d)
{
	const bool yes = d.result == 3;
	if (yes && screen == ScreenDriver && g.newRacer) {
		// The new racer goes.
		std::vector<RacerRecord>& list = g.list(g.chosen[0].list);
		if (g.chosen[0].index < list.size()) {
			list.erase(list.begin() + g.chosen[0].index);
			g.markRacers(g.chosen[0].list);
		}
		g.newRacer = false;
	}
	if (yes && screen == ScreenEditCar && state.afterNotice != 3) {
		editCarAction(g, state.afterNotice);
	}
	else if (yes) {
		navigate(ScreenGarage);
	}
	closeDialog();
}

// The rules the builders leave by.

void Menus::leaveBuilder()
{
	const auto pop = [&] { depth = uint8_t(std::max(0, depth - 1)); };
	if (screen == ScreenCarBuilder) {
		editSpin = state.builder.angle; // (edit car's car turns on from the builder's angle)
		spinKept = true;
		pop();
		return;
	}
	if (screen == ScreenEditCar && destination == ScreenCarBuilder) {
		push(ScreenCarBuilder);
		return;
	}
	pop();
	if (destination != ScreenGarage) {
		push(uint16_t(destination));
	}
	else if (state.saving) {
		push(ScreenSaveAll);
	}
}

// What the builders' 3D views show.

void Menus::builderCamera(MenuScene& scene) const
{
	const CarBuilder& b = state.builder;
	const std::array<std::array<float, 3>, 3>& Small = executableTables().builderEyes;
	const float large = std::clamp((b.eyeRadius - 5.9f) / (8.5f - 5.9f), 0.0f, 1.0f);
	const auto place = [&](int32_t i) { return vec(Small[size_t(i)]) * (1 - large) + Vec3{LargeEyes[i][0], LargeEyes[i][1], LargeEyes[i][2]} * large; };
	const auto eyeAt = [&](float p) {
		const int32_t i = std::clamp(int32_t(std::floor(p)), 0, 1);
		const float f = p - float(i);
		return place(i) * (1 - f) + place(i + 1) * f;
	};
	Vec3 eye = eyeAt(b.pitch);
	if (b.running & CarBuilder::ViewHeight) {
		const float e = tableSin(float(b.placeMs) / float(std::max(1, b.placeTime)) * 1.5707964f);
		eye = eyeAt(float(b.place)) + (eyeAt(b.pitchFrom) - eyeAt(float(b.place))) * e;
	}
	scene.eye = {eye.x, eye.y, eye.z};
	scene.target = {0, 0, 5};
	scene.up = {0, 0, 1};
	if (const MenuData::MenuWorld* w = data->menuWorld("garage"); w && w->hasCamera) {
		scene.fov = w->camera.fov, scene.nearPlane = w->camera.nearPlane, scene.farPlane = w->camera.farPlane;
	}
	scene.aspect = 420.0f / 354.0f; // (the view's viewport)
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
		man.skeleton = "MENUDATA/PARTDB/MENUPART/" + upperName(data->parts.bodies[4]) + ".SDB";
		man.partFile = "MENUDATA/PARTDB/MENUPART/" + upperName(data->parts.bodies[4]) + ".ADB";
		man.animation = 0; // ("velostop", one frame)
		man.looks = {g.working.looks[0], g.working.looks[1], g.working.looks[2], g.working.looks[3], g.working.expression};
		man.world = {turned(angle, MinifigScale), at + toWorld(turned(angle, 1), vec(c->driverMount))};
	}
}

const ChassisTable::Chassis* Menus::chassisOf(const std::vector<CarPiece>* pieces) const
{
	// The car's first piece is its chassis (the table's names are lower case).
	const int32_t type = pieces ? (pieces->empty() ? -1 : int32_t((*pieces)[0].type))
								: (state.car.entries.empty() ? -1 : int32_t(state.car.entries[0].piece.type));
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

int32_t Menus::bodyOf(const std::array<uint8_t, 5>& looks, bool seated) const
{
	// Body 2·legs variant + torso variant of the catalog's; the seated body is catalog body 4.
	const PartCatalog& cat = data->parts;
	const bool hooked = looks[3] < cat.rings[2].size() && cat.rings[2][looks[3]].variant;
	const bool pegs = looks[2] < cat.rings[3].size() && cat.rings[3][looks[2]].variant;
	return seated ? 4 : 2 * int32_t(pegs) + int32_t(hooked);
}

float Menus::chassisScale(const ChassisTable::Chassis* c) const
{
	// Each menus' chassis model at its own .GDB scale: not all are 0.015625.
	const size_t i = c ? size_t(c - data->menuChassis.chassis.data()) : data->chassisScales.size();
	return i < data->chassisScales.size() ? data->chassisScales[i] : 0.015625f;
}

// The car group's sphere (its centre the mean of the members', in member order car, chassis, driver; its radius the
// largest member radius + distance from that centre), in the world frame of the group standing at the origin, from the
// members' cached spheres. At the opening each
// is taken afresh: the car's and the chassis' at the facing edit car left them at (the same entities), the seated
// driver new (unturned) at the chassis' mount through that facing. At a commit only the driver's is taken again (the
// builder's facing, set after the opening, and every turn re-place it): the mount and its bounds turned by the angle
// shown. The car's and chassis' stay the opening's (a rebuild or a turn doesn't refresh them: QUIRK).
void Menus::takeGroupSphere(bool opening)
{
	CarBuilder& b = state.builder;
	const ChassisTable::Chassis* c = chassisOf(nullptr);
	if (opening) {
		const Basis edit = turned(editFacing, 1);
		b.memberCount = 0;
		if (!carMesh.vertices.empty()) {
			const Sphere s = meshBounds(carMesh);
			b.members[size_t(b.memberCount++)] = {toWorld(edit, s.centre), s.radius};
		}
		const size_t i = c ? size_t(c - data->menuChassis.chassis.data()) : data->chassisBounds.size();
		if (i < data->chassisBounds.size()) {
			b.members[size_t(b.memberCount++)] = {toWorld(edit, data->chassisBounds[i].centre), data->chassisBounds[i].radius};
		}
		if (c) {
			b.members[size_t(b.memberCount++)] = {toWorld(edit, vec(c->driverMount)) + data->seatedBounds.centre, data->seatedBounds.radius};
		}
	}
	else if (c && b.memberCount > 0) {
		b.members[size_t(b.memberCount - 1)] = {toWorld(turned(b.angle, 1), vec(c->driverMount) + data->seatedBounds.centre), data->seatedBounds.radius};
	}
	const int32_t count = b.memberCount;
	if (count == 0) {
		b.group = {};
		return;
	}
	const std::array<Sphere, 3>& m = b.members;
	const Vec3 c0 = m[0].centre;
	Vec3 sum;
	for (int32_t k = 0; k < count; k++) {
		sum += m[size_t(k)].centre - c0;
	}
	const Vec3 off = sum * (1.0f / float(count));
	float radius = 0;
	for (int32_t k = 0; k < count; k++) {
		radius = std::max(radius, m[size_t(k)].radius + lengthZYX(m[size_t(k)].centre - c0 - off));
	}
	b.group = {off + c0, radius};
}

const CarMesh& Menus::carouselItem(int32_t type, int32_t colour) const
{
	const uint32_t key = uint32_t(type) << 8 | uint32_t(colour & 255);
	for (const auto& [k, m] : carouselItems) {
		if (k == key) {
			return m;
		}
	}
	return carouselItems.emplace_back(key, Car::itemMesh(data->pieces, data->sets.materials, type, colour)).second;
}

void Menus::describeScene(const Widget& view, MenuScene& scene) const
{
	using Thing = MenuScene::Thing;
	const MenuLayout& layout = data->screens[screen].layout;
	const auto binding = std::find_if(layout.bindings.begin(), layout.bindings.end(), [&](const MenuLayout::Binding& b) { return b.id == view.id; });
	if (binding == layout.bindings.end()) {
		return;
	}
	const std::array<float, 9>& c = binding->camera;
	if (view.kind == Widget::Ring) {
		// A ring's camera looks along -x from its distance at its items, one per slot (the focused slot's the
		// selection).
		scene.eye = {c[7], 0, 0};
		scene.fov = c[6], scene.nearPlane = c[7], scene.farPlane = c[8];
		const int32_t count = int32_t(view.values.size());
		const CarSets::Set* set = screen == ScreenCarBuilder ? &data->sets.sets[size_t(state.sets[size_t(tree[tree[state.items[0]].carousel].index)])] : nullptr;
		// An item in a slot: the near plane's pixel size, the slot's fit, the item pushed back until its
		// bounds fit, across by the slot's centre, recentred on its bounds. (The view's middle at its width's half,
		// rounded down. Every slot holds an item; what lies outside the
		// ring's view is culled by the drawing.)
		const float d = c[7];
		const float pixel = 2 * d * std::tan(c[6] * 0.0087266462f) / float(std::max(1, view.rect.height()));
		const float midX = float(view.rect.width() / 2);
		const auto place = [&](int32_t slot, const Sphere& bounds) {
			const MenuRect& r = view.slotRects[size_t(slot)];
			const float fit = float(std::min(r.width(), r.height())) * pixel, diam = 2 * bounds.radius;
			float x = diam - fit;
			if (x > 0) {
				x = -(d / fit) * x;
			}
			x = std::min(x, -(diam + 2));
			const float across = (float(r.left + r.right) * 0.5f - midX) * pixel;
			return Vec3{x, across * (d - x) / d, 0} - bounds.centre;
		};
		// A step slides every item in a straight line from its old slot's place to its new one's over the scroll:
		// next moves items one slot down, so an item came from the slot after its own (the refilled end slot is placed
		// at once).
		const bool sliding = view.scrolled && view.scrollLeft > 0 && view.scrollMs > 0;
		const int32_t from = view.scrollSpeed > 0 ? 1 : -1;
		const float p = sliding ? 1 - float(view.scrollLeft) / float(view.scrollMs) : 1;
		scene.sliding = sliding;
		const auto slid = [&](int32_t slot, const Sphere& bounds) {
			const Vec3 to = place(slot, bounds);
			const int32_t old = slot + from;
			if (!sliding || old < 0 || size_t(old) >= view.slotRects.size()) {
				return to;
			}
			const Vec3 was = place(old, bounds);
			return was + (to - was) * p;
		};
		// The lights' colours: the selector style's item pair, the selected selector's, the focused item's.
		const Widget* selector = view.parent >= 0 && tree[view.parent].kind == Widget::Selector ? &tree[view.parent] : nullptr;
		const auto colours = [&](int32_t slot, MenuScene::Thing& t) {
			if (!selector) {
				return;
			}
			const size_t i = (selector->state & Widget::Selected) ? 2 : 0;
			bool pressed = false;
			for (int16_t a : selector->buttons) {
				pressed |= a >= 0 && (tree[a].state & Widget::Pressed);
			}
			const size_t k = slot == view.focusSlot && !pressed ? 4 + i : i;
			t.primary = selector->itemColours[k], t.secondary = selector->itemColours[k + 1];
		};
		if (set) {
			// The piece carousel: which slots hold an item, each its choice's piece spinning about the
			// tilted axis.
			const int32_t n = count, slots = int32_t(view.slotRects.size()), focus = view.focusSlot, k = view.index;
			std::vector<int32_t> holds(size_t(slots), -1);
			if (n >= slots - 1) {
				for (int32_t i = 0; i < slots && n > 0; i++) {
					holds[size_t(i)] = ((k - focus + i) % n + n) % n;
				}
			}
			else if (n > 0 && focus < slots) {
				holds[size_t(focus)] = k;
				for (int32_t j = 1; j <= std::min(focus, k); j++) {
					holds[size_t(focus - j)] = k - j;
				}
				const int32_t after = k >= focus ? n - k : slots - focus - 1;
				for (int32_t j = 1; j <= after && focus + j < slots; j++) {
					holds[size_t(focus + j)] = (k + j) % n;
				}
			}
			// Its spin's rows: (1, 0, 0) turned by -theta about the tilted axis, made square to it.
			const Vec3 axis = {float(std::cos(882.0 * 6.283185307179586 / 1024.0)), 0, float(std::cos(114.0 * 6.283185307179586 / 1024.0))};
			const auto spun = [&](float theta) {
				if (theta < 0) {
					return Basis{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
				}
				const Vec3 x = {1, 0, 0};
				const float ct = std::cos(-theta), st = std::sin(-theta);
				const Vec3 turnedX = x * ct + cross(axis, x) * st + axis * (dot(axis, x) * (1 - ct));
				const Vec3 row0 = normalize(turnedX - axis * dot(axis, turnedX));
				return Basis{row0, cross(axis, row0), axis};
			};
			// (Made items aren't turned before the ring's first tick; an item's place is recentred on its bounds as turned
			// when it was placed (made: unturned; a step: at the step's spin), not every frame.)
			const Basis rows = spun(view.fresh ? -1.0f : view.spin);
			for (int32_t slot = 0; slot < slots; slot++) {
				if (holds[size_t(slot)] < 0) {
					continue;
				}
				const CarSets::Choice& choice = set->choices[size_t(view.values[size_t(holds[size_t(slot)])])];
				Thing& t = scene.things.emplace_back();
				t.kind = Thing::Piece;
				t.slot = slot;
				t.type = choice.type;
				t.colour = choice.colour;
				t.angle = view.spin;
				t.lit = true;
				colours(slot, t);
				const Sphere bounds = meshBounds(carouselItem(choice.type, choice.colour));
				const Vec3 to = place(slot, {{}, bounds.radius}) - toWorld(spun(view.stepSpin), bounds.centre);
				const int32_t old = slot + from;
				Vec3 at = to;
				if (sliding && old >= 0 && size_t(old) < view.slotRects.size()) {
					const Vec3 was = place(old, {{}, bounds.radius}) - toWorld(spun(view.fromSpin), bounds.centre);
					at = was + (to - was) * p;
				}
				t.world = {rows, at};
			}
			return;
		}
		for (int32_t slot = 0; slot < view.slots && count; slot++) {
			const int32_t i = view.index + slot - view.focusSlot;
			const int32_t item = view.values[size_t((i % count + count) % count)];
			Thing& t = scene.things.emplace_back();
			t.slot = slot;
			t.angle = view.spin;
			t.kind = Thing::Part;
			t.type = item;
			t.colour = int32_t(std::find(state.items.begin(), state.items.begin() + 4, view.parent) - state.items.begin());
			colours(slot, t);
			const PartCatalog& cat = data->parts;
			Sphere bounds;
			switch (t.colour) {
			case 0: // hats: their head library entries
				if (size_t(item) < cat.rings[0].size()) {
					t.model = cat.rings[0][size_t(item)].name;
					bounds = data->hatBounds[size_t(item)];
				}
				break;
			case 1: t.model = "HEADS", bounds = data->ringBounds[0]; break;
			case 2: {
				const bool hooked = size_t(item) < cat.rings[2].size() && cat.rings[2][size_t(item)].variant;
				t.model = hooked ? "CHEST_HK" : "CHESTS", bounds = data->ringBounds[hooked ? 2 : 1];
				break;
			}
			default: {
				const bool pegs = size_t(item) < cat.rings[3].size() && cat.rings[3][size_t(item)].variant;
				t.model = pegs ? "LEG-PEGS" : "LEGSS", bounds = data->ringBounds[pegs ? 4 : 3];
				break;
			}
			}
			if (size_t(slot) >= view.slotRects.size()) {
				continue;
			}
			t.world = {{Vec3{1, 0, 0} * MinifigScale, Vec3{0, 1, 0} * MinifigScale, Vec3{0, 0, 1} * MinifigScale}, slid(slot, bounds)};
		}
		return;
	}
	// The world's camera when it has one, else the layout's.
	scene.world = binding->world;
	scene.drawWorld = binding->numbers[0] != 0;
	scene.blendedWorld = scene.drawWorld && binding->numbers[1] != 0;
	if (const MenuData::MenuWorld* w = data->menuWorld(binding->world); w && w->hasCamera) {
		const SceneFile::Camera& cam = w->camera;
		scene.eye = cam.position;
		scene.target = {cam.position[0] + cam.forward[0], cam.position[1] + cam.forward[1], cam.position[2] + cam.forward[2]};
		scene.up = cam.up;
		scene.fov = cam.fov, scene.nearPlane = cam.nearPlane, scene.farPlane = cam.farPlane;
		if (screen == ScreenEditCar && state.animMs > 0) {
			// (From its second frame the view looks with the world's up, not GARAGE Cam1's slightly rolled one.)
			scene.up = {0, 0, 1};
		}
	}
	else {
		scene.eye = {c[0], c[1], c[2]};
		scene.target = {c[3], c[4], c[5]};
		scene.fov = c[6], scene.nearPlane = c[7], scene.farPlane = c[8];
	}
	const Game& g = *game;
	// (The main menu's, the driver builder's and the license's minifig poses on catalog body 0's skeleton, the others on
	// their own body's)
	const auto minifig = [&](Vec3 at, Vec3 forward, Vec3 up, const std::array<uint8_t, 5>& looks, Thing::PartFile parts, int32_t animation, float frame) {
		Thing& t = scene.things.emplace_back();
		t.kind = Thing::Minifig;
		t.body = bodyOf(looks, false);
		const bool firstBody = screen == ScreenMainMenu || screen == ScreenDriver || screen == ScreenLicense;
		t.skeleton = "MENUDATA/PARTDB/MENUPART/" + upperName(data->parts.bodies[size_t(firstBody ? 0 : t.body)]) + ".SDB";
		t.at = {at.x, at.y, at.z};
		t.looks = looks;
		t.parts = parts;
		t.animation = animation;
		t.frame = frame;
		t.world = {facing(forward, up, MinifigScale), at};
	};
	const std::array<uint8_t, 5> working = {g.working.looks[0], g.working.looks[1], g.working.looks[2], g.working.looks[3], 0};
	switch (screen) {
	case ScreenCircuitRace:
	case ScreenSingleRace:
	case ScreenTimeRace: {
		// The preview: its PST scene (looping) as a cinematic draws it, the mascot in place of its
		// "guy1".
		const DriverTable::Driver* d = state.scene >= 0 ? data->drivers.find(state.mascot) : nullptr;
		const std::array<uint8_t, 5> mascot = d ? driverLooks(*d) : std::array<uint8_t, 5>{};
		describeCinema(state.cinema, data->screens[screen], scene, d ? &mascot : nullptr, nullptr);
		break;
	}
	case ScreenGarage:
	case ScreenRacerSelect:
	case ScreenPlayer2Select: {
		// The showcase: the racer's car on the turntable (turned as its shadow's node at the
		// turntable's clock), the minifig beside it.
		const int32_t n = int32_t(state.racers.size());
		const RacerRecord* r = screen == ScreenGarage ? (n && state.shown >= 0 ? g.racer(state.racers[size_t(state.shown % n)]) : nullptr) : g.racer(state.racer);
		// (A racer just shown is placed from the next frame)
		if (!r || (screen != ScreenGarage && state.racerChanged)) {
			break;
		}
		std::vector<NodeMatrix> spun;
		scene.blendedFrame = float(state.spinMs) * 0.03f; // (the turntable's shadows in BLENDED.WDB)
		poseSkeleton(data->turntableNodes, &data->turntable, {0, float(state.spinMs) * 0.03f, true}, Pose{{}, data->turntableFrame}, 1, spun);
		const Basis& turn = spun.empty() ? data->turntableFrame : spun[0].rows;
		carGroup(scene, {-11.52f, -6.767f, 0}, 0, false, r);
		for (Thing& t : scene.things) {
			const float k = t.kind == Thing::Chassis ? length(t.world.rows.f) : 1.0f; // (the chassis model's own scale)
			t.world.rows = facing(turn.f, turn.u, k);
			t.frame = float(state.spinMs) * 0.03f;
		}
		minifig({-0.938f, -0.898f, 1.487f}, {0.963630974f, -0.267237991f, 0}, {0, 0, 1}, {r->looks[0], r->looks[1], r->looks[2], r->looks[3], r->expression},
			Thing::Showcase, state.driverPart, state.driverFrame);
		break;
	}
	case ScreenMainMenu:
		// The minifig alone in TT.WDB, playing LEGOMAN's parts.
	{
		// The champion of the furthest circuit open: m = the first circuit j = 1..6 still locked,
		// less one (6 if all are open); the driver entry by m, entry 0 for 6.
		int32_t m = 6;
		for (int32_t j = 1; j <= 6; j++) {
			if (!(g.settings.circuits >> j & 1)) {
				m = j - 1;
				break;
			}
		}
		static const int32_t Champions[7] = {0x13, 0x12, 0x16, 0x15, 0x14, 0x17, 0};
		const std::vector<DriverTable::Driver>& all = data->drivers.drivers;
		const size_t e = size_t(Champions[m]);
		std::array<uint8_t, 5> looks = e < all.size() ? driverLooks(all[e]) : std::array<uint8_t, 5>{};
		looks[4] = 0; // the main menu shows dflt
		minifig({18.18122864f, -10.62275887f, 0.025708f}, {0.97237003f, -0.233445f, 0}, {0, 0, 1}, looks, Thing::Legoman, state.animPart,
			blendedFrame(data->minifig, state.animPart, state.animMs, state.animPart != 0));
		break;
	}
	case ScreenDriver:

		minifig({-5.3590002f, -3.1500001f, 0.026000001f}, {1, 0, 0}, {0, 0, 1}, working, Thing::Builder, state.driverPart,
			state.exitMs >= 0 ? blendedFrame(data->driverAnimations, state.driverPart, state.exitMs, true) : state.driverFrame);
		break;
	case ScreenLicense:
		// The photo: CMAMAN's pose, the chosen expression.

		minifig({-5.3593369f, -3.1500180f, 0}, {1, 0, 0}, {0, -0.0348990f, 0.9993910f},
			{working[0], working[1], working[2], working[3], state.expression}, Thing::Photo, 0, advance(data->photo, 0, 0, state.animMs));
		break;
	case ScreenEditCar: {

		// (Spinning on from the builder's angle when it comes back from there, else from 0.)
		carGroup(scene, {0, 0, 1}, std::fmod(editSpin + float(state.animMs) * 0.001f, 6.2831855f), false);
		if (state.animMs == 0 && !scene.things.empty() && scene.things.back().kind == Thing::Minifig) {
			scene.things.back().world.rows = turned(0, MinifigScale); // (its first frame: placed, not yet turned)
		}
		break;
	}
	case ScreenCarBuilder: {
		builderCamera(scene);
		const CarBuilder& b = state.builder;

		carGroup(scene, {0, 0, 1}, b.angle, b.pane == 2);
		// The held piece (at its grid place, the height in the car's frame): bobbing ghost, landing ghost, the piece.
		const bool moving = b.running & (CarBuilder::PieceDrop | CarBuilder::PieceRise), top = b.place == 2;
		const auto piece = [&](float height, int32_t alpha) {
			if (alpha <= 0) {
				return;
			}
			Thing& t = scene.things.emplace_back();
			t.kind = Thing::Piece;
			t.type = b.type, t.colour = b.colour, t.rotation = b.rotation;
			t.at = {float(b.x), float(b.y), height};
			t.angle = b.angle;
			t.alpha = uint8_t(std::min(alpha, 255));
			t.lit = b.pane == 1;
			t.world = {turned(b.angle, 1), {0, 0, height}};
		};
		if (!moving) {
			const bool bobbing = b.bob != CarBuilder::Still;
			if (bobbing && !top) {
				piece(b.bobHeight, 64);
			}
			int32_t alpha = int32_t(50 * std::cos(6.2831855f * float(b.bobClock) / 1000.0f)) + 100;
			if (bobbing) {
				const float v = b.bobHeight - b.restHeight - 1.2f;
				alpha = v < 1.2f && !top ? (v < 0 ? 0 : int32_t(v / 1.2f * 150)) : 150;
			}
			piece(b.restHeight, alpha);
		}
		if (moving || !top) {
			piece(moving ? b.dropHeight : 1.0f, 255);
		}
		for (const auto& m : b.hits) {
			if (b.running & CarBuilder::PieceRise) {
				break;
			}
			Thing& t = scene.things.emplace_back();
			t.kind = Thing::Mark;
			t.at = {float(m[0]) + state.car.offset[0], float(m[1]) + state.car.offset[1], float(b.overlayHeight) * 0.4f + state.car.offset[2]};
			t.angle = b.angle;
			t.world = {turned(b.angle, 1), {0, 0, 1.1f}};
		}
		break;
	}
	default: break;
	}
}

} // namespace lr
