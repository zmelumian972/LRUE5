// What the menus draw: the widget trees walked depth first, each part clipped to its parent, into a list of images,
// glyph runs, flat quads and 3D views in the 640x480 screen.

#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/World.h"

#include "Menus/Layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lr {

namespace {

constexpr MenuRect Screen640 = {0, 0, 640, 480};

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

} // namespace

int32_t Menus::lineWidth(int16_t font, std::u16string_view line) const
{
	if (font < 0) {
		return 0;
	}
	const MenuData::Font& f = data->languages[language].fonts[size_t(font)];
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

MenuRect Menus::measure(int16_t font, std::u16string_view text, int32_t wrap) const
{
	if (font < 0) {
		return {};
	}
	const MenuData::Font& f = data->languages[language].fonts[size_t(font)];
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

void Menus::drawImage(int16_t image, MenuRect dest, MenuRect source, const MenuRect* clipTo, uint32_t colour)
{
	MenuRect clip = Screen640;
	if (clipTo) {
		clip = {std::max(clip.left, clipTo->left), std::max(clip.top, clipTo->top), std::min(clip.right, clipTo->right), std::min(clip.bottom, clipTo->bottom)};
	}
	if (image < 0 || clip.width() <= 0 || clip.height() <= 0 || dest.width() <= 0 || dest.height() <= 0 || source.width() <= 0 || source.height() <= 0 || dest.left > clip.right ||
		dest.top > clip.bottom || dest.right <= clip.left || dest.bottom <= clip.top) {
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
bool glyphRun(const MenuData::Font& f, std::u16string_view text, int32_t x, int32_t y, float sx, float sy, MenuRect clip, std::vector<MenuGlyph>& out)
{
	clip = {std::max(0, clip.left), std::max(0, clip.top), std::min(640, clip.right), std::min(480, clip.bottom)};
	const int32_t H = int32_t(f.glyphs.height);
	if (sx <= 0 || sy <= 0 || clip.width() <= 0 || clip.height() <= 0 || x > clip.right || y > clip.bottom || y + int32_t(float(H) * sy) < clip.top) {
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
		if (d.bottom > clip.bottom) {
			s.bottom -= int32_t(float(d.bottom - clip.bottom) / sy);
			d.bottom = clip.bottom;
		}
		if (d.top < clip.top) {
			s.top += int32_t(float(clip.top - d.top) / sy);
			d.top = clip.top;
		}
		if (d.right > clip.right) {
			s.right -= int32_t(float(d.right - clip.right) / sx);
			d.right = clip.right;
		}
		if (d.left < clip.left) {
			s.left += int32_t(float(clip.left - d.left) / sx);
			d.left = clip.left;
		}
		// A quarter texel right; QUIRK: the bottom row pulled up one.
		out.push_back({{d.left, d.top, d.right, d.bottom}, {float(s.left) + 0.25f, float(s.top), float(s.right) + 0.25f, float(s.bottom) - 1.0f}});
	}
	return true;
}

} // namespace

void Menus::drawText(int16_t font, std::u16string_view text, MenuRect box, MenuRect clip, uint32_t colour)
{
	if (font < 0) {
		return;
	}
	const MenuData::Font& f = data->languages[language].fonts[size_t(font)];
	const int32_t textWidth = lineWidth(font, text);
	const float sx = textWidth ? float(box.width()) / float(textWidth) : 0.0f, sy = float(box.height()) / float(f.glyphs.height);
	MenuElement e;
	e.kind = MenuElement::Glyphs;
	e.image = font;
	e.colour = colour;
	e.firstGlyph = uint32_t(glyphs.size());
	if (!glyphRun(f, text, box.left, box.top, sx, sy, clip, glyphs) || glyphs.size() == e.firstGlyph) {
		return;
	}
	e.glyphCount = uint32_t(glyphs.size()) - e.firstGlyph;
	e.text = text;
	e.box = {box.left, box.top, box.width(), box.height()};
	elements.push_back(std::move(e));
}

void Menus::drawScaled(int16_t font, std::u16string_view text, int32_t x, int32_t y, float sx, float sy, uint32_t colour)
{
	if (font < 0) {
		return;
	}
	const MenuData::Font& f = data->languages[language].fonts[size_t(font)];
	MenuElement e;
	e.kind = MenuElement::Glyphs;
	e.image = font;
	e.colour = colour;
	e.firstGlyph = uint32_t(glyphs.size());
	if (!glyphRun(f, text, x, y, sx, sy, Screen640, glyphs) || glyphs.size() == e.firstGlyph) {
		return;
	}
	e.glyphCount = uint32_t(glyphs.size()) - e.firstGlyph;
	e.text = text;
	e.box = {x, y, int32_t(float(lineWidth(font, text)) * sx), int32_t(float(f.glyphs.height) * sy)};
	e.scaled = true;
	e.scale = {sx, sy};
	elements.push_back(std::move(e));
}

void Menus::drawWrapped(int16_t font, std::u16string_view text, int32_t x, int32_t y, int32_t wrap, bool centred, MenuRect clip, uint32_t colour)
{
	if (font < 0) {
		return;
	}
	const MenuData::Font& f = data->languages[language].fonts[size_t(font)];
	const int32_t lh = int32_t(f.glyphs.height);
	MenuElement e;
	e.kind = MenuElement::Glyphs;
	e.image = font;
	e.colour = colour;
	e.firstGlyph = uint32_t(glyphs.size());
	int32_t line = 0;
	bool any = false;
	for (const auto& [a, b] : wrapLines(f, text, wrap)) {
		const std::u16string_view s = text.substr(a, b - a);
		const int32_t w = lineWidth(font, s);
		const int32_t lx = centred && w < wrap ? x + ((wrap - w) >> 1) : x;
		any |= glyphRun(f, s, lx, y + line * lh, 1.0f, 1.0f, clip, glyphs);
		line++;
	}
	if (!any || glyphs.size() == e.firstGlyph) {
		return;
	}
	e.glyphCount = uint32_t(glyphs.size()) - e.firstGlyph;
	e.text = text;
	e.box = {x, y, wrap, line * lh};
	e.wrapped = true;
	elements.push_back(std::move(e));
}

void Menus::drawQuad(MenuRect r, uint32_t colour, int32_t alpha)
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

namespace {

// The view (in the widget's parent) clipped to the widget's rect r, and the screen rect with it; false when they miss.
bool clipView(const MenuRect& r, MenuRect& view, MenuRect& scr)
{
	if (view.left >= r.right || view.right <= r.left || view.top >= r.bottom || view.bottom <= r.top) {
		return false;
	}
	if (view.left < r.left) {
		scr.left += r.left - view.left;
		view.left = r.left;
	}
	if (view.right > r.right) {
		scr.right -= view.right - r.right;
		view.right = r.right;
	}
	if (view.top < r.top) {
		scr.top += r.top - view.top;
		view.top = r.top;
	}
	if (view.bottom > r.bottom) {
		scr.bottom -= view.bottom - r.bottom;
		view.bottom = r.bottom;
	}
	return true;
}

} // namespace

void Menus::draw(WidgetTree& t, int16_t w, MenuRect view, MenuRect scr)
{
	Widget& n = t[w];
	const MenuRect r = n.rect;
	if (!clipView(r, view, scr)) {
		return;
	}
	const MenuRect v = {view.left - r.left + n.offsetX, view.top - r.top + n.offsetY, view.right - r.left + n.offsetX, view.bottom - r.top + n.offsetY};
	if (!(n.flags & Widget::Visible)) {
		return;
	}
	const auto children = [&](MenuRect cv) {
		for (int16_t c = t[w].first; c >= 0; c = t[c].next) {
			draw(t, c, cv, scr);
		}
	};
	const uint8_t look = n.visualState();
	if (!(n.flags & Widget::DrawsItself)) {
		children(v);
		return;
	}
	switch (n.kind) {
	case Widget::Image:
		drawStretched(n, v, scr);
		children(v);
		return;
	case Widget::ImageButton:
	case Widget::Hotspot:
		// Its image takes the state's image and colour, at the button's size; a hotspot's lit region its highlight.
		if (n.child >= 0) {
			Widget& c = t[n.child];
			c.image = n.images[look];
			c.colour = n.stateColours[look];
			c.rect = {0, 0, r.width(), r.height()};
		}
		children(v);
		if (n.kind == Widget::Hotspot) {
			drawLitRegion(n, v, scr);
		}
		return;
	case Widget::TextField:
		drawTextField(t, n, v, scr);
		children(v);
		return;
	case Widget::TextButton:
		t[n.child].image = n.images[look];
		t[n.child].colour = n.imageColours[look];
		t[n.label].font = n.fonts[look];
		t[n.label].colour = n.stateColours[look];
		children(v);
		return;
	case Widget::Label:
		drawLabel(n, v, scr);
		children(v);
		return;
	case Widget::Quad:
		if (!n.blinkedOff) {
			drawQuad(scr, n.colour, -1);
		}
		children(v);
		return;
	case Widget::Carousel:
		children(v);
		if (n.wrapAround) {
			drawWrapAround(t, n, v, scr);
		}
		return;
	case Widget::SceneReference:
	case Widget::SceneView:
	case Widget::Ring:
		drawView(t, w);
		children(v);
		return;
	default: children(v); return;
	}
}

// An image widget: stretched to the rect, the source the view's part of it.
void Menus::drawStretched(Widget& n, MenuRect v, MenuRect scr)
{
	const MenuRect r = n.rect;
	const MenuData::ImageInfo* info = n.image >= 0 ? &data->images[size_t(n.image)] : nullptr;
	const int32_t rw = r.width(), rh = r.height();
	n.scaleX = info && rw > 0 ? float(info->width) / float(rw) : 0;
	n.scaleY = info && rh > 0 ? float(info->height) / float(rh) : 0;
	const MenuRect source = {int32_t(float(v.left) * n.scaleX), int32_t(float(v.top) * n.scaleY), int32_t(float(v.right) * n.scaleX), int32_t(float(v.bottom) * n.scaleY)};
	drawImage(n.image, scr, source, nullptr, n.colour);
}

// A hotspot's lit region: its image's part under the region's rect.
void Menus::drawLitRegion(const Widget& n, MenuRect v, MenuRect scr)
{
	const MenuRect r = n.rect;
	for (size_t i = 0; n.lit && i < n.regions.size(); i++) {
		if (n.regions[i] != n.lit) {
			continue;
		}
		const MenuRect g = n.regionRects[i];
		const MenuRect part = {std::max(g.left, 0), std::max(g.top, 0), std::min(g.right, r.width()), std::min(g.bottom, r.height())};
		const MenuRect dest = {scr.left - v.left + part.left, scr.top - v.top + part.top, scr.left - v.left + part.right, scr.top - v.top + part.bottom};
		drawImage(n.image, dest, part, &scr, 0xffffffff);
	}
}

// A text field: the text from the field's left, centred in its height less 4 px and fitted; the caret placed after it.
void Menus::drawTextField(WidgetTree& t, const Widget& n, MenuRect v, MenuRect scr)
{
	const MenuRect r = n.rect;
	const int32_t fh = n.font >= 0 ? int32_t(data->languages[language].fonts[size_t(n.font)].glyphs.height) : 0;
	const int32_t tw = lineWidth(n.font, n.text);
	const MenuRect inner = {v.left, v.top, v.right, v.bottom - 4};
	MenuRect text = {0, 0, tw, fh};
	centre(inner, text);
	const int32_t x = scr.left, y = scr.top - v.top + text.top;
	drawText(n.font, n.text, {x, y, x + text.width(), y + text.height()}, {scr.left, scr.top, scr.right, scr.bottom - 4}, n.colour);
	if (n.caret >= 0) {
		// Beside the field while it is selected; shown while there is room for more.
		const bool selected = n.state & Widget::Selected;
		if (selected != (t[n.caret].parent >= 0)) {
			selected ? t.adopt(n.caret, n.parent) : t.detach(n.caret); // (back as the last child)
		}
		Widget& caret = t[n.caret];
		const int32_t cw = caret.rect.width();
		caret.rect = {r.left + tw, r.bottom - 6, r.left + tw + cw, r.bottom - 2};
		show(t, n.caret, int32_t(n.text.size()) < n.maxLength);
	}
}

// A label: wrapped, or each line centred in its share of the rect's height.
void Menus::drawLabel(const Widget& n, MenuRect v, MenuRect scr)
{
	if (n.wrapping) {
		drawWrapped(n.font, n.text, scr.left - v.left, scr.top - v.top, n.wrap, n.centred, scr, n.colour);
		return;
	}
	const MenuRect r = n.rect;
	int32_t lines = 0;
	forEachLine(n.text, [&](std::u16string_view) { lines++; });
	const float lineH = float(r.height()) / float(lines);
	const int32_t fh = n.font >= 0 ? int32_t(data->languages[language].fonts[size_t(n.font)].glyphs.height) : 0;
	int32_t i = 0;
	forEachLine(n.text, [&](std::u16string_view line) {
		const MenuRect box = {0, int32_t(float(i) * lineH), r.width(), int32_t(float(i + 1) * lineH)};
		MenuRect text = {0, 0, lineWidth(n.font, line), fh};
		centre(box, text);
		const int32_t dx = scr.left - v.left, dy = scr.top - v.top;
		text = {text.left + dx, text.top + dy, text.right + dx, text.bottom + dy};
		drawText(n.font, line, text, scr, n.colour);
		i++;
	});
}

// A wrapping carousel's items again past its last one (a whole page on, paging), while the view reaches there.
void Menus::drawWrapAround(WidgetTree& t, const Widget& n, MenuRect v, MenuRect scr)
{
	int32_t extent = 0;
	for (int16_t c = n.first; c >= 0; c = t[c].next) {
		extent = t[c].rect.right;
	}
	const int32_t cw = n.rect.width();
	if (n.pageMode && cw > 0) {
		extent = (extent + cw - 1) / cw * cw;
	}
	if (cw + n.offsetX > extent) {
		for (int16_t c = n.first; c >= 0; c = t[c].next) {
			draw(t, c, {v.left - extent, v.top, v.right - extent, v.bottom}, scr);
		}
	}
}

// A scene reference (a cinematic over the screen, or the setup screens' preview), a scene view or a ring.
void Menus::drawView(WidgetTree& t, int16_t w)
{
	const Widget& n = t[w];
	const MenuRect g = global(t, w);
	if (n.kind != Widget::SceneReference) {
		addView(n, {float(std::max(0, g.left)), float(std::max(0, g.top)), float(std::min(640, g.right)), float(std::min(480, g.bottom))}, n.kind == Widget::Ring ? &n : nullptr);
	}
	else if (cinematicScreen()) {
		drawCinema();
	}
	else {
		addView(n, {float(g.left), float(g.top), float(g.right), float(g.bottom)}, nullptr);
	}
}

// A 3D view's element at dest (the screen's pixels), its scene described from the widget and lit (a ring by its own).
void Menus::addView(const Widget& n, std::array<float, 4> dest, const Widget* ring)
{
	MenuElement e;
	e.kind = MenuElement::View3D;
	e.dest = dest;
	e.widget = n.id;
	e.scene = int32_t(scenes.size());
	describeScene(n, scenes.emplace_back());
	scenes.back().aspect = (e.dest[2] - e.dest[0]) / std::max(1.0f, e.dest[3] - e.dest[1]);
	lightScene(scenes.back(), ring);
	elements.push_back(std::move(e));
}

void Menus::drawFrame()
{
	elements.clear();
	glyphs.clear();
	scenes.clear();
	const auto cursor = [&] {
		if (cursorOn) {
			const int16_t arrow = data->image("arrow", screen);
			const int32_t w = arrow >= 0 ? int32_t(data->images[size_t(arrow)].width) : 32;
			const int32_t h = arrow >= 0 ? int32_t(data->images[size_t(arrow)].height) : 32;
			// QUIRK: its bottom is y + its width.
			drawImage(arrow, {cursorX, cursorY, cursorX + w, cursorY + w}, {0, 0, w, h}, nullptr, 0xffffffff);
			// QUIRK: past the screen's right or bottom edge the cursor is squeezed into it, its whole image kept.
			if (!elements.empty() && elements.back().image == arrow && arrow >= 0) {
				MenuElement& e = elements.back();
				e.source[2] = float(w), e.source[3] = float(h);
				e.dest[2] = float(std::min(cursorX + w, 640)), e.dest[3] = float(std::min(cursorY + w, 480));
			}
		}
	};
	if (!tree.widgets.empty()) {
		draw(tree, 0, Screen640, Screen640);
	}
	cursor();
	for (size_t i = 0; i < dialogCount; i++) {
		draw(dialogs[i].tree, 0, Screen640, Screen640);
		cursor();
	}
	for (const Fade& f : fades) {
		if (!f.active || f.durationMs <= 0) {
			continue;
		}
		const float p = float(f.leftMs) / float(f.durationMs);
		const int32_t alpha = int32_t(f.in ? (1.0f - p) * 255.0f : p * 255.0f);
		drawQuad(f.rect, (f.colour & 0xffffff) | uint32_t(alpha) << 24, alpha);
	}
}

} // namespace lr

namespace lr {

namespace {

// A race time as m:ss:cc (mm:ss:cc from ten minutes, wrapping at an hour).
std::u16string raceTime(int32_t ms)
{
	const int32_t t = ms % 3600000;
	char text[16];
	std::snprintf(text, sizeof(text), "%d:%02d:%02d", t / 60000, t / 1000 % 60, t / 10 % 100);
	return std::u16string(text, text + std::char_traits<char>::length(text));
}

std::u16string number(int32_t n)
{
	const std::string s = std::to_string(n);
	return std::u16string(s.begin(), s.end());
}

} // namespace

// A line of the race's font at (x, y), its own size.
void Menus::drawLine(int16_t font, std::u16string_view text, int32_t x, int32_t y, uint32_t colour) { drawScaled(font, text, x, y, 1, 1, colour); }

void Menus::drawLoading(std::string_view track)
{
	const MenuData::Loading* l = data->loading(track);
	if (!l) {
		return;
	}
	// The screen as the race redraws it while it loads: the name centred, then as many dots as it has got through.
	static constexpr float Progress[27] = {0, 0.05f, 0.1f, 0.24f, 0.26f, 0.34f, 0.36f, 0.38f, 0.4f, 0.42f, 0.45f, 0.48f, 0.51f, 0.54f, 0.56f, 0.6f, 0.7f, 0.75f, 0.77f, 0.8f, 0.84f,
		0.86f, 0.88f, 0.9f, 0.93f, 0.97f, 1.0f};
	const MenuData::Language& lang = data->languages[language];
	const int16_t font = int16_t(lang.raceFonts);
	const std::u16string_view name = lang.game.at(size_t(l->screen.name));
	const MenuData::ImageInfo& tick = data->images[size_t(l->tick)];
	for (float p : Progress) {
		drawLine(font, name, (640 - lineWidth(font, name)) >> 1, 25, 0xffffffff);
		const int32_t dots = int32_t(float(l->screen.dots.size()) * p);
		for (int32_t i = 0; i < dots; i++) {
			const std::array<float, 2>& at = l->screen.dots[size_t(i)];
			const int32_t cx = int32_t(at[0] * 640), cy = int32_t(at[1] * 480);
			drawImage(l->tick, {cx - 8, cy - 8, cx + 8, cy + 8}, {0, 0, int32_t(tick.width), int32_t(tick.height)}, nullptr, 0xffffffff);
		}
	}
}

void Menus::drawTriangle(int16_t image, const std::array<std::array<float, 4>, 3>& corners, const std::array<uint32_t, 3>& colours, MenuRect clip)
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

namespace {

constexpr uint32_t HudWhite = 0xffffffff;

// The HUD's images by number (-1 past the list).
int16_t hudImage(const MenuData& data, size_t n) { return n < data.hudImages.size() ? data.hudImages[n] : int16_t(-1); }

// The gadget and banner scale: smaller in split screen.
float hudScale(const World& w) { return w.raceView.cameras.size() > 1 ? 0.8f : 1.0f; }

} // namespace

// The banner: the countdown's digits and GO growing, or FINISH, centred at x on the row y.
void Menus::drawHudBanner(const World& w, size_t p, int32_t cx, int32_t y)
{
	const Hud& h = w.raceView.huds[p];
	const MenuData::Language& lang = data->languages[language];
	const int16_t ignum = data->raceFont(language, "ignum");
	std::u16string text;
	float scale = hudScale(w);
	uint32_t colour = HudWhite;
	if (h.banner > 0) {
		const int32_t s = h.banner / 1000;
		colour = 0xffffff00;
		if (s < 3) {
			text = std::u16string(1, char16_t(u'3' - s));
			scale *= float(1000 * (s + 1) - h.banner) * 1.8f * 0.001f;
		}
		else if (s < 5) {
			text = lang.game.at(41);
			scale *= s == 3 ? 1.8f : float(5000 - h.banner) * 0.0018f;
		}
	}
	else {
		text = lang.game.at(40);
	}
	if (!text.empty()) {
		drawScaled(ignum, text, cx - (int32_t(float(lineWidth(ignum, text)) * scale) >> 1), y - (int32_t(64.0f * scale) >> 2), scale, scale, colour);
	}
}

// The power-up frame: a colour or a white brick held.
void Menus::drawPowerFrame(const World& w, size_t racer, MenuRect view)
{
	const Power& pw = w.racers.powers[racer];
	if (pw.held == NoColour && pw.whiteCount <= 0) {
		return;
	}
	const int32_t level = std::min<int32_t>(pw.whiteCount, 3);
	const int32_t fx = view.left + (view.width() >> 5), fy = view.bottom - 74;
	static constexpr int32_t Icons[5] = {-1, 2, 16, 20, 6};
	static constexpr uint32_t Tints[5] = {0xffffffff, 0xffff0000, 0xff5050ff, 0xff00ff00, 0xffffff00};
	const size_t colour = std::min<size_t>(pw.held, 4);
	drawImage(hudImage(*data, 11), {fx, fy, fx + 64, fy + 64}, {0, 0, 64, 64}, nullptr, HudWhite);
	drawImage(hudImage(*data, 12), {fx, fy, fx + 64, fy + 64}, {0, 0, 64, 64}, nullptr, Tints[colour]);
	if (Icons[colour] >= 0) {
		drawImage(hudImage(*data, size_t(Icons[colour] + level)), {fx + 15, fy + 22, fx + 47, fy + 54}, {0, 0, 32, 32}, nullptr, HudWhite);
	}
	if (level > 0) {
		drawImage(hudImage(*data, size_t(12 + level)), {fx + 24, fy, fx + 88, fy + 64}, {0, 0, 64, 64}, nullptr, HudWhite);
	}
}

// The speedometer: its dial, the needle (two triangles), the hub.
void Menus::drawSpeedometer(const World& w, size_t p, MenuRect view)
{
	const float ms = hudScale(w);
	const int32_t vr = view.right, vb = view.bottom;
	const int32_t wd = int32_t(128 * ms);
	drawImage(hudImage(*data, 0), {vr - wd - 2, vb - wd - 2, vr - 2, vb - 2}, {0, 0, 128, 128}, nullptr, HudWhite);
	const float hx = float(vr - ((52 * wd) >> 7) - 2), hy = float(vb - ((52 * wd) >> 7) - 2);
	const float theta = std::clamp(w.raceView.huds[p].speed * 18.0f, 0.0f, 3.4f) + 2.25f;
	const auto at = [&](float length, float angle) { return std::array<float, 4>{hx + length * std::cos(angle) * ms, hy + length * std::sin(angle) * ms, -1, -1}; };
	const std::array<float, 4> tip = at(45, theta), left = at(7, theta + 2.0943952f), right = at(7, theta + 4.1887903f);
	const std::array<float, 4> hub = {hx, hy, -1, -1};
	drawTriangle(-1, {right, tip, hub}, {0xff969696, 0xff969696, 0xffc8c8c8}, view);
	drawTriangle(-1, {left, hub, tip}, {HudWhite, 0xffc8c8c8, HudWhite}, view);
	const int32_t size = int32_t(16 * ms);
	const int32_t dx = int32_t(hx) - (size >> 1), dy = int32_t(hy) - (size >> 1);
	drawImage(hudImage(*data, 1), {dx, dy, dx + size, dy + size}, {0, 0, 16, 16}, nullptr, HudWhite);
}

// A racer's dot on a map (red for player 2's AI rival and the time race's record ghost).
void Menus::drawMapDot(const World& w, int32_t x, int32_t y, bool red, MenuRect view)
{
	const int32_t row = w.raceView.cameras.size() > 1 ? 16 : 0;
	drawImage(hudImage(*data, 10), {x - 8, y - 8, x + 8, y + 8}, {red ? 16 : 0, row, (red ? 16 : 0) + 16, row + 16}, &view, HudWhite);
}

// The whole map, north up, in the viewport's corner: the others' dots, the ghosts' in a time race, this racer's arrow.
void Menus::drawFixedMap(const World& w, size_t p, const MenuData::Loading& track, MenuRect view)
{
	const size_t i = size_t(w.raceView.cameras[p].racer);
	const std::array<float, 4>& b = track.mapBounds; // min x, max y, max x, min y
	const float minX = b[0], maxY = b[1], maxX = b[2], minY = b[3];
	const bool mirrored = w.session.brief.mirrored;
	const int32_t ox = view.right - 15, oy = view.bottom - 15;
	const float g = 128.0f / std::max(maxX - minX, maxY - minY) * hudScale(w);
	const float ry = mirrored ? -maxY : minY;
	const auto toMap = [&](Vec3 at, int32_t& x, int32_t& y) {
		x = ox + int32_t((at.x - maxX) * g);
		y = oy + int32_t((ry - at.y) * g);
	};
	if (track.map >= 0) {
		const float x0 = float(ox) + (minX - maxX) * g, top = float(oy) + (minY - maxY) * g, x1 = float(ox), bottom = float(oy);
		const float vTop = mirrored ? 0.0f : 0.999f, vBottom = mirrored ? 0.999f : 0.0f;
		const std::array<uint32_t, 3> c = {0xc8ffffff, 0xc8ffffff, 0xc8ffffff};
		drawTriangle(track.map, {{{x0, top, 0, vTop}, {x1, top, 0.999f, vTop}, {x1, bottom, 0.999f, vBottom}}}, c, view);
		drawTriangle(track.map, {{{x0, bottom, 0, vBottom}, {x0, top, 0, vTop}, {x1, bottom, 0.999f, vBottom}}}, c, view);
	}
	for (size_t j = w.racers.race.size(); j-- > 0;) {
		if (j != i) {
			int32_t x, y;
			toMap(w.racers.visible[j].position, x, y);
			drawMapDot(w, x, y, j == 1 && w.racers.race[1].ai, view);
		}
	}
	if (w.session.brief.mode == RaceBrief::Mode::TimeRace) {
		const auto ghost = [&](const GhostRun& run, bool valid, bool red) {
			Vec3 at;
			Quat q;
			if (valid && !run.finished && w.ghostAt(run, at, q)) {
				int32_t x, y;
				toMap(at, x, y);
				drawMapDot(w, x, y, red, view);
			}
		};
		ghost(w.session.timeRace.record, w.session.timeRace.recordValid, true);
		ghost(w.session.timeRace.best, w.session.timeRace.bestValid, false);
	}
	int32_t x, y;
	toMap(w.racers.visible[i].position, x, y);
	const Vec3 f = w.racers.visible[i].rotation.f;
	const float len = std::sqrt(f.x * f.x + f.y * f.y);
	const float dx = len > 0 ? f.x / len : 1, dy = len > 0 ? -f.y / len : 0, nx = dy, ny = -dx;
	const auto arrow = [&](float tip, float back, float half, uint32_t colour) {
		const float ax = float(x), ay = float(y);
		drawTriangle(-1,
			{{{ax - back * dx + half * nx, ay - back * dy + half * ny, -1, -1}, {ax + tip * dx, ay + tip * dy, -1, -1},
				{ax - back * dx - half * nx, ay - back * dy - half * ny, -1, -1}}},
			{colour, colour, colour}, view);
	};
	arrow(9.5f, 6.5f, 5.5f, 0xff000000);
	arrow(8.0f, 5.0f, 4.0f, 0xff00ff00);
}

// The rotating map: ahead is up, around this racer, inside its box.
void Menus::drawRotatingMap(const World& w, size_t p, const MenuData::Loading& track, MenuRect view)
{
	const PlayerCamera& cam = w.raceView.cameras[p];
	const size_t i = size_t(cam.racer);
	const std::array<float, 4>& b = track.mapBounds; // min x, max y, max x, min y
	const float minX = b[0], maxY = b[1], maxX = b[2], minY = b[3];
	const bool mirrored = w.session.brief.mirrored;
	const float ms = hudScale(w);
	const Vec3 me = w.racers.visible[i].position;
	const int32_t size = int32_t(128 * ms);
	const MenuRect box = {view.right - 15 - size, view.bottom - 15 - size, view.right - 15, view.bottom - 15};
	const float bx = float(box.left + box.right) * 0.5f, by = float(box.top + box.bottom) * 0.5f;
	Vec3 r_, d_, l_;
	cam.shownRows(r_, d_, l_);
	const float vlen = std::sqrt(l_.x * l_.x + l_.y * l_.y);
	const float ddx = vlen > 0 ? l_.x / vlen * 0.12f * ms : 0, ddy = vlen > 0 ? l_.y / vlen * 0.12f * ms : 0;
	const auto toBox = [&](float x, float y, float& sx, float& sy) {
		const float ex = x - me.x, ey = y - me.y;
		sx = bx + (ex * ddy - ey * ddx);
		sy = by - (ex * ddx + ey * ddy);
	};
	if (track.map >= 0) {
		const float yTop = mirrored ? -minY : maxY, yBottom = mirrored ? -maxY : minY;
		const float vTop = mirrored ? 0.0f : 0.999f, vBottom = mirrored ? 0.999f : 0.0f;
		std::array<std::array<float, 4>, 4> c;
		toBox(minX, yTop, c[0][0], c[0][1]);
		toBox(maxX, yTop, c[1][0], c[1][1]);
		toBox(maxX, yBottom, c[2][0], c[2][1]);
		toBox(minX, yBottom, c[3][0], c[3][1]);
		c[0][2] = 0, c[1][2] = 0.999f, c[2][2] = 0.999f, c[3][2] = 0;
		c[0][3] = c[1][3] = vTop, c[2][3] = c[3][3] = vBottom;
		const std::array<uint32_t, 3> colour = {0xc8ffffff, 0xc8ffffff, 0xc8ffffff};
		drawTriangle(track.map, {c[0], c[1], c[3]}, colour, box);
		drawTriangle(track.map, {c[3], c[1], c[2]}, colour, box);
	}
	for (size_t j = w.racers.race.size(); j-- > 0;) {
		if (j != i) {
			float sx, sy;
			toBox(w.racers.visible[j].position.x, w.racers.visible[j].position.y, sx, sy);
			drawMapDot(w, int32_t(sx), int32_t(sy), j == 1 && w.racers.race[1].ai, view);
		}
	}
	const auto arrow = [&](float tip, float back, float half, uint32_t colour) {
		// (pointing up: d = (0, -1), n = (-1, 0))
		drawTriangle(-1, {{{bx - half, by + back, -1, -1}, {bx, by - tip, -1, -1}, {bx + half, by + back, -1, -1}}}, {colour, colour, colour}, box);
	};
	arrow(9.5f, 6.5f, 5.5f, 0xff000000);
	arrow(8.0f, 5.0f, 4.0f, 0xff00ff00);
	// Its 1-px white frame.
	const float l = float(box.left), t = float(box.top), rr = float(box.right), bb = float(box.bottom);
	const auto band = [&](float left, float top, float right, float bottom) {
		drawTriangle(-1, {{{left, top, -1, -1}, {right, top, -1, -1}, {right, bottom, -1, -1}}}, {HudWhite, HudWhite, HudWhite}, Screen640);
		drawTriangle(-1, {{{left, bottom, -1, -1}, {left, top, -1, -1}, {right, bottom, -1, -1}}}, {HudWhite, HudWhite, HudWhite}, Screen640);
	};
	band(l, t, rr, t + 1);
	band(l, bb - 1, rr, bb);
	band(l, t + 1, l + 1, bb - 1);
	band(rr - 1, t + 1, rr, bb - 1);
}

// The HUDs: per player, in its viewport, the banner, the race info (lap, best, total or place,
// time behind, WRONG WAY), the power-up frame and the gadget (map or speedometer).
// The HUD's text columns in a player's viewport: the fonts and their scale, the times' widths, the time column (rx),
// the place's (px), the centre (cx), the four rows and the viewport's top and bottom.
struct Menus::HudColumns {
	int16_t ths, ignum;
	float ts;
	int32_t shortWidth, longWidth, rx, px, cx, y1, y2, y3, y4, vt, vb;
};

// A time at the time column (a short one moved right by the difference): the x it was drawn at.
int32_t Menus::hudTime(const HudColumns& at, int32_t t, int32_t y)
{
	const std::u16string text = raceTime(t);
	const int32_t x = at.rx + (text.size() == 7 ? at.longWidth - at.shortWidth : 0);
	drawScaled(at.ths, text, x, y, at.ts, at.ts, HudWhite);
	return x;
}

// Player p's racing rows: the lap and its time, the best lap, the total time (a time race) or the place, the time
// behind, and WRONG WAY.
void Menus::drawHudTimes(const World& w, size_t p, const HudColumns& at)
{
	const MenuData::Language& lang = data->languages[language];
	constexpr uint32_t White = HudWhite;
	const bool timed = w.session.brief.mode == RaceBrief::Mode::TimeRace;
	const int32_t laps = w.session.raceLaps;
	const Hud& h = w.raceView.huds[p];
	const Racer& r = w.racers.race[size_t(w.raceView.cameras[p].racer)];
	// The lap row: the lap (alone), LAP, the lap's time (the last lap's while a new one is under 3 s, blinking;
	// the last once finished, blinking 3 s with FINISH).
	const int32_t n = std::min(r.laps + 1, laps);
	const std::u16string lap = number(n);
	const int32_t lapX = at.rx - (int32_t(float(lineWidth(at.ths, lap)) * at.ts) + int32_t(14 * at.ts));
	drawScaled(at.ths, lap, lapX, at.y1, at.ts, at.ts, White);
	drawScaled(at.ths, lang.game.at(39), lapX - (int32_t(34 * at.ts) + int32_t(6 * at.ts)), at.y1, at.ts, at.ts, White);
	if (r.laps < laps) {
		const int32_t t = r.lapMs[std::clamp(r.laps, 0, 4)];
		if (r.laps > 0 && t < 3000) {
			if ((t / 250) % 2 == 0) {
				hudTime(at, r.lapMs[r.laps - 1], at.y1);
			}
		}
		else {
			hudTime(at, t, at.y1);
		}
	}
	else if (!(h.banner < 0 && -h.banner < 3000 && (-h.banner / 250) % 2 == 1)) {
		hudTime(at, r.lapMs[std::clamp(laps - 1, 0, 4)], at.y1);
	}
	if (h.bestMs > 0) {
		drawScaled(at.ths, lang.game.at(37), at.rx - (int32_t(49 * at.ts) + int32_t(14 * at.ts)), at.y2, at.ts, at.ts, White);
		hudTime(at, h.bestMs, at.y2);
	}
	if (timed) {
		drawScaled(at.ths, lang.game.at(38), at.rx - 74, at.y3, 1, 1, White);
		int32_t total = 0;
		for (int32_t l = 0; l < 5 && (r.laps < laps ? l <= r.laps : l < r.laps); l++) {
			total += r.lapMs[l];
		}
		hudTime(at, total, at.y3);
	}
	else if (h.place >= 0 && h.place < 8) {
		const float k = h.pulseMs == 0 ? 1.0f : h.pulseMs <= 175 ? 1.0f + float(h.pulseMs) / 175.0f : h.pulseMs <= 350 ? 2.0f - float(h.pulseMs - 175) / 175.0f : 1.0f;
		drawScaled(at.ths, lang.game.at(size_t(25 + h.place)), at.px + 5, at.y1, at.ts, at.ts, White);
		const std::u16string digit(1, char16_t(u'1' + h.place));
		drawScaled(at.ignum, digit, at.px - int32_t(float(lineWidth(at.ignum, digit)) * at.ts), at.y1, at.ts * k, at.ts * k, White);
	}
	if (r.timeBehindMs > 0) {
		const int32_t x = hudTime(at, r.timeBehind, at.y4);
		if (h.visible) {
			drawScaled(at.ths, lang.game.at(33), x - 11, at.y4, 1, 1, White);
		}
	}
	if ((r.flags & RacerWrongWayShown) && h.visible) {
		const std::u16string_view text = lang.game.at(36);
		drawScaled(at.ths, text, at.cx - (lineWidth(at.ths, text) >> 1), at.vt + (at.vb - at.vt) / 5, 1, 1, White);
	}
}

void Menus::drawHud(const World& w)
{
	hudLayers = {};
	const Session::Phase phase = w.session.phase;
	if (phase != Session::Countdown && phase != Session::Racing && phase != Session::Finished) {
		return;
	}
	const int16_t ths = data->raceFont(language, "font_ths"), ignum = data->raceFont(language, "ignum");
	const MenuData::Loading* track = data->loading(w.session.brief.track);
	const bool testDrive = w.session.brief.mode == RaceBrief::Mode::TestDrive;
	const bool split = w.raceView.cameras.size() > 1;
	const float ts = split ? 0.9f : 1.0f;
	const int32_t shortWidth = int32_t(float(lineWidth(ths, u"0:00:00")) * ts), longWidth = int32_t(float(lineWidth(ths, u"59:59:88")) * ts);
	const int32_t th = ths >= 0 ? int32_t(float(data->languages[language].fonts[size_t(ths)].glyphs.height) * ts) : 0;
	for (size_t p = 0; p < w.raceView.huds.size() && p < w.raceView.cameras.size() && p < hudLayers.size(); p++) {
		const Hud& h = w.raceView.huds[p];
		const PlayerCamera& cam = w.raceView.cameras[p];
		const size_t i = size_t(cam.racer);
		const Racer& r = w.racers.race[i];
		hudLayers[p].first = uint32_t(elements.size());
		const int32_t vl = cam.viewport[0], vt = cam.viewport[1], vr = cam.viewport[2], vb = cam.viewport[3], width = vr - vl;
		const MenuRect view = {vl, vt, vr, vb};
		const int32_t rx = vr - ((11 * longWidth) >> 3), y1 = vt + 7, y2 = y1 + ((7 * th) >> 3), y3 = y1 + ((14 * th) >> 3), y4 = y1 + ((21 * th) >> 3);
		const int32_t px = vl + width / 10, cx = (vl + vr) >> 1;
		if (h.banner != 0 && h.visible) {
			drawHudBanner(w, p, cx, y4);
		}
		if (r.flags & RacerGhost) {
			hudLayers[p].second = uint32_t(elements.size());
			continue; // (in the warp tube: only the banner)
		}
		if (!testDrive) {
			drawHudTimes(w, p, {ths, ignum, ts, shortWidth, longWidth, rx, px, cx, y1, y2, y3, y4, vt, vb});
		}
		drawPowerFrame(w, i, view);
		// The gadget.
		const uint8_t mode = p < w.raceView.mapModes.size() ? w.raceView.mapModes[p] : uint8_t(GadgetFixedMap);
		if (mode == GadgetSpeedometer) {
			drawSpeedometer(w, p, view);
		}
		else if (mode == GadgetFixedMap && track) {
			drawFixedMap(w, p, *track, view);
		}
		else if (mode == GadgetRotatingMap && track) {
			drawRotatingMap(w, p, *track, view);
		}
		hudLayers[p].second = uint32_t(elements.size());
	}
}

namespace {

// A slot's name on the standings, and whether its row shows (players' rows blink).
std::u16string_view slotName(const World& w, int32_t slot) { return std::u16string_view(w.session.brief.slots[size_t(slot)].name); }
bool rowShown(const World& w, int32_t slot) { return !w.racers.race[size_t(slot)].player || w.session.blinkMs >= 150; }

// A racer's race time: its laps' times summed.
int32_t raceMsOf(const World& w, int32_t s)
{
	int32_t sum = 0;
	for (int32_t lap = 0; lap < w.session.raceLaps; lap++) {
		sum += w.racers.race[size_t(s)].lapMs[lap];
	}
	return sum;
}

} // namespace

// The race's standings: by this race's points (ties to the last slot), with the gaps to the leader.
void Menus::drawRaceStandings(const World& w)
{
	const MenuData::Language& lang = data->languages[language];
	const int16_t font = int16_t(lang.raceFonts);
	const uint32_t white = 0xffffffff;
	const int32_t n = int32_t(w.racers.race.size());
	int32_t leader = n - 1, best = 0;
	for (int32_t s = 0; s < n; s++) {
		if (w.session.roundPoints[size_t(s)] >= best) {
			best = w.session.roundPoints[size_t(s)];
			leader = s;
		}
	}
	std::array<bool, 6> taken = {};
	char16_t sign = u'+';
	for (int32_t row = 0, y = 132; row < n && y < 324; row++, y += 32) {
		int32_t pick = leader;
		for (int32_t s = 0, most = 0; s < n; s++) {
			if (!taken[size_t(s)] && w.session.roundPoints[size_t(s)] >= most) {
				most = w.session.roundPoints[size_t(s)];
				pick = s;
			}
		}
		taken[size_t(pick)] = true;
		if (!rowShown(w, pick)) {
			continue;
		}
		drawLine(font, number(row + 1), 20, y, white);
		drawLine(font, slotName(w, pick), 50, y, white);
		drawLine(font, number(w.session.roundPoints[size_t(pick)]), 450, y, white);
		if (pick == leader) {
			drawLine(font, lang.game.at(42), 500, y, white);
		}
		else if (w.racers.race[size_t(pick)].flags & RacerFinished) {
			const int32_t gap = raceMsOf(w, pick) - raceMsOf(w, leader);
			sign = gap > 0 ? u'+' : gap < 0 ? u'-' : sign;
			drawLine(font, raceTime(std::abs(gap)), 520, y, white);
			drawLine(font, std::u16string(1, sign), 500, y, white);
		}
		else {
			drawLine(font, lang.game.at(43), 500, y, white);
		}
	}
}

// The circuit's standings: by its points (ties to the first slot).
void Menus::drawCircuitStandings(const World& w)
{
	const MenuData::Language& lang = data->languages[language];
	const int16_t font = int16_t(lang.raceFonts);
	const uint32_t white = 0xffffffff;
	const int32_t n = int32_t(w.racers.race.size());
	const std::u16string_view title = lang.game.at(45);
	drawLine(font, title, 320 - (lineWidth(font, title) >> 1), 60, white);
	std::array<bool, 6> taken = {};
	for (int32_t row = 0, y = 132; row < n && y < 324; row++, y += 32) {
		int32_t pick = 0;
		for (int32_t s = 0, most = 0; s < n; s++) {
			if (!taken[size_t(s)] && w.session.circuitPoints[size_t(s)] > most) {
				most = w.session.circuitPoints[size_t(s)];
				pick = s;
			}
		}
		taken[size_t(pick)] = true;
		if (!rowShown(w, pick)) {
			continue;
		}
		drawLine(font, number(row + 1), 20, y, white);
		drawLine(font, slotName(w, pick), 50, y, white);
		drawLine(font, number(w.session.circuitPoints[size_t(pick)]), 450, y, white);
	}
}

// The race's dialog over it.
void Menus::drawRaceDialog(const World& w)
{
	const MenuData::Language& lang = data->languages[language];
	const int16_t font = int16_t(lang.raceFonts);
	const uint32_t white = 0xffffffff;
	const RaceDialog& d = w.session.dialog;
	if (d.kind == RaceDialog::None) {
		return;
	}
	// The dialog: a dimmed screen, the prompt (and its second line) and the options centred, one line height apart.
	int32_t h = 0;
	for (int32_t i = 0; i < d.count; i++) {
		h = std::max(h, measure(font, lang.game.at(size_t(d.options[size_t(i)])), 0).height());
	}
	h += 2;
	const int32_t y0 = 240 - (d.count * h) / 2;
	drawQuad(Screen640, 0x40000000, 64);
	const auto centred = [&](int32_t text, int32_t y, uint32_t colour) {
		const std::u16string_view line = lang.game.at(size_t(text));
		drawLine(font, line, 320 - (lineWidth(font, line) >> 1), y, colour);
	};
	int32_t y = y0 - h - (d.second >= 0 ? h >> 1 : 0);
	centred(d.prompt, y, white);
	if (d.second >= 0) {
		y += h;
		centred(d.second, y, white);
	}
	for (int32_t i = 0; i < d.count; i++) {
		y += h;
		if (i == d.selection && !d.chosen) {
			// Blinking, its alpha pulsing.
			if (d.blinkMs < 250) {
				const int32_t angle = int32_t(float(d.blinkMs) * 0.002f * Tau * -162.97466f);
				const int32_t alpha = 191 + int32_t(cosEntry((-256 - angle) & 1023) * -64.0f);
				centred(d.options[size_t(i)], y, uint32_t(alpha) << 24 | 0xffff24);
			}
			continue;
		}
		centred(d.options[size_t(i)], y, 0xff7f7f12);
	}
}

void Menus::drawRace(const World& w)
{
	elements.clear();
	glyphs.clear();
	scenes.clear();
	drawHud(w);
	const MenuData::Language& lang = data->languages[language];
	const uint32_t white = 0xffffffff;
	// The standings table showing: the race's or the circuit's.
	const int32_t table = w.standingsTable();
	if (table == 1) {
		drawRaceStandings(w);
	}
	else if (table == 2) {
		drawCircuitStandings(w);
	}
	// The attract demo's DEMO, blinking.
	if (w.session.brief.mode == RaceBrief::Mode::Demo && w.raceView.demoMs > 250) {
		const int16_t ths = data->raceFont(language, "font_ths");
		const std::u16string_view demo = lang.game.at(46);
		drawLine(ths, demo, 320 - (lineWidth(ths, demo) >> 1), 443, white);
	}
	// The opening fade from black.
	if (w.session.openingMs > 0) {
		const int32_t alpha = 255 * w.session.openingMs / 500;
		drawQuad(Screen640, uint32_t(alpha) << 24, alpha);
	}
	// The fade to black, under the dialog.
	if (w.session.fade.active && w.session.fade.ms > 0) {
		const int32_t alpha = int32_t(255.0f * (1.0f - float(w.session.fade.left) / float(w.session.fade.ms)));
		drawQuad(Screen640, uint32_t(alpha) << 24, alpha);
	}
	drawRaceDialog(w);
}

} // namespace lr

namespace lr {

namespace {

// Colours are sRGB; tinting and blending happen in linear light (a texel times the item colour looks right
// only so).
uint32_t encoded(float l)
{
	const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
	return uint32_t(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct Linear {
	std::array<float, 256> of;
	std::vector<uint8_t> tinted; // [texel][colour]
	Linear()
		: tinted(256 * 256)
	{
		for (size_t i = 0; i < 256; i++) {
			const float c = float(i) / 255.0f;
			of[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}
		for (size_t t = 0; t < 256; t++) {
			for (size_t c = 0; c < 256; c++) {
				tinted[t * 256 + c] = uint8_t(encoded(of[t] * of[c]));
			}
		}
	}
	uint32_t tint(uint8_t texel, uint32_t colour) const { return tinted[size_t(texel) * 256 + colour]; }
};
const Linear linear;

// Texels from (u0, v0)..(u1, v1) over the pixels whose centres lie in [x0, x1) x [y0, y1), keyed texels left out,
// times the colour.
void paintTexels(const uint8_t* rgba, int32_t width, int32_t height, bool keyed, uint32_t key, std::array<float, 4> d, std::array<float, 4> s, uint32_t colour, uint32_t* target)
{
	if (!rgba || d[2] <= d[0] || d[3] <= d[1]) {
		return;
	}
	const int32_t px0 = std::max(0, int32_t(std::ceil(d[0] - 0.5f))), px1 = std::min(640, int32_t(std::ceil(d[2] - 0.5f)));
	const int32_t py0 = std::max(0, int32_t(std::ceil(d[1] - 0.5f))), py1 = std::min(480, int32_t(std::ceil(d[3] - 0.5f)));
	const float du = (s[2] - s[0]) / (d[2] - d[0]), dv = (s[3] - s[1]) / (d[3] - d[1]);
	const uint32_t cr = colour >> 16 & 255, cg = colour >> 8 & 255, cb = colour & 255;
	const uint32_t keyRgb = key & 0xffffff;
	for (int32_t py = py0; py < py1; py++) {
		const int32_t tv = std::clamp(int32_t(std::floor(s[1] + (float(py) + 0.5f - d[1]) * dv)), 0, height - 1);
		const uint8_t* row = rgba + size_t(tv) * size_t(width) * 4;
		uint32_t* out = target + size_t(py) * 640;
		for (int32_t px = px0; px < px1; px++) {
			const int32_t tu = std::clamp(int32_t(std::floor(s[0] + (float(px) + 0.5f - d[0]) * du)), 0, width - 1);
			const uint8_t* t = row + size_t(tu) * 4;
			if (keyed && (uint32_t(t[0]) << 16 | uint32_t(t[1]) << 8 | t[2]) == keyRgb) {
				continue;
			}
			out[px] = (colour & 0xffffff) == 0xffffff ? 0xff000000u | uint32_t(t[0]) << 16 | uint32_t(t[1]) << 8 | t[2]
													  : 0xff000000u | linear.tint(t[0], cr) << 16 | linear.tint(t[1], cg) << 8 | linear.tint(t[2], cb);
		}
	}
}

// A pixel blended over what is there at an alpha (in linear light; a clear pixel takes the colour at the alpha).
void blendPixel(uint32_t& out, uint32_t colour, uint32_t a)
{
	if (a >= 255) {
		out = 0xff000000u | (colour & 0xffffff);
		return;
	}
	if (a == 0) {
		return;
	}
	const float fa = float(a) / 255.0f, da = float(out >> 24) / 255.0f, oa = fa + da * (1 - fa);
	uint32_t mixed = uint32_t(oa * 255.0f + 0.5f) << 24;
	for (int shift = 0; shift <= 16; shift += 8) {
		const float c = linear.of[colour >> shift & 255], d = linear.of[out >> shift & 255];
		mixed |= (oa > 0 ? encoded((fa * c + da * (1 - fa) * d) / oa) : 0) << shift;
	}
	out = mixed;
}

// A triangle: the pixels whose centres are inside it (and its clip), its corners' colours interpolated, times the
// image's texel (point sampled) if it has one, blended at the result's alpha.
void paintTriangle(const MenuData& data, const MenuElement& e, uint32_t* target)
{
	const auto& v = e.vertices;
	const float area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[2][0] - v[0][0]) * (v[1][1] - v[0][1]);
	if (area == 0) {
		return;
	}
	const MenuData::ImageInfo* image = e.image >= 0 ? &data.images[size_t(e.image)] : nullptr;
	const uint32_t colours[3] = {e.colour, e.corners[0], e.corners[1]};
	const float x0 = std::min({v[0][0], v[1][0], v[2][0]}), x1 = std::max({v[0][0], v[1][0], v[2][0]});
	const float y0 = std::min({v[0][1], v[1][1], v[2][1]}), y1 = std::max({v[0][1], v[1][1], v[2][1]});
	const int32_t px0 = std::max({0, e.clip[0], int32_t(std::ceil(x0 - 0.5f))}), px1 = std::min({640, e.clip[2], int32_t(std::ceil(x1 - 0.5f))});
	const int32_t py0 = std::max({0, e.clip[1], int32_t(std::ceil(y0 - 0.5f))}), py1 = std::min({480, e.clip[3], int32_t(std::ceil(y1 - 0.5f))});
	for (int32_t py = py0; py < py1; py++) {
		for (int32_t px = px0; px < px1; px++) {
			const float x = float(px) + 0.5f, y = float(py) + 0.5f;
			float b[3];
			for (int k = 0; k < 3; k++) {
				const auto& a = v[size_t((k + 1) % 3)];
				const auto& c = v[size_t((k + 2) % 3)];
				b[k] = ((c[0] - a[0]) * (y - a[1]) - (x - a[0]) * (c[1] - a[1])) / area;
			}
			if (b[0] < 0 || b[1] < 0 || b[2] < 0) {
				continue;
			}
			float channel[4] = {};
			for (int k = 0; k < 3; k++) {
				for (int c = 0; c < 4; c++) {
					channel[c] += b[k] * float(colours[k] >> (8 * c) & 255);
				}
			}
			uint32_t colour = 0;
			for (int c = 0; c < 3; c++) {
				colour |= uint32_t(std::clamp(channel[c] + 0.5f, 0.0f, 255.0f)) << (8 * c);
			}
			uint32_t alpha = uint32_t(std::clamp(channel[3] + 0.5f, 0.0f, 255.0f));
			if (image && !image->rgba.empty()) {
				const float u = b[0] * v[0][2] + b[1] * v[1][2] + b[2] * v[2][2], w = b[0] * v[0][3] + b[1] * v[1][3] + b[2] * v[2][3];
				const int32_t tu = std::clamp(int32_t(std::floor(u * float(image->width))), 0, int32_t(image->width) - 1);
				const int32_t tv = std::clamp(int32_t(std::floor(w * float(image->height))), 0, int32_t(image->height) - 1);
				const uint8_t* t = &image->rgba[(size_t(tv) * image->width + size_t(tu)) * 4];
				colour = linear.tint(t[0], colour >> 16 & 255) << 16 | linear.tint(t[1], colour >> 8 & 255) << 8 | linear.tint(t[2], colour & 255);
				alpha = alpha * t[3] / 255;
			}
			blendPixel(target[size_t(py) * 640 + size_t(px)], colour, alpha);
		}
	}
}

} // namespace

void paint(const MenuData& data, size_t language, std::span<const MenuElement> elements, std::span<const MenuGlyph> glyphs, uint32_t* target)
{
	for (const MenuElement& e : elements) {
		switch (e.kind) {
		case MenuElement::Image: {
			const MenuData::ImageInfo& i = data.images[size_t(e.image)];
			paintTexels(i.rgba.data(), int32_t(i.width), int32_t(i.height), i.keyed, i.key, e.dest, e.source, e.colour, target);
			break;
		}
		case MenuElement::Glyphs: {
			const MenuData::Font& f = data.languages[language].fonts[size_t(e.image)];
			for (uint32_t g = e.firstGlyph; g < e.firstGlyph + e.glyphCount; g++) {
				const MenuGlyph& q = glyphs[g];
				paintTexels(f.rgba.data(), int32_t(f.width), int32_t(f.glyphs.height), f.keyed, f.key, {float(q.dest[0]), float(q.dest[1]), float(q.dest[2]), float(q.dest[3])},
					q.source, e.colour, target);
			}
			break;
		}
		case MenuElement::Quad: {
			// (A rect turned inside out covers its span.)
			const float x0 = std::min(e.dest[0], e.dest[2]), x1 = std::max(e.dest[0], e.dest[2]);
			const float y0 = std::min(e.dest[1], e.dest[3]), y1 = std::max(e.dest[1], e.dest[3]);
			const int32_t px0 = std::max(0, int32_t(std::ceil(x0 - 0.5f))), px1 = std::min(640, int32_t(std::ceil(x1 - 0.5f)));
			const int32_t py0 = std::max(0, int32_t(std::ceil(y0 - 0.5f))), py1 = std::min(480, int32_t(std::ceil(y1 - 0.5f)));
			const uint32_t a = e.alpha < 0 ? 255 : uint32_t(std::clamp(e.alpha, 0, 255));
			for (int32_t py = py0; py < py1; py++) {
				for (int32_t px = px0; px < px1; px++) {
					uint32_t& out = target[size_t(py) * 640 + size_t(px)];
					if (a == 255) {
						out = 0xff000000u | (e.colour & 0xffffff);
						continue;
					}
					// Over what is there (in linear light): a clear pixel takes the quad's colour at its alpha.
					const float fa = float(a) / 255.0f, da = float(out >> 24) / 255.0f, oa = fa + da * (1 - fa);
					uint32_t mixed = uint32_t(oa * 255.0f + 0.5f) << 24;
					for (int shift = 0; shift <= 16; shift += 8) {
						const float c = linear.of[e.colour >> shift & 255], d = linear.of[out >> shift & 255];
						mixed |= (oa > 0 ? encoded((fa * c + da * (1 - fa) * d) / oa) : 0) << shift;
					}
					out = mixed;
				}
			}
			break;
		}
		case MenuElement::Triangle: paintTriangle(data, e, target); break;
		case MenuElement::View3D: break;
		}
	}
}

} // namespace lr
