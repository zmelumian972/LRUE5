#pragma once

// Drawing into a draw list by the menus' rules: images, text in a language's fonts, quads and triangles in the
// 640x480 screen, and text measured. A pen is made where it draws (Menus::pen); it keeps no state of its own.

#include "LRCore/Menus/DrawList.h"
#include "LRCore/Menus/MenuData.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lr {

struct Pen {
	const MenuData* data;
	size_t language;
	std::vector<MenuElement>& elements;
	std::vector<MenuGlyph>& glyphs;

	// A line's width and a text's box (its lines', or wrapped at wrap > 0), in the font's pixels.
	int32_t lineWidth(int16_t font, std::u16string_view line) const;
	MenuRect measure(int16_t font, std::u16string_view text, int32_t wrap) const;
	// An image's source rect stretched to dest, clipped (to the screen, and clip when given), tinted.
	void drawImage(int16_t image, MenuRect dest, MenuRect source, const MenuRect* clip, uint32_t colour) const;
	// Text: fitted to a box; at (x, y) scaled; wrapped at a width (centred lines); a line at its own size. A run's glyphs'
	// element (null when nothing of it was drawn).
	void drawText(int16_t font, std::u16string_view text, MenuRect box, MenuRect clip, uint32_t colour) const;
	void drawScaled(int16_t font, std::u16string_view text, int32_t x, int32_t y, float sx, float sy, uint32_t colour) const;
	void drawWrapped(int16_t font, std::u16string_view text, int32_t x, int32_t y, int32_t wrap, bool centred, MenuRect clip,
		uint32_t colour) const;
	void drawLine(int16_t font, std::u16string_view text, int32_t x, int32_t y, uint32_t colour) const;
	MenuElement* glyphElement(int16_t font, uint32_t first, std::u16string_view text, std::array<int32_t, 4> box, uint32_t colour) const;
	// A flat quad (opaque, or blended at alpha 0..255) and a triangle (corners x, y, u, v; colours; clipped).
	void drawQuad(MenuRect rect, uint32_t colour, int32_t alpha) const;
	void drawTriangle(int16_t image, const std::array<std::array<float, 4>, 3>& corners, const std::array<uint32_t, 3>& colours,
		MenuRect clip) const;
};

// The text's lines (split at '\n').
template <typename Each> void forEachLine(std::u16string_view text, Each each)
{
	size_t start = 0;
	for (;;) {
		const size_t end = text.find(u'\n', start);
		each(text.substr(start, end == std::u16string_view::npos ? std::u16string_view::npos : end - start));
		if (end == std::u16string_view::npos) {
			return;
		}
		start = end + 1;
	}
}

} // namespace lr
