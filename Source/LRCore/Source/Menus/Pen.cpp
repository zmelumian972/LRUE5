// The menus' 2D primitives: images, text in the language's fonts, quads and triangles added to the draw list, and
// text measured.

#include "Menus/Pen.h"

#include "Menus/Layout.h"

#include <algorithm>
#include <cmath>

namespace lr {

int32_t Pen::lineWidth(int16_t font, std::u16string_view line) const
{
	if (font < 0) {
		return 0;
	}
	const MenuData::Font& f = data->languages[language].fonts[font];
	int32_t w = 0;
	for (char16_t c : line) {
		w += int32_t(f.glyphs.find(uint16_t(c)).width) + f.spacing;
	}
	return std::max(0, w - f.spacing);
}

namespace {

// Wrapping at a width (in the font's pixels): each line's [start, end) in the text.
std::vector<std::pair<size_t, size_t>> wrapLines(const MenuData::Font& f, std::u16string_view text, int32_t wrap)
{
	std::vector<std::pair<size_t, size_t>> lines;
	const auto adv = [&](char16_t c) { return int32_t(f.glyphs.find(uint16_t(c)).width) + f.spacing; };
	size_t i = 0;
	bool hardBreak = false;
	while (i < text.size()) {
		if (!hardBreak) {
			while (i < text.size() && text[i] == u' ') {
				i++;
			}
		}
		hardBreak = false;
		const size_t start = i;
		int32_t run = 0;
		size_t wordStart = std::u16string_view::npos;
		bool inWord = false, over = false;
		while (i < text.size() && text[i] != u'\r' && text[i] != u'\n') {
			run += adv(text[i]);
			if (run - f.spacing > wrap) {
				over = true;
				break;
			}
			if (text[i] == u' ') {
				if (inWord) {
					wordStart = i;
				}
				inWord = false;
			}
			else {
				inWord = true;
			}
			i++;
		}
		size_t end = i;
		if (over) {
			if (wordStart == std::u16string_view::npos) {
				end = std::max(i, start + 1); // one long word: cut where it went over
			}
			else {
				end = wordStart;
			}
			i = end;
		}
		lines.push_back({start, end});
		if (i < text.size() && (text[i] == u'\r' || text[i] == u'\n')) {
			i += text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n' ? 2 : 1;
			hardBreak = true;
		}
	}
	return lines;
}

} // namespace

MenuRect Pen::measure(int16_t font, std::u16string_view text, int32_t wrap) const
{
	if (font < 0) {
		return {};
	}
	const MenuData::Font& f = data->languages[language].fonts[font];
	const int32_t h = int32_t(f.glyphs.height);
	if (wrap > 0) {
		int32_t widest = 0, count = 0;
		for (const auto& [a, b] : wrapLines(f, text, wrap)) {
			widest = std::max(widest, lineWidth(font, text.substr(a, b - a)));
			count++;
		}
		if (f.spacing < 0) {
			widest -= f.spacing;
		}
		return {0, 0, widest, count * h};
	}
	int32_t w = 0, lines = 0;
	forEachLine(text, [&](std::u16string_view line) {
		w = std::max(w, lineWidth(font, line));
		lines++;
	});
	return {0, 0, w, lines * h};
}

void Pen::drawImage(int16_t image, MenuRect dest, MenuRect source, const MenuRect* clipTo, uint32_t colour) const
{
	MenuRect clip = ScreenRect;
	if (clipTo) {
		clip = {std::max(clip.left, clipTo->left), std::max(clip.top, clipTo->top), std::min(clip.right, clipTo->right),
			std::min(clip.bottom, clipTo->bottom)};
	}
	if (image < 0 || clip.width() <= 0 || clip.height() <= 0 || dest.width() <= 0 || dest.height() <= 0 || source.width() <= 0 ||
		source.height() <= 0 || dest.left > clip.right || dest.top > clip.bottom || dest.right <= clip.left || dest.bottom <= clip.top) {
		return;
	}
	const float kx = float(dest.width()) / float(source.width()), ky = float(dest.height()) / float(source.height());
	std::array<float, 4> d = {float(dest.left), float(dest.top), float(dest.right), float(dest.bottom)};
	std::array<float, 4> s = {float(source.left), float(source.top), float(source.right), float(source.bottom)};
	if (dest.left < clip.left) {
		s[0] += float(clip.left - dest.left) / kx;
		d[0] = float(clip.left);
	}
	if (dest.top < clip.top) {
		s[1] += float(clip.top - dest.top) / ky;
		d[1] = float(clip.top);
	}
	if (dest.right > clip.right) {
		s[2] -= float(dest.right - clip.right) / kx;
	}
	if (dest.bottom > clip.bottom) {
		s[3] -= float(dest.bottom - clip.bottom) / ky;
	}
	d[2] = d[0] + (s[2] - s[0]) * kx;
	d[3] = d[1] + (s[3] - s[1]) * ky;
	MenuElement e;
	e.kind = MenuElement::Image;
	e.image = image;
	e.colour = colour;
	e.dest = d;
	e.source = s;
	elements.push_back(std::move(e));
}

namespace {

// One run of glyphs at (x, y), scaled, clipped: its quads appended. False if nothing could be drawn.
bool glyphRun(const MenuData::Font& f, std::u16string_view text, int32_t x, int32_t y, float sx, float sy, MenuRect clip,
	std::vector<MenuGlyph>& out)
{
	clip = {std::max(0, clip.left), std::max(0, clip.top), std::min(ScreenWidth, clip.right), std::min(ScreenHeight, clip.bottom)};
	const int32_t H = int32_t(f.glyphs.height);
	if (sx <= 0 || sy <= 0 || clip.width() <= 0 || clip.height() <= 0 || x > clip.right || y > clip.bottom ||
		y + int32_t(float(H) * sy) < clip.top) {
		return false;
	}
	const int32_t bottom = y + int32_t(float(H) * sy);
	float px = float(x);
	int32_t ix = x;
	for (char16_t c : text) {
		const FontGlyphs::Glyph& g = f.glyphs.find(uint16_t(c));
		const int32_t dl = ix;
		if (ix >= clip.right) {
			break;
		}
		const float w = float(g.width) * sx;
		const int32_t dr = ix + int32_t(w);
		px += w;
		px += float(f.spacing) * sx;
		ix = int32_t(std::floor(px));
		if (dr < clip.left) {
			continue;
		}
		MenuRect d = {dl, y, dr, bottom};
		MenuRect s = {g.x, 0, g.x + g.width, H};
		cutTo(clip, d, s, sx, sy);
		// A quarter texel right; QUIRK: the bottom row pulled up one.
		out.push_back(
			{{d.left, d.top, d.right, d.bottom}, {float(s.left) + 0.25f, float(s.top), float(s.right) + 0.25f, float(s.bottom) - 1.0f}});
	}
	return true;
}

} // namespace

// The glyphs appended from first on as one text element of the font and colour, boxed (x y w h); null when none were.
MenuElement* Pen::glyphElement(int16_t font, uint32_t first, std::u16string_view text, std::array<int32_t, 4> box, uint32_t colour) const
{
	if (glyphs.size() == first) {
		return nullptr;
	}
	MenuElement& e = elements.emplace_back();
	e.kind = MenuElement::Glyphs;
	e.image = font;
	e.colour = colour;
	e.firstGlyph = first;
	e.glyphCount = uint32_t(glyphs.size()) - first;
	e.text = text;
	e.box = box;
	return &e;
}

void Pen::drawText(int16_t font, std::u16string_view text, MenuRect box, MenuRect clip, uint32_t colour) const
{
	if (font < 0) {
		return;
	}
	const MenuData::Font& f = data->languages[language].fonts[font];
	const int32_t textWidth = lineWidth(font, text);
	const float sx = textWidth ? float(box.width()) / float(textWidth) : 0.0f, sy = float(box.height()) / float(f.glyphs.height);
	const uint32_t first = uint32_t(glyphs.size());
	if (glyphRun(f, text, box.left, box.top, sx, sy, clip, glyphs)) {
		glyphElement(font, first, text, {box.left, box.top, box.width(), box.height()}, colour);
	}
}

void Pen::drawScaled(int16_t font, std::u16string_view text, int32_t x, int32_t y, float sx, float sy, uint32_t colour) const
{
	if (font < 0) {
		return;
	}
	const MenuData::Font& f = data->languages[language].fonts[font];
	const uint32_t first = uint32_t(glyphs.size());
	if (!glyphRun(f, text, x, y, sx, sy, ScreenRect, glyphs)) {
		return;
	}
	if (MenuElement* e = glyphElement(font, first, text,
			{x, y, int32_t(float(lineWidth(font, text)) * sx), int32_t(float(f.glyphs.height) * sy)}, colour)) {
		e->scaled = true;
		e->scale = {sx, sy};
	}
}

void Pen::drawWrapped(int16_t font, std::u16string_view text, int32_t x, int32_t y, int32_t wrap, bool centred, MenuRect clip,
	uint32_t colour) const
{
	if (font < 0) {
		return;
	}
	const MenuData::Font& f = data->languages[language].fonts[font];
	const int32_t lh = int32_t(f.glyphs.height);
	const uint32_t first = uint32_t(glyphs.size());
	int32_t line = 0;
	bool any = false;
	for (const auto& [a, b] : wrapLines(f, text, wrap)) {
		const std::u16string_view s = text.substr(a, b - a);
		const int32_t w = lineWidth(font, s);
		const int32_t lx = centred && w < wrap ? x + ((wrap - w) >> 1) : x;
		any |= glyphRun(f, s, lx, y + line * lh, 1.0f, 1.0f, clip, glyphs);
		line++;
	}
	if (MenuElement* e = any ? glyphElement(font, first, text, {x, y, wrap, line * lh}, colour) : nullptr) {
		e->wrapped = true;
	}
}

void Pen::drawQuad(MenuRect r, uint32_t colour, int32_t alpha) const
{
	// Two triangles between the corners as given (a rect turned inside out still covers its span). QUIRK: one with no
	// height is still drawn.
	if (r.width() == 0) {
		return;
	}
	MenuElement e;
	e.kind = MenuElement::Quad;
	e.colour = colour;
	e.corners = {colour, colour, colour};
	e.dest = {float(r.left), float(r.top), float(r.right), float(r.bottom)};
	e.alpha = alpha;
	elements.push_back(std::move(e));
}

// A line of the race's font at (x, y), its own size.
void Pen::drawLine(int16_t font, std::u16string_view text, int32_t x, int32_t y, uint32_t colour) const
{
	drawScaled(font, text, x, y, 1, 1, colour);
}

void Pen::drawTriangle(int16_t image, const std::array<std::array<float, 4>, 3>& corners, const std::array<uint32_t, 3>& colours,
	MenuRect clip) const
{
	MenuElement e;
	e.kind = MenuElement::Triangle;
	e.image = image;
	e.vertices = corners;
	e.colour = colours[0];
	e.corners = {colours[1], colours[2], 0};
	e.clip = {clip.left, clip.top, clip.right, clip.bottom};
	elements.push_back(std::move(e));
}

} // namespace lr
