// What the builders' 3D views show: the driver builder's minifig and rings, the license's photo, edit car's car,
// the piece carousel, the car builder's car, held piece and camera.

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

// An item in a ring's slot: the near plane's pixel size, the slot's fit, the item pushed back until its
// bounds fit, across by the slot's centre, recentred on its bounds. (The view's middle at its width's half, rounded down.
// Every slot holds an item; what lies outside the ring's view is culled by the drawing.)
// A step slides every item in a straight line from its old slot's place to its new one's over the scroll: next
// moves items one slot down, so an item came from the slot after its own (the refilled end slot is placed at once).
struct Builders::RingSlots {
	const Widget& view;
	const WidgetTree& tree;
	float d = 0, pixel = 0, midX = 0;
	bool sliding = false;
	int32_t from = 0;
	float p = 1;
	const Widget* selector = nullptr;

	RingSlots(const Widget& v, const WidgetTree& t, float distance, float fov)
		: view(v)
		, tree(t)
		, d(distance)
	{
		pixel = 2 * d * std::tan(fov * 0.008726646f) / float(std::max(1, view.rect.height()));
		midX = float(view.rect.width() / 2);
		sliding = view.scrolled && view.scrollLeft > 0 && view.scrollMs > 0;
		from = view.scrollSpeed > 0 ? 1 : -1;
		p = sliding ? 1 - float(view.scrollLeft) / float(view.scrollMs) : 1;
		selector = view.parent >= 0 && tree[view.parent].kind == Widget::Selector ? &tree[view.parent] : nullptr;
	}

	Vec3 place(int32_t slot, const Sphere& bounds) const
	{
		const MenuRect& r = view.slotRects[slot];
		const float fit = float(std::min(r.width(), r.height())) * pixel, diam = 2 * bounds.radius;
		float x = diam - fit;
		if (x > 0) {
			x = -(d / fit) * x;
		}
		x = std::min(x, -(diam + 2));
		const float across = (float(r.left + r.right) * 0.5f - midX) * pixel;
		return Vec3{x, across * (d - x) / d, 0} - bounds.centre;
	}

	Vec3 slid(int32_t slot, const Sphere& bounds) const
	{
		const Vec3 to = place(slot, bounds);
		const int32_t old = slot + from;
		if (!sliding || !inRange(view.slotRects, old)) {
			return to;
		}
		const Vec3 was = place(old, bounds);
		return was + (to - was) * p;
	}

	// The lights' colours: the selector style's item pair, the selected selector's, the focused item's.
	void colours(int32_t slot, MenuScene::Thing& t) const
	{
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
	}
};

// A ring's camera looks along -x from its distance at its items, one per slot (the focused slot's the
// selection).

void Builders::describeScene(const Menus& m, MenuScene& scene) const
{
	using Thing = MenuScene::Thing;
	const Game& g = *m.game;
	const std::array<uint8_t, 5> working = {g.working.looks[0], g.working.looks[1], g.working.looks[2], g.working.looks[3], 0};
	switch (m.screen) {
	case ScreenDriver:
		m.sceneMinifig(scene, {-5.359f, -3.15f, 0.026f}, {1, 0, 0}, {0, 0, 1}, working, Thing::Builder, state.driverPart,
			state.exitMs >= 0 ? blendedFrame(m.data->driverAnimations, state.driverPart, state.exitMs, true) : state.driverFrame);
		break;
	case ScreenLicense:
		// The photo: CMAMAN's pose, the chosen expression.
		m.sceneMinifig(scene, {-5.359337f, -3.150018f, 0}, {1, 0, 0}, {0, -0.034899f, 0.999391f},
			{working[0], working[1], working[2], working[3], state.expression}, Thing::Photo, 0,
			advance(m.data->photo, 0, 0, state.animMs));
		break;
	case ScreenEditCar:
		// (Spinning on from the builder's angle when it comes back from there, else from 0.)
		m.carGroup(scene, {0, 0, 1}, std::fmod(m_editSpin + float(state.animMs) * 0.001f, Tau), false);
		if (state.animMs == 0 && !scene.things.empty() && scene.things.back().kind == Thing::Minifig) {
			scene.things.back().world.rows = turned(0, MinifigScale); // (its first frame: placed, not yet turned)
		}
		break;
	case ScreenCarBuilder: carBuilderScene(m, scene); break;
	default: break;
	}
}

void Builders::describeRing(const Menus& m, const Widget& view, const std::array<float, 9>& c, MenuScene& scene) const
{
	scene.eye = {c[7], 0, 0};
	scene.fov = c[6], scene.nearPlane = c[7], scene.farPlane = c[8];
	const RingSlots slots(view, m.ui.tree, c[7], c[6]);
	scene.sliding = slots.sliding;
	if (m.screen == ScreenCarBuilder) {
		pieceRing(m, view, slots, builderSet(m), scene);
	}
	else {
		partRing(m, view, slots, scene);
	}
}

// The piece carousel: which slots hold an item, each its choice's piece spinning about the tilted axis.
void Builders::pieceRing(const Menus& m, const Widget& view, const RingSlots& slots, const CarSets::Set& set, MenuScene& scene) const
{
	using Thing = MenuScene::Thing;
	const int32_t n = int32_t(view.values.size()), count = int32_t(view.slotRects.size()), focus = view.focusSlot, k = view.index;
	List<int32_t> holds(size_t(count), -1);
	if (n >= count - 1) {
		for (int32_t i = 0; i < count && n > 0; i++) {
			holds[i] = ((k - focus + i) % n + n) % n;
		}
	}
	else if (n > 0 && focus < count) {
		holds[focus] = k;
		for (int32_t j = 1; j <= std::min(focus, k); j++) {
			holds[focus - j] = k - j;
		}
		const int32_t after = k >= focus ? n - k : count - focus - 1;
		for (int32_t j = 1; j <= after && focus + j < count; j++) {
			holds[focus + j] = (k + j) % n;
		}
	}
	// Its spin's rows: (1, 0, 0) turned by -theta about the tilted axis, made square to it.
	const Vec3 axis = {cosEntry(882), 0, cosEntry(114)};
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
	// (Made items aren't turned before the ring's first tick; an item's place is recentred on its bounds as turned when it
	// was placed (made: unturned; a step: at the step's spin), not every frame.)
	const Basis rows = spun(view.fresh ? -1.0f : view.spin);
	for (int32_t slot = 0; slot < count; slot++) {
		if (holds[slot] < 0) {
			continue;
		}
		const CarSets::Choice& choice = set.choices[view.values[holds[slot]]];
		Thing& t = scene.things.emplace_back();
		t.kind = Thing::Piece;
		t.slot = slot;
		t.type = choice.type;
		t.colour = choice.colour;
		t.angle = view.spin;
		t.lit = true;
		slots.colours(slot, t);
		const Sphere bounds = meshBounds(m.carouselItem(choice.type, choice.colour));
		const Vec3 to = slots.place(slot, {{}, bounds.radius}) - toWorld(spun(view.stepSpin), bounds.centre);
		const int32_t old = slot + slots.from;
		Vec3 at = to;
		if (slots.sliding && inRange(view.slotRects, old)) {
			const Vec3 was = slots.place(old, {{}, bounds.radius}) - toWorld(spun(view.fromSpin), bounds.centre);
			at = was + (to - was) * slots.p;
		}
		t.world = {rows, at};
	}
}

// The driver builder's rings: each slot's minifig part (hats, heads, chests, legs).
void Builders::partRing(const Menus& m, const Widget& view, const RingSlots& slots, MenuScene& scene) const
{
	using Thing = MenuScene::Thing;
	const int32_t count = int32_t(view.values.size());
	for (int32_t slot = 0; slot < view.slots && count; slot++) {
		const int32_t i = view.index + slot - view.focusSlot;
		const int32_t item = view.values[(i % count + count) % count];
		Thing& t = scene.things.emplace_back();
		t.slot = slot;
		t.angle = view.spin;
		t.kind = Thing::Part;
		t.type = item;
		t.colour = int32_t(std::find(m.items.begin(), m.items.begin() + 4, view.parent) - m.items.begin());
		slots.colours(slot, t);
		const PartCatalog& cat = m.data->parts;
		Sphere bounds;
		switch (t.colour) {
		case 0: // hats: their head library entries
			if (inRange(cat.rings[0], item)) {
				t.model = cat.rings[0][item].name;
				bounds = m.data->hatBounds[item];
			}
			break;
		case 1: t.model = "HEADS", bounds = m.data->ringBounds[0]; break;
		case 2: {
			const bool hooked = inRange(cat.rings[2], item) && cat.rings[2][item].variant;
			t.model = hooked ? "CHEST_HK" : "CHESTS", bounds = m.data->ringBounds[hooked ? 2 : 1];
			break;
		}
		default: {
			const bool pegs = inRange(cat.rings[3], item) && cat.rings[3][item].variant;
			t.model = pegs ? "LEG-PEGS" : "LEGSS", bounds = m.data->ringBounds[pegs ? 4 : 3];
			break;
		}
		}
		if (!inRange(view.slotRects, slot)) {
			continue;
		}
		t.world = {{Vec3{1, 0, 0} * MinifigScale, Vec3{0, 1, 0} * MinifigScale, Vec3{0, 0, 1} * MinifigScale}, slots.slid(slot, bounds)};
	}
}

// The car builder: the car group, then the held piece (at its grid place, the height in the car's frame): bobbing ghost,
// landing ghost, the piece; then the hit marks.
void Builders::carBuilderScene(const Menus& m, MenuScene& scene) const
{
	using Thing = MenuScene::Thing;
	builderCamera(m, scene);
	const CarBuilder& b = state.builder;

	m.carGroup(scene, {0, 0, 1}, b.angle, b.pane == 2);
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
		int32_t alpha = int32_t(50 * std::cos(Tau * float(b.bobClock) / 1000.0f)) + 100;
		if (bobbing) {
			const float v = b.bobHeight - b.restHeight - 1.2f;
			alpha = v < 1.2f && !top ? (v < 0 ? 0 : int32_t(v / 1.2f * 150)) : 150;
		}
		piece(b.restHeight, alpha);
	}
	if (moving || !top) {
		piece(moving ? b.dropHeight : 1.0f, 255);
	}
	for (const auto& mark : b.hits) {
		if (b.running & CarBuilder::PieceRise) {
			break;
		}
		Thing& t = scene.things.emplace_back();
		t.kind = Thing::Mark;
		t.at = {float(mark[0]) + state.car.offset[0], float(mark[1]) + state.car.offset[1],
			float(b.overlayHeight) * 0.4f + state.car.offset[2]};
		t.angle = b.angle;
		t.world = {turned(b.angle, 1), {0, 0, 1.1f}};
	}
}

void Builders::builderCamera(const Menus& m, MenuScene& scene) const
{
	const CarBuilder& b = state.builder;
	const std::array<std::array<float, 3>, 3>& Small = executableTables().builderEyes;
	const float large = std::clamp((b.eyeRadius - 5.9f) / (8.5f - 5.9f), 0.0f, 1.0f);
	const auto place = [&](int32_t i) {
		return vec(Small[size_t(i)]) * (1 - large) + Vec3{LargeEyes[i][0], LargeEyes[i][1], LargeEyes[i][2]} * large;
	};
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
	if (const MenuData::MenuWorld* w = m.data->menuWorld("garage"); w && w->hasCamera) {
		scene.fov = w->camera.fov, scene.nearPlane = w->camera.nearPlane, scene.farPlane = w->camera.farPlane;
	}
	scene.aspect = 420.0f / 354.0f; // (the view's viewport)
}

} // namespace lr
