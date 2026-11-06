// The front end's own screens (Screens): the main menu, options, controls, the language, the garage, the load and save
// steps and a new racer's card; what each builds, does each frame and with its items, and the garage's way out.

#include "LRCore/Menus/Menus.h"

#include "Show/Animation.h"
#include "Menus/MenuNames.h"
#include "Menus/Pen.h"

#include <algorithm>
#include <cstdio>

namespace lr {

namespace {

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

// The load and save screens: the operation runs as the screen is made; it leaves on its first update.
void Screens::buildStorage(Menus& m)
{
	Game& g = *m.game;
	if (m.screen == ScreenLoadAll) {
		for (auto& [slot, bytes] : g.openSlots()) {
			m.saves.push_back({slot, std::move(bytes)});
		}
	}
	else if (m.screen == ScreenSaveAll) {
		m.saveSlots(true);
		if (g.saveReminder) {
			g.saveReminder = false; // (its backdrop is skipped once)
			return;
		}
	}
	else if (m.screen == ScreenOpenCard || m.screen == ScreenSaveCard) {
		if (m.screen == ScreenSaveCard || !g.cards[g.currentCard].empty()) {
			m.saveSlots(false);
		}
	}
	m.ui.screenItem(NameBackdrop, NameBackdrop, -1);
}

void Screens::buildMainMenu(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	if (m.tune < 0) {
		m.playTune(0, true);
	}
	m.ui.screenItem(NameBackdrop, NameBackdrop, -1);
	m.ui.screenItem(NameLogo, NameLogo, -1);
	m.ui.screenItem(NamePlatform, NamePlatform, -1);
	static const uint32_t Items[7][2] = {{NameGarage, TextBuild}, {NameCircuitRace, TextCircuitRace}, {NameSingleRace, TextSingleRace},
		{NameVersusRace, TextVersusRace}, {NameTimeRace, TextTimeRace}, {NameOptions, TextOptions}, {NameQuit, TextQuit}};
	for (size_t i = 0; i < 7; i++) {
		m.items[MainGarage + i] = m.ui.screenItem(Items[i][0], StyleItem, int32_t(Items[i][1]));
	}
	if (g.pads == 0) {
		m.ui.enable(t, m.items[MainVersusRace], false); // VERSUS RACE needs a joystick
	}
	g.chosen = {};
	g.players = 0;
	m.ui.select(t, m.items[MainGarage], IconQuiet | IconSilent);
	state.idleMs = 30000;
	state.attractMs = 60000;
	state.animPart = 0;
}

void Screens::buildOptions(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	if (m.tune < 0) {
		m.playTune(0, true);
	}
	g.dirty = true; // it sets the racer and lap counts as it opens (their setters always mark the state)
	m.ui.screenItem(NameBackdrop, NameBackdrop, -1);
	m.items[OptionsBanner] = m.ui.makeBanner(TextOptionsTitle);
	// Page 0, then page 1's and page 2's items. (VIDEO OPTIONS opens the presentation's own settings: page 3 isn't built;
	// nor are the stereo selector and the brightness label the original makes but never shows.)
	static const uint32_t Page0[8][2] = {{NameGameOptions, TextGameOptions}, {NameVideoOptions, TextVideoOptions},
		{NameAudioOptions, TextAudioOptions}, {NamePlayer1Controls, TextPlayer1Controls}, {NamePlayer2Controls, TextPlayer2Controls},
		{NameLanguage, TextPickLanguage}, {NameCredits, TextViewCredits}, {NameBack, TextMainMenu}};
	for (size_t i = 0; i < 8; i++) {
		m.items[OptionsGame + i] = m.ui.screenItem(Page0[i][0], i == 7 ? StyleLeftArrow : StyleItem, int32_t(Page0[i][1]));
	}
	m.items[OptionsOpponents] = m.ui.makeTextSelector(NameOpponents, 6, TextDigits);
	m.items[OptionsLaps] = m.ui.makeTextSelector(NameLaps, 5, TextDigits + 1);
	m.ui.selectItem(t, t[m.items[OptionsOpponents]].carousel, g.settings.opponents);
	m.ui.selectItem(t, t[m.items[OptionsLaps]].carousel, g.settings.laps - 1);
	m.items[OptionsOpponentsLabel] = m.ui.screenItem(NameOpponentsText, StyleMenuFont, TextOpponentsLabel);
	m.items[OptionsLapsLabel] = m.ui.screenItem(NameLapsText, StyleMenuFont, TextLapsLabel);
	m.items[OptionsMusic] = m.ui.screenItem(NameMusicVolume, StyleSoundSlider, -1);
	m.items[OptionsSound] = m.ui.screenItem(NameSoundVolume, StyleSoundSlider, -1);
	m.ui.setValue(t, m.items[OptionsMusic], volumeStep(g.settings.music));
	m.ui.setValue(t, m.items[OptionsSound], volumeStep(g.settings.sound));
	m.items[OptionsMusicLabel] = m.ui.screenItem(NameMusicText, StyleMenuFont, TextMusicVolumeLabel);
	state.rows[0] = m.ui.screenItem(NameSoundText, StyleMenuFont, TextSoundVolumeLabel);
	showPage(m, 0);
}

void Screens::buildControls(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	state.player = m.screen == ScreenControl1 ? 0 : 1;
	m.ui.screenItem(NameBackdrop, NameBackdrop, -1);
	m.ui.makeBanner(state.player ? TextPlayer2Controls : TextPlayer1Controls);
	m.items[ControlsDevice] = m.ui.makeLabelSelector(NameDevices, StyleImageSelector, NameImageBox);
	// The devices: the joysticks present, then three keyboard layouts.
	for (uint8_t j = 0; j < 2 && j < g.pads; j++) {
		state.entries.push_back(j);
	}
	for (uint8_t k = 0; k < 3; k++) {
		state.entries.push_back(uint8_t(g.pads + 2 + k));
	}
	for (uint8_t e : state.entries) {
		const int16_t image = m.ui.make(t, m.data->screens[m.screen].layout, e < 2 ? "joystick" : "keyboard",
			e < 2 ? "joystick" : "keyboard", -1);
		m.ui.addItem(t, t[m.items[ControlsDevice]].carousel, image);
	}
	int32_t chosen = 0;
	for (size_t i = 0; i < state.entries.size(); i++) {
		chosen = state.entries[i] == g.settings.layout[state.player] ? int32_t(i) : chosen;
	}
	m.ui.selectItem(t, t[m.items[ControlsDevice]].carousel, chosen);
	for (size_t r = 0; r < 9; r++) {
		state.rows[r] = m.ui.screenItem(NameControlRows + uint32_t(r), StyleItem, -1);
		m.ui.screenItem(NameControlLabels + uint32_t(r), StyleMenuFont, TextControlLabels + int32_t(r));
	}
	m.items[ControlsDone] = m.ui.screenItem(NameBack, StyleCheck, TextFinish);
	refreshControls(m);
	m.ui.select(t, state.rows[0], IconQuiet | IconSilent);
}

void Screens::buildPickLanguage(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	m.ui.screenItem(NameBackdrop, NameBackdrop, -1);
	m.items[LanguageList] = m.ui.makeTextSelector(NameLanguages, 9, TextLanguages);
	m.ui.selectItem(t, t[m.items[LanguageList]].carousel, g.settings.language);
	m.items[LanguageBack] = m.ui.screenItem(NameBack, StyleLeftArrow, TextOptionsTitle);
	m.ui.makeBanner(TextPickLanguage);
	m.ui.select(t, m.items[LanguageList], IconQuiet | IconSilent);
}

// Options' pages.

void Screens::showPage(Menus& m, int32_t page)
{
	WidgetTree& t = m.ui.tree;
	state.page = page;
	for (size_t i = OptionsGame; i <= OptionsCredits; i++) {
		m.ui.show(t, m.items[i], page == 0);
	}
	for (size_t i : {OptionsOpponents, OptionsLaps, OptionsOpponentsLabel, OptionsLapsLabel}) {
		m.ui.show(t, m.items[i], page == 1);
	}
	for (size_t i : {OptionsMusic, OptionsSound, OptionsMusicLabel}) {
		m.ui.show(t, m.items[i], page == 2);
	}
	m.ui.show(t, state.rows[0], page == 2);
	// By page: the banner and the first item.
	static constexpr int32_t Banners[3] = {TextOptionsTitle, TextGameOptions, TextAudioOptions};
	static constexpr size_t Firsts[3] = {OptionsGame, OptionsOpponents, OptionsMusic};
	m.ui.relabel(t, m.items[OptionsBanner], Banners[page]);
	m.ui.alignRight(t, m.items[OptionsBanner], 20);
	m.ui.setString(t, m.items[OptionsBack], page == 0 ? TextMainMenu : TextOptionsTitle);
	m.ui.select(t, m.items[Firsts[page]], IconQuiet | IconSilent);
}

// The controls screen's rows: each binding's key name, dots while waiting.

void Screens::refreshControls(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const Widget& car = t[t[m.items[ControlsDevice]].carousel];
	const size_t layout = state.entries[car.index];
	for (size_t r = 0; r < 9; r++) {
		const uint32_t binding = g.settings.layouts[layout].bindings[r];
		const std::u16string name = state.waiting == int32_t(r) || !binding ? std::u16string(u"........") : keyName(binding);
		if (t[t[state.rows[r]].label].text != name) {
			m.ui.setText(t, state.rows[r], name);
		}
		if (r < 2) {
			m.ui.enable(t, state.rows[r], layout >= 2);
		}
	}
}

// The garage.

void Screens::buildGarage(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	if (m.tune < 0) {
		m.playTune(1, true);
	}
	if (m.depth == 1) {
		m.depth = 0;
		m.push(ScreenMainMenu);
		m.push(ScreenGarage);
	}
	g.newRacer = g.copying = g.backToGarage = false;
	g.chosen = {};
	// The racers: the session's, then each card's.
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
			found = state.racers[k] == g.settings.picks[0];
		}
		if (!found) {
			state.cursor = 1 % n;
		}
	}
	m.ui.makeNamed(t, NameBackdrop, NameBackdrop, -1);
	m.ui.makeNamed(t, NameShowcase, NamePlatform, -1);
	m.items[GarageRacers] = m.ui.makeTextSelector(NameRacerBox, n > 0 ? 3 : 0, -1);
	fillRacerNames(m);
	static const uint32_t Page0[5][2] = {{NameNewRacer, TextNewRacer}, {NameEditRacer, TextEditRacer}, {NameCopyRacer, TextCopyRacer},
		{NameDeleteRacer, TextDeleteRacer}, {NameTestDrive, TextTestDrive}};
	for (size_t i = 0; i < 5; i++) {
		m.items[GarageNew + i] = m.ui.makeNamed(t, Page0[i][0], StyleItem, -1, int32_t(Page0[i][1]));
	}
	m.items[GarageBack] = m.ui.makeNamed(t, NameBack, StyleLeftArrow, -1, TextMainMenu);
	static const uint32_t Page1[3][2] = {{NameEditDriver, TextBuildDriver}, {NameEditLicense, TextMakeLicense},
		{NameEditCar, TextBuildCar}};
	for (size_t i = 0; i < 3; i++) {
		m.items[GarageDriver + i] = m.ui.makeNamed(t, Page1[i][0], StyleItem, -1, int32_t(Page1[i][1]));
	}
	m.items[GarageBanner] = m.ui.makeBanner(TextBuildMenu);
	garagePage(m, 0);
}

void Screens::fillRacerNames(Menus& m)
{
	Game& g = *m.game;
	WidgetTree& t = m.ui.tree;
	const int16_t car = t[m.items[GarageRacers]].carousel;
	const int32_t n = int32_t(state.racers.size());
	int32_t k = -1;
	for (int16_t l = t[car].first; l >= 0; l = t[l].next, k++) {
		const RacerRecord* r = n ? g.racer(state.racers[(state.cursor + k + n) % n]) : nullptr;
		m.ui.setText(t, l, r ? std::u16string_view(r->name) : std::u16string_view());
	}
	if (n) {
		m.ui.selectItem(t, car, 1);
	}
}

void Screens::garagePage(Menus& m, int32_t page)
{
	WidgetTree& t = m.ui.tree;
	state.page = page;
	const int32_t n = int32_t(state.racers.size());
	for (size_t i = GarageNew; i <= GarageTestDrive; i++) {
		m.ui.show(t, m.items[i], page == 0);
	}
	for (size_t i = GarageDriver; i <= GarageCar; i++) {
		m.ui.show(t, m.items[i], page == 1);
	}
	m.ui.relabel(t, m.items[GarageBanner], page == 0 ? TextBuildMenu : TextEditRacerTitle);
	m.ui.alignRight(t, m.items[GarageBanner], 20);
	m.ui.setString(t, m.items[GarageBack], page == 0 ? TextMainMenu : TextBuildMenu);
	if (page == 0) {
		// QUIRK: "full" is exactly 209 racers, which two cards can't hold.
		const bool full = n == 209;
		m.ui.enable(t, m.items[GarageNew], !full);
		m.ui.enable(t, m.items[GarageEdit], n > 0);
		m.ui.enable(t, m.items[GarageCopy], n > 0 && !full);
		m.ui.enable(t, m.items[GarageDelete], n > 0);
		m.ui.enable(t, m.items[GarageTestDrive], n > 0);
		m.ui.enable(t, m.items[GarageRacers], n > 1);
		m.ui.select(t, full ? m.items[GarageEdit] : m.items[GarageNew], IconQuiet | IconSilent);
	}
	else {
		m.ui.enable(t, m.items[GarageRacers], false);
		m.ui.select(t, m.items[GarageDriver], IconQuiet | IconSilent);
	}
	if (page == 0) {
		state.shown = -2; // the slot is set again: a new idle animation
	}
}

void Screens::newRacerStep(Menus& m)
{
	Game& g = *m.game;
	// Into the current card: a copy of the garage's racer, or a new empty one.
	std::vector<RacerRecord>& list = g.cards[g.currentCard];
	const RacerList card = g.currentCard ? RacerList::Card1 : RacerList::Card0;
	if (list.size() >= Game::CardCapacity) {
		m.ui.cursorOn = true;
		m.ui.tell(TextSaveFull);
		return;
	}
	if (g.copying) {
		const RacerRecord* from = g.racer(g.chosen[0]);
		const RacerRecord copy = from ? *from : RacerRecord();
		list.push_back(copy);
		g.markRacers(card);
		g.chosen[0] = {card, uint16_t(list.size() - 1)};
		m.navigate(ScreenGarage);
	}
	else {
		list.emplace_back();
		g.markRacers(card);
		g.chosen[0] = {card, uint16_t(list.size() - 1)};
		g.working = list.back();
		m.navigate(ScreenDriver);
	}
}

// Each frame's timers that belong to the screen (with the widgets').

// The screen's own update, then its leave rule when it navigates.

// The main menu: the minifig's waiting animation (the glass tap once each time it passes its mark), a circuit's
// notice, the attract demo's wait, quitting.
void Screens::updateMainMenu(Menus& m, int32_t dt)
{
	Game& g = *m.game;
	state.idleMs = std::max(0, state.idleMs - dt);
	state.attractMs = std::max(0, state.attractMs - dt);
	// The glass tap: once each time the waiting animation passes its mark.
	const bool past = minifigFrame(m.data->minifig, state.animPart, state.animMs) >= 208;
	if (state.animPart == 2 && past && state.tapArmed) {
		m.play(SoundGlassTap);
		state.tapArmed = false;
	}
	else if (!past) {
		state.tapArmed = true;
	}
	if (g.circuitNotice) {
		g.circuitNotice = false;
		m.ui.tell(TextCircuitUnlocked);
	}
	if (state.idleMs == 0) {
		state.animPart = 2;
		state.animMs = 0;
		state.idleMs = 30000;
	}
	else if (state.animPart != 1 &&
		partEnded(m.data->minifig, state.animPart, minifigFrame(m.data->minifig, state.animPart, state.animMs))) {
		state.animPart = 1; // looping
		state.animMs = 0;
	}
	if (state.attractMs == 0) {
		m.navigate(-1);
	}
	if (state.quitting) {
		m.navigate(-1);
		m.quit = true;
	}
}

// The garage: a page asked for, the racer's list button, and the shown racer's idle animations.
void Screens::updateGarage(Menus& m)
{
	if (state.nextPage >= 0) {
		garagePage(m, state.nextPage);
		state.nextPage = -1;
	}
	const int32_t n = int32_t(state.racers.size());
	if (m.items[GarageCopy] >= 0 && n) {
		const bool session = state.racers[state.cursor].list == RacerList::Session;
		const std::u16string want(m.text(session ? TextSaveRacer : TextCopyRacer));
		if (m.ui.tree[m.ui.tree[m.items[GarageCopy]].label].text != want) {
			m.ui.setString(m.ui.tree, m.items[GarageCopy], session ? TextSaveRacer : TextCopyRacer);
		}
	}
	// The slot: a racer shown anew starts a random idle animation; one that ended starts another.
	if (n && state.shown != state.cursor) {
		// The racer's driver is put in the scene anew: it stands playing its first part until it is given another.
		state.shown = state.cursor;
		state.driverPart = 0;
		state.driverFrame = 0;
		if (!m.pending) {
			showcaseIdle(m, state.driverPart, state.driverFrame);
		}
	}
	else if (n && partEnded(m.data->showcase, state.driverPart, state.driverFrame) && !m.pending) {
		showcaseIdle(m, state.driverPart, state.driverFrame);
	}
}

// A random idle animation of the showcase's, never the one playing.
void showcaseIdle(const Menus& m, int32_t& part, float& frame)
{
	int32_t next = -1;
	do {
		next = partIndex(m.data->showcase, IdleParts[m.game->draw() % 7]);
	} while (next == part);
	part = next;
	frame = 0;
}

// The garage left: to the main menu, the test drive or the destination; every way out keeps player 1's racer and
// saves first.
void Screens::leaveGarage(Menus& m)
{
	Game& g = *m.game;
	if (m.destination == ScreenMainMenu) {
		m.pop(1);
	}
	else if (m.destination == -2) {
		m.depth = 0; // the test drive
	}
	else if (m.destination != ScreenSaveAll) {
		m.push(uint16_t(m.destination));
	}
	if (!state.racers.empty()) {
		g.settings.picks[0] = state.racers[state.cursor];
		g.dirty = true;
	}
	if (m.top() != ScreenSaveAll) {
		m.push(ScreenSaveAll);
	}
}

// The items' notices.

void Screens::mainMenuNotice(Menus& m, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	if (what != NoticeReleased) {
		return;
	}
	if (is(MainGarage)) {
		m.stopTune();
		m.navigate(ScreenGarage);
	}
	else if (is(MainCircuitRace)) {
		g.versus = false;
		m.navigate(ScreenCircuitRace);
	}
	else if (is(MainSingleRace) || is(MainVersusRace)) {
		g.versus = is(MainVersusRace);
		m.navigate(ScreenSingleRace);
	}
	else if (is(MainTimeRace)) {
		m.navigate(ScreenTimeRace);
	}
	else if (is(MainOptions)) {
		m.navigate(ScreenOptions);
	}
	else if (is(MainQuit)) {
		m.ui.ask(TextQuitQuestion, TextYes, TextNo);
	}
}

void Screens::optionsNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	if (what == NoticeValue) {
		if (is(OptionsOpponents) || is(OptionsLaps)) {
			const int32_t value = t[t[w].carousel].index;
			if (is(OptionsOpponents)) {
				g.settings.opponents = uint8_t(value);
				g.racerCount = value;
			}
			else {
				g.settings.laps = uint8_t(value + 1);
			}
			g.dirty = true;
		}
		else if (is(OptionsMusic) || is(OptionsSound)) {
			(is(OptionsMusic) ? g.settings.music : g.settings.sound) = volumeByte(t[w].index);
			g.dirty = true;
		}
		return;
	}
	if (what != NoticeReleased) {
		return;
	}
	if (is(OptionsGame)) {
		state.nextPage = 1;
	}
	else if (is(OptionsVideo) || is(OptionsAudio)) {
		state.afterNotice = is(OptionsVideo) ? 3 : 2;
		m.ui.tell(TextOptionsWarning);
	}
	else if (is(OptionsPlayer1) || is(OptionsPlayer2)) {
		m.navigate(is(OptionsPlayer1) ? ScreenControl1 : ScreenControl2);
	}
	else if (is(OptionsLanguage)) {
		m.navigate(ScreenPickLanguage);
	}
	else if (is(OptionsCredits)) {
		m.navigate(ScreenCredits);
	}
	else if (is(OptionsBack)) {
		if (state.page == 0) {
			m.navigate(ScreenMainMenu);
		}
		else {
			state.nextPage = 0;
		}
	}
}

void Screens::controlsNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	if (what == NoticeValue && is(ControlsDevice)) {
		refreshControls(m);
		return;
	}
	if (what == NoticeReleased && is(ControlsDone)) {
		g.pickLayout(state.player, state.entries[t[t[m.items[ControlsDevice]].carousel].index]);
		m.navigate(ScreenOptions);
		return;
	}
	// A row pressed waits (still pressed) for the key to bind.
	for (size_t r = 0; r < 9 && what == NoticePressed; r++) {
		if (w == state.rows[r]) {
			const size_t layout = state.entries[t[t[m.items[ControlsDevice]].carousel].index];
			g.bind(layout, r, 0);
			state.waiting = int32_t(r);
			refreshControls(m);
		}
	}
}

void Screens::garageNotice(Menus& m, WidgetTree& t, int16_t w, int32_t what)
{
	Game& g = *m.game;
	const auto is = [&](size_t i) { return m.isItem(w, i); };
	const int32_t n = int32_t(state.racers.size());
	if (what == NoticeValue && is(GarageRacers) && n > 1) {
		// The cursor steps with the carousel; the labels follow when it settles.
		const int32_t dir = t[t[w].carousel].index == 2 ? 1 : -1;
		state.cursor = (state.cursor + dir + n) % n;
		return;
	}
	if (what == NoticeSettled && is(GarageRacers)) {
		fillRacerNames(m);
		return;
	}
	if (what != NoticeReleased) {
		return;
	}
	const RacerRef current = n ? state.racers[state.cursor] : RacerRef();
	if (is(GarageNew)) {
		g.newRacer = true;
		g.working = {};
		m.navigate(ScreenPickCard);
	}
	else if (is(GarageEdit)) {
		state.nextPage = 1;
	}
	else if (is(GarageCopy)) {
		g.chosen[0] = current;
		g.copying = true;
		m.navigate(ScreenPickCard);
	}
	else if (is(GarageDelete)) {
		m.ui.ask(TextDeleteWarning, TextYes, TextNo);
	}
	else if (is(GarageTestDrive)) {
		g.backToGarage = true;
		g.versus = false;
		g.chosen[0] = current;
		g.players = 1;
		m.navigate(-2);
	}
	else if (is(GarageBack)) {
		if (state.page == 0) {
			g.chosen = {};
			m.stopTune();
			m.navigate(ScreenMainMenu);
		}
		else {
			state.nextPage = 0;
		}
	}
	else if (is(GarageDriver) || is(GarageLicense) || is(GarageCar)) {
		g.chosen[0] = current;
		if (const RacerRecord* r = g.racer(current)) {
			g.working = *r;
		}
		m.navigate(is(GarageDriver) ? ScreenDriver : is(GarageLicense) ? ScreenLicense : ScreenEditCar);
	}
}

// The screen's own look at a key, before the widgets.

// The minifig alone in TT.WDB, playing LEGOMAN's parts.
void Screens::mainMenuScene(const Menus& m, MenuScene& scene) const
{
	const Game& g = *m.game;
	// The champion of the furthest circuit open: open = the first circuit j = 1..6 still locked, less
	// one (6 if all are open); the driver entry by it, entry 0 for 6.
	int32_t open = 6;
	for (int32_t j = 1; j <= 6; j++) {
		if (!(g.settings.circuits >> j & 1)) {
			open = j - 1;
			break;
		}
	}
	static const int32_t Champions[7] = {0x13, 0x12, 0x16, 0x15, 0x14, 0x17, 0};
	const std::vector<DriverTable::Driver>& all = m.data->drivers.drivers;
	const size_t e = size_t(Champions[open]);
	std::array<uint8_t, 5> looks = e < all.size() ? driverLooks(all[e]) : std::array<uint8_t, 5>{};
	looks[4] = 0; // the main menu shows dflt
	m.sceneMinifig(scene, {18.181229f, -10.622759f, 0.025708f}, {0.97237f, -0.233445f, 0}, {0, 0, 1}, looks, MenuScene::Thing::Legoman,
		state.animPart, blendedFrame(m.data->minifig, state.animPart, state.animMs, state.animPart != 0));
}

} // namespace lr
