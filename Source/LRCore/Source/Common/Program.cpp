// The game from the boot: menus, then the session they set up race by race, then the menus again.

#include "LRCore/Common/Program.h"

#include <algorithm>

namespace lr {

void Program::boot(const MenuData& menuData, const Cache& files, uint8_t pads, std::span<const std::vector<uint8_t>> saves)
{
	data = &menuData;
	cache = &files;
	game.reset(pads);
	for (size_t slot = 0; slot < saves.size() && slot < 2; slot++) {
		if (!saves[slot].empty()) {
			(void)game.loadSlot(slot, saves[slot]); // (a file that isn't a save leaves its slot empty, as if none)
		}
	}
	mode = Mode::Menus;
	menus.start(menuData, game, ScreenLegal);
}

void Program::startRace()
{
	RaceBrief brief = game.brief(*data, *cache);
	// The standings' and the dialogs' blinks run on from race to race.
	const int32_t standingsBlink = world.session.blinkMs, dialogBlink = world.session.dialog.blinkMs;
	if (!(world.replaying() ? world.setupSession(*cache, brief) : world.setup(*cache, brief))) {
		// The race's files are missing or malformed: the session ends and the menus start again at the main menu.
		game.racing = false;
		mode = Mode::Menus;
		menus.start(*data, game, ScreenMainMenu);
		return;
	}
	world.session.blinkMs = standingsBlink;
	world.session.dialog.blinkMs = dialogBlink;
	menus.drawLoading(brief.track);
	mode = Mode::Race;
	loaded = true;
}

void Program::frame(int32_t dt, const Input& input)
{
	loaded = ended = false;
	if (mode == Mode::Menus) {
		menus.frame(game, dt, input.keys, input.mouseX, input.mouseY, input.buttons);
		if (menus.quit) {
			mode = Mode::Quit;
			return;
		}
		if (menus.raceStart) {
			menus.raceStart = false;
			game.startSession(*data);
			menus.elements.clear();
			menus.glyphs.clear();
			menus.scenes.clear();
			if (game.nextRace() >= 0) {
				startRace();
			}
		}
		return;
	}
	if (mode != Mode::Race) {
		return;
	}
	menus.sounds.clear();
	menus.saves.clear();
	menus.tuneStarted = false;
	std::vector<uint32_t> pressed, released;
	for (const MenuKey& k : input.keys) {
		(k.down ? pressed : released).push_back(k.key);
	}
	for (size_t p = 0; p < world.racers.keys.size() && p < input.players.size(); p++) {
		world.racers.keys[p] = input.players[p];
	}
	// A one-player race in a window wider than 4:3 draws Hor+: the frustum is the window's (its vertical field the
	// original's, its width the window's).
	if (world.raceView.cameras.size() == 1) {
		world.raceView.cameras[0].aspect = input.windowAspect > 4.0f / 3 + 0.01f ? input.windowAspect : 4.0f / 3;
	}
	world.update(dt, pressed, released, world.replaying() ? nullptr : cache);
	world.updateHuds(dt);
	menus.drawRace(world);
	if (world.session.ending == Session::End::None) {
		return;
	}
	// The race is over: the session's next race loads now, or the menus start again.
	ended = true;
	game.takeResult(world);
	if (game.nextRace() >= 0) {
		startRace();
		return;
	}
	bool save = false;
	const uint16_t next = game.menusStart(save);
	menus.start(*data, game, next, save);
	mode = Mode::Menus;
}

uint16_t Program::raceKeys(const KeyLayout& layout, std::span<const uint32_t> down)
{
	// The rows: turn left, turn right, accelerate, brake, use, camera, map, slide, look back.
	static constexpr uint16_t Bits[9] = {KeyLeft, KeyRight, KeyThrottle, KeyBrake, KeyUse, KeyCamera, KeyMap, KeyDrift, KeyLookBack};
	const auto held = [&](uint32_t binding) { return binding && std::find(down.begin(), down.end(), binding) != down.end(); };
	uint16_t keys = 0;
	for (size_t slot = 0; slot < 9; slot++) {
		const uint32_t binding = layout.bindings[slot];
		const bool stick = layout.kind == KeyLayout::Joystick && slot < 2 && !binding;
		keys |= held(stick ? SourceAxis | uint32_t(1 - slot) : binding) ? Bits[slot] : 0;
	}
	return keys;
}

} // namespace lr
