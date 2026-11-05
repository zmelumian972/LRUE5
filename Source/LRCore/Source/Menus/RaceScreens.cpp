// The race setup screens: circuit race, single race (and versus) and time race, each a circuit
// carousel over a looping preview of the chosen race; and racer select: the racers in a strip, the chosen
// one's car and minifig in a 3D view, the "chosen" animation the screen waits for before it leaves.

#include "LRCore/Menus/Menus.h"
#include "LRData/Archive.h"
#include "LRCore/Common/Tables.h"

#include "Show/Animation.h"
#include "Menus/MenuNames.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace lr {

namespace {

enum : uint32_t {
	NameBrickBox = 71,
	NameGoNext = 64,
	NameGoRace = 65,
	StyleNuButton = 66,
	NameShowcase = 110,
	NameRacerBox = 112,
	NameSelector = 149,
	NameRaceBox = 105,
};

constexpr MenuRect Viewport = {260, 135, 580, 391}; // the preview's scene region

// The preview frame's border colour by theme index (circuit · 4 + place).
uint32_t frameColour(int32_t theme)
{
	const ExecutableTables& t = executableTables();
	return t.frameColours[t.frameMap[size_t(std::clamp(theme, 0, 24))] % 5];
}

// A record time as m:ss:cc.
std::u16string timeText(uint32_t ms)
{
	const uint32_t t = ms % 3600000;
	char text[16];
	std::snprintf(text, sizeof(text), "%u:%02u:%02u", t / 60000, t / 1000 % 60, t / 10 % 100);
	return std::u16string(text, text + std::char_traits<char>::length(text));
}

// The race a circuit lists k-th (its empty places skipped); -1 none.
int32_t listedRace(const MenuData& data, int32_t circuit, int32_t k)
{
	for (int32_t r : data.circuits.circuits[size_t(circuit)].races) {
		if (r >= 0 && k-- == 0) {
			return r;
		}
	}
	return -1;
}

// The idle and "chosen" animations (RSANIM.ADB's parts).
const char* const Idles[7] = {"breath1", "breath2", "swayf", "watch", "hips", "tapfoot", "hey!"};
const char* const Chosen[3] = {"jump", "yes!", "spin"};

} // namespace

// The setup screens.

void Menus::buildRaceSetup(Game& g)
{
	WidgetTree& t = tree;
	const bool circuitScreen = screen == ScreenCircuitRace, timeRace = screen == ScreenTimeRace;
	const auto item = [&](uint32_t binding, uint32_t style, int32_t text) { return makeNamed(t, binding, style, -1, text); };
	const auto selector = [&](std::string_view binding, uint32_t box) {
		const int16_t sel = make(t, data->screens[screen].layout, binding, data->names.narrow(circuitScreen || binding == "selector" ? StyleImageSelector : StyleTextSelector), -1);
		const int16_t car = makeNamed(t, box, StylePicker, sel);
		t[sel].carousel = car;
		t[car].colour = t[sel].colour;
		return sel;
	};
	if (tune < 0) {
		playTune(0, true);
	}
	state.racersFound = g.racerCount;
	item(NameBackdrop, NameBackdrop, -1);
	item(NameBrickBox, NameBrickBox, -1);
	const int16_t view = make(t, data->screens[screen].layout, "singrace", "", -1);
	state.items[0] = make(t, data->screens[screen].layout, "dimmer", "", view);
	t[state.items[0]].kind = Widget::Quad;
	t[state.items[0]].rect = {0, 0, t[view].rect.width(), t[view].rect.height()};
	state.items[1] = selector("selector", NameImageBox);
	for (int32_t c = 0; c < (timeRace ? 3 : 7); c++) {
		const std::string name = "circ" + std::to_string(c + 1);
		addItem(t, t[state.items[1]].carousel, make(t, data->screens[screen].layout, name, "setimage", -1));
	}
	if (circuitScreen) {
		item(91, StyleMenuFont, 74); // hosting
		state.items[2] = item(92, StyleMenuFont, -1); // the champion
		state.items[3] = item(93, StyleMenuFont, -1); // the race's name
		item(95, StyleMenuFont, 75); // circuit number
		state.items[4] = item(96, StyleMenuFont, -1); // its number
	}
	else {
		state.items[5] = selector("racesel", NameTextBox);
	}
	state.items[6] = item(NameGoNext, StyleCheck, TextOk);
	state.items[7] = item(NameBack, StyleLeftArrow, 2);
	if (timeRace) {
		state.items[8] = item(106, StyleMenuFont, -1); // the record's time
		state.items[9] = item(108, StyleMenuFont, -1); // its holder
		state.items[10] = item(107, StyleMenuFont, -1); // lap or race
		state.items[11] = item(109, StyleMenuFont, 73); // TIME TRIAL WON
	}
	const int16_t banner = item(StyleBanner, StyleBanner, circuitScreen ? 13 : timeRace ? 15 : g.versus ? 25 : 14);
	alignRight(t, banner, 20);
	state.showingLap = true;
	state.toggleMs = 2500;
	chooseCircuit(g, 0);
	select(t, state.items[6], true, false, false);
	if (timeRace) {
		refreshRecord(g);
	}
}

// A circuit chosen (the carousel's selection, or the screen opening).
void Menus::chooseCircuit(Game& g, int32_t c)
{
	WidgetTree& t = tree;
	state.circuit = c;
	const CircuitTable::Circuit& circuit = data->circuits.circuits[size_t(c)];
	const bool open = ((g.settings.circuits | 1) >> c) & 1;
	if (screen == ScreenCircuitRace) {
		g.mode = Game::Mode::Circuit;
		setString(t, state.items[4], 97 + c);
		setString(t, state.items[2], 76 + c);
		g.circuit = c;
		for (size_t p = 0; p < 4; p++) {
			g.races[p] = int16_t(circuit.races[p]);
		}
		enable(t, state.items[6], open);
		if (!open) {
			select(t, state.items[7], false, false, false);
		}
		show(t, state.items[0], !open);
		state.place = 0;
		state.previewMs = 0;
		showPlace(g);
		return;
	}
	// The race carousel: the circuit's races in place order.
	const int16_t box = t[state.items[5]].carousel;
	std::vector<int16_t> old;
	for (int16_t i = t[box].first; i >= 0; i = t[i].next) {
		old.push_back(i);
	}
	t[box].first = t[box].last = -1;
	for (int16_t i : old) {
		t[i].parent = -1;
	}
	t[box].index = 0;
	for (int32_t r : circuit.races) {
		if (r < 0) {
			continue;
		}
		const int16_t l = makeNamed(t, NameContext, StyleMenuFont, -1);
		t[l].rect = {0, 0, 0, 0}; // measured to its name
		setText(t, l, data->languages[language].circuits.at(size_t(data->races.races[size_t(r)].display)));
		addItem(t, box, l);
	}
	selectItem(t, box, 0);
	enable(t, state.items[6], open);
	show(t, state.items[0], !open);
	const int32_t race = circuit.races[0] >= 0 ? circuit.races[0] : -1;
	showRace(g, race, 4 * c);
}

// The circuit race's preview: place q of the circuit.
void Menus::showPlace(Game& g)
{
	const int32_t c = state.circuit, race = data->circuits.circuits[size_t(c)].races[size_t(state.place)];
	if (race < 0) {
		state.previewMs = 2000;
	}
	else {
		showRace(g, race, state.place + 4 * c);
	}
	state.previewChanged = true;
}

// The preview showing a race: its theme's scene (a new one restarts), the frame's colour, the mascot.
void Menus::showRace(Game& g, int32_t race, int32_t theme)
{
	WidgetTree& t = tree;
	if (race < 0) {
		return;
	}
	const RaceTable::Race& r = data->races.races[size_t(race)];
	const std::vector<CinemaFile::Scene>& previews = data->screens[screen].cinema.scenes;
	int32_t scene = -1;
	for (size_t i = 0; i < previews.size(); i++) {
		scene = scene < 0 && sameName(previews[i].name, r.theme, 8) ? int32_t(i) : scene;
	}
	const int16_t frame = t.find(screen == ScreenCircuitRace ? 20 : 14);
	const auto colour = [&] {
		const uint32_t c = frameColour(theme);
		for (int16_t e = t[t[frame].first].next; e >= 0; e = t[e].next) {
			t[e].colour = c;
		}
	};
	if (scene >= 0 && scene != state.scene) {
		state.scene = scene;
		state.animMs = 0;
		if (screen == ScreenCircuitRace) {
			state.previewMs = 2000;
			colour();
			setText(t, state.items[3], data->languages[language].circuits.at(size_t(r.display)));
		}
		else if (screen == ScreenTimeRace) {
			state.recordRace = race;
			refreshRecord(g);
		}
	}
	if (screen != ScreenCircuitRace) {
		colour();
	}
	state.mascot = screen == ScreenTimeRace ? std::string("VV") : r.featured;
}

// The time race's record panel: the race's lap or race record and its holder, and whether it was won.
void Menus::refreshRecord(Game& g)
{
	WidgetTree& t = tree;
	const int32_t n = state.recordRace;
	const int32_t slot = n < 13 ? n : n < 26 ? n - 13 : -1;
	const BestTime* best = slot >= 0 ? &g.settings.best[size_t(slot)] : nullptr;
	const uint32_t ms = best ? (state.showingLap ? best->lapMs : best->raceMs) : 0;
	if (ms != 0 && ms != 0xffffffff) {
		setText(t, state.items[8], timeText(ms));
		setText(t, state.items[9], (state.showingLap ? best->lapHolder : best->raceHolder).substr(0, 14));
	}
	else {
		setText(t, state.items[8], u"");
		setString(t, state.items[9], state.showingLap ? 190 : 191);
	}
	setString(t, state.items[10], state.showingLap ? 70 : 71);
	state.toggleMs = 2500;
	show(t, state.items[11], (g.settings.races >> n) & 1);
}

// Racer select.

RacerRef Menus::stepRacer(const Game& g, RacerRef from, bool forward) const
{
	static constexpr RacerList Order[4] = {RacerList::Defaults, RacerList::Session, RacerList::Card0, RacerList::Card1};
	const auto size = [&](RacerList l) {
		return l == RacerList::Defaults ? g.defaults.size() : l == RacerList::Session ? g.session.size() : g.cards[l == RacerList::Card1].size();
	};
	if (forward && from.index + 1u < size(from.list)) {
		return {from.list, uint16_t(from.index + 1)};
	}
	if (!forward && from.index > 0 && from.list != RacerList::None) {
		return {from.list, uint16_t(from.index - 1)};
	}
	const size_t at = size_t(std::find(std::begin(Order), std::end(Order), from.list) - std::begin(Order));
	for (size_t k = 1; k <= 4; k++) {
		const RacerList l = Order[(at + (forward ? k : 4 * 4 - k)) % 4];
		if (size(l)) {
			return {l, uint16_t(forward ? 0 : size(l) - 1)};
		}
	}
	return from;
}

void Menus::buildRacerSelect(Game& g)
{
	WidgetTree& t = tree;
	const MenuLayout& layout = data->screens[screen].layout;
	const auto item = [&](uint32_t binding, uint32_t style, int32_t text) { return makeNamed(t, binding, style, -1, text); };
	makeNamed(t, NameBackdrop, NameBackdrop, -1);
	make(t, layout, "showcase", "", -1);
	const int16_t sel = makeNamed(t, NameRacerBox, StyleTextSelector, -1);
	const int16_t car = makeNamed(t, NameTextBox, StylePicker, sel);
	t[sel].carousel = car;
	t[car].colour = t[sel].colour;
	t[sel].unselectedInput = g.versus;
	state.items[0] = sel;
	for (int32_t k = 0; k < 3; k++) {
		addItem(t, car, makeNamed(t, NameContext, StyleMenuFont, -1));
	}
	alignRight(t, item(StyleBanner, StyleBanner, g.versus ? 21 + g.players : 20), 20);
	state.items[1] = item(9, StyleNuButton, 185); // CONFIG
	state.items[2] = item(NameGoRace, StyleCheck, TextOk);
	state.items[3] = item(NameGoNext, StyleCheck, TextOk);
	state.items[4] = item(NameBack, StyleCancel, TextCancel);
	// The racer: the kept choice when it isn't the first, else the first of the player's own.
	const RacerRef first = stepRacer(g, {RacerList::Defaults, uint16_t(g.defaults.size())}, true);
	RacerRef at = first, pick = first;
	bool found = false;
	for (RacerRef r = stepRacer(g, first, true); !(r == first); r = stepRacer(g, r, true)) {
		if (r == g.settings.picks[0]) {
			pick = r;
			found = true;
			break;
		}
	}
	if (!found) {
		for (at = stepRacer(g, first, true); !(at == first) && at.list == RacerList::Defaults; at = stepRacer(g, at, true)) {
		}
		pick = at;
	}
	state.racer = pick;
	fillPickNames(g);
	showRacer(g);
	state.driverPart = -1;
	state.animating = false;
	const bool nextPlayer = g.versus && g.players == 0;
	show(t, state.items[2], !nextPlayer);
	show(t, state.items[3], nextPlayer);
	select(t, state.items[nextPlayer ? 3 : 2], true, false, false);
}

// The strip's three names: the racer before, the racer, the one after; the middle one shown.
void Menus::fillPickNames(Game& g)
{
	WidgetTree& t = tree;
	const int16_t car = t[state.items[0]].carousel;
	const RacerRef around[3] = {stepRacer(g, state.racer, false), state.racer, stepRacer(g, state.racer, true)};
	int32_t k = 0;
	for (int16_t l = t[car].first; l >= 0 && k < 3; l = t[l].next, k++) {
		const RacerRecord* r = g.racer(around[k]);
		setText(t, l, r ? std::u16string_view(r->name) : std::u16string_view());
	}
	selectItem(t, car, 1);
}

// The racer's car, built for the showcase (Menus::carMesh).
void Menus::showRacer(Game& g)
{
	const RacerRecord* r = g.racer(state.racer);
	state.car.load(data->pieces, r ? r->pieces : std::vector<CarPiece>());
	state.car.chooseOffset(data->pieces);
	carMesh = state.car.menuMesh(data->pieces, data->sets.materials);
	state.racerChanged = true;
}

void Menus::startIdle(Game& g)
{
	int32_t part = -1;
	do {
		part = partIndex(data->showcase, Idles[g.draw() % 7]);
	} while (part == state.driverPart);
	state.driverPart = part;
	state.driverFrame = 0;
	state.blendMs = -1;
	state.animating = true;
}

// Each frame's clocks (with the widgets'): the preview's scene, the minifig and the turntable.
void Menus::tickRaceScreens(int32_t dt)
{
	if (screen == ScreenRacerSelect || screen == ScreenPlayer2Select) {
		state.spinMs += dt;
		if (!state.animating) {
			return;
		}
		if (state.blendMs >= 0) {
			state.blendMs += dt;
			state.driverFrame = blendedFrame(data->showcase, state.driverPart, state.blendMs, true);
		}
		else if (!partEnded(data->showcase, state.driverPart, state.driverFrame)) {
			state.driverFrame = advance(data->showcase, state.driverPart, state.driverFrame, dt);
		}
		return;
	}
	state.animMs += dt;
}

void Menus::updateRaceScreens(Game& g, int32_t dt)
{
	WidgetTree& t = tree;
	if (screen == ScreenRacerSelect || screen == ScreenPlayer2Select) {
		const bool leaving = pending;
		if (state.racerChanged) {
			state.racerChanged = false;
			if (!leaving) {
				// The minifig just built is on its first part, so a first draw giving that part is drawn again.
				state.driverPart = 0;
				startIdle(g);
			}
		}
		else if (state.animating && state.blendMs < 0 && partEnded(data->showcase, state.driverPart, state.driverFrame) && g.chosen[0].list == RacerList::None && !leaving) {
			startIdle(g);
		}
		// The leave gate: open unless the minifig is playing a part that hasn't ended; during the chosen blend the old
		// part's end as it froze (an idle that ended on the press's frame lets the screen leave at once).
		const bool blending = state.blendMs >= 0 && state.blendMs < 200;
		state.held = pending && state.animating && !(blending ? state.oldEnded : partEnded(data->showcase, state.driverPart, state.driverFrame));
		return;
	}
	// The setup screens.
	if (screen == ScreenTimeRace) {
		if (dt > state.toggleMs) {
			state.showingLap = !state.showingLap;
			refreshRecord(g);
		}
		else {
			state.toggleMs -= dt;
		}
	}
	if (screen != ScreenCircuitRace) {
		return;
	}
	if (state.fade >= 0 && !fades[size_t(state.fade)].active) {
		state.fade = -1;
	}
	const bool rocket = state.circuit == 6;
	if (uint32_t(dt) > state.previewMs && !rocket) {
		state.place = (state.place + 1) & 3;
		showPlace(g);
	}
	else {
		state.previewMs -= uint32_t(dt);
	}
	const bool fading = std::any_of(fades.begin(), fades.end(), [](const Fade& f) { return f.active; });
	if (state.previewMs < 250 && !fading && !rocket) {
		state.fade = startFade(250, true, 0xff000000, Viewport);
	}
	if (state.previewChanged) {
		if (state.fade >= 0) {
			fades[size_t(state.fade)].active = false;
		}
		state.fade = startFade(250, false, 0xff000000, Viewport);
		state.previewChanged = false;
	}
	(void)t;
}

void Menus::raceScreenNotice(Game& g, WidgetTree& t, int16_t w, int32_t what)
{
	const auto is = [&](size_t i) { return state.items[i] >= 0 && w == state.items[i]; };
	if (screen == ScreenRacerSelect || screen == ScreenPlayer2Select) {
		if (what == NoticeValue && is(0)) {
			// The cursor steps with the strip; the models change at once, the names when it settles.
			const bool forward = t[t[w].carousel].index == 2;
			state.racer = stepRacer(g, state.racer, forward);
			showRacer(g);
			return;
		}
		if (what == NoticeSettled && is(0)) {
			fillPickNames(g);
			return;
		}
		if (what != NoticeReleased || pending) {
			return;
		}
		if (is(1) || is(4)) {
			state.animating = false;
			state.action = is(1) ? 2 : 3;
			navigate(0);
			return;
		}
		if (is(2) || is(3)) {
			if (is(2) && g.versus) {
				g.racerCount = 0;
			}
			// The "chosen" animation, blended into from the pose now.
			state.oldEnded = state.animating && partEnded(data->showcase, state.driverPart, state.driverFrame);
			state.driverPart = partIndex(data->showcase, Chosen[g.draw() % 3]);
			state.driverFrame = 0;
			state.blendMs = 0;
			state.animating = true;
			if (g.players < 2) {
				g.chosen[g.players++] = state.racer;
			}
			state.action = is(2) ? 0 : 1;
			navigate(0);
		}
		return;
	}
	// The setup screens.
	if (what == NoticeValue && is(1)) {
		chooseCircuit(g, t[t[w].carousel].index);
		return;
	}
	if (what == NoticeValue && is(5)) {
		const int32_t index = t[t[w].carousel].index;
		showRace(g, listedRace(*data, state.circuit, index), index + 4 * state.circuit);
		return;
	}
	if (what != NoticeReleased) {
		return;
	}
	if (is(6)) {
		if (screen != ScreenCircuitRace) {
			g.races[0] = int16_t(listedRace(*data, state.circuit, t[t[state.items[5]].carousel].index));
			g.circuit = state.circuit;
			g.mode = screen == ScreenTimeRace ? Game::Mode::TimeRace : Game::Mode::Single;
			if (screen == ScreenTimeRace) {
				g.racerCount = 0;
			}
		}
		navigate(ScreenRacerSelect);
	}
	else if (is(7)) {
		if (screen != ScreenCircuitRace) {
			g.versus = false;
			g.racerCount = state.racersFound;
		}
		navigate(ScreenMainMenu);
	}
}

void Menus::leaveRaceScreen(Game& g)
{
	const auto pop = [&](int n) { depth = uint8_t(std::max(0, depth - n)); };
	if (screen == ScreenCircuitRace || screen == ScreenSingleRace || screen == ScreenTimeRace) {
		if (state.fade >= 0) {
			fades[size_t(state.fade)].active = false;
		}
		if (destination == ScreenRacerSelect) {
			push(ScreenRacerSelect);
		}
		else {
			pop(1);
		}
		return;
	}
	// Racer select: the button's stack change, the racer kept as player 1's choice, then the save screen first.
	const int32_t p = g.players;
	switch (state.action) {
	case 0:
		if (g.mode == Game::Mode::Circuit && p == 1) {
			push(uint16_t(ScreenCircuitIntro + g.circuit));
			g.racerCount = 6;
		}
		else {
			depth = 0;
			g.racerCount = g.versus ? 0 : g.racerCount + p;
		}
		g.versus = false;
		break;
	case 1: push(ScreenPlayer2Select); break;
	case 2: push(uint16_t(ScreenControl1 + p)); break;
	default:
		if (g.players > 0) {
			g.players--;
		}
		pop(1);
		break;
	}
	g.settings.picks[0] = state.racer;
	g.dirty = true;
	if (top() != ScreenSaveAll) {
		push(ScreenSaveAll);
	}
}

} // namespace lr
