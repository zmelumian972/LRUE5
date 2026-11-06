// The race setup screens: circuit race, single race (and versus) and time race, each a circuit
// carousel over a looping preview of the chosen race; and racer select: the racers in a strip, the chosen
// one's car and minifig in a 3D view, the "chosen" animation the screen waits for before it leaves.

#include "LRCore/Menus/Menus.h"
#include "LRData/Archive.h"
#include "LRCore/Common/Tables.h"

#include "Show/Animation.h"
#include "Menus/MenuNames.h"
#include "Menus/Pen.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace lr {

namespace {

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
	for (int32_t r : data.circuits.circuits[circuit].races) {
		if (r >= 0 && k-- == 0) {
			return r;
		}
	}
	return -1;
}

// The "chosen" animations (RSANIM.ADB's parts).
const char* const Chosen[3] = {"jump", "yes!", "spin"};

} // namespace

// The setup screens.

void RaceScreens::buildRaceSetup(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const bool circuitScreen = m.screen == ScreenCircuitRace, timeRace = m.screen == ScreenTimeRace;
	const auto selector = [&](std::string_view binding, uint32_t box) {
		const int16_t sel = m.ui.make(t, m.data->screens[m.screen].layout, binding,
			m.data->names.narrow(circuitScreen || binding == "selector" ? StyleImageSelector : StyleTextSelector), -1);
		m.ui.makeCarousel(sel, m.data->names.narrow(box), m.data->names.narrow(StylePicker));
		return sel;
	};
	if (m.tune < 0) {
		m.playTune(0, true);
	}
	state.racersFound = g.racerCount;
	m.ui.screenItem(NameBackdrop, NameBackdrop, -1);
	m.ui.screenItem(NameBrickBox, NameBrickBox, -1);
	const int16_t view = m.ui.make(t, m.data->screens[m.screen].layout, "singrace", "", -1);
	m.items[SetupDimmer] = m.ui.make(t, m.data->screens[m.screen].layout, "dimmer", "", view);
	t[m.items[SetupDimmer]].kind = Widget::Quad;
	t[m.items[SetupDimmer]].rect = {0, 0, t[view].rect.width(), t[view].rect.height()};
	m.items[SetupCircuits] = selector("selector", NameImageBox);
	for (int32_t c = 0; c < (timeRace ? 3 : 7); c++) {
		const std::string name = "circ" + std::to_string(c + 1);
		m.ui.addItem(t, t[m.items[SetupCircuits]].carousel, m.ui.make(t, m.data->screens[m.screen].layout, name, "setimage", -1));
	}
	if (circuitScreen) {
		m.ui.screenItem(NameHosting, StyleMenuFont, TextHosting);
		m.items[SetupChampion] = m.ui.screenItem(NameChampion, StyleMenuFont, -1);
		m.items[SetupRaceName] = m.ui.screenItem(NameRaceName, StyleMenuFont, -1);
		m.ui.screenItem(NameCircuitText, StyleMenuFont, TextCircuitNumber);
		m.items[SetupCircuitNumber] = m.ui.screenItem(NameCircuitNumber, StyleMenuFont, -1);
	}
	else {
		m.items[SetupRaces] = selector("racesel", NameTextBox);
	}
	m.items[SetupOk] = m.ui.screenItem(NameGoNext, StyleCheck, TextOk);
	m.items[SetupBack] = m.ui.screenItem(NameBack, StyleLeftArrow, TextMainMenu);
	if (timeRace) {
		m.items[SetupRecordTime] = m.ui.screenItem(NameBestTime, StyleMenuFont, -1);
		m.items[SetupRecordHolder] = m.ui.screenItem(NameBestName, StyleMenuFont, -1);
		m.items[SetupRecordKind] = m.ui.screenItem(NameBestType, StyleMenuFont, -1); // lap or race
		m.items[SetupTimeTrialWon] = m.ui.screenItem(NameTimeTrialWon, StyleMenuFont, TextTimeTrialWon);
	}
	m.ui.makeBanner(circuitScreen ? TextCircuitRaceTitle
			: timeRace			  ? TextTimeRaceTitle
			: g.versus			  ? TextVersusRaceTitle
								  : TextSingleRaceTitle);
	state.showingLap = true;
	state.toggleMs = 2500;
	chooseCircuit(m, 0);
	m.ui.select(t, m.items[SetupOk], IconQuiet);
	if (timeRace) {
		refreshRecord(m);
	}
}

// A circuit chosen (the carousel's selection, or the screen opening).
void RaceScreens::chooseCircuit(Menus& m, int32_t c)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	state.circuit = c;
	const CircuitTable::Circuit& circuit = m.data->circuits.circuits[c];
	const bool open = ((g.settings.circuits | 1) >> c) & 1;
	if (m.screen == ScreenCircuitRace) {
		g.mode = Game::Mode::Circuit;
		m.ui.setString(t, m.items[SetupCircuitNumber], TextDigits + 1 + c);
		m.ui.setString(t, m.items[SetupChampion], TextChampions + c);
		g.circuit = c;
		for (size_t p = 0; p < 4; p++) {
			g.races[p] = int16_t(circuit.races[p]);
		}
		m.ui.enable(t, m.items[SetupOk], open);
		if (!open) {
			m.ui.select(t, m.items[SetupBack]);
		}
		m.ui.show(t, m.items[SetupDimmer], !open);
		state.place = 0;
		state.previewMs = 0;
		showPlace(m);
		return;
	}
	// The race carousel: the circuit's races in place order.
	const int16_t box = t[m.items[SetupRaces]].carousel;
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
		const int16_t l = m.ui.makeNamed(t, NameContext, StyleMenuFont, -1);
		t[l].rect = {0, 0, 0, 0}; // measured to its name
		m.ui.setText(t, l, m.data->languages[m.language].circuits.at(size_t(m.data->races.races[r].display)));
		m.ui.addItem(t, box, l);
	}
	m.ui.selectItem(t, box, 0);
	m.ui.enable(t, m.items[SetupOk], open);
	m.ui.show(t, m.items[SetupDimmer], !open);
	const int32_t race = circuit.races[0] >= 0 ? circuit.races[0] : -1;
	showRace(m, race, 4 * c);
}

// The circuit race's preview: place q of the circuit.
void RaceScreens::showPlace(Menus& m)
{
	const int32_t c = state.circuit, race = m.data->circuits.circuits[c].races[state.place];
	if (race < 0) {
		state.previewMs = 2000;
	}
	else {
		showRace(m, race, state.place + 4 * c);
	}
	state.previewChanged = true;
}

// The preview showing a race: its theme's scene (a new one restarts), the frame's colour, the mascot.
void RaceScreens::showRace(Menus& m, int32_t race, int32_t theme)
{
	WidgetTree& t = m.ui.tree;
	if (race < 0) {
		return;
	}
	const RaceTable::Race& r = m.data->races.races[race];
	const std::vector<CinemaFile::Scene>& previews = m.data->screens[m.screen].cinema.scenes;
	int32_t scene = -1;
	for (size_t i = 0; i < previews.size(); i++) {
		scene = scene < 0 && sameName(previews[i].name, r.theme, 8) ? int32_t(i) : scene;
	}
	const int16_t frame = t.find(m.screen == ScreenCircuitRace ? 20 : 14);
	const auto colour = [&] {
		const uint32_t c = frameColour(theme);
		for (int16_t e = t[t[frame].first].next; e >= 0; e = t[e].next) {
			t[e].colour = c;
		}
	};
	if (scene >= 0 && scene != state.scene) {
		state.scene = scene;
		state.animMs = 0;
		if (m.screen == ScreenCircuitRace) {
			state.previewMs = 2000;
			colour();
			m.ui.setText(t, m.items[SetupRaceName], m.data->languages[m.language].circuits.at(size_t(r.display)));
		}
		else if (m.screen == ScreenTimeRace) {
			state.recordRace = race;
			refreshRecord(m);
		}
	}
	if (m.screen != ScreenCircuitRace) {
		colour();
	}
	state.mascot = m.screen == ScreenTimeRace ? std::string("VV") : r.featured;
}

// The time race's record panel: the race's lap or race record and its holder, and whether it was won.
void RaceScreens::refreshRecord(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const int32_t slot = recordSlot(state.recordRace);
	const BestTime* best = slot >= 0 ? &g.settings.best[slot] : nullptr;
	const uint32_t ms = best ? (state.showingLap ? best->lapMs : best->raceMs) : 0;
	if (ms != 0 && ms != 0xffffffff) {
		m.ui.setText(t, m.items[SetupRecordTime], timeText(ms));
		m.ui.setText(t, m.items[SetupRecordHolder], (state.showingLap ? best->lapHolder : best->raceHolder).substr(0, 14));
	}
	else {
		m.ui.setText(t, m.items[SetupRecordTime], u"");
		m.ui.setString(t, m.items[SetupRecordHolder], state.showingLap ? TextNoLapHolder : TextNoRaceHolder);
	}
	m.ui.setString(t, m.items[SetupRecordKind], state.showingLap ? TextBestLapTime : TextBestTotalTime);
	state.toggleMs = 2500;
	m.ui.show(t, m.items[SetupTimeTrialWon], (g.settings.races >> state.recordRace) & 1);
}

// Racer select.

RacerRef RaceScreens::stepRacer(const Menus& m, RacerRef from, bool forward) const
{
	const Game& g = *m.game;
	static constexpr RacerList Order[4] = {RacerList::Defaults, RacerList::Session, RacerList::Card0, RacerList::Card1};
	const auto size = [&](RacerList l) {
		return l == RacerList::Defaults ? g.defaults.size()
			: l == RacerList::Session	? g.session.size()
										: g.cards[l == RacerList::Card1].size();
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

void RaceScreens::buildRacerSelect(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const MenuLayout& layout = m.data->screens[m.screen].layout;
	m.ui.makeNamed(t, NameBackdrop, NameBackdrop, -1);
	m.ui.make(t, layout, "showcase", "", -1);
	m.items[SelectRacers] = m.ui.makeTextSelector(NameRacerBox, 3, -1);
	t[m.items[SelectRacers]].unselectedInput = g.versus;
	m.ui.makeBanner(g.versus ? TextPlayer1RacerSelect + g.players : TextRacerSelect);
	m.items[SelectConfig] = m.ui.screenItem(NameControl, StyleItem, TextConfig);
	m.items[SelectRace] = m.ui.screenItem(NameGoRace, StyleCheck, TextOk);
	m.items[SelectNext] = m.ui.screenItem(NameGoNext, StyleCheck, TextOk);
	m.items[SelectCancel] = m.ui.screenItem(NameBack, StyleCancel, TextCancel);
	// The racer: the kept choice when it isn't the first, else the first of the player's own.
	const RacerRef first = stepRacer(m, {RacerList::Defaults, uint16_t(g.defaults.size())}, true);
	RacerRef at = first, pick = first;
	bool found = false;
	for (RacerRef r = stepRacer(m, first, true); !(r == first); r = stepRacer(m, r, true)) {
		if (r == g.settings.picks[0]) {
			pick = r;
			found = true;
			break;
		}
	}
	if (!found) {
		for (at = stepRacer(m, first, true); !(at == first) && at.list == RacerList::Defaults; at = stepRacer(m, at, true)) {
		}
		pick = at;
	}
	state.racer = pick;
	fillPickNames(m);
	showRacer(m);
	state.driverPart = -1;
	state.animating = false;
	const bool nextPlayer = g.versus && g.players == 0;
	m.ui.show(t, m.items[SelectRace], !nextPlayer);
	m.ui.show(t, m.items[SelectNext], nextPlayer);
	m.ui.select(t, m.items[nextPlayer ? SelectNext : SelectRace], IconQuiet);
}

// The strip's three names: the racer before, the racer, the one after; the middle one shown.
void RaceScreens::fillPickNames(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const int16_t car = t[m.items[SelectRacers]].carousel;
	const RacerRef around[3] = {stepRacer(m, state.racer, false), state.racer, stepRacer(m, state.racer, true)};
	int32_t k = 0;
	for (int16_t l = t[car].first; l >= 0 && k < 3; l = t[l].next, k++) {
		const RacerRecord* r = g.racer(around[k]);
		m.ui.setText(t, l, r ? std::u16string_view(r->name) : std::u16string_view());
	}
	m.ui.selectItem(t, car, 1);
}

// The racer's car, built for the showcase (Menus::carMesh).
void RaceScreens::showRacer(Menus& m)
{
	Game& g = *m.game;
	const RacerRecord* r = g.racer(state.racer);
	state.car.load(m.data->pieces, r ? r->pieces : std::vector<CarPiece>());
	state.car.chooseOffset(m.data->pieces);
	m.carMesh = state.car.menuMesh(m.data->pieces, m.data->sets.materials);
	state.racerChanged = true;
}

void RaceScreens::startIdle(Menus& m)
{
	showcaseIdle(m, state.driverPart, state.driverFrame);
	state.blendMs = -1;
	state.animating = true;
}

// Each frame's clocks (with the widgets'): the preview's scene, the minifig and the turntable.
void RaceScreens::tickRaceScreens(Menus& m, int32_t dt)
{
	if (m.screen == ScreenRacerSelect || m.screen == ScreenPlayer2Select) {
		state.spinMs += dt;
		if (!state.animating) {
			return;
		}
		if (state.blendMs >= 0) {
			state.blendMs += dt;
			state.driverFrame = blendedFrame(m.data->showcase, state.driverPart, state.blendMs, true);
		}
		else if (!partEnded(m.data->showcase, state.driverPart, state.driverFrame)) {
			state.driverFrame = advance(m.data->showcase, state.driverPart, state.driverFrame, dt);
		}
		return;
	}
	state.animMs += dt;
}

void RaceScreens::updateRaceScreens(Menus& m, int32_t dt)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	if (m.screen == ScreenRacerSelect || m.screen == ScreenPlayer2Select) {
		const bool leaving = m.pending;
		if (state.racerChanged) {
			state.racerChanged = false;
			if (!leaving) {
				// The minifig just built is on its first part, so a first draw giving that part is drawn again.
				state.driverPart = 0;
				startIdle(m);
			}
		}
		else if (state.animating && state.blendMs < 0 && partEnded(m.data->showcase, state.driverPart, state.driverFrame) &&
			g.chosen[0].list == RacerList::None && !leaving) {
			startIdle(m);
		}
		// The leave gate: open unless the minifig is playing a part that hasn't ended; during the chosen blend the old
		// part's end as it froze (an idle that ended on the press's frame lets the screen leave at once).
		const bool blending = state.blendMs >= 0 && state.blendMs < 200;
		state.held = m.pending && state.animating &&
			!(blending ? state.oldEnded : partEnded(m.data->showcase, state.driverPart, state.driverFrame));
		return;
	}
	// The setup screens.
	if (m.screen == ScreenTimeRace) {
		if (dt > state.toggleMs) {
			state.showingLap = !state.showingLap;
			refreshRecord(m);
		}
		else {
			state.toggleMs -= dt;
		}
	}
	if (m.screen != ScreenCircuitRace) {
		return;
	}
	if (state.fade >= 0 && !m.fades[state.fade].active) {
		state.fade = -1;
	}
	const bool rocket = state.circuit == 6;
	if (uint32_t(dt) > state.previewMs && !rocket) {
		state.place = (state.place + 1) & 3;
		showPlace(m);
	}
	else {
		state.previewMs -= uint32_t(dt);
	}
	const bool fading = std::any_of(m.fades.begin(), m.fades.end(), [](const Menus::Fade& f) { return f.active; });
	if (state.previewMs < 250 && !fading && !rocket) {
		state.fade = m.startFade(250, true, 0xff000000, Viewport);
	}
	if (state.previewChanged) {
		if (state.fade >= 0) {
			m.fades[state.fade].active = false;
		}
		state.fade = m.startFade(250, false, 0xff000000, Viewport);
		state.previewChanged = false;
	}
	(void)t;
}

void RaceScreens::raceScreenNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	if (m.screen == ScreenRacerSelect || m.screen == ScreenPlayer2Select) {
		if (what == NoticeValue && is(SelectRacers)) {
			// The cursor steps with the strip; the models change at once, the names when it settles.
			const bool forward = t[t[w].carousel].index == 2;
			state.racer = stepRacer(m, state.racer, forward);
			showRacer(m);
			return;
		}
		if (what == NoticeSettled && is(SelectRacers)) {
			fillPickNames(m);
			return;
		}
		if (what != NoticeReleased || m.pending) {
			return;
		}
		if (is(SelectConfig) || is(SelectCancel)) {
			state.animating = false;
			state.action = is(SelectConfig) ? 2 : 3;
			m.navigate(0);
			return;
		}
		if (is(SelectRace) || is(SelectNext)) {
			if (is(SelectRace) && g.versus) {
				g.racerCount = 0;
			}
			// The "chosen" animation, blended into from the pose now.
			state.oldEnded = state.animating && partEnded(m.data->showcase, state.driverPart, state.driverFrame);
			state.driverPart = partIndex(m.data->showcase, Chosen[g.draw() % 3]);
			state.driverFrame = 0;
			state.blendMs = 0;
			state.animating = true;
			if (g.players < 2) {
				g.chosen[g.players++] = state.racer;
			}
			state.action = is(SelectRace) ? 0 : 1;
			m.navigate(0);
		}
		return;
	}
	// The setup screens.
	if (what == NoticeValue && is(SetupCircuits)) {
		chooseCircuit(m, t[t[w].carousel].index);
		return;
	}
	if (what == NoticeValue && is(SetupRaces)) {
		const int32_t index = t[t[w].carousel].index;
		showRace(m, listedRace(*m.data, state.circuit, index), index + 4 * state.circuit);
		return;
	}
	if (what != NoticeReleased) {
		return;
	}
	if (is(SetupOk)) {
		if (m.screen != ScreenCircuitRace) {
			g.races[0] = int16_t(listedRace(*m.data, state.circuit, t[t[m.items[SetupRaces]].carousel].index));
			g.circuit = state.circuit;
			g.mode = m.screen == ScreenTimeRace ? Game::Mode::TimeRace : Game::Mode::Single;
			if (m.screen == ScreenTimeRace) {
				g.racerCount = 0;
			}
		}
		m.navigate(ScreenRacerSelect);
	}
	else if (is(SetupBack)) {
		if (m.screen != ScreenCircuitRace) {
			g.versus = false;
			g.racerCount = state.racersFound;
		}
		m.navigate(ScreenMainMenu);
	}
}

void RaceScreens::leaveRaceScreen(Menus& m)
{
	Game& g = *m.game;
	if (m.screen == ScreenCircuitRace || m.screen == ScreenSingleRace || m.screen == ScreenTimeRace) {
		if (state.fade >= 0) {
			m.fades[state.fade].active = false;
		}
		if (m.destination == ScreenRacerSelect) {
			m.push(ScreenRacerSelect);
		}
		else {
			m.pop(1);
		}
		return;
	}
	// Racer select: the button's stack change, the racer kept as player 1's choice, then the save screen first.
	const int32_t p = g.players;
	switch (state.action) {
	case 0:
		if (g.mode == Game::Mode::Circuit && p == 1) {
			m.push(uint16_t(ScreenCircuitIntro + g.circuit));
			g.racerCount = 6;
		}
		else {
			m.depth = 0;
			g.racerCount = g.versus ? 0 : g.racerCount + p;
		}
		g.versus = false;
		break;
	case 1: m.push(ScreenPlayer2Select); break;
	case 2: m.push(uint16_t(ScreenControl1 + p)); break;
	default:
		if (g.players > 0) {
			g.players--;
		}
		m.pop(1);
		break;
	}
	g.settings.picks[0] = state.racer;
	g.dirty = true;
	if (m.top() != ScreenSaveAll) {
		m.push(ScreenSaveAll);
	}
}

// The setup screens' preview: the pst scene looping on the frame's ms.
void RaceScreens::updatePreview(Menus& m, int32_t dt)
{
	CinemaPlayer& c = state.preview;
	const MenuData::Screen& s = m.data->screens[m.screen];
	if (!inRange(s.cinema.scenes, state.scene)) {
		return;
	}
	if (c.byStart.size() != s.cinema.scenes[state.scene].cues.size() || c.scene != size_t(state.scene) || c.placed.empty()) {
		// (Switching: the old scene's active cues end first, its cameras and lights with them.)
		for (const uint16_t cue : std::vector<uint16_t>(c.placed.empty() ? std::vector<uint16_t>() : c.begun)) {
			Cinema::cue3d(c, s, cue, false);
		}
		Cinema::cinemaStart3d(c, s, size_t(state.scene));
	}
	m.cinema.playScene(m, c, s, dt, dt, true, false); // (the frame's ms from the first update on, as a cinematic's)
}

} // namespace lr
