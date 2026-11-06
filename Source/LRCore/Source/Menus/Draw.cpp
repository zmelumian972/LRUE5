// What the menus draw: the widget trees walked depth first, each part clipped to its parent, into a list of images,
// glyph runs, flat quads and 3D views in the 640x480 screen; the cursor and the fades over them.

#include "LRCore/Menus/Menus.h"

#include "Menus/Layout.h"
#include "Menus/Pen.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

// The view (in the widget's parent) clipped to the widget's rect r, and the screen rect with it; false when they miss.
bool clipView(const MenuRect& r, MenuRect& view, MenuRect& scr)
{
	if (view.left >= r.right || view.right <= r.left || view.top >= r.bottom || view.bottom <= r.top) {
		return false;
	}
	cutTo(r, view, scr, 1, 1);
	return true;
}

} // namespace

void Ui::draw(WidgetTree& t, int16_t w, MenuRect view, MenuRect scr)
{
	Pen pen = m.pen();
	Widget& n = t[w];
	const MenuRect r = n.rect;
	if (!clipView(r, view, scr)) {
		return;
	}
	const MenuRect v = {view.left - r.left + n.offsetX, view.top - r.top + n.offsetY, view.right - r.left + n.offsetX,
		view.bottom - r.top + n.offsetY};
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
			pen.drawQuad(scr, n.colour, -1);
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
void Ui::drawStretched(Widget& n, MenuRect v, MenuRect scr)
{
	Pen pen = m.pen();
	const MenuRect r = n.rect;
	const MenuData::ImageInfo* info = n.image >= 0 ? &m.data->images[n.image] : nullptr;
	const int32_t rw = r.width(), rh = r.height();
	n.scaleX = info && rw > 0 ? float(info->width) / float(rw) : 0;
	n.scaleY = info && rh > 0 ? float(info->height) / float(rh) : 0;
	const MenuRect source = {int32_t(float(v.left) * n.scaleX), int32_t(float(v.top) * n.scaleY), int32_t(float(v.right) * n.scaleX),
		int32_t(float(v.bottom) * n.scaleY)};
	pen.drawImage(n.image, scr, source, nullptr, n.colour);
}

// A hotspot's lit region: its image's part under the region's rect.
void Ui::drawLitRegion(const Widget& n, MenuRect v, MenuRect scr)
{
	Pen pen = m.pen();
	const MenuRect r = n.rect;
	for (size_t i = 0; n.lit && i < n.regions.size(); i++) {
		if (n.regions[i] != n.lit) {
			continue;
		}
		const MenuRect g = n.regionRects[i];
		const MenuRect part = {std::max(g.left, 0), std::max(g.top, 0), std::min(g.right, r.width()), std::min(g.bottom, r.height())};
		const MenuRect dest = {scr.left - v.left + part.left, scr.top - v.top + part.top, scr.left - v.left + part.right,
			scr.top - v.top + part.bottom};
		pen.drawImage(n.image, dest, part, &scr, 0xffffffff);
	}
}

// A text field: the text from the field's left, centred in its height less 4 px and fitted; the caret placed after it.
void Ui::drawTextField(WidgetTree& t, const Widget& n, MenuRect v, MenuRect scr)
{
	Pen pen = m.pen();
	const MenuRect r = n.rect;
	const int32_t fh = pen.measure(n.font, u"", 0).height(); // a line's height
	const int32_t tw = pen.lineWidth(n.font, n.text);
	const MenuRect inner = {v.left, v.top, v.right, v.bottom - 4};
	MenuRect text = {0, 0, tw, fh};
	centre(inner, text);
	const int32_t x = scr.left, y = scr.top - v.top + text.top;
	pen.drawText(n.font, n.text, {x, y, x + text.width(), y + text.height()}, {scr.left, scr.top, scr.right, scr.bottom - 4}, n.colour);
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
void Ui::drawLabel(const Widget& n, MenuRect v, MenuRect scr)
{
	Pen pen = m.pen();
	if (n.wrapping) {
		pen.drawWrapped(n.font, n.text, scr.left - v.left, scr.top - v.top, n.wrap, n.centred, scr, n.colour);
		return;
	}
	const MenuRect r = n.rect;
	int32_t lines = 0;
	forEachLine(n.text, [&](std::u16string_view) { lines++; });
	const float lineH = float(r.height()) / float(lines);
	const int32_t fh = pen.measure(n.font, u"", 0).height(); // a line's height
	int32_t i = 0;
	forEachLine(n.text, [&](std::u16string_view line) {
		const MenuRect box = {0, int32_t(float(i) * lineH), r.width(), int32_t(float(i + 1) * lineH)};
		MenuRect text = {0, 0, pen.lineWidth(n.font, line), fh};
		centre(box, text);
		const int32_t dx = scr.left - v.left, dy = scr.top - v.top;
		text = {text.left + dx, text.top + dy, text.right + dx, text.bottom + dy};
		pen.drawText(n.font, line, text, scr, n.colour);
		i++;
	});
}

// A wrapping carousel's items again past its last one (a whole page on, paging), while the view reaches there.
void Ui::drawWrapAround(WidgetTree& t, const Widget& n, MenuRect v, MenuRect scr)
{
	const int32_t extent = t.extent(n);
	if (n.rect.width() + n.offsetX > extent) {
		for (int16_t c = n.first; c >= 0; c = t[c].next) {
			draw(t, c, {v.left - extent, v.top, v.right - extent, v.bottom}, scr);
		}
	}
}

// A scene reference (a cinematic over the screen, or the setup screens' preview), a scene view or a ring.
void Ui::drawView(WidgetTree& t, int16_t w)
{
	const Widget& n = t[w];
	const MenuRect g = global(t, w);
	if (n.kind != Widget::SceneReference) {
		addView(n,
			{float(std::max(0, g.left)), float(std::max(0, g.top)), float(std::min(ScreenWidth, g.right)),
				float(std::min(ScreenHeight, g.bottom))},
			n.kind == Widget::Ring ? &n : nullptr);
	}
	else if (m.cinematicScreen()) {
		m.cinema.drawCinema(m);
	}
	else {
		addView(n, {float(g.left), float(g.top), float(g.right), float(g.bottom)}, nullptr);
	}
}

// A 3D view's element at dest (the screen's pixels), its scene described from the widget and lit (a ring by its own).
void Ui::addView(const Widget& n, std::array<float, 4> dest, const Widget* ring)
{
	MenuElement e;
	e.kind = MenuElement::View3D;
	e.dest = dest;
	e.widget = n.id;
	e.scene = int32_t(m.scenes.size());
	m.describeScene(n, m.scenes.emplace_back());
	m.scenes.back().aspect = (e.dest[2] - e.dest[0]) / std::max(1.0f, e.dest[3] - e.dest[1]);
	m.lightScene(m.scenes.back(), ring);
	m.elements.push_back(std::move(e));
}

void Menus::drawFrame()
{
	elements.clear();
	glyphs.clear();
	scenes.clear();
	const auto cursor = [&] {
		if (ui.cursorOn) {
			const int16_t arrow = data->image("arrow", screen);
			const int32_t w = arrow >= 0 ? int32_t(data->images[arrow].width) : 32;
			const int32_t h = arrow >= 0 ? int32_t(data->images[arrow].height) : 32;
			// QUIRK: its bottom is y + its width.
			pen().drawImage(arrow, {ui.cursorX, ui.cursorY, ui.cursorX + w, ui.cursorY + w}, {0, 0, w, h}, nullptr, 0xffffffff);
			// QUIRK: past the screen's right or bottom edge the cursor is squeezed into it, its whole image kept.
			if (!elements.empty() && elements.back().image == arrow && arrow >= 0) {
				MenuElement& e = elements.back();
				e.source[2] = float(w), e.source[3] = float(h);
				e.dest[2] = float(std::min(ui.cursorX + w, ScreenWidth)), e.dest[3] = float(std::min(ui.cursorY + w, ScreenHeight));
			}
		}
	};
	if (!ui.tree.widgets.empty()) {
		ui.draw(ui.tree, 0, ScreenRect, ScreenRect);
	}
	cursor();
	for (size_t i = 0; i < ui.dialogCount; i++) {
		ui.draw(ui.dialogs[i].tree, 0, ScreenRect, ScreenRect);
		cursor();
	}
	for (const Fade& f : fades) {
		if (!f.active || f.durationMs <= 0) {
			continue;
		}
		const float p = float(f.leftMs) / float(f.durationMs);
		const int32_t alpha = int32_t(f.in ? (1.0f - p) * 255.0f : p * 255.0f);
		pen().drawQuad(f.rect, (f.colour & 0xffffff) | uint32_t(alpha) << 24, alpha);
	}
}

} // namespace lr
