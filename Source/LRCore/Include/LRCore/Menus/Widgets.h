#pragma once

// The widgets: input events and the keys the menus give a meaning, the widget a screen's or dialog's tree is made of,
// the tree (a flat array linked by index), a dialog, what a widget tells its screen, and Ui: the screen's and dialogs'
// trees with the input, made, walked, keyed and drawn.

#include "LRCore/Common/Api.h"
#include "LRData/Menus.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

struct Menus;

// An input event: a key (source << 28 | code) pressed or released.
struct MenuKey {
	uint32_t key = 0;
	bool down = false;
	bool repeat = false;
};

enum : uint32_t {
	SourceKeyboard = 1u << 28,
	SourceMouse = 2u << 28,
	SourcePad = 3u << 28, // every pad button is code 4 in the menus
	SourceAxis = 4u << 28, // 0 right, 1 left, 2 down, 3 up
	SourceCharacter = 7u << 28, // a typed character (for a text field), before its key
	// The keys the menus give a meaning.
	MenuEnter = SourceKeyboard | 0x1c,
	MenuEscape = SourceKeyboard | 0x01,
	MenuSpace = SourceKeyboard | 0x39,
	MenuTab = SourceKeyboard | 0x0f,
	MenuUp = SourceKeyboard | 0xc8,
	MenuDown = SourceKeyboard | 0xd0,
	MenuLeft = SourceKeyboard | 0xcb,
	MenuRight = SourceKeyboard | 0xcd,
	MenuShift = SourceKeyboard | 0x2a,
	MenuRightShift = SourceKeyboard | 0x36,
	MenuCtrl = SourceKeyboard | 0x1d,
	MenuRightCtrl = SourceKeyboard | 0x9d,
	MenuPadButton = SourcePad | 4,
};

// The keyboard's DirectInput code for a USB key code (0 if the menus don't know it).
LRCORE_API uint32_t menuKey(uint32_t usb);
// The character a USB key types into a text field (letters as capitals, digits, Enter, Backspace,
// Space, the US punctuation); 0 none. The presentation sends it (SourceCharacter) before the key, while a text field's
// screen (the license) is up.
LRCORE_API char16_t menuCharacter(uint32_t usb);

// A widget (one per element of a screen's tree; dialogs have their own trees).
struct Widget {
	enum Kind : uint8_t {
		Root,
		Region,
		Image,
		ImageButton,
		Label,
		TextButton,
		Frame,
		Quad,
		Selector,
		Carousel,
		Options,
		SceneView,
		SceneReference,
		Ring, // a 3D carousel: its items are models the scene draws
		Hotspot, // an image button with regions
		TextField,
	};
	enum Flag : uint8_t { Visible = 1, Focused = 2, DrawsItself = 4 };
	enum IconState : uint8_t { Enabled = 1, Selected = 2, Pressed = 4 };
	Kind kind = Region;
	uint8_t flags = Visible | DrawsItself;
	int32_t id = 0;
	MenuRect rect;
	uint32_t colour = 0xffffffff;
	float scaleX = 1, scaleY = 1;
	int32_t offsetX = 0, offsetY = 0;
	int16_t parent = -1, first = -1, last = -1, next = -1, focus = -1; // the tree; focus: the child on the focus path
	// An icon's (buttons, selectors, options rows, the root).
	bool icon = false;
	uint8_t state = 0; // IconState
	int16_t iconParent = -1, iconFirst = -1, iconLast = -1, iconNext = -1, selected = -1;
	uint32_t holding = 0; // the key holding it pressed
	std::array<int32_t, 5> sounds = {}; // selected, deselected, pressed, released (the bank's numbers)
	std::array<uint32_t, 6> stateColours = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
	// The kind's own.
	int16_t image = -1; // an image's (MenuData::images)
	std::array<int16_t, 6> images = {-1, -1, -1, -1, -1, -1}; // a button's state images
	std::array<uint32_t, 6> imageColours = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
	std::array<int16_t, 6> fonts = {-1, -1, -1, -1, -1, -1}; // a text button's
	std::u16string text; // a label's (upper case)
	int16_t font = -1;
	int32_t wrap = 0, maxWidth = 0;
	bool wrapping = false, centred = false; // the dialog's message wraps
	int16_t child = -1, label = -1; // a button's image; a text button's label
	Array<int16_t, 2> buttons = {-1, -1}; // a selector's or options row's previous and next
	int16_t carousel = -1; // a selector's
	bool unselectedInput = false;
	std::array<uint32_t, 8> itemColours = {}; // a selector's style's item and focused-item colours (a 3D ring's lights)
	// A carousel's.
	int32_t index = 0; // the selection; an options row's value
	int32_t scrollMs = 0, scrollLeft = 0;
	float scrollSpeed = 0, scrollAt = 0;
	bool pageMode = false, wrapAround = false, scrolled = false;
	std::array<int32_t, 3> carouselSounds = {};
	// An options row's.
	int32_t steps = 0;
	std::array<int32_t, 2> stepSounds = {};
	int16_t track = -1, thumb = -1;
	bool grabbed = false; // its thumb is being dragged
	// A ring's items (the screen's values: parts, choices), its slots; a piece ring's spin.
	List<int32_t> values;
	int32_t slots = 0, focusSlot = 0;
	List<MenuRect> slotRects;
	float spin = 0, spinRate = 0;
	// A piece ring's items: made this frame (not turned before the ring's next tick); the spin their
	// places were recentred at, by the last step and the one before (-1: unturned, as made).
	bool fresh = false;
	float stepSpin = -1, fromSpin = -1;
	// A hotspot's regions (in its space) and the lit one (0 none); its highlight is `image`.
	std::vector<int32_t> regions;
	std::vector<MenuRect> regionRects;
	int32_t lit = 0;
	// A text field's: its longest text, its characters (MENUTEXT's), its sounds (typed, added, erased, refused), its caret.
	int32_t maxLength = 31, charset = -1;
	std::array<int32_t, 4> editSounds = {};
	int16_t caret = -1;
	// A blinking colour box: on and off times, the time to its next toggle, whether it is off now.
	int32_t blinkOn = 0, blinkOff = 0, blinkLeft = 0;
	bool blinkedOff = false;

	// 0 disabled, 2 normal, 4 selected; +1 pressed.
	uint8_t visualState() const { return uint8_t((state & Enabled ? (state & Selected ? 4 : 2) : 0) + (state & Pressed ? 1 : 0)); }
};

struct WidgetTree {
	List<Widget> widgets;
	int16_t add(const Widget& w, int16_t parent); // appended last among the parent's children
	// A widget made without a parent made its child: the last, or after a sibling; and taken out again.
	void adopt(int16_t w, int16_t parent, int16_t after = -1);
	void detach(int16_t w);
	void attachIcon(int16_t w, int16_t parent); // joined to an icon parent's icon children (last)
	int32_t extent(const Widget& carousel) const; // its items' right edge (whole pages, paging)
	Widget& operator[](int16_t i) { return widgets[i]; }
	const Widget& operator[](int16_t i) const { return widgets[i]; }
	int16_t find(int32_t id) const; // the first widget with that id; -1 none
	int16_t focused() const; // the end of the focus path (the root if it alone has it); -1 none
};

struct MenuDialog {
	enum Phase : uint8_t { Opening, Open, Closing, Closed };
	enum Kind : uint8_t { TimedNotice, OneButton, YesNo };
	enum Result : uint8_t { Pending, TimedOut, Pressed, Yes, No }; // Pressed: a one-button dialog's button
	WidgetTree tree;
	Kind kind = TimedNotice;
	int32_t message = 0; // MENUTEXT's
	int32_t result = Pending;
	int32_t ageMs = 0, animMs = 0;
	Phase phase = Opening;
	float halfWidth = 0, halfHeight = 0;
	int16_t frame = -1, label = -1;
	Array<int16_t, 2> buttons = {-1, -1};
};

// How an icon is selected, pressed or released: without telling its screen, by itself (not its icon parents), without
// its sound.
enum IconHow : uint32_t { IconQuiet = 1, IconLocal = 2, IconSilent = 4 };

// What a widget tells its screen.
enum MenuNotice : int32_t { NoticeSelected, NoticePressed, NoticeReleased, NoticeValue, NoticeSettled, NoticeHover };

// The widget trees (the screen's, the dialogs' over it) and the input that reaches them: widgets made from the screen's
// layout and the styles, the icons' selection, pressing and walk, keys and the cursor on each kind, the timers, the
// dialogs, and the trees drawn. The widgets tell the screen through Menus::notice.
struct LRCORE_API Ui {
	Menus& m; // the menus it serves: the data and screen widgets are made from, where notices and sounds go
	WidgetTree tree; // the screen's
	std::array<MenuDialog, 2> dialogs;
	uint8_t dialogCount = 0;
	// Input: the queue (queued: its length), the keys held down, the modifiers, the cursor (drawn on item screens) and
	// the mouse.
	std::array<MenuKey, 32> queue;
	uint8_t queued = 0;
	std::vector<uint32_t> held;
	bool shift = false, ctrl = false;
	int32_t cursorX = 0, cursorY = 0, mouseX = 0, mouseY = 0;
	uint32_t mouseButtons = 0;
	bool cursorOn = false;

	explicit Ui(Menus& menus)
		: m(menus)
	{
	}
	WidgetTree& active() { return dialogCount ? dialogs[dialogCount - 1].tree : tree; } // the top dialog's, else the screen's
	// The frame's key and button events queued, then the repeats (Input.cpp).
	void queueInput(std::span<const MenuKey> keys, uint32_t buttons, int32_t dt);

	// Making widgets (Widgets.cpp): the root; from a binding (by name, or MENUNAME's numbers) and a style; the screen's
	// items, its banner, a selector's carousel, a selector over a carousel, one over labels.
	int16_t makeRoot(WidgetTree& t);
	int16_t make(WidgetTree& t, const MenuLayout& layout, std::string_view binding, std::string_view style, int16_t parent,
		int32_t text = -1);
	int16_t makeNamed(WidgetTree& t, uint32_t binding, uint32_t style, int16_t parent, int32_t text = -1);
	int16_t screenItem(uint32_t binding, uint32_t style, int32_t text);
	int16_t makeBanner(int32_t text);
	int16_t makeCarousel(int16_t selector, std::string_view box, std::string_view style);
	int16_t makeLabelSelector(uint32_t binding, uint32_t style, uint32_t box);
	int16_t makeTextSelector(uint32_t binding, int32_t count, int32_t first);
	// Their texts, items, layouts and values; shown or hidden; a widget's rect on the screen.
	void setText(WidgetTree& t, int16_t w, std::u16string_view text);
	void setString(WidgetTree& t, int16_t w, int32_t text); // MENUTEXT's
	void relabel(WidgetTree& t, int16_t w, int32_t text); // and measured again
	void alignRight(WidgetTree& t, int16_t w, int32_t margin);
	void addItem(WidgetTree& t, int16_t carousel, int16_t item);
	void selectItem(WidgetTree& t, int16_t carousel, int32_t index); // at once
	void layoutCarousel(WidgetTree& t, int16_t carousel);
	void layoutFrame(WidgetTree& t, int16_t frame);
	void setValue(WidgetTree& t, int16_t row, int32_t value);
	void setCharset(WidgetTree& t, int16_t field, int32_t text); // a text field's (MENUTEXT's)
	void show(WidgetTree& t, int16_t w, bool visible);
	MenuRect global(const WidgetTree& t, int16_t w) const;
	// The icons: enabling, selecting, pressing; the selection's walk (first, last, next, previous); keys and the cursor
	// on widgets; selectors and option rows stepped; the timers (Icons.cpp, WidgetKeys.cpp).
	void enable(WidgetTree& t, int16_t w, bool on);
	bool select(WidgetTree& t, int16_t w, uint32_t how = 0);
	void deselect(WidgetTree& t, int16_t w, uint32_t how = 0);
	void press(WidgetTree& t, int16_t w, uint32_t how = 0);
	void release(WidgetTree& t, int16_t w, uint32_t how = 0);
	void focusOn(WidgetTree& t, int16_t w);
	void unfocus(WidgetTree& t, int16_t w);
	int16_t first(WidgetTree& t, int16_t icon);
	int16_t last(WidgetTree& t, int16_t icon);
	int16_t next(WidgetTree& t, int16_t icon);
	int16_t previous(WidgetTree& t, int16_t icon);
	void moveSelection(WidgetTree& t, bool forward);
	bool keyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool keyUp(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool routeKey(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool routeCursor(WidgetTree& t, int16_t w, int32_t x, int32_t y);
	bool cursorOver(WidgetTree& t, int16_t w, int32_t dx); // the mouse over an icon (dx: its motion, dragging a thumb)
	void stepSelector(WidgetTree& t, int16_t selector, bool forward);
	void tick(WidgetTree& t, int16_t w, int32_t dt);
	// Dialogs (Dialogs.cpp): opened, handed their buttons, then shown; updated and closed (the top one's); a yes/no
	// question and a notice with OK.
	void openDialog(MenuDialog::Kind kind, int32_t message);
	void giveButton(uint32_t binding, uint32_t style, int32_t text);
	void showDialog(int32_t defaultButton);
	void updateDialog(int32_t dt);
	void closeDialog();
	void ask(int32_t message, int32_t yes, int32_t no);
	void tell(int32_t message);
	// The trees drawn (Draw.cpp): a widget and what it holds.
	void draw(WidgetTree& t, int16_t w, MenuRect view, MenuRect screen);

private:
	Array<int32_t, 2> m_repeatMs = {500, 500}; // the key repeats: keyboard, mouse
	void queueKey(MenuKey k);
	void holdKey(uint32_t key, bool down);
	// make's kinds: each widget w (its id, rect and colour set) made from its binding and style, and its own children.
	int16_t makeImage(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo,
		bool styleColour);
	int16_t makeLabel(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo, int32_t text);
	int16_t makeFrame(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeImageButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeTextButton(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo,
		int32_t text);
	int16_t makeSelector(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st,
		int16_t attachTo);
	int16_t makeOptions(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st,
		int16_t attachTo);
	int16_t makeSceneView(WidgetTree& t, const MenuLayout& layout, Widget& w, const MenuLayout::Binding& b, int16_t parent,
		int16_t attachTo);
	int16_t makeRing(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeHotspot(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	int16_t makeTextField(WidgetTree& t, Widget& w, const MenuLayout::Binding& b, const MenuStyles::Style& st, int16_t attachTo);
	void recompute(WidgetTree& t, int16_t w);
	bool stepperKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y); // selectors, option rows
	bool ringKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	bool textFieldKeyDown(WidgetTree& t, int16_t w, const MenuKey& key);
	bool hotspotKeyDown(WidgetTree& t, int16_t w, const MenuKey& key, int32_t x, int32_t y);
	void stepRow(WidgetTree& t, int16_t row, bool forward);
	// draw's parts: an image stretched to its rect, a hotspot's lit region, a text field, a label, a wrapping carousel's
	// items again, the 3D views and a 3D view's element.
	void drawStretched(Widget& n, MenuRect view, MenuRect screen);
	void drawLitRegion(const Widget& n, MenuRect view, MenuRect screen);
	void drawTextField(WidgetTree& t, const Widget& n, MenuRect view, MenuRect screen);
	void drawLabel(const Widget& n, MenuRect view, MenuRect screen);
	void drawWrapAround(WidgetTree& t, const Widget& n, MenuRect view, MenuRect screen);
	void drawView(WidgetTree& t, int16_t w);
	void addView(const Widget& n, std::array<float, 4> dest, const Widget* ring);
};

} // namespace lr
