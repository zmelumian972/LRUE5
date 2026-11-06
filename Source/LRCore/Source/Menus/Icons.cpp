// The icons: enabled, selected, focused, pressed and released, and the selection's walk over the icon
// tree.

#include "LRCore/Menus/Menus.h"

#include "Menus/MenuNames.h"

#include <algorithm>

namespace lr {

namespace {

// Each of a selector's or options row's buttons.
template <typename Each> void eachButton(const Widget& n, Each each)
{
	for (int16_t b : n.buttons) {
		if (b >= 0) {
			each(b);
		}
	}
}

} // namespace

// The icon operations.

void Ui::recompute(WidgetTree& t, int16_t w)
{
	Widget& n = t[w];
	n.colour = n.kind == Widget::Root ? 0 : n.stateColours[n.visualState()];
	// A selector's carousel and items take its colour; an options row's track and thumb.
	if (n.kind == Widget::Selector && n.carousel >= 0) {
		Widget& c = t[n.carousel];
		c.colour = n.colour;
		for (int16_t i = c.first; i >= 0; i = t[i].next) {
			t[i].colour = n.colour;
		}
	}
	if (n.kind == Widget::Options && n.track >= 0) {
		t[n.track].colour = t[n.thumb].colour = n.colour;
	}
}

void Ui::enable(WidgetTree& t, int16_t w, bool on)
{
	Widget& n = t[w];
	if (on) {
		if (n.state & Widget::Enabled) {
			return;
		}
		n.state |= Widget::Enabled;
		recompute(t, w);
		eachButton(n, [&](int16_t b) { enable(t, b, true); });
		return;
	}
	if (!(n.state & Widget::Enabled)) {
		return;
	}
	// A selected icon hands the selection on first.
	// (A selector's arrow marked selected beside it hands nothing on.)
	const bool arrow = n.parent >= 0 && (t[n.parent].buttons[0] == w || t[n.parent].buttons[1] == w);
	if ((n.state & Widget::Selected) && !arrow && next(t, 0) < 0 && first(t, 0) < 0) {
		deselect(t, w, IconSilent);
	}
	t[w].state &= ~Widget::Enabled;
	recompute(t, w);
	eachButton(t[w], [&](int16_t b) { enable(t, b, false); });
}

namespace {

bool navigable(const Widget& n) { return (n.state & Widget::Enabled) && (n.flags & Widget::Visible); }

// The first icon walk finds among an icon's children (backwards when back; from the child `from` on, -1 every one); -1
// none.
template <typename Walk> int16_t walkChildren(const WidgetTree& t, int16_t icon, bool back, int16_t from, Walk walk)
{
	// (Back: each child's sibling before it, found from the first.)
	const auto before = [&](int16_t c) {
		int16_t b = -1;
		for (int16_t i = t[icon].iconFirst; i >= 0 && i != c; i = t[i].iconNext) {
			b = i;
		}
		return b;
	};
	bool on = from < 0;
	for (int16_t c = back ? before(-1) : t[icon].iconFirst; c >= 0; c = back ? before(c) : t[c].iconNext) {
		on |= c == from;
		if (const int16_t found = on ? walk(c) : int16_t(-1); found >= 0) {
			return found;
		}
	}
	return -1;
}

int16_t selectedLeaf(const WidgetTree& t)
{
	int16_t i = 0;
	while (t[i].selected >= 0) {
		i = t[i].selected;
	}
	return i;
}

} // namespace

bool Ui::select(WidgetTree& t, int16_t w, uint32_t how)
{
	const bool quiet = how & IconQuiet, local = how & IconLocal, silent = how & IconSilent;
	Widget& n = t[w];
	if (!(n.state & Widget::Enabled)) {
		return false;
	}
	if ((n.state & Widget::Selected) && n.selected < 0) {
		// A selected text field that lost the focus (a click went to the root) takes it back (hovering
		// the license's name field again refocuses it, quietly).
		if (n.kind == Widget::TextField && !(n.flags & Widget::Focused)) {
			focusOn(t, w);
		}
		return true;
	}
	if (!local) {
		const int16_t leaf = selectedLeaf(t);
		if (leaf != 0 && leaf != w) {
			deselect(t, leaf, IconLocal | IconSilent);
			// Clear the chain up from the old leaf to where it meets the new one's.
			for (int16_t p = t[leaf].iconParent; p > 0; p = t[p].iconParent) {
				bool ancestor = false;
				for (int16_t q = w; q >= 0; q = t[q].iconParent) {
					ancestor |= q == p;
				}
				if (ancestor) {
					break;
				}
				t[p].state &= ~Widget::Selected;
				t[p].selected = -1;
			}
		}
	}
	for (int16_t i = w; i >= 0; i = local ? int16_t(-1) : t[i].iconParent) {
		t[i].state |= Widget::Selected;
		if (i == w) {
			recompute(t, i); // QUIRK: only the icon's own visual state
			t[i].selected = -1;
		}
		if (!quiet) {
			m.notice(t, w, NoticeSelected);
		}
		const int16_t p = t[i].iconParent;
		if (p >= 0) {
			t[p].selected = i;
		}
	}
	eachButton(t[w],
		[&](int16_t b) { select(t, b, IconQuiet | IconLocal | IconSilent); }); // a selector's or options row's buttons follow it
	if (t[w].kind == Widget::TextField) {
		focusOn(t, w);
	}
	if (!silent) {
		m.play(t[w].sounds[0]);
	}
	return true;
}

void Ui::deselect(WidgetTree& t, int16_t w, uint32_t how)
{
	const bool local = how & IconLocal, silent = how & IconSilent;
	Widget& n = t[w];
	if (!(n.state & Widget::Enabled) || !(n.state & Widget::Selected)) {
		return;
	}
	if (n.state & Widget::Pressed) {
		release(t, w, IconQuiet); // unpressed: no notice, but its sound
	}
	// An icon's mark cleared: its look, a text field's focus, its buttons' marks.
	const auto clear = [&](int16_t i) {
		t[i].state &= ~Widget::Selected;
		recompute(t, i);
		if (t[i].kind == Widget::TextField) {
			unfocus(t, i);
		}
		eachButton(t[i], [&](int16_t b) { deselect(t, b, IconLocal | IconSilent); });
	};
	if (t[w].iconParent >= 0 && !local) {
		// The whole selected chain, from its leaf (whose sound plays) up.
		w = selectedLeaf(t);
		for (int16_t i = w; i > 0; i = t[i].iconParent) {
			t[i].selected = -1;
			clear(i);
		}
		t[0].selected = -1;
	}
	else {
		clear(w);
	}
	if (!silent) {
		m.play(t[w].sounds[1]);
	}
}

void Ui::focusOn(WidgetTree& t, int16_t w)
{
	// The leaf on the focus path loses it, then the path leads to w.
	int16_t leaf = t[0].focus;
	if (leaf >= 0) {
		while (t[leaf].focus >= 0) {
			leaf = t[leaf].focus;
		}
		unfocus(t, leaf);
	}
	t[w].flags |= Widget::Focused;
	for (int16_t c = w, p = t[w].parent; p >= 0; c = p, p = t[p].parent) {
		t[p].focus = c;
	}
}

void Ui::unfocus(WidgetTree& t, int16_t w)
{
	t[w].flags &= ~Widget::Focused;
	t[w].focus = -1;
	if (t[w].kind == Widget::TextField) {
		t[w].grabbed = false;
	}
	for (int16_t p = t[w].parent; p >= 0; p = t[p].parent) {
		t[p].focus = -1;
	}
}

void Ui::press(WidgetTree& t, int16_t w, uint32_t how)
{
	const bool local = how & IconLocal, quiet = how & IconQuiet, silent = how & IconSilent;
	if (t[w].state & Widget::Pressed) {
		return;
	}
	if (!local && t[w].iconParent >= 0) {
		press(t, t[w].iconParent, IconQuiet | IconSilent);
	}
	focusOn(t, w);
	t[w].state |= Widget::Pressed;
	recompute(t, w);
	if (!quiet) {
		m.notice(t, w, NoticePressed);
	}
	if (!silent) {
		m.play(t[w].sounds[2]);
	}
}

void Ui::release(WidgetTree& t, int16_t w, uint32_t how)
{
	const bool local = how & IconLocal, quiet = how & IconQuiet, silent = how & IconSilent;
	t[w].holding = 0;
	if (!(t[w].state & Widget::Pressed)) {
		return;
	}
	if (t[w].flags & Widget::Focused) {
		unfocus(t, w);
	}
	if (!local && t[w].iconParent >= 0) {
		release(t, t[w].iconParent, IconQuiet | IconSilent);
	}
	t[w].state &= ~Widget::Pressed;
	recompute(t, w);
	// QUIRK: a dialog's button tells the screen before its sound plays.
	const bool dialog = dialogCount && &t == &dialogs[dialogCount - 1].tree;
	if (!quiet && dialog) {
		m.notice(t, w, NoticeReleased);
	}
	if (!silent) {
		m.play(t[w].sounds[3]);
	}
	if (!quiet && !dialog) {
		m.notice(t, w, NoticeReleased);
	}
}

// Walking the selection.

int16_t Ui::first(WidgetTree& t, int16_t icon)
{
	if (!navigable(t[icon]) && icon != 0) {
		return -1;
	}
	const int16_t found = walkChildren(t, icon, false, -1, [&](int16_t c) { return first(t, c); });
	return found >= 0 || icon == 0 ? found : select(t, icon) ? icon : int16_t(-1);
}

int16_t Ui::last(WidgetTree& t, int16_t icon)
{
	if (!navigable(t[icon]) && icon != 0) {
		return -1;
	}
	const int16_t found = walkChildren(t, icon, true, -1,
		[&](int16_t c) { return first(t, c); }); // QUIRK: each child's first, not its last
	return found >= 0 || icon == 0 ? found : select(t, icon) ? icon : int16_t(-1);
}

// next and previous: on from a selected icon's selected child (none: the icon is let go), else into its children, then
// the icon itself.
int16_t Ui::next(WidgetTree& t, int16_t icon)
{
	if (icon != 0 && !navigable(t[icon])) {
		return -1;
	}
	const auto walk = [&](int16_t c) { return next(t, c); };
	if (const int16_t sel = t[icon].selected; t[icon].state & Widget::Selected) {
		if (sel < 0 && icon != 0) {
			deselect(t, icon, IconLocal | IconSilent);
		}
		return sel < 0 ? int16_t(-1) : walkChildren(t, icon, false, sel, walk);
	}
	const int16_t found = walkChildren(t, icon, false, -1, walk);
	return found >= 0 ? found : select(t, icon) ? icon : int16_t(-1);
}

int16_t Ui::previous(WidgetTree& t, int16_t icon)
{
	if (icon != 0 && !navigable(t[icon])) {
		return -1;
	}
	const auto walk = [&](int16_t c) { return previous(t, c); };
	if (const int16_t sel = t[icon].selected; t[icon].state & Widget::Selected) {
		if (sel < 0 && icon != 0) {
			deselect(t, icon, IconLocal | IconSilent);
		}
		return sel < 0 ? int16_t(-1) : walkChildren(t, icon, true, sel, walk);
	}
	const int16_t found = walkChildren(t, icon, true, -1, walk);
	return found >= 0 ? found : select(t, icon) ? icon : int16_t(-1);
}

void Ui::moveSelection(WidgetTree& t, bool forward)
{
	if ((forward ? next(t, 0) : previous(t, 0)) < 0) {
		forward ? first(t, 0) : last(t, 0);
	}
}

MenuRect Ui::global(const WidgetTree& t, int16_t w) const
{
	MenuRect r = t[w].rect;
	for (int16_t p = t[w].parent; p >= 0; p = t[p].parent) {
		r.left += t[p].rect.left;
		r.top += t[p].rect.top;
		r.right += t[p].rect.left;
		r.bottom += t[p].rect.top;
	}
	return r;
}

} // namespace lr
