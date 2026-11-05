// The front end's screens: what each builds, does each frame and with its items, and the rule it leaves by. The
// builders, the race setup screens, racer select and the cinematics' visuals are other parts'.

#include "LRCore/Menus/Menus.h"

#include "Show/Animation.h"
#include "Menus/MenuNames.h"

#include <algorithm>
#include <cstdio>

namespace lr {

namespace {

bool cinematic(uint16_t n)
{
	return n == ScreenLegal || n == ScreenCredits || (n >= ScreenAward1 && n <= ScreenVeronicaCar) || (n >= ScreenCircuitIntro && n <= ScreenCircuitIntro + 6);
}
bool step(uint16_t n) { return (n >= ScreenLoadAll && n <= ScreenSaveCard) || n == ScreenPickCard || n == ScreenNewRacer; }

// The idle animations the garage's minifig picks from (RSANIM.ADB's parts by name).
const char* const IdleParts[7] = {"breath1", "breath2", "swayf", "watch", "hips", "tapfoot", "hey!"};

// The main menu's minifig: where its part is, in frames, after the 200 ms blend into it (the first part has none).
float minifigFrame(const AnimationFile& file, int32_t part, int32_t ms) { return blendedFrame(file, part, ms, part != 0); }

// The name a key shows on the controls screen: a letter's letter, else "KEY" and its code.
std::u16string keyName(uint32_t binding)
{
	static const char Letters[] = "QWERTYUIOP\0\0\0\0ASDFGHJKL\0\0\0\0\0ZXCVBNM";
	const uint32_t code = binding & 0xfffffff;
	if ((binding >> 28) == 1 && code >= 0x10 && code < 0x10 + sizeof(Letters) - 1 && Letters[code - 0x10]) {
		return std::u16string(1, char16_t(Letters[code - 0x10]));
	}
	char text[16];
	std::snprintf(text, sizeof(text), "KEY %02X", code);
	return std::u16string(text, text + std::char_traits<char>::length(text));
}

} // namespace

// Building a screen when it becomes the top.

int16_t Menus::screenItem(uint32_t binding, uint32_t style, int32_t text) { return makeNamed(tree, binding, style, -1, text); }

int16_t Menus::makeBanner(int32_t text)
{
	const int16_t b = screenItem(StyleBanner, StyleBanner, text);
	alignRight(tree, b, 20);
	return b;
}

// A selector over a carousel of labels (or images).
int16_t Menus::makeLabelSelector(uint32_t binding, uint32_t style, uint32_t box)
{
	WidgetTree& t = tree;
	const int16_t sel = makeNamed(t, binding, style, -1);
	if (sel < 0) {
		return sel;
	}
	const int16_t car = makeNamed(t, box, StylePicker, sel);
	t[sel].carousel = car;
	t[car].colour = t[sel].colour;
	return sel;
}

int16_t Menus::addLabel(int16_t sel, std::u16string_view text)
{
	WidgetTree& t = tree;
	const int16_t l = makeNamed(t, NameContext, StyleMenuFont, -1);
	setText(t, l, text);
	addItem(t, t[sel].carousel, l);
	return l;
}

// The load and save screens: the operation runs as the screen is made; it leaves on its first update.
void Menus::buildStorage(Game& g)
{
	if (screen == ScreenLoadAll) {
		for (auto& [slot, bytes] : g.openSlots()) {
			saves.push_back({slot, std::move(bytes)});
		}
	}
	else if (screen == ScreenSaveAll) {
		saveSlots(g, true);
		if (g.saveReminder) {
			g.saveReminder = false; // (its backdrop is skipped once)
			return;
		}
	}
	else if (screen == ScreenOpenCard || screen == ScreenSaveCard) {
		if (screen == ScreenSaveCard || !g.cards[g.currentCard].empty()) {
			saveSlots(g, false);
		}
	}
	screenItem(NameBackdrop, NameBackdrop, -1);
}

void Menus::buildMainMenu(Game& g)
{
	WidgetTree& t = tree;
	if (tune < 0) {
		playTune(0, true);
	}
	screenItem(NameBackdrop, NameBackdrop, -1);
	screenItem(NameLogo, NameLogo, -1);
	screenItem(NamePlatform, NamePlatform, -1);
	static const uint32_t Items[7][2] = {{3, 37}, {85, 33}, {86, 34}, {87, 35}, {88, 36}, {8, 38}, {89, 39}};
	for (size_t i = 0; i < 7; i++) {
		state.items[i] = screenItem(Items[i][0], StyleItem, int32_t(Items[i][1]));
	}
	if (g.pads == 0) {
		enable(t, state.items[3], false); // VERSUS RACE needs a joystick
	}
	g.chosen = {};
	g.players = 0;
	select(t, state.items[0], true, false, true);
	state.idleMs = 30000;
	state.attractMs = 60000;
	state.animPart = 0;
}

void Menus::buildOptions(Game& g)
{
	WidgetTree& t = tree;
	if (tune < 0) {
		playTune(0, true);
	}
	g.dirty = true; // it sets the racer and lap counts as it opens (their setters always mark the state)
	screenItem(NameBackdrop, NameBackdrop, -1);
	state.items[0] = makeBanner(16);
	// Page 0, then page 1's and page 2's items. (VIDEO OPTIONS opens the presentation's own settings: page 3 isn't built;
	// nor are the stereo selector and the brightness label the original makes but never shows.)
	static const uint32_t Page0[8][2] = {{237, 17}, {238, 18}, {239, 19}, {240, 23}, {241, 24}, {242, 156}, {39, 88}, {NameBack, 2}};
	for (size_t i = 0; i < 8; i++) {
		state.items[1 + i] = screenItem(Page0[i][0], i == 7 ? StyleLeftArrow : StyleItem, int32_t(Page0[i][1]));
	}
	state.items[9] = makeLabelSelector(243, StyleTextSelector, NameTextBox);
	for (int32_t i = 0; i < 6; i++) {
		addLabel(state.items[9], text(96 + i));
	}
	state.items[10] = makeLabelSelector(244, StyleTextSelector, NameTextBox);
	for (int32_t i = 0; i < 5; i++) {
		addLabel(state.items[10], text(97 + i));
	}
	selectItem(t, t[state.items[9]].carousel, g.settings.opponents);
	selectItem(t, t[state.items[10]].carousel, g.settings.laps - 1);
	state.items[11] = screenItem(256, StyleMenuFont, 89);
	state.items[12] = screenItem(257, StyleMenuFont, 90);
	state.items[13] = screenItem(249, StyleSoundSlider, -1);
	state.items[14] = screenItem(250, StyleSoundSlider, -1);
	setValue(t, state.items[13], volumeStep(g.settings.music));
	setValue(t, state.items[14], volumeStep(g.settings.sound));
	state.items[15] = screenItem(260, StyleMenuFont, 93);
	state.rows[0] = screenItem(261, StyleMenuFont, 94);
	showPage(0);
}

void Menus::buildControls(Game& g)
{
	WidgetTree& t = tree;
	state.player = screen == ScreenControl1 ? 0 : 1;
	screenItem(NameBackdrop, NameBackdrop, -1);
	makeBanner(state.player ? 24 : 23);
	state.items[0] = makeLabelSelector(273, StyleImageSelector, NameImageBox);
	// The devices: the joysticks present, then three keyboard layouts.
	for (uint8_t j = 0; j < 2 && j < g.pads; j++) {
		state.entries.push_back(j);
	}
	for (uint8_t k = 0; k < 3; k++) {
		state.entries.push_back(uint8_t(g.pads + 2 + k));
	}
	for (uint8_t e : state.entries) {
		const int16_t image = make(t, data->screens[screen].layout, e < 2 ? "joystick" : "keyboard", e < 2 ? "joystick" : "keyboard", -1);
		addItem(t, t[state.items[0]].carousel, image);
	}
	int32_t chosen = 0;
	for (size_t i = 0; i < state.entries.size(); i++) {
		chosen = state.entries[i] == g.settings.layout[state.player] ? int32_t(i) : chosen;
	}
	selectItem(t, t[state.items[0]].carousel, chosen);
	for (size_t r = 0; r < 9; r++) {
		state.rows[r] = screenItem(264 + uint32_t(r), StyleItem, -1);
		screenItem(278 + uint32_t(r), StyleMenuFont, 105 + int32_t(r));
	}
	state.items[1] = screenItem(NameBack, StyleCheck, 30);
	refreshControls(g);
	select(t, state.rows[0], true, false, true);
}

void Menus::buildPickLanguage(Game& g)
{
	WidgetTree& t = tree;
	screenItem(NameBackdrop, NameBackdrop, -1);
	state.items[0] = makeLabelSelector(311, StyleTextSelector, NameTextBox);
	for (int32_t i = 0; i < 9; i++) {
		addLabel(state.items[0], text(157 + i));
	}
	selectItem(t, t[state.items[0]].carousel, g.settings.language);
	state.items[1] = screenItem(NameBack, StyleLeftArrow, 16);
	makeBanner(156);
	select(t, state.items[0], true, false, true);
}

void Menus::build(Game& g)
{
	screen = top();
	language = g.settings.language; // a language picked takes effect as the next screen is made
	state = {};
	pending = false;
	destination = -1;
	queued = 0; // keys from before the screen existed are dropped
	WidgetTree& t = tree;
	makeRoot(t);
	cursorOn = !cinematic(screen) && !step(screen);
	switch (screen) {
	case ScreenLegal:
	case ScreenCredits:
		make(t, data->screens[screen].layout, screen == ScreenLegal ? "legal" : "credits", "backdrp", 0);
		startCinema(g);
		if (screen == ScreenCredits) {
			playTune(17, true);
		}
		break;
	case ScreenLoadAll:
	case ScreenLoadCard:
	case ScreenOpenCard:
	case ScreenSaveAll:
	case ScreenSaveCard: buildStorage(g); break;
	case ScreenPickCard:
	case ScreenNewRacer:
		screenItem(NameBackdrop, NameBackdrop, -1);
		if (screen == ScreenNewRacer) {
			newRacerStep(g);
		}
		break;
	case ScreenMainMenu: buildMainMenu(g); break;
	case ScreenOptions: buildOptions(g); break;
	case ScreenControl1:
	case ScreenControl2: buildControls(g); break;
	case ScreenPickLanguage: buildPickLanguage(g); break;
	case ScreenGarage: buildGarage(g); break;
	case ScreenDriver: buildDriver(g); break;
	case ScreenLicense: buildLicense(g); break;
	case ScreenEditCar: buildEditCar(g); break;
	case ScreenCarBuilder: buildCarBuilder(g); break;
	case ScreenCircuitRace:
	case ScreenSingleRace:
	case ScreenTimeRace: buildRaceSetup(g); break;
	case ScreenRacerSelect:
	case ScreenPlayer2Select: buildRacerSelect(g); break;
	default:
		if (cinematic(screen)) {
			// A cinematic screen: its one scene view.
			make(t, data->screens[screen].layout, data->names.narrow(screen), "", 0);
			startCinema(g);
		}
		break;
	}
	openLighting();
}

void Menus::leaveScreen()
{
	if (cinematic(screen)) {
		stopTune();
	}
}

// Options' pages.

void Menus::showPage(int32_t page)
{
	WidgetTree& t = tree;
	state.page = page;
	for (size_t i = 1; i <= 7; i++) {
		show(t, state.items[i], page == 0);
	}
	for (size_t i : {size_t(9), size_t(10), size_t(11), size_t(12)}) {
		show(t, state.items[i], page == 1);
	}
	for (size_t i : {size_t(13), size_t(14), size_t(15)}) {
		show(t, state.items[i], page == 2);
	}
	show(t, state.rows[0], page == 2);
	relabel(t, state.items[0], page == 0 ? 16 : page == 1 ? 17 : 19);
	alignRight(t, state.items[0], 20);
	setString(t, state.items[8], page == 0 ? 2 : 16);
	const int16_t focus = page == 0 ? state.items[1] : page == 1 ? state.items[9] : state.items[13];
	select(t, focus, true, false, true);
}

// The controls screen's rows: each binding's key name, dots while waiting.

void Menus::refreshControls(Game& g)
{
	WidgetTree& t = tree;
	const Widget& car = t[t[state.items[0]].carousel];
	const size_t layout = state.entries[size_t(car.index)];
	for (size_t r = 0; r < 9; r++) {
		const uint32_t binding = g.settings.layouts[layout].bindings[r];
		const std::u16string name = state.waiting == int32_t(r) || !binding ? std::u16string(u"........") : keyName(binding);
		if (t[t[state.rows[r]].label].text != name) {
			setText(t, state.rows[r], name);
		}
		if (r < 2) {
			enable(t, state.rows[r], layout >= 2);
		}
	}
}

// The garage.

void Menus::buildGarage(Game& g)
{
	WidgetTree& t = tree;
	if (tune < 0) {
		playTune(1, true);
	}
	if (depth == 1) {
		depth = 0;
		push(ScreenMainMenu);
		push(ScreenGarage);
	}
	g.newRacer = g.copying = g.backToGarage = false;
	g.chosen = {};
	// The racers: the session's, then each card'state.
	state.racers.clear();
	for (uint16_t i = 0; i < g.session.size(); i++) {
		state.racers.push_back({RacerList::Session, i});
	}
	for (size_t c = 0; c < 2; c++) {
		for (uint16_t i = 0; i < g.cards[c].size(); i++) {
			state.racers.push_back({c ? RacerList::Card1 : RacerList::Card0, i});
		}
	}
	const int32_t n = int32_t(state.racers.size());
	state.cursor = 0;
	if (n > 1) {
		// Forward round the ring to the kept choice; back at the start, one step more.
		bool found = false;
		for (int32_t k = 1; k < n && !found; k++) {
			state.cursor = k;
			found = state.racers[size_t(k)] == g.settings.picks[0];
		}
		if (!found) {
			state.cursor = 1 % n;
		}
	}
	makeNamed(t, NameBackdrop, NameBackdrop, -1);
	makeNamed(t, 110, NamePlatform, -1);
	const int16_t sel = makeNamed(t, 112, StyleTextSelector, -1);
	const int16_t car = makeNamed(t, NameTextBox, StylePicker, sel);
	t[sel].carousel = car;
	t[car].colour = t[sel].colour;
	state.items[0] = sel;
	for (int32_t k = 0; k < 3 && n > 0; k++) {
		const int16_t l = makeNamed(t, NameContext, StyleMenuFont, -1);
		addItem(t, car, l);
	}
	fillRacerNames(g);
	static const uint32_t Page0[5][2] = {{144, 40}, {145, 41}, {146, 42}, {147, 44}, {148, 45}};
	for (size_t i = 0; i < 5; i++) {
		state.items[1 + i] = makeNamed(t, Page0[i][0], StyleItem, -1, int32_t(Page0[i][1]));
	}
	state.items[6] = makeNamed(t, NameBack, StyleLeftArrow, -1, 2);
	static const uint32_t Page1[3][2] = {{15, 9}, {152, 10}, {17, 11}};
	for (size_t i = 0; i < 3; i++) {
		state.items[7 + i] = makeNamed(t, Page1[i][0], StyleItem, -1, int32_t(Page1[i][1]));
	}
	state.items[10] = makeNamed(t, StyleBanner, StyleBanner, -1, 3);
	alignRight(t, state.items[10], 20);
	garagePage(0);
}

void Menus::fillRacerNames(Game& g)
{
	WidgetTree& t = tree;
	const int16_t car = t[state.items[0]].carousel;
	const int32_t n = int32_t(state.racers.size());
	int32_t k = -1;
	for (int16_t l = t[car].first; l >= 0; l = t[l].next, k++) {
		const RacerRecord* r = n ? g.racer(state.racers[size_t((state.cursor + k + n) % n)]) : nullptr;
		setText(t, l, r ? std::u16string_view(r->name) : std::u16string_view());
	}
	if (n) {
		selectItem(t, car, 1);
	}
}

void Menus::garagePage(int32_t page)
{
	WidgetTree& t = tree;
	state.page = page;
	const int32_t n = int32_t(state.racers.size());
	for (size_t i = 1; i <= 5; i++) {
		show(t, state.items[i], page == 0);
	}
	for (size_t i = 7; i <= 9; i++) {
		show(t, state.items[i], page == 1);
	}
	relabel(t, state.items[10], page == 0 ? 3 : 4);
	alignRight(t, state.items[10], 20);
	setString(t, state.items[6], page == 0 ? 2 : 3);
	if (page == 0) {
		// QUIRK: "full" is exactly 209 racers, which two cards can't hold.
		const bool full = n == 209;
		enable(t, state.items[1], !full);
		enable(t, state.items[2], n > 0);
		enable(t, state.items[3], n > 0 && !full);
		enable(t, state.items[4], n > 0);
		enable(t, state.items[5], n > 0);
		enable(t, state.items[0], n > 1);
		select(t, full ? state.items[2] : state.items[1], true, false, true);
	}
	else {
		enable(t, state.items[0], false);
		select(t, state.items[7], true, false, true);
	}
	if (page == 0) {
		state.shown = -2; // the slot is set again: a new idle animation
	}
}

void Menus::newRacerStep(Game& g)
{
	// Into the current card: a copy of the garage's racer, or a new empty one.
	std::vector<RacerRecord>& list = g.cards[g.currentCard];
	const RacerList card = g.currentCard ? RacerList::Card1 : RacerList::Card0;
	if (list.size() >= Game::CardCapacity) {
		cursorOn = true;
		openDialog(MenuDialog::OneButton, 188);
		giveButton(NameDialog, StyleCheck, TextOk);
		showDialog(0);
		return;
	}
	if (g.copying) {
		const RacerRecord* from = g.racer(g.chosen[0]);
		const RacerRecord copy = from ? *from : RacerRecord();
		list.push_back(copy);
		g.markRacers(card);
		g.chosen[0] = {card, uint16_t(list.size() - 1)};
		navigate(ScreenGarage);
	}
	else {
		list.emplace_back();
		g.markRacers(card);
		g.chosen[0] = {card, uint16_t(list.size() - 1)};
		g.working = list.back();
		navigate(ScreenDriver);
	}
}

// Each frame's timers that belong to the screen (with the widgets').

void Menus::tickScreen(int32_t dt)
{
	if (cinematic(screen)) {
		updateCinema(dt);
	}
	if (raceScreen(screen)) {
		tickRaceScreens(dt);
	}
	if (screen == ScreenMainMenu) {
		state.animMs += dt;
	}
	if (screen == ScreenGarage && state.driverPart >= 0) {
		state.driverFrame = advance(data->showcase, state.driverPart, state.driverFrame, dt);
	}
	tickBuilders(dt);
}

// The screen's own update, then its leave rule when it navigates.

// The main menu: the minifig's waiting animation (the glass tap once each time it passes its mark), a circuit's
// notice, the attract demo's wait, quitting.
void Menus::updateMainMenu(Game& g, int32_t dt)
{
	state.idleMs = std::max(0, state.idleMs - dt);
	state.attractMs = std::max(0, state.attractMs - dt);
	// The glass tap: once each time the waiting animation passes its mark.
	const bool past = minifigFrame(data->minifig, state.animPart, state.animMs) >= 208;
	if (state.animPart == 2 && past && state.tapArmed) {
		play(29);
		state.tapArmed = false;
	}
	else if (!past) {
		state.tapArmed = true;
	}
	if (g.circuitNotice) {
		g.circuitNotice = false;
		openDialog(MenuDialog::OneButton, 124);
		giveButton(NameDialog, StyleCheck, TextOk);
		showDialog(0);
	}
	if (state.idleMs == 0) {
		state.animPart = 2;
		state.animMs = 0;
		state.idleMs = 30000;
	}
	else if (state.animPart != 1 && partEnded(data->minifig, state.animPart, minifigFrame(data->minifig, state.animPart, state.animMs))) {
		state.animPart = 1; // looping
		state.animMs = 0;
	}
	if (state.attractMs == 0) {
		navigate(-1);
	}
	if (state.quitting) {
		navigate(-1);
		quit = true;
	}
}

// The garage: a page asked for, the racer's list button, and the shown racer's idle animations.
void Menus::updateGarage(Game& g)
{
	if (state.nextPage >= 0) {
		garagePage(state.nextPage);
		state.nextPage = -1;
	}
	const int32_t n = int32_t(state.racers.size());
	if (state.items[3] >= 0 && n) {
		const bool session = state.racers[size_t(state.cursor)].list == RacerList::Session;
		const std::u16string want(text(session ? 43 : 42));
		if (tree[tree[state.items[3]].label].text != want) {
			setString(tree, state.items[3], session ? 43 : 42);
		}
	}
	// The slot: a racer shown anew starts a random idle animation; one that ended starts another.
	if (n && state.shown != state.cursor) {
		// The racer's driver is put in the scene anew: it stands playing its first part until it is given another.
		state.shown = state.cursor;
		state.driverPart = 0;
		state.driverFrame = 0;
		if (!pending) {
			idleAnimation(g);
		}
	}
	else if (n && partEnded(data->showcase, state.driverPart, state.driverFrame) && !pending) {
		idleAnimation(g);
	}
}

void Menus::updateScreen(Game& g, int32_t dt)
{
	switch (screen) {
	case ScreenLegal:
	case ScreenCredits:
		state.skippable = true;
		if (state.finished) {
			navigate(-1);
		}
		break;
	case ScreenCircuitRace:
	case ScreenSingleRace:
	case ScreenTimeRace:
	case ScreenRacerSelect:
	case ScreenPlayer2Select:
		updateRaceScreens(g, dt);
		// The preview after the screen's own update (a race shown this frame plays from this frame).
		if (screen == ScreenCircuitRace || screen == ScreenSingleRace || screen == ScreenTimeRace) {
			updatePreview(dt);
		}
		break;
	case ScreenLoadAll:
	case ScreenLoadCard:
	case ScreenOpenCard:
	case ScreenSaveAll:
	case ScreenSaveCard: navigate(-1); break;
	case ScreenPickCard:
		g.currentCard = 0;
		navigate(ScreenDriver);
		break;
	case ScreenMainMenu: updateMainMenu(g, dt); break;
	case ScreenOptions:
		if (state.nextPage >= 0) {
			showPage(state.nextPage);
			state.nextPage = -1;
		}
		break;
	case ScreenControl1:
	case ScreenControl2: refreshControls(g); break;
	case ScreenGarage: updateGarage(g); break;
	default:
		if (cinematic(screen)) {
			// The circuits' intros can be skipped after their first update; the awards and win cars never.
			state.skippable = screen >= ScreenCircuitIntro && !state.cinema.first;
			if (state.finished) {
				navigate(-1);
			}
			break;
		}
		updateBuilders(g);
		break;
	}
	if (pending && !state.held) {
		leave(g);
	}
}

void Menus::idleAnimation(Game& g)
{
	int32_t part = -1;
	do {
		part = partIndex(data->showcase, IdleParts[g.draw() % 7]);
	} while (part == state.driverPart);
	state.driverPart = part;
	state.driverFrame = 0;
}

// The garage left: to the main menu, the test drive or the destination; every way out keeps player 1's racer and
// saves first.
void Menus::leaveGarage(Game& g)
{
	if (destination == ScreenMainMenu) {
		depth = uint8_t(std::max(0, depth - 1));
	}
	else if (destination == -2) {
		depth = 0; // the test drive
	}
	else if (destination != ScreenSaveAll) {
		push(uint16_t(destination));
	}
	if (!state.racers.empty()) {
		g.settings.picks[0] = state.racers[size_t(state.cursor)];
		g.dirty = true;
	}
	if (top() != ScreenSaveAll) {
		push(ScreenSaveAll);
	}
}

void Menus::leave(Game& g)
{
	pending = false;
	const auto pop = [&](int n) { depth = uint8_t(std::max(0, depth - n)); };
	switch (screen) {
	case ScreenLegal:
		pop(1);
		push(ScreenMainMenu);
		push(ScreenLoadAll);
		return;
	case ScreenCredits: {
		const bool alone = depth == 1;
		pop(1);
		if (alone) {
			push(ScreenMainMenu);
		}
		return;
	}
	case ScreenMainMenu:
		if (destination < 0) {
			depth = 0; // the attract demo, or QUIT
			return;
		}
		push(uint16_t(destination));
		return;
	case ScreenOptions:
		if (destination == ScreenMainMenu) {
			pop(1);
		}
		else {
			push(uint16_t(destination));
		}
		push(ScreenSaveAll);
		return;
	case ScreenControl1:
	case ScreenControl2:
	case ScreenPickLanguage:
		pop(1);
		push(ScreenSaveAll);
		return;
	case ScreenPickCard:
		pop(1);
		if (g.newRacer) {
			push(ScreenDriver);
		}
		push(ScreenNewRacer);
		push(ScreenOpenCard);
		return;
	case ScreenNewRacer:
		if (destination == ScreenGarage) {
			pop(2);
			push(ScreenGarage);
		}
		else {
			pop(1);
		}
		return;
	case ScreenGarage: leaveGarage(g); return;
	case ScreenDriver:
	case ScreenLicense:
	case ScreenEditCar:
	case ScreenCarBuilder: leaveBuilder(); return;
	default:
		if (raceScreen(screen)) {
			leaveRaceScreen(g);
		}
		else if (cinematic(screen)) {
			leaveCinema(g);
		}
		else {
			pop(1);
		}
		return;
	}
}

// The items' notices.

void Menus::mainMenuNotice(Game& g, int16_t w, int32_t what)
{
	const auto is = [&](size_t i) { return isItem(w, i); };
	if (what != NoticeReleased) {
		return;
	}
	if (is(0)) {
		stopTune();
		navigate(ScreenGarage);
	}
	else if (is(1)) {
		g.versus = false;
		navigate(ScreenCircuitRace);
	}
	else if (is(2) || is(3)) {
		g.versus = is(3);
		navigate(ScreenSingleRace);
	}
	else if (is(4)) {
		navigate(ScreenTimeRace);
	}
	else if (is(5)) {
		navigate(ScreenOptions);
	}
	else if (is(6)) {
		ask(117, TextYes, TextNo);
	}
}

void Menus::optionsNotice(Game& g, WidgetTree& t, int16_t w, int32_t what)
{
	const auto is = [&](size_t i) { return isItem(w, i); };
	if (what == NoticeValue) {
		if (is(9) || is(10)) {
			const int32_t value = t[t[w].carousel].index;
			if (is(9)) {
				g.settings.opponents = uint8_t(value);
				g.racerCount = value;
			}
			else {
				g.settings.laps = uint8_t(value + 1);
			}
			g.dirty = true;
		}
		else if (is(13) || is(14)) {
			(is(13) ? g.settings.music : g.settings.sound) = volumeByte(t[w].index);
			g.dirty = true;
		}
		return;
	}
	if (what != NoticeReleased) {
		return;
	}
	if (is(1)) {
		state.nextPage = 1;
	}
	else if (is(2) || is(3)) {
		state.afterNotice = is(2) ? 3 : 2;
		openDialog(MenuDialog::OneButton, 189);
		giveButton(NameDialog, StyleCheck, TextOk);
		showDialog(0);
	}
	else if (is(4) || is(5)) {
		navigate(is(4) ? ScreenControl1 : ScreenControl2);
	}
	else if (is(6)) {
		navigate(ScreenPickLanguage);
	}
	else if (is(7)) {
		navigate(ScreenCredits);
	}
	else if (is(8)) {
		if (state.page == 0) {
			navigate(ScreenMainMenu);
		}
		else {
			state.nextPage = 0;
		}
	}
}

void Menus::controlsNotice(Game& g, WidgetTree& t, int16_t w, int32_t what)
{
	const auto is = [&](size_t i) { return isItem(w, i); };
	if (what == NoticeValue && is(0)) {
		refreshControls(g);
		return;
	}
	if (what == NoticeReleased && is(1)) {
		g.pickLayout(state.player, state.entries[size_t(t[t[state.items[0]].carousel].index)]);
		navigate(ScreenOptions);
		return;
	}
	// A row pressed waits (still pressed) for the key to bind.
	for (size_t r = 0; r < 9 && what == NoticePressed; r++) {
		if (w == state.rows[r]) {
			const size_t layout = state.entries[size_t(t[t[state.items[0]].carousel].index)];
			g.bind(layout, r, 0);
			state.waiting = int32_t(r);
			refreshControls(g);
		}
	}
}

void Menus::garageNotice(Game& g, WidgetTree& t, int16_t w, int32_t what)
{
	const auto is = [&](size_t i) { return isItem(w, i); };
	const int32_t n = int32_t(state.racers.size());
	if (what == NoticeValue && is(0) && n > 1) {
		// The cursor steps with the carousel; the labels follow when it settles.
		const int32_t dir = t[t[w].carousel].index == 2 ? 1 : -1;
		state.cursor = (state.cursor + dir + n) % n;
		return;
	}
	if (what == NoticeSettled && is(0)) {
		fillRacerNames(g);
		return;
	}
	if (what != NoticeReleased) {
		return;
	}
	const RacerRef current = n ? state.racers[size_t(state.cursor)] : RacerRef();
	if (is(1)) {
		g.newRacer = true;
		g.working = {};
		navigate(ScreenPickCard);
	}
	else if (is(2)) {
		state.nextPage = 1;
	}
	else if (is(3)) {
		g.chosen[0] = current;
		g.copying = true;
		navigate(ScreenPickCard);
	}
	else if (is(4)) {
		ask(118, TextYes, TextNo);
	}
	else if (is(5)) {
		g.backToGarage = true;
		g.versus = false;
		g.chosen[0] = current;
		g.players = 1;
		navigate(-2);
	}
	else if (is(6)) {
		if (state.page == 0) {
			g.chosen = {};
			stopTune();
			navigate(ScreenMainMenu);
		}
		else {
			state.nextPage = 0;
		}
	}
	else if (is(7) || is(8) || is(9)) {
		g.chosen[0] = current;
		if (const RacerRecord* r = g.racer(current)) {
			g.working = *r;
		}
		navigate(is(7) ? ScreenDriver : is(8) ? ScreenLicense : ScreenEditCar);
	}
}

void Menus::notice(Game& g, WidgetTree& t, int16_t w, int32_t what)
{
	if (dialogCount && &t == &dialogs[dialogCount - 1].tree) {
		// A dialog's button: the screen decides.
		MenuDialog& d = dialogs[dialogCount - 1];
		if (what != NoticeReleased) {
			return;
		}
		d.result = d.kind == MenuDialog::OneButton ? MenuDialog::Pressed : w == d.buttons[0] ? MenuDialog::Yes : MenuDialog::No;
		dialogNotice(g, d);
		return;
	}
	if (&t != &tree) {
		return;
	}
	switch (screen) {
	case ScreenMainMenu: mainMenuNotice(g, w, what); return;
	case ScreenOptions: optionsNotice(g, t, w, what); return;
	case ScreenControl1:
	case ScreenControl2: controlsNotice(g, t, w, what); return;
	case ScreenPickLanguage:
		if (what == NoticeValue && isItem(w, 0)) {
			g.settings.language = uint8_t(t[t[w].carousel].index);
			g.dirty = true;
		}
		else if (what == NoticeReleased && isItem(w, 1)) {
			navigate(ScreenOptions);
		}
		return;
	case ScreenGarage: garageNotice(g, t, w, what); return;
	default:
		if (raceScreen(screen)) {
			raceScreenNotice(g, t, w, what);
			return;
		}
		builderNotice(g, t, w, what);
		return;
	}
}

void Menus::dialogNotice(Game& g, MenuDialog& d)
{
	switch (screen) {
	case ScreenMainMenu: state.quitting = d.result == MenuDialog::Yes; break;
	case ScreenOptions:
		state.nextPage = state.afterNotice;
		if (state.afterNotice == 3) {
			videoSettings = true;
			state.nextPage = -1;
		}
		break;
	case ScreenGarage:
		if (d.result == MenuDialog::Yes) {
			// The racer goes from its list, the cursor on the one after it; page 0 again, then the garage anew, saved
			// (once the dialog has closed).
			const RacerRef gone = state.racers[size_t(state.cursor)];
			std::vector<RacerRecord>& list = g.list(gone.list);
			list.erase(list.begin() + gone.index);
			g.markRacers(gone.list);
			state.racers.erase(state.racers.begin() + state.cursor);
			for (RacerRef& r : state.racers) {
				r.index = uint16_t(r.list == gone.list && r.index > gone.index ? r.index - 1 : r.index);
			}
			state.cursor = state.racers.empty() ? 0 : state.cursor % int32_t(state.racers.size());
			fillRacerNames(g);
			garagePage(0);
			if (!state.racers.empty()) {
				select(tree, state.items[4], true, false, true);
			}
			navigate(ScreenSaveAll);
		}
		break;
	case ScreenNewRacer: navigate(ScreenGarage); break;
	default: builderDialog(g, d); return;
	}
	closeDialog();
}

// The screen's own look at a key, before the widgets.

bool Menus::screenKey(Game& g, const MenuKey& k)
{
	if (dialogCount) {
		// A dialog swallows every key until it is open.
		return dialogs[dialogCount - 1].phase != MenuDialog::Open;
	}
	const uint32_t source = k.key >> 28;
	if (cinematic(screen)) {
		if (state.skippable && state.cinemaFrame >= state.rate && ((k.down && !k.repeat && source <= 3) || !k.down)) {
			state.finished = true;
			return true;
		}
		return false;
	}
	if ((screen == ScreenControl1 || screen == ScreenControl2) && state.waiting >= 0) {
		// The row waiting for a key takes the next one (a few can't be bound).
		static const uint32_t Refused[] = {0x01, 0xb7, 0x46, 0xc5, 0x45, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe3};
		if (!k.down || source == 2 || source == 4) {
			return true;
		}
		if (source == 1 && std::find(std::begin(Refused), std::end(Refused), k.key & 0xfffffff) != std::end(Refused)) {
			play(8);
			return true;
		}
		const size_t layout = state.entries[size_t(tree[tree[state.items[0]].carousel].index)];
		g.bind(layout, size_t(state.waiting), k.key);
		const int16_t row = state.rows[size_t(state.waiting)];
		state.waiting = -1;
		release(tree, row, false, true, false);
		refreshControls(g);
		return true;
	}
	if (raceScreen(screen) && state.held) {
		return true; // leaving: the keys wait
	}
	return builderKey(g, k);
}

} // namespace lr
