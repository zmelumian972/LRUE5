#pragma once

// The game from the boot: the menus until their stack empties, then the session they set up, race by
// race (a circuit runs its races in turn, each loaded in the frame the last one ended), then the menus again with the
// screen the session chose. A race loads in one frame (its loading screen's steps are drawn in it); the race's dialogs,
// standings and fade are drawn into the menus' draw list.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/World.h"

#include <array>
#include <span>
#include <vector>

namespace lr {

class Cache;

struct LRCORE_API Program {
	enum class Mode : uint8_t { Menus, Race, Quit };
	const MenuData* data = nullptr;
	const Cache* cache = nullptr; // the data cache the races are set up from
	Game game;
	Menus menus;
	World world;
	Mode mode = Mode::Menus;
	bool loaded = false, ended = false; // a race was set up (the presentation loads its track), one ended, this frame

	struct Input {
		std::span<const MenuKey> keys;      // in order (menuKey's codes)
		int32_t mouseX = 0, mouseY = 0;     // 640x480
		uint32_t buttons = 0;
		std::array<uint16_t, 2> players = {}; // each player's race keys (Key bits)
	};

	// A fresh game for the joysticks present, the save files found (by slot; empty: none), then the legal screen.
	void boot(const MenuData& menuData, const Cache& files, uint8_t pads, std::span<const std::vector<uint8_t>> saves);
	// One frame of dt ms. What it draws and plays is the menus' output (menus.elements, sounds, tune; in a race also
	// world.tune), the save files to write are menus.saves.
	void frame(int32_t dt, const Input& input);

	// A race of the session: its brief, the world set up (only its session when the world replays a captured run),
	// the loading screen drawn.
	void startRace();

	// A player's race keys (Key bits) from its controls (turn left, turn right, accelerate, brake,
	// use, camera, map, slide, look back) and what is held down, as bindings: keyboard keys, the player's pad's buttons
	// and its stick as axis-buttons. A pad steers with its stick while its turn rows are unbound.
	static uint16_t raceKeys(const KeyLayout& layout, std::span<const uint32_t> down);
};

} // namespace lr
