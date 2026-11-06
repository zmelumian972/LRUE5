// The car builder: each frame's clocks (the view, the held piece bobbing, dropping or rising), the
// held piece taken, moved, turned, placed and taken back, the view turned and raised, its keys and pads, and the
// builder by mouse.

#include "LRCore/Menus/Menus.h"
#include "LRCore/Common/Tables.h"
#include "LRData/Archive.h"

#include "Show/Animation.h"
#include "Menus/BuilderParts.h"
#include "Menus/Layout.h"
#include "Menus/MenuNames.h"
#include "Menus/Pen.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

// The builder's view: its turn (eased over 150 ms) and height change.
void tickView(CarBuilder& b, int32_t dt)
{
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
}

// The held piece bobs over where it would land while it fits; else it sits there.
void bobHeld(CarBuilder& b, bool fits, int32_t dt)
{
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

} // namespace

void Builders::tickBuilders(Menus& m, int32_t dt)
{
	if (m.screen == ScreenLicense || m.screen == ScreenEditCar) {
		state.animMs += dt; // the photo's pose, the car's spin
	}
	if (m.screen == ScreenEditCar) {
		m_editFacing = std::fmod(m_editSpin + float(state.animMs) * 0.001f, Tau); // (as its scene draws it)
	}
	if (m.screen == ScreenDriver) {
		if (state.exitMs >= 0) {
			state.exitMs += dt;
		}
		else {
			state.driverFrame = advance(m.data->driverAnimations, state.driverPart, state.driverFrame, dt);
		}
		return;
	}
	if (m.screen != ScreenCarBuilder) {
		return;
	}
	CarBuilder& b = state.builder;
	const PieceLibrary& lib = m.data->pieces;
	b.clickMs = std::min(b.clickMs + dt, 100000);
	b.dragMs = std::min(b.dragMs + dt, 100000);
	tickView(b, dt);
	b.bobClock = dt >= b.bobClock ? b.bobClock - dt + 1000 : b.bobClock - dt;
	if (b.running & (CarBuilder::PieceDrop | CarBuilder::PieceRise)) {
		tickDrop(m, dt);
		return;
	}
	// The held piece, over where it would land.
	const PieceLibrary::Piece* p = lib.find(b.type);
	if (!p) {
		return;
	}
	const bool fits = state.car.test(lib, *p, b.x, b.y, b.rotation) >= 0;
	bobHeld(b, fits, dt);
}

// A drop ends (the piece placed) the frame after it reaches where it lands; a rise likewise at the hover height.
void Builders::tickDrop(Menus& m, int32_t dt)
{
	CarBuilder& b = state.builder;
	const PieceLibrary& lib = m.data->pieces;
	const bool drop = b.running & CarBuilder::PieceDrop;
	if (drop ? b.dropHeight <= b.restHeight || b.dropMs >= 2500 : b.dropHeight >= 1.0f || b.dropMs >= 2000) {
		b.running &= uint8_t(~(CarBuilder::PieceDrop | CarBuilder::PieceRise));
		if (drop) {
			Game& g = *m.game;
			state.car.place(lib, {uint16_t(b.type), uint8_t(b.x), uint8_t(b.y), uint8_t(b.rotation), uint8_t(b.colour), uint16_t(b.part)},
				g);
			m.carMesh = state.car.menuMesh(lib, m.data->sets.materials);
			if (state.car.status) {
				m.ui.tell(TextBuildingLimit);
				state.car.remove(lib, state.car.entries.size() - 1);
				m.carMesh = state.car.menuMesh(lib, m.data->sets.materials);
			}
			else {
				m.play(SoundConnect);
			}
			// (The piece carousel's items are made again, before its tick turns them.)
			Widget& ring = m.ui.tree[m.ui.tree[m.items[BuilderPieces]].carousel];
			ring.stepSpin = ring.fromSpin = -1;
			takeChoice(m);
		}
		b.mode = CarBuilder::Browse;
	}
	else {
		b.dropMs += dt;
		b.dropHeight = drop ? std::max(b.restHeight, b.dropHeight - 0.01f * float(dt)) : std::min(1.0f, b.dropHeight + 0.006f * float(dt));
	}
}

const CarSets::Set& Builders::builderSet(const Menus& m) const
{
	return m.data->sets.sets[state.sets[m.ui.tree[m.ui.tree[m.items[BuilderSets]].carousel].index]];
}

void Builders::takeChoice(Menus& m)
{
	// The held piece takes the piece selector's choice: the first time in the middle of the grid, after that keeping
	// the corner it was by (turned if it doesn't fit the grid as it was).
	CarBuilder& b = state.builder;
	const Widget& ring = m.ui.tree[m.ui.tree[m.items[BuilderPieces]].carousel];
	const CarSets::Set& set = builderSet(m);
	const CarSets::Choice& choice = set.choices[ring.values[ring.index]];
	const PieceLibrary::Piece* p = m.data->pieces.find(choice.type);
	if (!p) {
		return;
	}
	b.type = choice.type;
	b.colour = choice.colour;
	b.part = set.part;
	b.width = p->width;
	b.height = p->height;
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
		const auto [ex, ey] = extents(b);
		b.x = std::min(b.anchorX - (b.anchor & 2 ? ex : 0), Car::Width - ex);
		b.y = std::min(b.anchorY - (b.anchor & 1 ? ey : 0), Car::Depth - ey);
	}
	b.x = std::max(b.x, 0);
	b.y = std::max(b.y, 0);
	heldChanged(m, true);
}

void Builders::heldChanged(Menus& m, bool chooseAnchor)
{
	// The corner nearest the grid's edge the piece is by is kept (when chosen again); then the marks under it.
	CarBuilder& b = state.builder;
	const auto [ex, ey] = extents(b);
	const auto point = [&] {
		b.anchorX = b.x + (b.anchor & 2 ? ex : 0);
		b.anchorY = b.y + (b.anchor & 1 ? ey : 0);
	};
	point();
	if (chooseAnchor) {
		b.anchor = (b.anchorX >= 5 ? 2 : 0) | (b.anchorY >= 3 ? 1 : 0);
		point();
	}
	const PieceLibrary& lib = m.data->pieces;
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

void Builders::moveHeld(Menus& m, int32_t direction, bool refusalSound)
{
	CarBuilder& b = state.builder;
	const ExecutableTables& steps = executableTables();
	const int32_t dx = steps.builderStepX[b.slot][direction], dy = steps.builderStepY[b.slot][direction];
	const bool independent = direction & 1;
	const auto [ex, ey] = extents(b);
	// One stud at a time; a step it can't take does nothing.
	const auto along = [&](int32_t d, bool y) {
		bool moved = false;
		for (int32_t i = 0; i < std::abs(d); i++) {
			int32_t& c = y ? b.y : b.x;
			const int32_t limit = (y ? Car::Depth : Car::Width) - (y ? ey : ex);
			if (d > 0 ? c < limit : c > 0) {
				c += d > 0 ? 1 : -1;
				moved = true;
				heldChanged(m, true);
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
		m.play(moved ? SoundMoveBrick : SoundCantDo);
	}
}

void Builders::turnHeld(Menus& m)
{
	// A turn that wouldn't fit the grid is skipped; the corner kept stays where it was.
	CarBuilder& b = state.builder;
	if (b.rotation & 1 ? b.height > Car::Depth || b.width > Car::Width : b.width > Car::Depth || b.height > Car::Width) {
		b.rotation++;
	}
	b.rotation = (b.rotation + 1) & 3;
	const auto [ex, ey] = extents(b);
	b.x = std::clamp(b.anchorX - (b.anchor & 2 ? ex : 0), 0, Car::Width - ex);
	b.y = std::clamp(b.anchorY - (b.anchor & 1 ? ey : 0), 0, Car::Depth - ey);
	heldChanged(m, false);
	m.play(SoundRotate);
}

void Builders::turnView(int32_t step)
{
	CarBuilder& b = state.builder;
	if (!(b.running & CarBuilder::ViewTurn)) {
		turnTo(b, slotAngle(b.slot), (b.slot + step + 8) & 7);
	}
}

void Builders::raiseView(int32_t step)
{
	CarBuilder& b = state.builder;
	if (!(b.running & CarBuilder::ViewHeight) && b.place + step >= 0 && b.place + step <= 2) {
		raiseTo(b, b.place + step);
	}
}

void Builders::commit(Menus& m)
{
	Game& g = *m.game;
	CarBuilder& b = state.builder;
	const PieceLibrary::Piece* p = m.data->pieces.find(b.type);
	const int32_t fit = p ? state.car.test(m.data->pieces, *p, b.x, b.y, b.rotation) : Car::NothingBelow;
	if (fit == Car::TooMany) {
		m.ui.tell(TextBuildingLimit);
		return;
	}
	if (fit < 0) {
		m.play(SoundNoConnect);
		return;
	}
	g.working.stockCar = false;
	takeGroupSphere(m, false); // (before the piece goes into the car)
	b.dropHeight = 1.0f;
	b.dropMs = 0;
	b.running |= CarBuilder::PieceDrop;
	b.mode = CarBuilder::Busy;
}

void Builders::undo(Menus& m)
{
	Game& g = *m.game;
	CarBuilder& b = state.builder;
	const PieceLibrary& lib = m.data->pieces;
	if (state.car.entries.size() <= 1) {
		m.play(SoundNoConnect);
		return;
	}
	const CarPiece last = state.car.entries.back().piece;
	state.car.remove(lib, state.car.entries.size() - 1);
	m.carMesh = state.car.menuMesh(lib, m.data->sets.materials);
	// It comes back to the hand where it was, in its set.
	WidgetTree& t = m.ui.tree;
	const int32_t set = m.data->sets.setOfPart(last.part);
	const auto it = std::find(state.sets.begin(), state.sets.end(), set);
	if (last.part && it != state.sets.end()) {
		Widget& ring = t[t[m.items[BuilderPieces]].carousel];
		m.ui.selectItem(t, t[m.items[BuilderSets]].carousel, int32_t(it - state.sets.begin()));
		const std::vector<CarSets::Choice>& choices = m.data->sets.sets[set].choices;
		offerChoices(ring, choices.size());
		ring.index = 0;
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
	takeChoice(m);
	g.working.stockCar = false;
	m.play(SoundDisconnect);
	b.dropHeight = b.restHeight;
	b.dropMs = 0;
	b.running |= CarBuilder::PieceRise;
	b.mode = CarBuilder::Busy;
}

bool Builders::builderKey(Menus& m, const MenuKey& k)
{
	if (m.screen != ScreenCarBuilder) {
		return false;
	}
	CarBuilder& b = state.builder;
	WidgetTree& t = m.ui.tree;
	if (b.mode == CarBuilder::Busy) {
		return true;
	}
	if ((k.key >> 28) == (SourceMouse >> 28)) {
		return builderMouse(m, k);
	}
	// A set or piece key holds its selector's arrow down.
	const bool setKey = k.key == KeyInsert || k.key == KeyPageUp, pieceKey = k.key == KeyDelete || k.key == KeyPageDown;
	if (setKey || pieceKey) {
		const int16_t sel = m.items[setKey ? 0 : 1];
		const int16_t arrow = t[sel].buttons[k.key == KeyPageUp || k.key == KeyPageDown ? 1 : 0];
		uint32_t& holding = setKey ? b.setKey : b.pieceKey;
		if (!k.down) {
			if (holding == k.key) {
				m.ui.release(t, arrow, IconQuiet | IconLocal | IconSilent);
				holding = 0;
			}
			return true;
		}
		if (!k.repeat && !holding) {
			holding = k.key;
			m.ui.press(t, arrow, IconQuiet | IconLocal | IconSilent);
			m.ui.stepSelector(t, sel, k.key == KeyPageUp || k.key == KeyPageDown);
		}
		return true;
	}
	if (!k.down || k.repeat || t.focused() >= 0) {
		return false;
	}
	if (const int32_t d = keypadDirection(k.key); d >= 0) {
		moveHeld(m, d, true);
	}
	else if (k.key == (SourceKeyboard | 0x4c)) {
		turnHeld(m);
	}
	else if (k.key == KeyA || k.key == KeyD) {
		turnView(k.key == KeyA ? -1 : 1);
	}
	else if (k.key == KeyS || k.key == KeyX) {
		raiseView(k.key == KeyS ? 1 : -1);
	}
	else if (k.key == KeyPlus) {
		commit(m);
	}
	else if (k.key == KeyMinus) {
		undo(m);
	}
	else {
		return false;
	}
	return true;
}

// A pad's region clicked: the move pad's eight directions; the view pad's turns, heights and its centre's reset (back to
// slot 1, the middle height).
void Builders::builderPad(Menus& m, bool movePad, int32_t region)
{
	if (movePad) {
		moveHeld(m, region - 1, false);
	}
	else if (region == 3 || region == 7) {
		// (A step that starts sounds.)
		if (!(state.builder.running & CarBuilder::ViewTurn)) {
			m.play(SoundCamMove);
		}
		turnView(region == 3 ? 1 : -1);
	}
	else if (region == 1 || region == 5) {
		const int32_t to = state.builder.place + (region == 1 ? 1 : -1);
		if (!(state.builder.running & CarBuilder::ViewHeight) && to >= 0 && to <= 2) {
			m.play(SoundCamMove);
		}
		raiseView(region == 1 ? 1 : -1);
	}
	else if (region == 9) {
		turnView((9 - state.builder.slot) & 7);
		raiseView(1 - state.builder.place);
	}
}

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
	const float tv = std::tan(scene.fov * 0.008726646f), aspect = float(viewport.width()) / float(std::max(1, viewport.height()));
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
	return {int32_t(project(s.centre - right * rh)[0]), int32_t(project(s.centre - down * rv)[1]),
		int32_t(project(s.centre + right * rh)[0]), int32_t(project(s.centre + down * rv)[1])};
}
} // namespace

void Builders::placeMouseAreas(Menus& m)
{
	CarBuilder& b = state.builder;
	WidgetTree& t = m.ui.tree;
	m.heldMesh = state.car.heldMesh(m.data->pieces, m.data->sets.materials,
		{uint16_t(b.type), uint8_t(b.x), uint8_t(b.y), uint8_t(b.rotation), uint8_t(b.colour), 0}, 21);
	MenuScene scene;
	builderCamera(m, scene);
	const int16_t view = t.find(24); // the scene view
	if (view < 0 || m.items[BuilderPieceArea] < 0 || m.items[BuilderCarArea] < 0) {
		return;
	}
	const MenuRect viewport = m.ui.global(t, view);
	// The held piece at the hover height (its mesh is built 21 plates up), the car group at the stand point, both
	// the piece turned with the car, the car group's cached sphere as taken (it doesn't follow the group's turns).
	const Basis turn = turned(b.angle, 1);
	Sphere piece = meshBounds(m.heldMesh);
	piece = {toWorld(turn, piece.centre) + Vec3{0, 0, 1}, piece.radius};
	const Sphere car = {b.group.centre + Vec3{0, 0, 1}, b.group.radius};
	b.pieceRect = screenRect(scene, viewport, piece);
	b.carRect = screenRect(scene, viewport, car);
	for (const auto& [w, r] : {std::pair{m.items[BuilderPieceArea], b.pieceRect}, std::pair{m.items[BuilderCarArea], b.carRect}}) {
		Widget& n = t[w];
		const MenuRect parent = m.ui.global(t, n.parent);
		n.rect = {r.left - parent.left, r.top - parent.top, r.right - parent.left, r.bottom - parent.top};
	}
	// Hovering lights the area the cursor is on (the held piece's first, only inside the view), its button marked selected.
	const bool over = inside(viewport, m.ui.cursorX, m.ui.cursorY);
	const uint8_t pane = b.mode == CarBuilder::CarView			  ? 2
		: b.mode == CarBuilder::PieceView						  ? 1
		: over && inside(b.pieceRect, m.ui.cursorX, m.ui.cursorY) ? 1
		: over && inside(b.carRect, m.ui.cursorX, m.ui.cursorY)	  ? 2
																  : 0;
	if (pane != b.pane) {
		for (const auto& [w, on] : {std::pair{m.items[BuilderPieceArea], pane == 1}, std::pair{m.items[BuilderCarArea], pane == 2}}) {
			t[w].state = uint8_t(on ? t[w].state | Widget::Selected : t[w].state & ~Widget::Selected);
		}
		b.pane = pane;
	}
}

void Builders::enterView(Menus& m, CarBuilder::Mode mode)
{
	CarBuilder& b = state.builder;
	WidgetTree& t = m.ui.tree;
	// The selectors, the pads and buttons the view doesn't use go off; the view's pad is selected.
	// (A selector's arrows keep their selected mark through it.)
	const auto off = [&](int16_t w) {
		std::array<uint8_t, 2> marks = {};
		for (size_t i = 0; i < 2; i++) {
			marks[i] = t[w].buttons[i] >= 0 ? t[t[w].buttons[i]].state & Widget::Selected : 0;
		}
		m.ui.enable(t, w, false);
		for (size_t i = 0; i < 2; i++) {
			if (t[w].buttons[i] >= 0) {
				t[t[w].buttons[i]].state |= marks[i];
			}
		}
	};
	m.play(mode == CarBuilder::CarView ? SoundCamAct : SoundMoveAct);
	// (The other view's pad first: the order the selection sounds come out in.)
	off(m.items[mode == CarBuilder::CarView ? BuilderMovePad : BuilderViewPad]);
	off(m.items[BuilderSets]);
	off(m.items[BuilderPieces]);
	if (mode == CarBuilder::CarView) {
		off(m.items[BuilderTurn]), off(m.items[BuilderAdd]), off(m.items[BuilderUndo]);
	}
	m.ui.select(t, m.items[mode == CarBuilder::CarView ? BuilderViewPad : BuilderMovePad]);
	m.ui.focusOn(t, 0);
	b.mode = mode;
	b.freeSlot = float(b.slot);
	b.dragX = b.dragY = 0;
	m.ui.cursorOn = mode != CarBuilder::CarView;
}

void Builders::leaveView(Menus& m)
{
	CarBuilder& b = state.builder;
	WidgetTree& t = m.ui.tree;
	const bool car = b.mode == CarBuilder::CarView;
	for (size_t i : {0, 1, 2, 3, 4, 5, 6}) {
		m.ui.enable(t, m.items[i], true);
		for (int16_t a : t[m.items[i]].buttons) {
			if (a >= 0) {
				m.ui.enable(t, a, true);
			}
		}
	}
	m.ui.unfocus(t, 0);
	m.play(car ? SoundCamAct : SoundMoveAct);
	b.mode = CarBuilder::Browse;
	m.ui.cursorOn = true;
	if (!car) {
		return;
	}
	// The view snaps to the nearest slot and height.
	const int32_t k = int32_t(b.freeSlot + 0.5f) & 7;
	if (k != b.slot || b.freeSlot != std::floor(b.freeSlot)) {
		turnTo(b, b.angle, k);
	}
	b.freeSlot = -1;
	MenuScene now;
	builderCamera(m, now);
	int32_t nearest = 0;
	float best = 1e30f;
	for (int32_t i = 0; i < 3; i++) {
		CarBuilder probe = b;
		probe.pitch = float(i);
		probe.running = 0;
		std::swap(state.builder, probe);
		MenuScene at;
		builderCamera(m, at);
		std::swap(state.builder, probe);
		const float d = lengthSquared(vec(at.eye) - vec(now.eye));
		if (d < best) {
			best = d, nearest = i;
		}
	}
	if (b.pitch != float(nearest)) {
		raiseTo(b, nearest);
	}
	b.place = nearest;
}

bool Builders::builderMouse(Menus& m, const MenuKey& k)
{
	CarBuilder& b = state.builder;
	const uint32_t button = k.key & 0xfffffff;
	if (!k.down) {
		if (button == 0 && (b.mode == CarBuilder::CarView || b.mode == CarBuilder::PieceView)) {
			leaveView(m);
			return true;
		}
		return false;
	}
	if (k.repeat || b.mode != CarBuilder::Browse) {
		return b.mode != CarBuilder::Browse;
	}
	if (inside(b.pieceRect, m.ui.cursorX, m.ui.cursorY)) {
		if (button == 1) {
			turnHeld(m);
			return true;
		}
		if (button != 0) {
			return false;
		}
		// A second click soon after the first places it.
		if (b.clickMs < 500) {
			b.clickMs = 1000;
			commit(m);
			return true;
		}
		b.clickMs = 0;
		enterView(m, CarBuilder::PieceView);
		return true;
	}
	if (button == 0 && inside(b.carRect, m.ui.cursorX, m.ui.cursorY)) {
		enterView(m, CarBuilder::CarView);
		return true;
	}
	return false;
}

bool Builders::builderMotion(Menus& m, int32_t dx, int32_t dy)
{
	CarBuilder& b = state.builder;
	if (b.mode == CarBuilder::CarView) {
		// Turning and raising freely, each refused while its stepping change runs.
		if (!(b.running & CarBuilder::ViewTurn) && dx) {
			b.freeSlot = std::fmod(b.freeSlot - float(dx) * 0.01f + 8, 8.0f);
			const float whole = std::floor(b.freeSlot);
			b.angle = slotAngle(int32_t(whole)) + (b.freeSlot - whole) * 0.7853982f;
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
			// The step by the drag's side on each axis (-5 or less, between, 5 or more); 0 none.
			static constexpr int32_t Steps[3][3] = {{7, 6, 5}, {0, 0, 4}, {1, 2, 3}};
			const auto side = [](int32_t v) { return size_t(1 + (v >= 5) - (v <= -5)); };
			moveHeld(m, Steps[side(b.dragX)][side(b.dragY)], false);
			b.dragX = b.dragY = 0;
			b.dragMs = 0;
		}
		return true;
	}
	return false;
}

// The car group's sphere (its centre the mean of the members', in member order car, chassis, driver; its radius the
// largest member radius + distance from that centre), in the world frame of the group standing at the origin, from the
// members' cached spheres. At the opening each
// is taken afresh: the car's and the chassis' at the facing edit car left them at (the same entities), the seated
// driver new (unturned) at the chassis' mount through that facing. At a commit only the driver's is taken again (the
// builder's facing, set after the opening, and every turn re-place it): the mount and its bounds turned by the angle
// shown. The car's and chassis' stay the opening's (a rebuild or a turn doesn't refresh them: QUIRK).
void Builders::takeGroupSphere(Menus& m, bool opening)
{
	CarBuilder& b = state.builder;
	const ChassisTable::Chassis* c = m.chassisOf(nullptr);
	if (opening) {
		const Basis edit = turned(m_editFacing, 1);
		b.memberCount = 0;
		if (!m.carMesh.vertices.empty()) {
			const Sphere s = meshBounds(m.carMesh);
			b.members[b.memberCount++] = {toWorld(edit, s.centre), s.radius};
		}
		const size_t i = c ? size_t(c - m.data->menuChassis.chassis.data()) : m.data->chassisBounds.size();
		if (i < m.data->chassisBounds.size()) {
			b.members[b.memberCount++] = {toWorld(edit, m.data->chassisBounds[i].centre), m.data->chassisBounds[i].radius};
		}
		if (c) {
			b.members[b.memberCount++] = {toWorld(edit, vec(c->driverMount)) + m.data->seatedBounds.centre, m.data->seatedBounds.radius};
		}
	}
	else if (c && b.memberCount > 0) {
		b.members[b.memberCount - 1] = {toWorld(turned(b.angle, 1), vec(c->driverMount) + m.data->seatedBounds.centre),
			m.data->seatedBounds.radius};
	}
	const int32_t count = b.memberCount;
	if (count == 0) {
		b.group = {};
		return;
	}
	const Array<Sphere, 3>& ms = b.members;
	const Vec3 c0 = ms[0].centre;
	Vec3 sum;
	for (int32_t k = 0; k < count; k++) {
		sum += ms[k].centre - c0;
	}
	const Vec3 off = sum * (1.0f / float(count));
	float radius = 0;
	for (int32_t k = 0; k < count; k++) {
		radius = std::max(radius, ms[k].radius + lengthZYX(ms[k].centre - c0 - off));
	}
	b.group = {off + c0, radius};
}

} // namespace lr
