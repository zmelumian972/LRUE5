// Keys and the cursor on each kind of widget (the point in its own space), routed down a tree; selectors and options
// rows stepped; the widgets' timers.

#include "LRCore/Menus/Menus.h"

#include "Menus/Layout.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

bool isMouse(uint32_t key) { return (key >> 28) == 2; }

bool hits(const Widget& n, int32_t x, int32_t y) { return x >= 0 && x <= n.rect.width() && y >= 0 && y <= n.rect.height(); }

// A key a button or stepper refuses: it's disabled, another key holds it, or it repeats while it isn't pressed.
bool refuses(const Widget& n, const MenuKey& key)
{
	return !(n.state & Widget::Enabled) || (n.holding && n.holding != key.key) || (!(n.state & Widget::Pressed) && key.repeat);
}

} // namespace

// Keys on one widget (the point in its own space).

// A selector or an options row: its arrows (a click on one, or the keys) step it; a press on the thumb grabs it.
bool Ui::stepperKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	Widget& n = t[w];
	const uint32_t k = key.key;
	if (refuses(n, key)) {
		return false;
	}
	uint32_t code = k;
	if (isMouse(k)) {
		// A click on a button is that button's step.
		code = 0;
		for (size_t i = 0; i < 2; i++) {
			const int16_t b = n.buttons[i];
			const MenuRect r = t[b].rect;
			if (!code && hits(t[b], x - r.left, y - r.top)) {
				code = i == 0 ? SourcePad | 0xb : SourcePad | 0xa;
			}
		}
		// A press on the thumb grabs it (a held button's repeats over it grab it again).
		if (n.kind == Widget::Options && !code && (n.state & Widget::Selected)) {
			const Widget& thumb = t[n.thumb];
			if (hits(thumb, x - thumb.rect.left, y - thumb.rect.top)) {
				n.holding = k;
				m.play(n.stepSounds[0]);
				n.grabbed = true;
				press(t, w, IconQuiet | IconSilent);
				return true;
			}
		}
	}
	int forward = -1;
	if (code == (SourcePad | 7) || code == (SourcePad | 0xa) || code == (SourceAxis | 0) || code == MenuRight) {
		forward = 1;
	}
	else if (code == (SourcePad | 9) || code == (SourcePad | 0xb) || code == (SourceAxis | 1) || code == MenuLeft) {
		forward = 0;
	}
	if (forward < 0 || !((n.state & Widget::Selected) || n.unselectedInput)) {
		return false;
	}
	const int16_t button = n.buttons[forward ? 1 : 0];
	if (key.repeat && (t[button].state & Widget::Pressed)) {
		n.kind == Widget::Selector ? stepSelector(t, w, forward) : stepRow(t, w, forward);
	}
	else if (!(n.state & Widget::Pressed)) {
		press(t, button, IconQuiet | IconLocal);
		n.kind == Widget::Selector ? stepSelector(t, w, forward) : stepRow(t, w, forward);
		press(t, w);
	}
	t[w].holding = k;
	return true;
}

bool Ui::ringKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	Widget& n = t[w];
	const uint32_t k = key.key;
	// A click on a slot brings its item to the focused slot, at once and silently.
	if (!isMouse(k) || !hits(n, x, y) || n.values.empty()) {
		return false;
	}
	const int32_t count = int32_t(n.values.size());
	for (size_t slot = 0; slot < n.slotRects.size(); slot++) {
		const MenuRect& r = n.slotRects[slot];
		if (inside(r, x, y)) {
			const int32_t to = ((int32_t(slot) + n.index - n.focusSlot) % count + count) % count;
			if (to != n.index && !key.repeat) {
				n.index = to;
				if (n.parent >= 0 && t[n.parent].kind == Widget::Selector) {
					m.notice(t, n.parent, NoticeValue);
				}
			}
			break;
		}
	}
	return true;
}

bool Ui::textFieldKeyDown(WidgetTree& t, int16_t w, const MenuKey& key)
{
	Widget& n = t[w];
	const uint32_t k = key.key;
	// A typed character: Backspace erases; one the field doesn't have is refused and goes on; past the length refused.
	if (!(n.state & Widget::Enabled) || !(n.state & Widget::Selected) || (k >> 28) != (SourceCharacter >> 28)) {
		return false;
	}
	char16_t c = char16_t(k & 0xffff);
	const bool lower = (c >= 'a' && c <= 'z') || (c > 0xe0 && c <= 0xff);
	c = char16_t(lower ? c - 32 : c);
	if (c == 8) {
		m.play(n.editSounds[n.text.empty() ? 3 : 2]);
		if (!n.text.empty()) {
			n.text.pop_back();
			m.notice(t, w, NoticeValue);
		}
		return true;
	}
	if (m.text(n.charset).find(c) == std::u16string_view::npos) {
		m.play(n.editSounds[3]);
		return false;
	}
	if (int32_t(n.text.size()) >= n.maxLength) {
		m.play(n.editSounds[3]);
		return true;
	}
	n.text += c;
	m.play(n.editSounds[0]);
	m.notice(t, w, NoticeValue);
	return true;
}

bool Ui::hotspotKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	Widget& n = t[w];
	const uint32_t k = key.key;
	// Unpressed: a click lights the region under it and presses; Space or keypad Enter presses. Pressed: they
	// release it, and it takes every other key.
	constexpr uint32_t KeypadEnter = SourceKeyboard | 0x9c;
	const bool activate = k == MenuSpace || k == KeypadEnter || k == MenuPadButton;
	if (!(n.state & Widget::Enabled)) {
		return false;
	}
	if (n.state & Widget::Pressed) {
		if (activate) {
			release(t, w);
		}
		return !isMouse(k);
	}
	if (!(n.state & Widget::Selected)) {
		return false;
	}
	if (isMouse(k)) {
		// A click lights the region under it (the screen hears it again while the button repeats); it doesn't press
		// the button.
		if (!hits(n, x, y)) {
			return false;
		}
		if (key.repeat) {
			if (n.lit) {
				m.notice(t, w, NoticeValue);
			}
			return true;
		}
		for (size_t i = 0; i < n.regions.size() && !n.lit; i++) {
			const MenuRect& r = n.regionRects[i];
			if (inside(r, x, y)) {
				n.lit = n.regions[i];
				m.notice(t, w, NoticeValue);
			}
		}
		t[w].holding = k; // (its release clears the region)
		return true;
	}
	if (key.repeat || !activate) {
		return false;
	}
	press(t, w);
	t[w].holding = k;
	return true;
}

bool Ui::keyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	Widget& n = t[w];
	const uint32_t k = key.key;
	switch (n.kind) {
	case Widget::Selector:
	case Widget::Options: return stepperKeyDown(t, w, key, x, y);
	case Widget::Ring: return ringKeyDown(t, w, key, x, y);
	case Widget::TextField: return textFieldKeyDown(t, w, key);
	case Widget::Hotspot: return hotspotKeyDown(t, w, key, x, y);
	case Widget::Root:
	case Widget::ImageButton:
	case Widget::TextButton: {
		const bool acts = (n.state & Widget::Selected) &&
			(k == MenuEnter || k == MenuSpace || k == MenuPadButton || (isMouse(k) && hits(n, x, y)));
		if (refuses(n, key) || !acts) {
			return false;
		}
		press(t, w);
		t[w].holding = k;
		return true;
	}
	default: return false;
	}
}

bool Ui::keyUp(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	Widget& n = t[w];
	if (!n.icon || !n.holding || n.holding != key.key) {
		return false;
	}
	n.holding = 0;
	if (n.kind == Widget::Hotspot) {
		if (isMouse(key.key)) {
			n.lit = 0;
			release(t, w);
		}
		return true;
	}
	if (n.kind == Widget::Selector || n.kind == Widget::Options) {
		// A released thumb stays where it was dragged; the value doesn't follow (the release is the row's).
		n.grabbed = false;
		if ((n.state & Widget::Enabled) && (n.state & Widget::Pressed) && ((n.state & Widget::Selected) || n.unselectedInput)) {
			for (int16_t b : n.buttons) {
				if (t[b].state & Widget::Pressed) {
					release(t, b, IconQuiet | IconLocal | IconSilent);
					break;
				}
			}
		}
		release(t, w);
		return true;
	}
	if (isMouse(key.key) && (n.state & Widget::Pressed) && !hits(n, x, y)) {
		release(t, w, IconQuiet); // the click is cancelled
		return true;
	}
	release(t, w);
	return true;
}

bool Ui::routeKey(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	const Widget& n = t[w];
	if (!(n.flags & Widget::Visible)) {
		return false;
	}
	const bool ownFirst = n.kind == Widget::Selector || n.kind == Widget::Options;
	if (ownFirst && (key.down ? keyDown(t, w, key, x, y) : keyUp(t, w, key, x, y))) {
		return true;
	}
	for (int16_t c = t[w].first; c >= 0; c = t[c].next) {
		if (routeKey(t, c, key, x - t[c].rect.left, y - t[c].rect.top)) {
			return true;
		}
	}
	if (ownFirst) {
		return false;
	}
	return key.down ? keyDown(t, w, key, x, y) : keyUp(t, w, key, x, y);
}

bool Ui::cursorOver(WidgetTree& t, int16_t w, int32_t dx)
{
	Widget& n = t[w];
	if (!n.icon) {
		return false;
	}
	if (n.flags & Widget::Focused) {
		if (n.kind == Widget::Options && n.grabbed) {
			// The thumb follows the mouse, between the buttons.
			Widget& thumb = t[n.thumb];
			const int32_t uw = thumb.rect.width();
			const int32_t lo = t[n.buttons[0]].rect.right, hi = t[n.buttons[1]].rect.left - uw;
			const int32_t left = std::clamp(thumb.rect.left + dx, lo, std::max(lo, hi));
			thumb.rect.right = left + uw;
			thumb.rect.left = left;
		}
		m.notice(t, w, NoticeHover);
		return true;
	}
	// Hovering selects it; a group of icons lets its own go on.
	if (n.iconParent < 0 || !(n.state & Widget::Enabled)) {
		return false;
	}
	select(t, w);
	if (t[w].kind == Widget::TextField) {
		t[w].grabbed = (t[w].flags & Widget::Focused) != 0; // the mouse holds it now (see Menus::frame)
	}
	if (t[w].iconFirst >= 0) {
		return false;
	}
	m.notice(t, w, NoticeHover);
	return true;
}

bool Ui::routeCursor(WidgetTree& t, int16_t w, int32_t x, int32_t y)
{
	if (!(t[w].flags & Widget::Visible)) {
		return false;
	}
	for (int16_t c = t[w].first; c >= 0; c = t[c].next) {
		const int32_t cx = x - t[c].rect.left, cy = y - t[c].rect.top;
		if (hits(t[c], cx, cy) && routeCursor(t, c, cx, cy)) {
			return true;
		}
	}
	return cursorOver(t, w, 0);
}

// Selectors and options rows.

void Ui::stepSelector(WidgetTree& t, int16_t selector, bool forward)
{
	const int16_t c = t[selector].carousel;
	if (c < 0 || t[c].scrolled || (t[c].first < 0 && t[c].values.empty())) {
		return; // still scrolling from the last step
	}
	Widget& car = t[c];
	int32_t count = int32_t(car.values.size());
	for (int16_t i = car.first; i >= 0; i = t[i].next) {
		count++;
	}
	// A ring wraps when it has the items to fill its slots.
	if (car.kind == Widget::Ring) {
		car.wrapAround = count >= car.slots - 1;
	}
	const int32_t was = car.index;
	if (!car.wrapAround && (forward ? was == count - 1 : was == 0)) {
		m.play(car.carouselSounds[2]);
		return;
	}
	car.index = (was + (forward ? 1 : count - 1)) % count;
	car.fromSpin = car.stepSpin;
	car.stepSpin = car.spin;
	const int32_t cw = car.rect.width();
	car.scrollLeft = car.scrollMs;
	car.scrollSpeed = float(forward ? cw : -cw) / float(car.scrollMs);
	car.scrolled = true;
	m.play(car.carouselSounds[forward ? 0 : 1]);
	if (car.index != was) {
		m.notice(t, selector, NoticeValue);
	}
}

void Ui::stepRow(WidgetTree& t, int16_t row, bool forward)
{
	Widget& r = t[row];
	if (forward ? r.index >= r.steps - 1 : r.index <= 0) {
		m.play(r.stepSounds[1]);
		return;
	}
	setValue(t, row, r.index + (forward ? 1 : -1));
	m.notice(t, row, NoticeValue);
	m.play(r.stepSounds[0]);
}

// The timers.

void Ui::tick(WidgetTree& t, int16_t w, int32_t dt)
{
	Widget& n = t[w];
	if (n.blinkOn) {
		if (dt < n.blinkLeft) {
			n.blinkLeft -= dt;
		}
		else {
			n.blinkedOff = !n.blinkedOff;
			n.blinkLeft = n.blinkedOff ? n.blinkOff : n.blinkOn;
		}
	}
	if (n.kind == Widget::Ring) {
		n.spin = std::fmod(n.spin + n.spinRate * float(dt), Tau);
		n.fresh = false;
		if (n.scrolled && n.scrollLeft == 0) {
			n.scrolled = false;
			if (n.parent >= 0 && t[n.parent].kind == Widget::Selector) {
				m.notice(t, n.parent, NoticeSettled);
			}
		}
		else if (n.scrolled) {
			n.scrollLeft -= std::min(dt, n.scrollLeft);
		}
	}
	// A carousel scrolls to its selection; the frame after it gets there it settles.
	if (n.kind == Widget::Carousel && n.scrolled) {
		if (n.scrollLeft == 0) {
			n.scrolled = false;
			layoutCarousel(t, w);
			for (int16_t p = n.parent; p >= 0; p = t[p].parent) {
				if (t[p].kind == Widget::Selector) {
					m.notice(t, p, NoticeSettled);
					break;
				}
			}
		}
		else {
			const int32_t e = std::min(dt, n.scrollLeft);
			n.scrollLeft -= e;
			n.scrollAt += n.scrollSpeed * float(e);
			if (n.scrollAt < 0) {
				n.scrollAt += float(t.extent(n));
			}
			n.offsetX = int32_t(n.scrollAt);
		}
	}
	if (n.kind == Widget::Selector && (n.state & Widget::Pressed) && n.carousel >= 0 && !t[n.carousel].scrolled) {
		// A held button steps again as soon as the last scroll is done.
		for (size_t i = 0; i < 2; i++) {
			if (n.buttons[i] >= 0 && (t[n.buttons[i]].state & Widget::Pressed)) {
				stepSelector(t, w, i == 1);
				break;
			}
		}
	}
	for (int16_t c = t[w].first; c >= 0; c = t[c].next) {
		tick(t, c, dt);
	}
}

} // namespace lr
