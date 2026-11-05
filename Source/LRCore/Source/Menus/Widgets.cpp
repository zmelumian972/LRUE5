// The widgets: making them from a binding and a style, the icon tree's selection and pressing, keys and the cursor
// on each kind, and their timers.

#include "LRCore/Menus/Menus.h"

#include "Menus/Layout.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

constexpr uint32_t White = 0xffffffff;
constexpr int32_t EdgeId = 1024, FillId = 666;

bool isMouse(uint32_t key) { return (key >> 28) == 2; }

// The style a binding names: the screen's table, then the global one.
const MenuStyles::Style* findStyle(const MenuData& data, uint16_t screen, std::string_view name)
{
	const MenuStyles::Style* s = screen < data.screens.size() ? data.screens[screen].styles.find(name) : nullptr;
	return s ? s : data.styles.find(name);
}

} // namespace

int16_t WidgetTree::add(const Widget& w, int16_t parent)
{
	const int16_t i = int16_t(widgets.size());
	widgets.push_back(w);
	Widget& n = widgets.back();
	n.parent = parent;
	if (parent >= 0) {
		Widget& p = widgets[size_t(parent)];
		if (p.last >= 0) {
			widgets[size_t(p.last)].next = i;
		}
		else {
			p.first = i;
		}
		p.last = i;
	}
	return i;
}

void WidgetTree::adopt(int16_t w, int16_t parent, int16_t after)
{
	Widget& p = widgets[size_t(parent)];
	Widget& n = widgets[size_t(w)];
	n.parent = parent;
	if (after >= 0) {
		n.next = widgets[size_t(after)].next;
		widgets[size_t(after)].next = w;
		p.last = p.last == after ? w : p.last;
		return;
	}
	n.next = -1;
	if (p.last >= 0) {
		widgets[size_t(p.last)].next = w;
	}
	else {
		p.first = w;
	}
	p.last = w;
}

void WidgetTree::detach(int16_t w)
{
	Widget& n = widgets[size_t(w)];
	Widget& p = widgets[size_t(n.parent)];
	int16_t before = -1;
	for (int16_t c = p.first; c >= 0 && c != w; c = widgets[size_t(c)].next) {
		before = c;
	}
	(before >= 0 ? widgets[size_t(before)].next : p.first) = n.next;
	p.last = p.last == w ? before : p.last;
	n.parent = n.next = -1;
}

int16_t WidgetTree::find(int32_t id) const
{
	for (size_t i = 0; i < widgets.size(); i++) {
		if (widgets[i].id == id && widgets[i].parent >= 0) {
			return int16_t(i);
		}
	}
	return -1;
}

int16_t WidgetTree::focused() const
{
	int16_t leaf = widgets[0].flags & Widget::Focused ? int16_t(0) : int16_t(-1);
	for (int16_t f = widgets[0].focus; f >= 0; f = widgets[size_t(f)].focus) {
		leaf = f;
	}
	return leaf;
}

int16_t Menus::makeRoot(WidgetTree& t)
{
	t.widgets.clear();
	Widget root;
	root.kind = Widget::Root;
	root.id = 1;
	root.rect = {0, 0, 640, 480};
	root.colour = 0;
	root.icon = true;
	root.state = Widget::Enabled | Widget::Selected;
	return t.add(root, -1);
}

namespace {

// Joins an icon to its icon parent's children (last).
void attachIcon(WidgetTree& t, int16_t w, int16_t parent)
{
	Widget& n = t[w];
	n.iconParent = parent;
	Widget& p = t[parent];
	if (p.iconLast >= 0) {
		t[p.iconLast].iconNext = w;
	}
	else {
		p.iconFirst = w;
	}
	p.iconLast = w;
}

} // namespace

int16_t Menus::makeNamed(WidgetTree& t, uint32_t binding, uint32_t style, int16_t parent, int32_t text)
{
	return make(t, data->screens[screen].layout, data->names.narrow(binding), data->names.narrow(style), parent, text);
}

namespace {

// A widget's rect sized by its image where the binding leaves it zero.
void sizeFromImage(const MenuData& data, Widget& n, int16_t image)
{
	if (image < 0) {
		return;
	}
	const MenuData::ImageInfo& info = data.images[size_t(image)];
	if (n.rect.right == 0) {
		n.rect.right = n.rect.left + int32_t(info.width);
	}
	if (n.rect.bottom == 0) {
		n.rect.bottom = n.rect.top + int32_t(info.height);
	}
}

// An icon's sounds and state colours: the binding's when it has them, else the style's. (The styles' state rects, which
// would move an icon as its state changes, are all zero: not kept.)
void iconFrom(Widget& n, const MenuLayout::Binding& b, const MenuIcon& styleIcon)
{
	n.icon = true;
	n.sounds = b.icon.hasSounds ? b.icon.sounds : styleIcon.sounds;
	n.stateColours = b.icon.hasColours ? b.icon.colours : styleIcon.colours;
}

} // namespace

int16_t Menus::makeImage(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, bool styleColour)
{
	w.kind = st.kind == MenuStyles::Carousel ? Widget::Carousel : Widget::Image;
	if (w.kind == Widget::Carousel) {
		w.scrollMs = st.scrollMs;
		w.pageMode = st.pageMode;
		w.wrapAround = st.wrapAround;
		w.carouselSounds = st.sounds;
		w.colour = b.colour;
		return t.add(w, attachTo);
	}
	// QUIRK: with the style's colour the style's image wins over the binding's own.
	w.image = !b.images.empty() && !styleColour ? data->image(b.images[0], screen) : st.images.empty() ? int16_t(-1) : data->image(st.images[0], screen);
	if (w.image < 0 && !b.images.empty()) {
		w.image = data->image(b.images[0], screen);
	}
	sizeFromImage(*data, w, w.image);
	return t.add(w, attachTo);
}

int16_t Menus::makeLabel(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, int32_t text)
{
	w.kind = Widget::Label;
	w.font = data->font(language, !b.fonts.empty() && !b.fonts[0].empty() ? b.fonts[0] : st.font);
	w.wrap = b.wrap ? b.wrap : st.wrap;
	w.centred = st.centred;
	const int16_t made = t.add(w, attachTo);
	if (text >= 0) {
		setString(t, made, text);
	}
	return made;
}

int16_t Menus::makeFrame(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::Frame;
	std::array<int16_t, 8> edges = {};
	for (size_t i = 0; i < 8; i++) {
		const std::string& name = i < b.images.size() && !b.images[i].empty() ? b.images[i] : i < st.images.size() ? st.images[i] : std::string();
		edges[i] = data->image(name, screen);
	}
	const bool fromStyle = !b.hasColour && st.colours.size() >= 2;
	const uint32_t edge = fromStyle ? st.colours[0] : b.colours.size() >= 1 ? b.colours[0] : White;
	const uint32_t fill = fromStyle ? st.colours[1] : b.colours.size() >= 2 ? b.colours[1] : White;
	w.colour = edge;
	const int16_t made = t.add(w, attachTo);
	Widget f;
	f.kind = Widget::Quad;
	f.id = FillId;
	f.colour = fill;
	if (!b.hasFill) {
		f.flags &= ~Widget::DrawsItself;
	}
	t.add(f, made);
	for (size_t i = 0; i < 8; i++) {
		Widget e;
		e.kind = Widget::Image;
		e.id = EdgeId;
		e.image = edges[i];
		e.colour = edge;
		t.add(e, made);
	}
	return made;
}

int16_t Menus::makeImageButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::ImageButton;
	iconFrom(w, b, st.icon);
	for (size_t i = 0; i < 6; i++) {
		const std::string& name = i < b.images.size() && !b.images[i].empty() ? b.images[i] : i < st.images.size() ? st.images[i] : std::string();
		w.images[i] = data->image(name, screen);
	}
	sizeFromImage(*data, w, w.images[0]);
	const int16_t made = t.add(w, attachTo);
	// Its image: the button's rect and its first state's look until it is drawn.
	Widget c;
	c.kind = Widget::Image;
	c.id = w.id;
	c.rect = w.rect;
	c.image = w.images[0];
	c.colour = w.stateColours[0];
	t[made].child = t.add(c, made);
	return made;
}

int16_t Menus::makeTextButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, int32_t text)
{
	w.kind = Widget::TextButton;
	iconFrom(w, b, st.icon);
	const std::vector<uint32_t>& colours = b.colours.size() >= 13 ? b.colours : st.colours;
	for (size_t i = 0; i < 6; i++) {
		const std::string& image = i < b.images.size() && !b.images[i].empty() ? b.images[i] : st.images.size() > i ? st.images[i] : std::string();
		const std::string& font = i < b.fonts.size() && !b.fonts[i].empty() ? b.fonts[i] : st.fonts[i];
		w.images[i] = data->image(image, screen);
		w.fonts[i] = data->font(language, font);
		if (colours.size() >= 13) {
			w.imageColours[i] = colours[1 + i];
			if (!b.icon.hasColours) {
				w.stateColours[i] = colours[7 + i];
			}
		}
	}
	w.maxWidth = b.wrap ? b.wrap : st.maxWidth;
	const int16_t made = t.add(w, attachTo);
	Widget image;
	image.kind = Widget::Image;
	image.id = w.id;
	image.image = w.images[0];
	sizeFromImage(*data, image, image.image);
	image.colour = w.imageColours[0];
	const int16_t c = t.add(image, made);
	Widget label;
	label.kind = Widget::Label;
	label.id = w.id;
	label.font = w.fonts[0];
	label.colour = w.stateColours[0];
	const int16_t l = t.add(label, made);
	t[made].child = c;
	t[made].label = l;
	setString(t, made, text);
	return made;
}

int16_t Menus::makeSelector(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::Selector;
	iconFrom(w, b, st.icon);
	w.unselectedInput = b.unselectedInput;
	for (size_t i = 0; i < w.itemColours.size() && i < st.colours.size(); i++) {
		w.itemColours[i] = st.colours[i];
	}
	const int16_t made = t.add(w, attachTo);
	for (size_t i = 0; i < 2; i++) {
		const int16_t button = make(t, layout, b.buttons[i], st.buttons[i], made);
		t[made].buttons[i] = button;
	}
	return made;
}

int16_t Menus::makeOptions(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::Options;
	iconFrom(w, b, st.icon);
	w.steps = b.steps;
	w.index = b.value;
	w.stepSounds = b.hasEditSounds ? std::array<int32_t, 2>{b.editSounds[0], b.editSounds[1]} : std::array<int32_t, 2>{st.sounds[0], st.sounds[1]};
	const int16_t made = t.add(w, attachTo);
	for (size_t i = 0; i < 2; i++) {
		const int16_t button = make(t, layout, b.buttons[i], st.buttons[i], made);
		t[made].buttons[i] = button;
	}
	for (size_t i = 0; i < 2; i++) {
		const int16_t image = make(t, layout, b.track[i], st.track[i], made);
		(i ? t[made].thumb : t[made].track) = image;
	}
	setValue(t, made, w.index);
	return made;
}

int16_t Menus::makeSceneView(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, int16_t parent, int16_t attachTo)
{
	w.kind = Widget::SceneView;
	if (!b.frame.empty()) {
		// A framed view: the frame at the view's rect, the view in the frame's fill.
		const int16_t frame = make(t, layout, b.frame, "brickbox", parent);
		if (frame >= 0) {
			t[frame].rect = w.rect;
			layoutFrame(t, frame);
			const int16_t fill = t[frame].first;
			w.rect = {0, 0, t[fill].rect.width(), t[fill].rect.height()};
			return t.add(w, fill);
		}
	}
	if (w.rect.right == 0 && w.rect.bottom == 0) {
		w.rect.right = t[parent].rect.width();
		w.rect.bottom = t[parent].rect.height();
	}
	return t.add(w, attachTo);
}

// Its scroll and sounds are its carousel style's.
int16_t Menus::makeRing(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::Ring;
	w.scrollMs = st.scrollMs;
	w.carouselSounds = st.sounds;
	w.slots = b.numbers[0];
	w.focusSlot = b.numbers[1];
	w.slotRects = b.slots;
	w.spinRate = b.step;
	w.colour = b.colour;
	return t.add(w, attachTo);
}

// An image button, its highlight image and the style's regions.
int16_t Menus::makeHotspot(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::Hotspot;
	iconFrom(w, b, st.icon);
	for (size_t i = 0; i < 6; i++) {
		w.images[i] = i < b.images.size() ? data->image(b.images[i], screen) : -1;
	}
	w.image = data->image(!b.world.empty() ? b.world : st.images.empty() ? std::string() : st.images[0], screen);
	w.regions = st.regions;
	w.regionRects = st.regionRects;
	sizeFromImage(*data, w, w.images[0]);
	const int16_t made = t.add(w, attachTo);
	Widget c;
	c.kind = Widget::Image;
	c.id = w.id;
	c.rect = {0, 0, w.rect.width(), w.rect.height()};
	c.image = w.images[0];
	c.colour = w.stateColours[0];
	t[made].child = t.add(c, made);
	return made;
}

// The caret comes after it, under the same parent: a white box that blinks (sized by the field's characters).
int16_t Menus::makeTextField(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::TextField;
	iconFrom(w, b, st.icon);
	w.font = data->font(language, !b.fonts.empty() && !b.fonts[0].empty() ? b.fonts[0] : st.font);
	w.maxLength = b.maxLength;
	w.editSounds = b.editSounds;
	const int16_t made = t.add(w, attachTo);
	Widget caret;
	caret.kind = Widget::Quad;
	caret.id = EdgeId;
	caret.colour = White;
	caret.blinkOn = caret.blinkOff = 1000;
	t[made].caret = t.add(caret, attachTo);
	return made;
}

int16_t Menus::make(WidgetTree& t, const MenuLayout& layout, std::string_view bindingName, std::string_view styleName, int16_t parent, int32_t text)
{
	const MenuLayout::Binding* b = layout.find(bindingName);
	const MenuStyles::Style* st = findStyle(*data, screen, styleName);
	// A scene view or a cinematic's region needs no style.
	static const MenuStyles::Style None;
	if (b && !st && (b->kind == MenuLayout::SceneView || b->kind == MenuLayout::SceneReference || b->kind == MenuLayout::Region)) {
		st = &None;
	}
	if (!b || !st) {
		return -1;
	}
	using L = MenuLayout;
	Widget w;
	w.id = b->id;
	w.rect = b->rect;
	const bool styleColour = !b->hasColour && !st->colours.empty() && (st->kind == MenuStyles::Image || st->kind == MenuStyles::Text);
	w.colour = styleColour ? st->colours[0] : b->colour;
	// The parent: the given one, else the binding's (made already), else the root.
	if (parent < 0 && !b->parent.empty()) {
		const MenuLayout::Binding* pb = layout.find(b->parent);
		parent = pb ? t.find(pb->id) : -1;
	}
	if (parent < 0) {
		parent = 0;
	}
	const int16_t attachTo = b->attach ? parent : int16_t(-1);
	int16_t made = -1;
	switch (b->kind) {
	case L::Image: made = makeImage(t, w, *b, *st, attachTo, styleColour); break;
	case L::Label: made = makeLabel(t, w, *b, *st, attachTo, text); break;
	case L::Frame: made = makeFrame(t, w, *b, *st, attachTo); break;
	case L::ImageButton: made = makeImageButton(t, w, *b, *st, attachTo); break;
	case L::TextButton: made = makeTextButton(t, w, *b, *st, attachTo, text); break;
	case L::Selector: made = makeSelector(t, layout, w, *b, *st, attachTo); break;
	case L::Options: made = makeOptions(t, layout, w, *b, *st, attachTo); break;
	case L::SceneView: made = makeSceneView(t, layout, w, *b, parent, attachTo); break;
	case L::SceneReference:
		w.kind = Widget::SceneReference;
		made = t.add(w, attachTo);
		break;
	case L::Ring: made = makeRing(t, w, *b, *st, attachTo); break;
	case L::Hotspot: made = makeHotspot(t, w, *b, *st, attachTo); break;
	case L::TextField: made = makeTextField(t, w, *b, *st, attachTo); break;
	default:
		w.kind = Widget::Region;
		made = t.add(w, attachTo);
		break;
	}
	if (made < 0) {
		return -1;
	}
	Widget& n = t[made];
	if (n.kind == Widget::Frame) {
		layoutFrame(t, made);
	}
	if (n.icon) {
		if (b->iconAttach && attachTo >= 0) {
			int16_t ip = attachTo;
			while (ip > 0 && !t[ip].icon) {
				ip = t[ip].parent;
			}
			attachIcon(t, made, ip < 0 ? 0 : ip);
		}
		recompute(t, made);
		if (b->enabled) {
			enable(t, made, true);
		}
	}
	return made;
}

void Menus::layoutFrame(WidgetTree& t, int16_t frame)
{
	Widget& f = t[frame];
	const int32_t W = f.rect.width(), H = f.rect.height();
	std::array<int16_t, 9> piece = {};
	int16_t c = f.first;
	for (size_t i = 0; i < 9 && c >= 0; i++, c = t[c].next) {
		piece[i] = c;
	}
	std::array<int32_t, 8> w = {}, h = {};
	for (size_t i = 0; i < 8; i++) {
		const int16_t image = t[piece[i + 1]].image;
		if (image >= 0) {
			w[i] = int32_t(data->images[size_t(image)].width);
			h[i] = int32_t(data->images[size_t(image)].height);
		}
	}
	const auto set = [&](size_t i, MenuRect r) { t[piece[i + 1]].rect = r; };
	set(0, {0, 0, w[0], h[0]});
	set(1, {w[0], 0, W - w[2], h[1]});
	set(2, {W - w[2], 0, W, h[0]}); // QUIRK: image 0's height
	set(3, {W - w[3], h[0], W, H - h[4]});
	set(4, {W - w[4], H - h[4], W, H});
	set(5, {w[6], H - h[5], W - w[4], H});
	set(6, {0, H - h[6], w[6], H});
	set(7, {0, t[piece[1]].rect.bottom, w[7], H - h[6]});
	t[piece[0]].rect = {t[piece[1]].rect.right, t[piece[1]].rect.bottom, t[piece[3]].rect.left, t[piece[7]].rect.top};
}

void Menus::setString(WidgetTree& t, int16_t w, int32_t index) { setText(t, w, index >= 0 ? text(index) : std::u16string_view()); }

void Menus::relabel(WidgetTree& t, int16_t w, int32_t index)
{
	Widget& n = t[w];
	n.rect.right = n.rect.left;
	n.rect.bottom = n.rect.top;
	setString(t, w, index);
}

void Menus::setText(WidgetTree& t, int16_t w, std::u16string_view s)
{
	Widget& n = t[w];
	if (n.kind == Widget::TextButton) {
		setText(t, n.label, s);
		// The label beside the image, its width capped; the button as tall as the image.
		const Widget& image = t[n.child];
		Widget& label = t[n.label];
		MenuRect text = measure(label.font, label.text, 0);
		int32_t tw = text.width();
		if (t[w].maxWidth > 0 && tw > t[w].maxWidth) {
			tw = t[w].maxWidth;
		}
		MenuRect r = {0, 0, tw, text.height()};
		centre({0, 0, tw, image.rect.height()}, r);
		r.left += image.rect.right;
		r.right += image.rect.right;
		label.rect = r;
		Widget& b = t[w];
		b.rect = {b.rect.left, b.rect.top, b.rect.left + r.right, b.rect.top + image.rect.height()};
		return;
	}
	// Upper case: a unit's low byte, a..z and 224 up moved down by 32. QUIRK: the high byte is dropped.
	n.text.clear();
	for (char16_t u : s) {
		uint32_t c = u & 255;
		if (c >= 224 || (c >= 'a' && c <= 'z')) {
			c -= 32;
		}
		n.text += char16_t(c);
	}
	if (n.kind != Widget::Label) {
		return;
	}
	if (n.wrapping) {
		// The dialog's message: one line when it fits the rect, else wrapped at the wrap width and measured.
		const MenuRect m = measure(n.font, n.text, n.wrap);
		n.rect = {n.rect.left, n.rect.top, n.rect.left + m.width(), n.rect.top + m.height()};
		return;
	}
	if (n.rect.right == 0 || n.rect.bottom == 0 || (n.rect.right == n.rect.left && n.rect.bottom == n.rect.top)) {
		const MenuRect m = measure(n.font, n.text, 0);
		n.rect.right = n.rect.left + m.width();
		n.rect.bottom = n.rect.top + m.height();
	}
}

void Menus::alignRight(WidgetTree& t, int16_t w, int32_t margin)
{
	Widget& n = t[w];
	const int32_t wd = n.rect.width();
	n.rect.right = t[n.parent].rect.width() - margin;
	n.rect.left = n.rect.right - wd;
}

void Menus::addItem(WidgetTree& t, int16_t carousel, int16_t item)
{
	Widget& it = t[item];
	it.parent = -1;
	it.next = -1;
	// Re-linked under the carousel.
	Widget& c = t[carousel];
	if (c.last >= 0) {
		t[c.last].next = item;
	}
	else {
		c.first = item;
	}
	c.last = item;
	t[item].parent = carousel;
	t[item].colour = t[carousel].colour;
	layoutCarousel(t, carousel);
}

void Menus::layoutCarousel(WidgetTree& t, int16_t carousel)
{
	Widget& c = t[carousel];
	const int32_t cw = c.rect.width();
	int32_t x = 0, n = 0, at = 0;
	for (int16_t i = c.first; i >= 0; i = t[i].next, n++) {
		Widget& it = t[i];
		const int32_t iw = it.rect.width();
		if (c.pageMode && cw > 0) {
			const int32_t slot = (x + cw - 1) / cw * cw;
			MenuRect r = {it.rect.left, it.rect.top, it.rect.right, it.rect.bottom};
			centre({slot, it.rect.top, slot + cw, it.rect.bottom}, r);
			it.rect = r;
			x = slot + cw;
		}
		else {
			it.rect = {x, it.rect.top, x + iw, it.rect.bottom};
			x += iw;
		}
		if (n == c.index) {
			at = it.rect.left;
		}
	}
	if (c.scrollLeft == 0 && !c.scrolled) {
		c.offsetX = c.pageMode && cw > 0 ? at / cw * cw : at;
		c.scrollAt = float(c.offsetX);
	}
}

void Menus::selectItem(WidgetTree& t, int16_t carousel, int32_t index)
{
	t[carousel].index = index;
	layoutCarousel(t, carousel);
}

void Menus::setValue(WidgetTree& t, int16_t row, int32_t value)
{
	Widget& r = t[row];
	r.index = value;
	const MenuRect prev = t[r.buttons[0]].rect, next = t[r.buttons[1]].rect;
	const int32_t W = r.rect.width(), H = r.rect.height();
	const int32_t bw0 = prev.width(), bw1 = next.width();
	t[r.buttons[0]].rect = {0, 0, bw0, H};
	t[r.buttons[1]].rect = {W - bw1, 0, W, H};
	const int32_t prevRight = bw0, nextLeft = W - bw1;
	Widget& track = t[r.track];
	const int32_t th = track.rect.height();
	track.rect = {prevRight + 1, (H >> 1) - (th >> 1), nextLeft - 1, (H >> 1) - (th >> 1) + th};
	Widget& thumb = t[r.thumb];
	const int32_t uw = thumb.rect.width(), uh = thumb.rect.height();
	const int32_t minX = prevRight + (uw >> 1), maxX = nextLeft - (uw >> 1);
	float step = float(maxX - minX);
	if (r.steps > 2) {
		step /= float(r.steps - 1);
	}
	const int32_t left = minX + int32_t(double(step) * value) - (uw >> 1);
	thumb.rect = {left, (H >> 1) - (uh >> 1), left + uw, (H >> 1) - (uh >> 1) + uh};
}

void Menus::show(WidgetTree& t, int16_t w, bool visible)
{
	if (w < 0) {
		return;
	}
	t[w].flags = visible ? t[w].flags | Widget::Visible : t[w].flags & ~Widget::Visible;
}

// The icon operations.

void Menus::recompute(WidgetTree& t, int16_t w)
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

void Menus::enable(WidgetTree& t, int16_t w, bool on)
{
	Widget& n = t[w];
	if (on) {
		if (n.state & Widget::Enabled) {
			return;
		}
		n.state |= Widget::Enabled;
		recompute(t, w);
		for (int16_t b : n.buttons) {
			if (b >= 0) {
				enable(t, b, true);
			}
		}
		return;
	}
	if (!(n.state & Widget::Enabled)) {
		return;
	}
	// A selected icon hands the selection on first.
	// (A selector's arrow marked selected beside it hands nothing on.)
	const bool arrow = n.parent >= 0 && (t[n.parent].buttons[0] == w || t[n.parent].buttons[1] == w);
	if ((n.state & Widget::Selected) && !arrow && next(t, 0) < 0 && first(t, 0) < 0) {
		deselect(t, w, false, true);
	}
	t[w].state &= ~Widget::Enabled;
	recompute(t, w);
	for (int16_t b : t[w].buttons) {
		if (b >= 0) {
			enable(t, b, false);
		}
	}
}

namespace {

bool navigable(const Widget& n) { return (n.state & Widget::Enabled) && (n.flags & Widget::Visible); }

int16_t selectedLeaf(const WidgetTree& t)
{
	int16_t i = 0;
	while (t[i].selected >= 0) {
		i = t[i].selected;
	}
	return i;
}

} // namespace

bool Menus::select(WidgetTree& t, int16_t w, bool quiet, bool local, bool silent)
{
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
			deselect(t, leaf, true, true);
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
			notice(*game, t, w, NoticeSelected);
		}
		const int16_t p = t[i].iconParent;
		if (p >= 0) {
			t[p].selected = i;
		}
	}
	for (int16_t b : t[w].buttons) {
		if (b >= 0) {
			select(t, b, true, true, true); // a selector's or options row's buttons follow it
		}
	}
	if (t[w].kind == Widget::TextField) {
		focusOn(t, w);
	}
	if (!silent) {
		play(t[w].sounds[0]);
	}
	return true;
}

void Menus::deselect(WidgetTree& t, int16_t w, bool local, bool silent)
{
	Widget& n = t[w];
	if (!(n.state & Widget::Enabled) || !(n.state & Widget::Selected)) {
		return;
	}
	if (n.state & Widget::Pressed) {
		release(t, w, false, true, false); // unpressed: no notice, but its sound
	}
	if (t[w].iconParent >= 0 && !local) {
		const int16_t leaf = selectedLeaf(t);
		for (int16_t i = leaf; i > 0; i = t[i].iconParent) {
			t[i].state &= ~Widget::Selected;
			t[i].selected = -1;
			recompute(t, i);
			if (t[i].kind == Widget::TextField) {
				unfocus(t, i);
			}
			for (int16_t b : t[i].buttons) {
				if (b >= 0) {
					deselect(t, b, true, true);
				}
			}
		}
		t[0].selected = -1;
		if (!silent) {
			play(t[leaf].sounds[1]);
		}
		return;
	}
	t[w].state &= ~Widget::Selected;
	recompute(t, w);
	if (t[w].kind == Widget::TextField) {
		unfocus(t, w);
	}
	for (int16_t b : t[w].buttons) {
		if (b >= 0) {
			deselect(t, b, true, true);
		}
	}
	if (!silent) {
		play(t[w].sounds[1]);
	}
}

void Menus::setCharset(WidgetTree& t, int16_t field, int32_t textIndex)
{
	// The caret is as wide as the widest of the characters, 4 high.
	Widget& f = t[field];
	f.charset = textIndex;
	int32_t widest = 0;
	for (char16_t c : text(textIndex)) {
		widest = std::max(widest, lineWidth(f.font, std::u16string_view(&c, 1)));
	}
	t[f.caret].rect = {0, 0, widest, 4};
}

void Menus::focusOn(WidgetTree& t, int16_t w)
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

void Menus::unfocus(WidgetTree& t, int16_t w)
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

void Menus::press(WidgetTree& t, int16_t w, bool local, bool quiet, bool silent)
{
	if (t[w].state & Widget::Pressed) {
		return;
	}
	if (!local && t[w].iconParent >= 0) {
		press(t, t[w].iconParent, false, true, true);
	}
	focusOn(t, w);
	t[w].state |= Widget::Pressed;
	recompute(t, w);
	if (!quiet) {
		notice(*game, t, w, NoticePressed);
	}
	if (!silent) {
		play(t[w].sounds[2]);
	}
}

void Menus::release(WidgetTree& t, int16_t w, bool local, bool quiet, bool silent)
{
	t[w].holding = 0;
	if (!(t[w].state & Widget::Pressed)) {
		return;
	}
	if (t[w].flags & Widget::Focused) {
		unfocus(t, w);
	}
	if (!local && t[w].iconParent >= 0) {
		release(t, t[w].iconParent, false, true, true);
	}
	t[w].state &= ~Widget::Pressed;
	recompute(t, w);
	// QUIRK: a dialog's button tells the screen before its sound plays.
	const bool dialog = dialogCount && &t == &dialogs[dialogCount - 1].tree;
	if (!quiet && dialog) {
		notice(*game, t, w, NoticeReleased);
	}
	if (!silent) {
		play(t[w].sounds[3]);
	}
	if (!quiet && !dialog) {
		notice(*game, t, w, NoticeReleased);
	}
}

// Walking the selection.

int16_t Menus::first(WidgetTree& t, int16_t icon)
{
	if (!navigable(t[icon]) && icon != 0) {
		return -1;
	}
	for (int16_t c = t[icon].iconFirst; c >= 0; c = t[c].iconNext) {
		const int16_t found = first(t, c);
		if (found >= 0) {
			return found;
		}
	}
	if (icon == 0) {
		return -1;
	}
	return select(t, icon, false, false, false) ? icon : int16_t(-1);
}

int16_t Menus::last(WidgetTree& t, int16_t icon)
{
	if (!navigable(t[icon]) && icon != 0) {
		return -1;
	}
	std::vector<int16_t> children;
	for (int16_t c = t[icon].iconFirst; c >= 0; c = t[c].iconNext) {
		children.push_back(c);
	}
	for (auto it = children.rbegin(); it != children.rend(); ++it) {
		const int16_t found = first(t, *it); // QUIRK: each child's first, not its last
		if (found >= 0) {
			return found;
		}
	}
	if (icon == 0) {
		return -1;
	}
	return select(t, icon, false, false, false) ? icon : int16_t(-1);
}

int16_t Menus::next(WidgetTree& t, int16_t icon)
{
	if (icon != 0 && !navigable(t[icon])) {
		return -1;
	}
	if (t[icon].state & Widget::Selected) {
		int16_t c = t[icon].selected;
		if (c < 0) {
			if (icon != 0) {
				deselect(t, icon, true, true);
			}
			return -1;
		}
		for (; c >= 0; c = t[c].iconNext) {
			const int16_t found = next(t, c);
			if (found >= 0) {
				return found;
			}
		}
		return -1;
	}
	for (int16_t c = t[icon].iconFirst; c >= 0; c = t[c].iconNext) {
		const int16_t found = next(t, c);
		if (found >= 0) {
			return found;
		}
	}
	return select(t, icon, false, false, false) ? icon : int16_t(-1);
}

int16_t Menus::previous(WidgetTree& t, int16_t icon)
{
	if (icon != 0 && !navigable(t[icon])) {
		return -1;
	}
	std::vector<int16_t> children;
	for (int16_t c = t[icon].iconFirst; c >= 0; c = t[c].iconNext) {
		children.push_back(c);
	}
	if (t[icon].state & Widget::Selected) {
		const int16_t sel = t[icon].selected;
		if (sel < 0) {
			if (icon != 0) {
				deselect(t, icon, true, true);
			}
			return -1;
		}
		bool from = false;
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			from |= *it == sel;
			if (!from) {
				continue;
			}
			const int16_t found = previous(t, *it);
			if (found >= 0) {
				return found;
			}
		}
		return -1;
	}
	for (auto it = children.rbegin(); it != children.rend(); ++it) {
		const int16_t found = previous(t, *it);
		if (found >= 0) {
			return found;
		}
	}
	return select(t, icon, false, false, false) ? icon : int16_t(-1);
}

void Menus::moveSelection(WidgetTree& t, bool forward)
{
	if ((forward ? next(t, 0) : previous(t, 0)) < 0) {
		forward ? first(t, 0) : last(t, 0);
	}
}

MenuRect Menus::global(const WidgetTree& t, int16_t w) const
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

namespace {

bool hits(const Widget& n, int32_t x, int32_t y) { return x >= 0 && x <= n.rect.width() && y >= 0 && y <= n.rect.height(); }

} // namespace

// Keys on one widget (the point in its own space).

// A selector or an options row: its arrows (a click on one, or the keys) step it; a press on the thumb grabs it.
bool Menus::stepperKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	Widget& n = t[w];
	const uint32_t k = key.key;
	if (!(n.state & Widget::Enabled) || (n.holding && n.holding != k) || (!(n.state & Widget::Pressed) && key.repeat)) {
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
				play(n.stepSounds[0]);
				n.grabbed = true;
				press(t, w, false, true, true);
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
		press(t, button, true, true, false);
		n.kind == Widget::Selector ? stepSelector(t, w, forward) : stepRow(t, w, forward);
		press(t, w, false, false, false);
	}
	t[w].holding = k;
	return true;
}

bool Menus::ringKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
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
		if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
			const int32_t to = ((int32_t(slot) + n.index - n.focusSlot) % count + count) % count;
			if (to != n.index && !key.repeat) {
				n.index = to;
				if (n.parent >= 0 && t[n.parent].kind == Widget::Selector) {
					notice(*game, t, n.parent, NoticeValue);
				}
			}
			break;
		}
	}
	return true;
}

bool Menus::textFieldKeyDown(WidgetTree& t, int16_t w, const MenuKey& key)
{
	Widget& n = t[w];
	const uint32_t k = key.key;
	// A typed character: Backspace erases; one the field doesn't have is refused and goes on; past the length refused.
	if (!(n.state & Widget::Enabled) || !(n.state & Widget::Selected) || (k >> 28) != (SourceCharacter >> 28)) {
		return false;
	}
	char16_t c = char16_t(k & 0xffff);
	c = char16_t(c >= 'a' && c <= 'z' ? c - 32 : c > 0xe0 && c <= 0xff ? c - 32 : c);
	if (c == 8) {
		play(n.editSounds[n.text.empty() ? 3 : 2]);
		if (!n.text.empty()) {
			n.text.pop_back();
			notice(*game, t, w, NoticeValue);
		}
		return true;
	}
	if (text(n.charset).find(c) == std::u16string_view::npos) {
		play(n.editSounds[3]);
		return false;
	}
	if (int32_t(n.text.size()) >= n.maxLength) {
		play(n.editSounds[3]);
		return true;
	}
	n.text += c;
	play(n.editSounds[0]);
	notice(*game, t, w, NoticeValue);
	return true;
}

bool Menus::hotspotKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
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
			release(t, w, false, false, false);
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
				notice(*game, t, w, NoticeValue);
			}
			return true;
		}
		for (size_t i = 0; i < n.regions.size() && !n.lit; i++) {
			const MenuRect& r = n.regionRects[i];
			if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
				n.lit = n.regions[i];
				notice(*game, t, w, NoticeValue);
			}
		}
		t[w].holding = k; // (its release clears the region)
		return true;
	}
	if (key.repeat) {
		return false;
	}
	if (!activate) {
		return false;
	}
	press(t, w, false, false, false);
	t[w].holding = k;
	return true;
}

bool Menus::keyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
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
		if (!(n.state & Widget::Enabled) || (n.holding && n.holding != k) || (!(n.state & Widget::Pressed) && key.repeat)) {
			return false;
		}
		const bool acts = (n.state & Widget::Selected) && (k == MenuEnter || k == MenuSpace || k == MenuPadButton || (isMouse(k) && hits(n, x, y)));
		if (!acts) {
			return false;
		}
		press(t, w, false, false, false);
		t[w].holding = k;
		return true;
	}
	default: return false;
	}
}

bool Menus::keyUp(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
{
	Widget& n = t[w];
	if (!n.icon || !n.holding || n.holding != key.key) {
		return false;
	}
	n.holding = 0;
	if (n.kind == Widget::Hotspot) {
		if (isMouse(key.key)) {
			n.lit = 0;
			release(t, w, false, false, false);
		}
		return true;
	}
	if (n.kind == Widget::Selector || n.kind == Widget::Options) {
		// A released thumb stays where it was dragged; the value doesn't follow (the release is the row's).
		n.grabbed = false;
		if ((n.state & Widget::Enabled) && (n.state & Widget::Pressed) && ((n.state & Widget::Selected) || n.unselectedInput)) {
			for (int16_t b : n.buttons) {
				if (t[b].state & Widget::Pressed) {
					release(t, b, true, true, true);
					break;
				}
			}
		}
		release(t, w, false, false, false);
		return true;
	}
	if (isMouse(key.key) && (n.state & Widget::Pressed) && !hits(n, x, y)) {
		release(t, w, false, true, false); // the click is cancelled
		return true;
	}
	release(t, w, false, false, false);
	return true;
}

bool Menus::routeKey(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y)
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

bool Menus::cursorOver(WidgetTree& t, int16_t w, int32_t dx)
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
		notice(*game, t, w, NoticeHover);
		return true;
	}
	// Hovering selects it; a group of icons lets its own go on.
	if (n.iconParent < 0 || !(n.state & Widget::Enabled)) {
		return false;
	}
	select(t, w, false, false, false);
	if (t[w].kind == Widget::TextField) {
		t[w].grabbed = (t[w].flags & Widget::Focused) != 0; // the mouse holds it now (see Menus::frame)
	}
	if (t[w].iconFirst >= 0) {
		return false;
	}
	notice(*game, t, w, NoticeHover);
	return true;
}

bool Menus::routeCursor(WidgetTree& t, int16_t w, int32_t x, int32_t y)
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

void Menus::stepSelector(WidgetTree& t, int16_t selector, bool forward)
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
		play(car.carouselSounds[2]);
		return;
	}
	car.index = (was + (forward ? 1 : count - 1)) % count;
	car.fromSpin = car.stepSpin;
	car.stepSpin = car.spin;
	const int32_t cw = car.rect.width();
	car.scrollLeft = car.scrollMs;
	car.scrollSpeed = float(forward ? cw : -cw) / float(car.scrollMs);
	car.scrolled = true;
	play(car.carouselSounds[forward ? 0 : 1]);
	if (car.index != was) {
		notice(*game, t, selector, NoticeValue);
	}
}

void Menus::stepRow(WidgetTree& t, int16_t row, bool forward)
{
	Widget& r = t[row];
	if (forward ? r.index >= r.steps - 1 : r.index <= 0) {
		play(r.stepSounds[1]);
		return;
	}
	setValue(t, row, r.index + (forward ? 1 : -1));
	notice(*game, t, row, NoticeValue);
	play(r.stepSounds[0]);
}

// The timers.

void Menus::tick(WidgetTree& t, int16_t w, int32_t dt)
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
				notice(*game, t, n.parent, NoticeSettled);
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
					notice(*game, t, p, NoticeSettled);
					break;
				}
			}
		}
		else {
			const int32_t e = std::min(dt, n.scrollLeft);
			n.scrollLeft -= e;
			n.scrollAt += n.scrollSpeed * float(e);
			if (n.scrollAt < 0) {
				int32_t extent = 0;
				for (int16_t i = n.first; i >= 0; i = t[i].next) {
					extent = t[i].rect.right;
				}
				const int32_t cw = n.rect.width();
				if (n.pageMode && cw > 0) {
					extent = (extent + cw - 1) / cw * cw;
				}
				n.scrollAt += float(extent);
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
