// The menus' frame and flow: starting, one frame (the dispatcher, the mouse, the screen's update), fades, music, the
// save files, and the screens' routing: built, ticked, updated, left by their rules, told of their widgets' notices
// and dialogs' answers, and their own look at a key.

#include "LRCore/Menus/Menus.h"

#include "Menus/MenuNames.h"
#include "Menus/Pen.h"
#include "Show/Animation.h"

#include <algorithm>

namespace lr {

Pen Menus::pen() { return {data, language, elements, glyphs}; }

void Menus::playTune(int32_t number, bool loops)
{
	tune = number;
	tuneLoops = loops;
	tuneStarted = true;
}

int32_t Menus::startFade(int32_t ms, bool in, uint32_t colour, MenuRect rect)
{
	for (size_t i = 0; i < fades.size(); i++) {
		if (!fades[i].active) {
			fades[i] = {ms, ms, in, true, false, colour, rect};
			return int32_t(i);
		}
	}
	return -1;
}

void Menus::navigate(int32_t where)
{
	pending = true;
	destination = where;
}

void Menus::saveSlots(bool both)
{
	Game& g = *game;
	for (size_t slot : {1u, 0u}) {
		if (!both && slot != g.currentCard) {
			continue;
		}
		std::vector<uint8_t> bytes = g.saveSlot(slot);
		if (!bytes.empty()) {
			saves.push_back({uint8_t(slot), std::move(bytes)});
		}
	}
}

void Menus::start(const MenuData& menuData, Game& g, uint16_t next, bool save)
{
	data = &menuData;
	game = &g;
	if (g.defaults.empty()) {
		g.defaults = menuData.defaults; // racer select lists them
	}
	g.racerCount = g.settings.opponents; // the race setup screens start from the options' count
	depth = 0;
	push(next);
	if (save) {
		push(ScreenSaveAll);
	}
	pending = false;
	quit = raceStart = false;
	ui.dialogCount = 0;
	ui.queued = 0;
	ui.held.clear();
	tune = -1;
	language = g.settings.language;
	build(); // drawn from the next frame on
}

// Dialogs.

// The frame.

void Menus::frame(Game& g, int32_t dt, std::span<const MenuKey> keys, int32_t mx, int32_t my, uint32_t buttons)
{
	game = &g;
	sounds.clear();
	saves.clear();
	tuneStarted = false;
	const uint16_t before = top();

	// The widgets' timers (the dialog's when one is open), a cinematic's clock.
	WidgetTree& timed = ui.active();
	if (!timed.widgets.empty()) {
		ui.tick(timed, 0, dt);
	}
	if (!ui.dialogCount) {
		tickScreen(dt);
	}

	// The frame's events and the repeats, then the dispatcher.
	ui.queueInput(keys, buttons, dt);
	dispatchKeys();

	// The mouse: its motion is handled where the cursor was; the cursor moves at the frame's end.
	const int32_t dx = mx - ui.mouseX, dy = my - ui.mouseY;
	const bool moved = dx || dy;
	ui.mouseX = mx;
	ui.mouseY = my;
	if (moved) {
		moveMouse(dx, dy);
	}

	// The dialog's update, or the screen's.
	if (ui.dialogCount) {
		ui.updateDialog(dt);
	}
	else {
		updateScreen(dt);
	}
	if (quit) {
		return;
	}
	if (depth == 0) {
		leaveScreen();
		raceStart = true;
	}
	else if (top() != before) {
		leaveScreen();
		build();
	}
	stepFades(dt);
	if (!raceStart) {
		drawFrame();
	}
	if (moved) {
		// Kept a quarter of the arrow (32x32) from the right and bottom edges.
		ui.cursorX = std::clamp(mx, 0, ScreenWidth - 8);
		ui.cursorY = std::clamp(my, 0, ScreenHeight - 8);
	}
}

// The dispatcher: the queued events in order, until one is taken by the screen or the focused widget.
void Menus::dispatchKeys()
{
	size_t taken = 0;
	for (; taken < ui.queued; taken++) {
		// The screen's own handler, the focused widget (either taking it ends the frame's events: the rest wait), then
		// the selection keys, then down the tree.
		const MenuKey k = ui.queue[taken];
		if (screenKey(k)) {
			taken++;
			break;
		}
		WidgetTree& now = ui.active();
		if (now.widgets.empty()) {
			continue;
		}
		const int16_t focused = now.focused();
		if (focused >= 0) {
			const MenuRect r = ui.global(now, focused);
			if (k.down ? ui.keyDown(now, focused, k, ui.cursorX - r.left, ui.cursorY - r.top)
					   : ui.keyUp(now, focused, k, ui.cursorX - r.left, ui.cursorY - r.top)) {
				taken++;
				break;
			}
		}
		if (k.key == MenuShift || k.key == MenuRightShift) {
			ui.shift = k.down;
			continue;
		}
		if (k.key == MenuCtrl || k.key == MenuRightCtrl) {
			ui.ctrl = k.down;
			continue;
		}
		const bool tab = k.key == MenuTab;
		const bool forward = (tab && !ui.shift) || k.key == MenuDown || k.key == (SourceAxis | 2) || k.key == (SourcePad | 8);
		const bool back = (tab && ui.shift) || k.key == MenuUp || k.key == (SourceAxis | 3) || k.key == (SourcePad | 6);
		if (forward || back || k.key == (SourcePad | 1)) {
			if (k.down && (forward || back)) {
				ui.moveSelection(now, forward);
			}
			continue;
		}
		ui.routeKey(now, 0, k, ui.cursorX, ui.cursorY);
	}
	std::copy(ui.queue.begin() + taken, ui.queue.begin() + ui.queued, ui.queue.begin());
	ui.queued = uint8_t(ui.queued - taken);
}

// The mouse's motion, handled where the cursor was: the builder's drag, the focused widget keeping it, else down the
// tree.
void Menus::moveMouse(int32_t dx, int32_t dy)
{
	WidgetTree& now = ui.active();
	const int16_t focused = now.widgets.empty() ? int16_t(-1) : now.focused();
	// A focused text field keeps the motion only once the mouse has taken it there (hovering it); the focus it has
	// from its screen's opening doesn't hold the mouse (on the license, the first motion
	// hover-selects the button under the cursor, a later one after hovering the field is kept by it).
	const bool keeps = focused >= 0 && (now[focused].kind != Widget::TextField || now[focused].grabbed);
	const bool builder = !ui.dialogCount && screen == ScreenCarBuilder && builders.builderMotion(*this, dx, dy);
	if (!builder && !(keeps && ui.cursorOver(now, focused, dx)) && !now.widgets.empty()) {
		ui.routeCursor(now, 0, ui.cursorX, ui.cursorY);
	}
}

// The fades: each one's time down; ended the frame after it reaches 0, let go the frame after that.
void Menus::stepFades(int32_t dt)
{
	for (Fade& f : fades) {
		if (!f.active) {
			continue;
		}
		if (f.leftMs == 0) {
			if (f.ended) {
				f.active = false;
				continue;
			}
			f.ended = true;
		}
		f.leftMs = std::max(0, f.leftMs - dt);
	}
}

namespace {

bool cinematic(uint16_t n)
{
	return n == ScreenLegal || n == ScreenCredits || (n >= ScreenAward1 && n <= ScreenVeronicaCar) ||
		(n >= ScreenCircuitIntro && n <= ScreenCircuitIntro + 6);
}
bool step(uint16_t n) { return (n >= ScreenLoadAll && n <= ScreenSaveCard) || n == ScreenPickCard || n == ScreenNewRacer; }

} // namespace

void Menus::build()
{
	Game& g = *game;
	screen = top();
	language = g.settings.language; // a language picked takes effect as the next screen is made
	items = {};
	screens.state = {};
	race.state = {};
	builders.state = {};
	cinema.state = {};
	pending = false;
	destination = -1;
	ui.queued = 0; // keys from before the screen existed are dropped
	WidgetTree& t = ui.tree;
	ui.makeRoot(t);
	ui.cursorOn = !cinematic(screen) && !step(screen);
	switch (screen) {
	case ScreenLegal:
	case ScreenCredits:
		ui.make(t, data->screens[screen].layout, screen == ScreenLegal ? "legal" : "credits", "backdrp", 0);
		cinema.startCinema(*this);
		if (screen == ScreenCredits) {
			playTune(17, true);
		}
		break;
	case ScreenLoadAll:
	case ScreenLoadCard:
	case ScreenOpenCard:
	case ScreenSaveAll:
	case ScreenSaveCard: screens.buildStorage(*this); break;
	case ScreenPickCard:
	case ScreenNewRacer:
		ui.screenItem(NameBackdrop, NameBackdrop, -1);
		if (screen == ScreenNewRacer) {
			screens.newRacerStep(*this);
		}
		break;
	case ScreenMainMenu: screens.buildMainMenu(*this); break;
	case ScreenOptions: screens.buildOptions(*this); break;
	case ScreenControl1:
	case ScreenControl2: screens.buildControls(*this); break;
	case ScreenPickLanguage: screens.buildPickLanguage(*this); break;
	case ScreenGarage: screens.buildGarage(*this); break;
	case ScreenDriver: builders.buildDriver(*this); break;
	case ScreenLicense: builders.buildLicense(*this); break;
	case ScreenEditCar: builders.buildEditCar(*this); break;
	case ScreenCarBuilder: builders.buildCarBuilder(*this); break;
	case ScreenCircuitRace:
	case ScreenSingleRace:
	case ScreenTimeRace: race.buildRaceSetup(*this); break;
	case ScreenRacerSelect:
	case ScreenPlayer2Select: race.buildRacerSelect(*this); break;
	default:
		if (cinematic(screen)) {
			// A cinematic screen: its one scene view.
			ui.make(t, data->screens[screen].layout, data->names.narrow(screen), "", 0);
			cinema.startCinema(*this);
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

void Menus::tickScreen(int32_t dt)
{
	if (cinematic(screen)) {
		cinema.updateCinema(*this, dt);
	}
	if (raceScreen(screen)) {
		race.tickRaceScreens(*this, dt);
	}
	if (screen == ScreenMainMenu) {
		screens.state.animMs += dt;
	}
	if (screen == ScreenGarage && screens.state.driverPart >= 0) {
		screens.state.driverFrame = advance(data->showcase, screens.state.driverPart, screens.state.driverFrame, dt);
	}
	builders.tickBuilders(*this, dt);
}

void Menus::updateScreen(int32_t dt)
{
	Game& g = *game;
	switch (screen) {
	case ScreenLegal:
	case ScreenCredits:
		cinema.state.skippable = true;
		if (cinema.state.finished) {
			navigate(-1);
		}
		break;
	case ScreenCircuitRace:
	case ScreenSingleRace:
	case ScreenTimeRace:
	case ScreenRacerSelect:
	case ScreenPlayer2Select:
		race.updateRaceScreens(*this, dt);
		// The preview after the screen's own update (a race shown this frame plays from this frame).
		if (screen == ScreenCircuitRace || screen == ScreenSingleRace || screen == ScreenTimeRace) {
			race.updatePreview(*this, dt);
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
	case ScreenMainMenu: screens.updateMainMenu(*this, dt); break;
	case ScreenOptions:
		if (screens.state.nextPage >= 0) {
			screens.showPage(*this, screens.state.nextPage);
			screens.state.nextPage = -1;
		}
		break;
	case ScreenControl1:
	case ScreenControl2: screens.refreshControls(*this); break;
	case ScreenGarage: screens.updateGarage(*this); break;
	default:
		if (cinematic(screen)) {
			// The circuits' intros can be skipped after their first update; the awards and win cars never.
			cinema.state.skippable = screen >= ScreenCircuitIntro && !cinema.state.cinema.first;
			if (cinema.state.finished) {
				navigate(-1);
			}
			break;
		}
		builders.updateBuilders(*this);
		break;
	}
	if (pending && !race.state.held) {
		leave();
	}
}

void Menus::leave()
{
	Game& g = *game;
	pending = false;
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
	case ScreenGarage: screens.leaveGarage(*this); return;
	case ScreenDriver:
	case ScreenLicense:
	case ScreenEditCar:
	case ScreenCarBuilder: builders.leaveBuilder(*this); return;
	default:
		if (raceScreen(screen)) {
			race.leaveRaceScreen(*this);
		}
		else if (cinematic(screen)) {
			cinema.leaveCinema(*this);
		}
		else {
			pop(1);
		}
		return;
	}
}

void Menus::notice(WidgetTree& t, int16_t w, int32_t what)
{
	if (ui.dialogCount && &t == &ui.dialogs[ui.dialogCount - 1].tree) {
		// A dialog's button: the screen decides.
		MenuDialog& d = ui.dialogs[ui.dialogCount - 1];
		if (what != NoticeReleased) {
			return;
		}
		d.result = d.kind == MenuDialog::OneButton ? MenuDialog::Pressed : w == d.buttons[0] ? MenuDialog::Yes : MenuDialog::No;
		dialogNotice(d);
		return;
	}
	if (&t != &ui.tree) {
		return;
	}
	switch (screen) {
	case ScreenMainMenu: screens.mainMenuNotice(*this, w, what); return;
	case ScreenOptions: screens.optionsNotice(*this, t, w, what); return;
	case ScreenControl1:
	case ScreenControl2: screens.controlsNotice(*this, t, w, what); return;
	case ScreenPickLanguage:
		if (what == NoticeValue && isItem(w, 0)) {
			game->settings.language = uint8_t(t[t[w].carousel].index);
			game->dirty = true;
		}
		else if (what == NoticeReleased && isItem(w, 1)) {
			navigate(ScreenOptions);
		}
		return;
	case ScreenGarage: screens.garageNotice(*this, t, w, what); return;
	default:
		if (raceScreen(screen)) {
			race.raceScreenNotice(*this, t, w, what);
			return;
		}
		builders.builderNotice(*this, t, w, what);
		return;
	}
}

void Menus::dialogNotice(MenuDialog& d)
{
	Game& g = *game;
	switch (screen) {
	case ScreenMainMenu: screens.state.quitting = d.result == MenuDialog::Yes; break;
	case ScreenOptions:
		screens.state.nextPage = screens.state.afterNotice;
		if (screens.state.afterNotice == 3) {
			videoSettings = true;
			screens.state.nextPage = -1;
		}
		break;
	case ScreenGarage:
		if (d.result == MenuDialog::Yes) {
			// The racer goes from its list, the cursor on the one after it; page 0 again, then the garage anew, saved
			// (once the dialog has closed).
			const RacerRef gone = screens.state.racers[screens.state.cursor];
			std::vector<RacerRecord>& list = g.list(gone.list);
			list.erase(list.begin() + gone.index);
			g.markRacers(gone.list);
			screens.state.racers.erase(screens.state.racers.begin() + screens.state.cursor);
			for (RacerRef& r : screens.state.racers) {
				r.index = uint16_t(r.list == gone.list && r.index > gone.index ? r.index - 1 : r.index);
			}
			screens.state.cursor = screens.state.racers.empty() ? 0 : screens.state.cursor % int32_t(screens.state.racers.size());
			screens.fillRacerNames(*this);
			screens.garagePage(*this, 0);
			if (!screens.state.racers.empty()) {
				ui.select(ui.tree, items[GarageDelete], IconQuiet | IconSilent);
			}
			navigate(ScreenSaveAll);
		}
		break;
	case ScreenNewRacer: navigate(ScreenGarage); break;
	default: builders.builderDialog(*this, d); return;
	}
	ui.closeDialog();
}

bool Menus::screenKey(const MenuKey& k)
{
	Game& g = *game;
	if (ui.dialogCount) {
		// A dialog swallows every key until it is open.
		return ui.dialogs[ui.dialogCount - 1].phase != MenuDialog::Open;
	}
	const uint32_t source = k.key >> 28;
	if (cinematic(screen)) {
		if (cinema.state.skippable && cinema.state.cinemaFrame >= cinema.state.rate && ((k.down && !k.repeat && source <= 3) || !k.down)) {
			cinema.state.finished = true;
			return true;
		}
		return false;
	}
	if ((screen == ScreenControl1 || screen == ScreenControl2) && screens.state.waiting >= 0) {
		// The row waiting for a key takes the next one (a few can't be bound).
		static const uint32_t Refused[] = {0x01, 0xb7, 0x46, 0xc5, 0x45, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe3};
		if (!k.down || source == 2 || source == 4) {
			return true;
		}
		if (source == 1 && std::find(std::begin(Refused), std::end(Refused), k.key & 0xfffffff) != std::end(Refused)) {
			play(SoundCantDo);
			return true;
		}
		const size_t layout = screens.state.entries[ui.tree[ui.tree[items[ControlsDevice]].carousel].index];
		g.bind(layout, size_t(screens.state.waiting), k.key);
		const int16_t row = screens.state.rows[screens.state.waiting];
		screens.state.waiting = -1;
		ui.release(ui.tree, row, IconQuiet);
		screens.refreshControls(*this);
		return true;
	}
	if (raceScreen(screen) && race.state.held) {
		return true; // leaving: the keys wait
	}
	return builders.builderKey(*this, k);
}

} // namespace lr
