#pragma once

// The presentation's VIDEO SETTINGS screen: seven rows, each a list of values. Up/Down move between
// rows and Left/Right step the row's value, both wrapping; the mouse's hover selects a row, a left click steps it
// forward, a right click back; Esc (a pad's B) closes. A value that isn't in its row's list (a resolution the player
// set elsewhere) shows as itself and steps to the list's first (forward) or last (back). Drawn as 2D elements over the
// menus' studded backdrop in the menus' fonts. Plain C++: the presentation applies and saves the values (its own
// settings, never the core's save) and plays the sounds it lists.

#include "LRCore/Common/Api.h"
#include "LRCore/Menus/Menus.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

struct LRCORE_API VideoSettings {
	enum Row : uint8_t { Resolution, Window, VSync, FrameCap, Quality, RenderScale, Smoothing, Rows };
	static std::u16string_view name(size_t row);
	static std::span<const std::u16string_view> values(size_t row);

	// Each row's value: an index in its list, or -1 when the setting is none of them (shown as `other`).
	std::array<int8_t, Rows> value = {};
	std::array<std::u16string, Rows> other;
	uint8_t row = 0;
	bool pad = false; // the last input came from a pad (the prompt says so)
	bool closed = false; // Esc or B: the presentation hides the screen
	std::vector<int32_t> sounds; // the menus' sound numbers to play (the presentation clears them)

	// The geometry (640x480): the rows from y 84, 34 apart; names left at x 36, values right at x 604.
	static constexpr int32_t Top = 84;
	static constexpr int32_t Pitch = 34;
	static constexpr int32_t Left = 36;
	static constexpr int32_t Right = 604;

	enum Key : uint8_t { Up, Down, Back, Forward, Close };
	// A key pressed (Back/Forward: Left/Right). The row whose value changed, or -1.
	int32_t key(Key k, bool fromPad);
	// The mouse at (x, y) and the buttons that went down this frame (1 left, 2 right). The row whose value changed, or -1.
	int32_t mouse(int32_t x, int32_t y, uint32_t pressed);
	// A row's value one step on (or back), wrapping.
	void step(size_t row, bool forward);
	std::u16string_view shown(size_t r) const; // the value's text

	// The screen's draw list.
	void draw(const MenuData& data, size_t language, std::vector<MenuElement>& elements, std::vector<MenuGlyph>& glyphs) const;

private:
	int32_t mouseX = -1, mouseY = -1;
};

} // namespace lr
