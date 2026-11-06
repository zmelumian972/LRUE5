// The builders: the driver builder, the license, edit car and the car builder; what each builds, does
// each frame, with its items and with the dialogs' answers, and the rule it leaves by. The car's own rules are Car's.

#include "LRCore/Menus/Menus.h"
#include "LRData/Archive.h"
#include "LRCore/Common/Tables.h"

#include "Show/Animation.h"
#include "Menus/BuilderParts.h"
#include "Menus/Layout.h"
#include "Menus/MenuNames.h"
#include "Menus/Pen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace lr {

// Building each screen.

void Builders::buildDriver(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const MenuLayout& layout = m.data->screens[m.screen].layout;
	const bool editing = !g.newRacer;
	const RacerRecord* kept = stored(m);
	m.ui.make(t, layout, "backdrp", "backdrp", -1);
	static const char* const Selectors[4] = {"hatsel", "facesel", "torsosel", "legsel"};
	for (size_t r = 0; r < 4; r++) {
		// Each ring offers the parts the stored racer wears and the ones unlocked, in the catalog's order.
		const int16_t sel = m.ui.make(t, layout, Selectors[r], "partcont", -1);
		const int16_t ring = m.ui.makeCarousel(sel, r == 1 ? "headbox" : "partbox", "picker");
		t.adopt(ring, sel);
		const List<PartCatalog::Part>& parts = m.data->parts.rings[r];
		const uint8_t worn = kept ? kept->looks[RingLook[r]] : 0;
		for (int32_t i = 0; i < int32_t(parts.size()); i++) {
			if (i == worn || offered(parts[i].unlock, g.settings)) {
				t[ring].values.push_back(i);
			}
			if (i == g.working.looks[RingLook[r]] && !t[ring].values.empty() && t[ring].values.back() == i) {
				t[ring].index = int32_t(t[ring].values.size()) - 1;
			}
		}
		m.items[r] = sel;
	}
	m.ui.make(t, layout, "platform", "", -1);
	m.items[DriverMix] = m.ui.make(t, layout, "mix", "nubutton", -1, TextMix);
	m.items[DriverDone] = m.ui.make(t, layout, "gonext", editing ? "buttonch" : "buttonra", -1, editing ? TextOk : TextMakeLicense);
	m.items[DriverCancel] = m.ui.make(t, layout, "goback", "buttonca", -1, TextCancel);
	m.ui.makeBanner(TextBuildDriver);
	m.ui.select(t, m.items[DriverTopRing], IconQuiet | IconSilent);
	driverPick(m);
}

void Builders::buildLicense(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const MenuLayout& layout = m.data->screens[m.screen].layout;
	const bool editing = !g.newRacer;
	m.ui.make(t, layout, "backdrp", "backdrp", -1);
	m.items[LicenseSnapshot] = m.ui.make(t, layout, "swapface", "nubutton", -1, TextSnapshot);
	m.items[LicenseNext] = m.ui.make(t, layout, "gonext", editing ? "buttonch" : "buttonra", -1, editing ? TextOk : TextBuildCar);
	m.items[LicenseBack] = m.ui.make(t, layout, "goback", editing ? "buttonca" : "buttonla", -1, editing ? TextCancel : TextBuildDriver);
	m.ui.make(t, layout, "license", "license", -1);
	// The trophies won: gold, silver or bronze in each circuit's place.
	static const char* const Trophies[3] = {"gold", "silver", "bronze"};
	for (size_t k = 0; k < g.working.trophies.size(); k++) {
		if (const uint8_t trophy = g.working.trophies[k]; trophy >= 1 && trophy <= 3) {
			m.ui.make(t, layout, "trophy" + std::to_string(k + 1), Trophies[trophy - 1], -1);
		}
	}
	m.items[LicenseNameBox] = m.ui.make(t, layout, "firstbox", "roundbox", -1);
	const int16_t field = m.ui.make(t, layout, "ftext", "textedit", -1);
	m.items[LicenseName] = field;
	m.ui.setCharset(t, field, TextCharset);
	m.ui.setText(t, field, g.working.name.empty() ? m.text(TextPlayer) : std::u16string_view(g.working.name));
	m.ui.make(t, layout, "platform", "", -1);
	m.ui.makeBanner(TextMakeLicense);
	state.expression = g.working.expression;
	m.ui.select(t, field, IconQuiet | IconSilent);
}

void Builders::buildEditCar(Menus& m)
{
	Game& g = *m.game;
	m_editSpin = std::exchange(m_spinKept, false) ? m_editSpin : 0;
	WidgetTree& t = m.ui.tree;
	const MenuLayout& layout = m.data->screens[m.screen].layout;
	const bool editing = !g.newRacer;
	m.ui.make(t, layout, "backdrp", "backdrp", -1);
	m.ui.make(t, layout, "garage", "", -1);
	m.ui.makeBanner(TextBuildCar);
	m.items[EditBuild] = m.ui.make(t, layout, "build", "nubutton", -1, TextBuild);
	m.items[EditRemoveBricks] = m.ui.make(t, layout, "ripapart", "nubutton", -1, TextRemoveBricks);
	m.items[EditQuickBuild] = m.ui.make(t, layout, "qbuild", "nubutton", -1, TextQuickBuild);
	m.items[EditDone] = m.ui.make(t, layout, "gonext", "buttonch", -1, editing ? TextOk : TextFinish);
	m.items[EditBack] = m.ui.make(t, layout, "goback", editing ? "buttonca" : "buttonla", -1, editing ? TextCancel : TextMakeLicense);
	const int16_t sel = m.ui.make(t, layout, "selector", "imagecon", -1);
	const int16_t car = m.ui.makeCarousel(sel, "imagebox", "picker");
	m.items[EditChassis] = sel;
	state.car.load(m.data->pieces, g.working.pieces);
	state.sets = setsOffered(m.data->sets, g.settings, state.car);
	for (int32_t set : state.sets) {
		m.ui.addItem(t, car, m.ui.make(t, layout, "setimage", SetImages[set], -1));
	}
	if (!state.car.entries.empty()) {
		state.car.chooseOffset(m.data->pieces);
		m.carMesh = state.car.menuMesh(m.data->pieces, m.data->sets.materials);
		const PieceLibrary::Piece* chassis = m.data->pieces.find(g.working.chassis);
		for (size_t i = 0; chassis && i < state.sets.size(); i++) {
			if (m.data->sets.sets[state.sets[i]].part == chassis->type) {
				m.ui.selectItem(t, car, int32_t(i));
			}
		}
	}
	else {
		changeChassis(m, state.sets[0]); // a new racer's: the first set's (a draw for its colour)
	}
	editCarButtons(m);
	m.ui.select(t, m.items[EditBuild], IconQuiet | IconSilent);
}

void Builders::buildCarBuilder(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const MenuLayout& layout = m.data->screens[m.screen].layout;
	const int16_t backdrop = m.ui.make(t, layout, "backdrp", "backdrp", -1);
	m.ui.make(t, layout, "garage", "", -1);
	state.car.load(m.data->pieces, g.working.pieces);
	state.car.chooseOffset(m.data->pieces);
	m.carMesh = state.car.menuMesh(m.data->pieces, m.data->sets.materials);
	state.sets = setsOffered(m.data->sets, g.settings, state.car);
	const int16_t sets = m.ui.make(t, layout, "sets", "imagecon", -1);
	const int16_t box = m.ui.makeCarousel(sets, "imagebox", "pieceset");
	for (int32_t set : state.sets) {
		m.ui.addItem(t, box, m.ui.make(t, layout, "setimage", SetImages[set], -1));
	}
	const int16_t pieces = m.ui.make(t, layout, "pieces", "partcont", -1);
	const int16_t ring = m.ui.makeCarousel(pieces, "piecesel", "picker");
	m.items[BuilderSets] = sets;
	m.items[BuilderPieces] = pieces;
	// The move pad, the rotate / place / undo buttons, the view pad, the held piece's and the car's mouse areas
	// (placed by the presentation over them: hidden here), the door.
	const auto at = [&](int16_t w, int32_t x, int32_t y) {
		Widget& n = t[w];
		n.rect = {x, y, x + n.rect.width(), y + n.rect.height()};
	};
	m.items[BuilderMovePad] = m.ui.make(t, layout, "procker", "procker", backdrop);
	at(m.items[BuilderMovePad], 51, 40);
	m.items[BuilderTurn] = m.ui.make(t, layout, "rotbrik", "icon", -1);
	m.items[BuilderAdd] = m.ui.make(t, layout, "addbrik", "icon", -1);
	m.items[BuilderUndo] = m.ui.make(t, layout, "undobrik", "icon", -1);
	m.items[BuilderViewPad] = m.ui.make(t, layout, "crocker", "crocker", backdrop);
	at(m.items[BuilderViewPad], 51, 301);
	m.items[BuilderPieceArea] = m.ui.make(t, layout, "piece", "icon", -1);
	m.items[BuilderCarArea] = m.ui.make(t, layout, "car", "icon", -1);
	m.ui.show(t, m.items[BuilderPieceArea], false);
	m.ui.show(t, m.items[BuilderCarArea], false);
	m.items[BuilderBack] = m.ui.make(t, layout, "goback", "icon", -1);
	CarBuilder& b = state.builder;
	b = {};
	offerChoices(t[ring], m.data->sets.sets[state.sets[0]].choices.size());
	takeChoice(m);
	// The view: slot 1, the camera jumping to its middle height.
	b.place = 1;
	b.pitch = 1;
	b.placeMs = b.placeTime = 1;
	b.pitchFrom = 0;
	b.running = CarBuilder::ViewHeight;
	takeGroupSphere(m, true);
	b.eyeRadius = b.group.radius;
	m.ui.select(t, sets, IconQuiet | IconSilent);
}

// Each frame, before the input: the driver's animation, the builder's view and held piece.

// The screen's own update.

void Builders::updateBuilders(Menus& m)
{
	Game& g = *m.game;
	if (m.screen == ScreenCarBuilder) {
		placeMouseAreas(m);
	}
	if (m.screen == ScreenLicense) {
		m.ui.show(m.ui.tree, m.items[LicenseNameBox],
			m.ui.tree[m.items[LicenseName]].state & Widget::Selected); // the name's frame marks it selected
	}
	if (m.screen == ScreenDriver) {
		if (state.exitMs >= 0) {
			if (partEnded(m.data->driverAnimations, state.driverPart,
					blendedFrame(m.data->driverAnimations, state.driverPart, state.exitMs, true))) {
				state.exitMs = -1;
				m.navigate(g.newRacer ? ScreenLicense : ScreenGarage);
			}
		}
		else if (partEnded(m.data->driverAnimations, state.driverPart, state.driverFrame) && !m.pending) {
			driverPick(m);
		}
	}
}

void Builders::driverPick(Menus& m)
{
	Game& g = *m.game;
	// The part queued by a ring's change, else a random idle one; never the one that just played.
	int32_t part = state.queuedPart;
	state.queuedPart = -1;
	while (part < 0 || part == state.driverPart) {
		part = partIndex(m.data->driverAnimations, DriverIdle[g.draw() % 8]);
	}
	state.driverPart = part;
	state.driverFrame = 0;
}

RacerRecord* Builders::stored(Menus& m)
{
	Game& g = *m.game;
	const RacerRef r = g.chosen[0];
	std::vector<RacerRecord>& list = g.list(r.list);
	return r.list != RacerList::None && r.list != RacerList::Defaults && r.index < list.size() ? &list[r.index] : nullptr;
}

// Edit car.

void Builders::editCarAction(Menus& m, int32_t action)
{
	Game& g = *m.game;
	if (action == 1) {
		// REMOVE BRICKS: the bare chassis again.
		changeChassis(m, state.sets[m.ui.tree[m.ui.tree[m.items[EditChassis]].carousel].index]);
		return;
	}
	// QUICK BUILD: the next stored car on this chassis, from the one after the cursor on.
	const auto& builds = m.data->quickBuilds;
	for (int32_t n = 0; n < 25 + 5 && !builds.empty(); n++) {
		state.quickBuild = (state.quickBuild + 1) % int32_t(builds.size());
		if (builds[state.quickBuild].chassis == g.working.chassis) {
			state.car.load(m.data->pieces, builds[state.quickBuild].pieces);
			g.working.stockCar = true;
			carChanged(m);
			return;
		}
	}
}

void Builders::changeChassis(Menus& m, int32_t set)
{
	Game& g = *m.game;
	// The car is the set's bare chassis, in a random colour.
	const CarSets::Set& s = m.data->sets.sets[set];
	state.car.clear();
	g.working.chassis = s.chassis.substr(0, 8);
	if (const PieceLibrary::Piece* p = m.data->pieces.find(s.chassis)) {
		state.car.place(m.data->pieces, {uint16_t(p->type), 0, 0, 0, 3, 0}, g);
	}
	g.working.stockCar = true;
	carChanged(m);
}

void Builders::carChanged(Menus& m)
{
	Game& g = *m.game;
	g.working.pieces = state.car.pieces();
	state.car.chooseOffset(m.data->pieces);
	m.carMesh = state.car.menuMesh(m.data->pieces, m.data->sets.materials);
	editCarButtons(m);
}

void Builders::editCarButtons(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const bool bricks = state.car.entries.size() > 1;
	m.ui.enable(t, m.items[EditChassis], !bricks || g.working.stockCar);
	m.ui.enable(t, m.items[EditRemoveBricks], bricks);
}

// The car builder's held piece.

// Keys the screens take before the widgets.

// The builder by mouse.

// The items' notices.

void Builders::backToGarage(Menus& m, bool changed)
{
	if (changed) {
		m.ui.ask(TextLoseChanges, TextContinue, TextCancel);
	}
	else {
		m.navigate(ScreenGarage);
	}
}

void Builders::builderNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	switch (m.screen) {
	case ScreenDriver: driverNotice(m, t, w, what); return;
	case ScreenLicense: licenseNotice(m, t, w, what); return;
	case ScreenEditCar: editCarNotice(m, t, w, what); return;
	case ScreenCarBuilder: carBuilderNotice(m, t, w, what); return;
	default: return;
	}
}

void Builders::driverNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	RacerRecord* const kept = stored(m);
	// Ring r's part worn.
	const auto wear = [&](size_t r) {
		const Widget& ring = t[t[m.items[r]].carousel];
		g.working.looks[RingLook[r]] = uint8_t(ring.values[ring.index]);
	};
	if (what == NoticeValue) {
		for (size_t r = 0; r < 4; r++) {
			if (!is(r)) {
				continue;
			}
			// The part worn changes, and the minifig shows it off.
			wear(r);
			state.changed = true;
			const char* next = r < 2 ? "head1" : "chest";
			if (r > 2) {
				next = g.draw() & 1 ? "shaking" : "leg";
			}
			state.queuedPart = partIndex(m.data->driverAnimations, next);
		}
		return;
	}
	if (what != NoticeReleased || state.exitMs >= 0) {
		return;
	}
	if (is(DriverMix)) {
		// MIX: a random part on every ring.
		for (size_t r = 0; r < 4; r++) {
			const int16_t ring = t[m.items[r]].carousel;
			m.ui.selectItem(t, ring, int32_t(g.draw() % t[ring].values.size()));
			wear(r);
		}
		state.changed = true;
	}
	else if (is(DriverDone)) {
		if (!g.newRacer && kept) {
			kept->looks = g.working.looks;
			g.markRacers(g.chosen[0].list);
		}
		// The exit animation, then off.
		state.saving = true;
		state.driverPart = partIndex(m.data->driverAnimations, g.draw() % 2 ? "kiss" : "jump");
		state.exitMs = 0;
	}
	else if (is(DriverCancel)) {
		if (g.newRacer) {
			m.ui.ask(TextCancelNewRacer, TextYes, TextNo);
		}
		else {
			backToGarage(m, kept && kept->looks != g.working.looks);
		}
	}
}

void Builders::licenseNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	RacerRecord* const kept = stored(m);
	if (what != NoticeReleased) {
		return;
	}
	const std::u16string& name = t[m.items[LicenseName]].text;
	if (is(LicenseSnapshot)) {
		state.expression = uint8_t((state.expression + 1) % 6);
	}
	else if (is(LicenseNext) || (is(LicenseBack) && g.newRacer)) {
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
		m.navigate(is(LicenseBack) ? ScreenDriver : g.newRacer ? ScreenEditCar : ScreenGarage);
	}
	else if (is(LicenseBack)) {
		backToGarage(m, kept && (kept->name != name || kept->expression != state.expression));
	}
}

void Builders::editCarNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	RacerRecord* const kept = stored(m);
	if (what == NoticeValue && is(EditChassis)) {
		changeChassis(m, state.sets[t[t[w].carousel].index]);
		return;
	}
	if (what != NoticeReleased) {
		return;
	}
	if (is(EditBuild)) {
		m.navigate(ScreenCarBuilder);
	}
	else if (is(EditRemoveBricks) || is(EditQuickBuild)) {
		// Changes to a car of the player's own are lost: asked first.
		state.afterNotice = is(EditRemoveBricks) ? 1 : 2;
		if (g.working.stockCar) {
			editCarAction(m, state.afterNotice);
		}
		else {
			m.ui.ask(TextLoseChanges, TextContinue, TextCancel);
		}
	}
	else if (is(EditDone)) {
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
		m.navigate(ScreenGarage);
	}
	else if (is(EditBack)) {
		if (g.newRacer) {
			m.navigate(ScreenLicense);
		}
		else if (kept && kept->pieces != g.working.pieces) {
			state.afterNotice = 3;
			m.ui.ask(TextLoseChanges, TextContinue, TextCancel);
		}
		else {
			m.navigate(ScreenGarage);
		}
	}
}

void Builders::carBuilderNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	if (what == NoticeValue && (is(BuilderMovePad) || is(BuilderViewPad)) && t[w].lit && state.builder.mode != CarBuilder::Busy) {
		builderPad(m, is(BuilderMovePad), t[w].lit);
		return;
	}
	if (what != NoticeReleased) {
		if (what == NoticeValue && (is(BuilderSets) || is(BuilderPieces))) {
			// A new set offers its pieces from the first; either way the hand takes the choice.
			if (is(BuilderSets)) {
				offerChoices(t[t[m.items[BuilderPieces]].carousel], builderSet(m).choices.size());
				m.ui.selectItem(t, t[m.items[BuilderPieces]].carousel, 0);
			}
			takeChoice(m);
		}
		return;
	}
	if (state.builder.mode == CarBuilder::Busy) {
		return;
	}
	if (is(BuilderTurn)) {
		turnHeld(m);
	}
	else if (is(BuilderAdd)) {
		commit(m);
	}
	else if (is(BuilderUndo)) {
		undo(m);
	}
	else if (is(BuilderBack)) {
		g.working.pieces = state.car.pieces();
		m.navigate(ScreenEditCar);
	}
}

void Builders::builderDialog(Menus& m, MenuDialog& d)
{
	Game& g = *m.game;
	const bool yes = d.result == MenuDialog::Yes;
	if (yes && m.screen == ScreenDriver && g.newRacer) {
		// The new racer goes.
		std::vector<RacerRecord>& list = g.list(g.chosen[0].list);
		if (g.chosen[0].index < list.size()) {
			list.erase(list.begin() + g.chosen[0].index);
			g.markRacers(g.chosen[0].list);
		}
		g.newRacer = false;
	}
	if (yes && m.screen == ScreenEditCar && state.afterNotice != 3) {
		editCarAction(m, state.afterNotice);
	}
	else if (yes) {
		m.navigate(ScreenGarage);
	}
	m.ui.closeDialog();
}

// The rules the builders leave by.

void Builders::leaveBuilder(Menus& m)
{
	if (m.screen == ScreenCarBuilder) {
		m_editSpin = state.builder.angle; // (edit car's car turns on from the builder's angle)
		m_spinKept = true;
		m.pop(1);
		return;
	}
	if (m.screen == ScreenEditCar && m.destination == ScreenCarBuilder) {
		m.push(ScreenCarBuilder);
		return;
	}
	m.pop(1);
	if (m.destination != ScreenGarage) {
		m.push(uint16_t(m.destination));
	}
	else if (state.saving) {
		m.push(ScreenSaveAll);
	}
}

// What the builders' 3D views show.

} // namespace lr
