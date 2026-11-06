// The widgets made: the tree's links, each kind from a binding and a style, the screens' common items (a banner,
// selectors over carousels), and their texts, items, layouts and values.

#include "LRCore/Menus/Menus.h"

#include "Menus/Layout.h"
#include "Menus/MenuNames.h"
#include "Menus/Pen.h"

#include <algorithm>

namespace lr {

namespace {

constexpr uint32_t White = 0xffffffff;
constexpr int32_t EdgeId = 1024, FillId = 666;

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
		Widget& p = widgets[parent];
		if (p.last >= 0) {
			widgets[p.last].next = i;
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
	Widget& p = widgets[parent];
	Widget& n = widgets[w];
	n.parent = parent;
	if (after >= 0) {
		n.next = widgets[after].next;
		widgets[after].next = w;
		p.last = p.last == after ? w : p.last;
		return;
	}
	n.next = -1;
	if (p.last >= 0) {
		widgets[p.last].next = w;
	}
	else {
		p.first = w;
	}
	p.last = w;
}

void WidgetTree::detach(int16_t w)
{
	Widget& n = widgets[w];
	Widget& p = widgets[n.parent];
	int16_t before = -1;
	for (int16_t c = p.first; c >= 0 && c != w; c = widgets[c].next) {
		before = c;
	}
	(before >= 0 ? widgets[before].next : p.first) = n.next;
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
	for (int16_t f = widgets[0].focus; f >= 0; f = widgets[f].focus) {
		leaf = f;
	}
	return leaf;
}

int16_t Ui::makeRoot(WidgetTree& t)
{
	t.widgets.clear();
	Widget root;
	root.kind = Widget::Root;
	root.id = 1;
	root.rect = ScreenRect;
	root.colour = 0;
	root.icon = true;
	root.state = Widget::Enabled | Widget::Selected;
	return t.add(root, -1);
}

void WidgetTree::attachIcon(int16_t w, int16_t parent)
{
	Widget& p = widgets[parent];
	widgets[w].iconParent = parent;
	(p.iconLast >= 0 ? widgets[p.iconLast].iconNext : p.iconFirst) = w;
	p.iconLast = w;
}

int32_t WidgetTree::extent(const Widget& carousel) const
{
	int32_t right = 0;
	for (int16_t i = carousel.first; i >= 0; i = widgets[i].next) {
		right = widgets[i].rect.right;
	}
	const int32_t cw = carousel.rect.width();
	return carousel.pageMode && cw > 0 ? (right + cw - 1) / cw * cw : right;
}

int16_t Ui::makeNamed(WidgetTree& t, uint32_t binding, uint32_t style, int16_t parent, int32_t text)
{
	return make(t, m.data->screens[m.screen].layout, m.data->names.narrow(binding), m.data->names.narrow(style), parent, text);
}

namespace {

// A widget's rect sized by its image where the binding leaves it zero.
void sizeFromImage(const MenuData& data, Widget& n, int16_t image)
{
	if (image < 0) {
		return;
	}
	const MenuData::ImageInfo& info = data.images[image];
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

// Image i of a binding's list when it names one, else the style's (none past both).
std::string_view boundImage(const MenuLayout::Binding& b, const MenuStyles::Style& st, size_t i)
{
	if (i < b.images.size() && !b.images[i].empty()) {
		return b.images[i];
	}
	return i < st.images.size() ? st.images[i] : std::string_view();
}

// A button's image made in it at rect (its first state's look until it is drawn); the button.
int16_t addButtonImage(WidgetTree& t, int16_t button, MenuRect rect)
{
	Widget c;
	c.kind = Widget::Image;
	c.id = t[button].id;
	c.rect = rect;
	c.image = t[button].images[0];
	c.colour = t[button].stateColours[0];
	t[button].child = t.add(c, button);
	return button;
}

} // namespace

int16_t Ui::makeImage(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo,
	bool styleColour)
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
	const bool own = !b.images.empty() && !styleColour;
	w.image = own || !st.images.empty() ? m.data->image(own ? b.images[0] : st.images[0], m.screen) : int16_t(-1);
	if (w.image < 0 && !b.images.empty()) {
		w.image = m.data->image(b.images[0], m.screen);
	}
	sizeFromImage(*m.data, w, w.image);
	return t.add(w, attachTo);
}

int16_t Ui::makeLabel(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, int32_t text)
{
	w.kind = Widget::Label;
	w.font = m.data->font(m.language, !b.fonts.empty() && !b.fonts[0].empty() ? b.fonts[0] : st.font);
	w.wrap = b.wrap ? b.wrap : st.wrap;
	w.centred = st.centred;
	const int16_t made = t.add(w, attachTo);
	if (text >= 0) {
		setString(t, made, text);
	}
	return made;
}

int16_t Ui::makeFrame(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::Frame;
	std::array<int16_t, 8> edges = {};
	for (size_t i = 0; i < 8; i++) {
		edges[i] = m.data->image(boundImage(b, st, i), m.screen);
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

int16_t Ui::makeImageButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::ImageButton;
	iconFrom(w, b, st.icon);
	for (size_t i = 0; i < 6; i++) {
		w.images[i] = m.data->image(boundImage(b, st, i), m.screen);
	}
	sizeFromImage(*m.data, w, w.images[0]);
	return addButtonImage(t, t.add(w, attachTo), w.rect);
}

int16_t Ui::makeTextButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo,
	int32_t text)
{
	w.kind = Widget::TextButton;
	iconFrom(w, b, st.icon);
	const std::vector<uint32_t>& colours = b.colours.size() >= 13 ? b.colours : st.colours;
	for (size_t i = 0; i < 6; i++) {
		const std::string& font = i < b.fonts.size() && !b.fonts[i].empty() ? b.fonts[i] : st.fonts[i];
		w.images[i] = m.data->image(boundImage(b, st, i), m.screen);
		w.fonts[i] = m.data->font(m.language, font);
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
	sizeFromImage(*m.data, image, image.image);
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

int16_t Ui::makeSelector(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st,
	int16_t attachTo)
{
	w.kind = Widget::Selector;
	iconFrom(w, b, st.icon);
	w.unselectedInput = b.unselectedInput;
	for (size_t i = 0; i < w.itemColours.size() && i < st.colours.size(); i++) {
		w.itemColours[i] = st.colours[i];
	}
	const int16_t made = t.add(w, attachTo);
	for (size_t i = 0; i < 2; i++) {
		t[made].buttons[i] = make(t, layout, b.buttons[i], st.buttons[i], made); // (made before t[made] is taken)
	}
	return made;
}

int16_t Ui::makeOptions(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st,
	int16_t attachTo)
{
	w.kind = Widget::Options;
	iconFrom(w, b, st.icon);
	w.steps = b.steps;
	w.index = b.value;
	w.stepSounds = b.hasEditSounds ? std::array<int32_t, 2>{b.editSounds[0], b.editSounds[1]}
								   : std::array<int32_t, 2>{st.sounds[0], st.sounds[1]};
	const int16_t made = t.add(w, attachTo);
	for (size_t i = 0; i < 2; i++) {
		t[made].buttons[i] = make(t, layout, b.buttons[i], st.buttons[i], made); // (made before t[made] is taken)
	}
	for (size_t i = 0; i < 2; i++) {
		(i ? t[made].thumb : t[made].track) = make(t, layout, b.track[i], st.track[i], made);
	}
	setValue(t, made, w.index);
	return made;
}

int16_t Ui::makeSceneView(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, int16_t parent,
	int16_t attachTo)
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
int16_t Ui::makeRing(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
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
int16_t Ui::makeHotspot(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::Hotspot;
	iconFrom(w, b, st.icon);
	for (size_t i = 0; i < 6; i++) {
		w.images[i] = i < b.images.size() ? m.data->image(b.images[i], m.screen) : -1;
	}
	w.image = m.data->image(!b.world.empty() ? b.world : st.images.empty() ? std::string() : st.images[0], m.screen);
	w.regions = st.regions;
	w.regionRects = st.regionRects;
	sizeFromImage(*m.data, w, w.images[0]);
	return addButtonImage(t, t.add(w, attachTo), {0, 0, w.rect.width(), w.rect.height()});
}

// The caret comes after it, under the same parent: a white box that blinks (sized by the field's characters).
int16_t Ui::makeTextField(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo)
{
	w.kind = Widget::TextField;
	iconFrom(w, b, st.icon);
	w.font = m.data->font(m.language, !b.fonts.empty() && !b.fonts[0].empty() ? b.fonts[0] : st.font);
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

int16_t Ui::make(WidgetTree& t, const MenuLayout& layout, std::string_view bindingName, std::string_view styleName, int16_t parent,
	int32_t text)
{
	const MenuLayout::Binding* b = layout.find(bindingName);
	const MenuStyles::Style* st = findStyle(*m.data, m.screen, styleName);
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
			t.attachIcon(made, ip < 0 ? 0 : ip);
		}
		recompute(t, made);
		if (b->enabled) {
			enable(t, made, true);
		}
	}
	return made;
}

void Ui::layoutFrame(WidgetTree& t, int16_t frame)
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
			w[i] = int32_t(m.data->images[image].width);
			h[i] = int32_t(m.data->images[image].height);
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

void Ui::setString(WidgetTree& t, int16_t w, int32_t index) { setText(t, w, index >= 0 ? m.text(index) : std::u16string_view()); }

void Ui::relabel(WidgetTree& t, int16_t w, int32_t index)
{
	Widget& n = t[w];
	n.rect.right = n.rect.left;
	n.rect.bottom = n.rect.top;
	setString(t, w, index);
}

void Ui::setText(WidgetTree& t, int16_t w, std::u16string_view s)
{
	Pen pen = m.pen();
	Widget& n = t[w];
	if (n.kind == Widget::TextButton) {
		setText(t, n.label, s);
		// The label beside the image, its width capped; the button as tall as the image.
		const Widget& image = t[n.child];
		Widget& label = t[n.label];
		MenuRect text = pen.measure(label.font, label.text, 0);
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
	// The dialog's message (one line when it fits the rect, else wrapped at the wrap width), or a label without a size:
	// measured.
	if (n.wrapping || n.rect.right == 0 || n.rect.bottom == 0 || (n.rect.right == n.rect.left && n.rect.bottom == n.rect.top)) {
		const MenuRect size = pen.measure(n.font, n.text, n.wrapping ? n.wrap : 0);
		n.rect.right = n.rect.left + size.width();
		n.rect.bottom = n.rect.top + size.height();
	}
}

void Ui::alignRight(WidgetTree& t, int16_t w, int32_t margin)
{
	Widget& n = t[w];
	const int32_t wd = n.rect.width();
	n.rect.right = t[n.parent].rect.width() - margin;
	n.rect.left = n.rect.right - wd;
}

void Ui::addItem(WidgetTree& t, int16_t carousel, int16_t item)
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

void Ui::layoutCarousel(WidgetTree& t, int16_t carousel)
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

void Ui::selectItem(WidgetTree& t, int16_t carousel, int32_t index)
{
	t[carousel].index = index;
	layoutCarousel(t, carousel);
}

void Ui::setValue(WidgetTree& t, int16_t row, int32_t value)
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

void Ui::show(WidgetTree& t, int16_t w, bool visible)
{
	if (w < 0) {
		return;
	}
	t[w].flags = visible ? t[w].flags | Widget::Visible : t[w].flags & ~Widget::Visible;
}

int16_t Ui::screenItem(uint32_t binding, uint32_t style, int32_t text) { return makeNamed(tree, binding, style, -1, text); }

int16_t Ui::makeBanner(int32_t text)
{
	const int16_t b = screenItem(StyleBanner, StyleBanner, text);
	alignRight(tree, b, 20);
	return b;
}

// A carousel made in a selector (from the screen's layout), the selector's, in its colour.
int16_t Ui::makeCarousel(int16_t sel, std::string_view box, std::string_view style)
{
	const int16_t car = make(tree, m.data->screens[m.screen].layout, box, style, sel);
	tree[sel].carousel = car;
	tree[car].colour = tree[sel].colour;
	return car;
}

// A selector over a carousel of labels (or images).
int16_t Ui::makeLabelSelector(uint32_t binding, uint32_t style, uint32_t box)
{
	const int16_t sel = screenItem(binding, style, -1);
	if (sel >= 0) {
		makeCarousel(sel, m.data->names.narrow(box), m.data->names.narrow(StylePicker));
	}
	return sel;
}

// A selector over count labels: MENUTEXT's texts from first on, or empty when first < 0 (racers' names, set later).
int16_t Ui::makeTextSelector(uint32_t binding, int32_t count, int32_t first)
{
	const int16_t sel = makeLabelSelector(binding, StyleTextSelector, NameTextBox);
	for (int32_t i = 0; i < count; i++) {
		const int16_t l = makeNamed(tree, NameContext, StyleMenuFont, -1);
		if (first >= 0) {
			setText(tree, l, m.text(first + i));
		}
		addItem(tree, tree[sel].carousel, l);
	}
	return sel;
}

void Ui::setCharset(WidgetTree& t, int16_t field, int32_t textIndex)
{
	Pen pen = m.pen();
	// The caret is as wide as the widest of the characters, 4 high.
	Widget& f = t[field];
	f.charset = textIndex;
	int32_t widest = 0;
	for (char16_t c : m.text(textIndex)) {
		widest = std::max(widest, pen.lineWidth(f.font, std::u16string_view(&c, 1)));
	}
	t[f.caret].rect = {0, 0, widest, 4};
}

} // namespace lr
