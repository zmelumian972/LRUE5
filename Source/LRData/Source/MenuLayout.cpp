// Layouts (.MIB): each binding's widget block and its kind's own and icon fields.

#include "LRData/Menus.h"
#include "LRData/Archive.h"
#include "LRData/Tokens.h"
#include "MenuValues.h"

#include <algorithm>

namespace lr {

const MenuLayout::Binding* MenuLayout::find(std::string_view name) const
{
	return findWhere(bindings, [&](const Binding& b) { return b.name == name; });
}

namespace {

// A binding's widget block "{ 2f rect | 30 id | 31 parent | 32 attach | 2a colour }" (.MIB).
enum : int32_t {
	WidgetColour = 0x2a,
	WidgetRect = 0x2f,
	WidgetId = 0x30,
	WidgetParent = 0x31,
	WidgetAttach = 0x32,
};

// A binding's own fields, by its kind (the sections table).
enum : int32_t {
	BindingImages = 0x28,
	BindingFonts = 0x29,
	BindingColour = 0x2a, // a colour, or a frame's and a text button's colours
	BindingSounds = 0x2b, // an options row's and a text field's: the icon's sounds, then edit sounds
	BindingWorld = 0x2d, // a scene view's world, a scene reference's cutscene
	BindingCamera = 0x2e,
	BindingSlots = 0x2f, // a ring's slot rects
	BindingNumbers = 0x33, // the kind's numbers: a label's wrap, a frame's fill, a ring's slots, ...
	BindingClearMode = 0x35,
	BindingWidget = 0x36, // the widget block
	BindingTrack = 0x38, // an options row's track and thumb image bindings
	BindingFrame = 0x3a,
	BindingButtons = 0x3b, // the button bindings; a hotspot's image-button body
};

// An interactive binding's icon fields (and IconSounds, IconColours).
enum : int32_t {
	IconHelp = 0x2c,
	IconName = 0x31,
	IconTreeAttach = 0x32,
	IconEnabled = 0x33,
	IconUnused = 0x34,
};

bool readWidgetBlock(TokenReader& r, MenuLayout::Binding& b, int32_t& nextId)
{
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		const Values v = valuesAfter(r);
		switch (key) {
		case WidgetRect: b.rect = rectAt(v.integers, 0); break;
		case WidgetId:
			if (!v.integers.empty()) {
				b.id = v.integers[0];
				nextId = b.id + 1;
			}
			break;
		case WidgetParent: b.parent = v.text(0); break;
		case WidgetAttach: b.attach = v.integers.empty() || v.integers[0] != 0; break;
		case WidgetColour:
			b.hasColour = true;
			b.colour = colourAt(v.integers, 0);
			break;
		default: return false;
		}
	}
	return !r.failed();
}

using Binding = MenuLayout::Binding;

bool isIcon(MenuLayout::Kind k)
{
	using L = MenuLayout;
	return k == L::ImageButton || k == L::MultiState || k == L::Hotspot || k == L::Selector || k == L::Options || k == L::TextField ||
		k == L::TextButton;
}

// What a field's keyword is to a binding's kind: one of its own (now read), not its own (an icon's field, if it is
// an icon), or one it can't have.
enum class Field { Own, NotOwn, Bad };

void readSounds(MenuIcon& icon, const Values& v)
{
	icon.hasSounds = true;
	for (size_t i = 0; i < 5; i++) {
		icon.sounds[i] = v.integer(i);
	}
}

void readCamera(Binding& b, const Values& v)
{
	for (size_t i = 0; i < 9 && i < v.reals.size(); i++) {
		b.camera[i] = v.reals[i];
	}
}

// Image, Label and Frame: images, a colour or colours, a font, a wrap or a fill.
Field readPlainField(Binding& b, int key, const Values& v)
{
	if (key == BindingImages && b.kind != MenuLayout::Label) {
		b.images = v.texts;
	}
	else if (key == BindingColour) {
		b.hasColour = true;
		if (b.kind == MenuLayout::Frame) {
			b.colours = coloursOf(v.integers);
		}
		else {
			b.colour = colourAt(v.integers, 0);
		}
	}
	else if (key == BindingFonts && b.kind == MenuLayout::Label) { // the font, then always a colour
		b.fonts = {v.text(0)};
		b.hasColour = true;
		b.colour = colourAt(v.integers, 0);
	}
	else if (key == BindingNumbers && b.kind == MenuLayout::Label) {
		b.wrap = v.integer(0);
	}
	else if (key == BindingNumbers && b.kind == MenuLayout::Frame) {
		b.hasFill = v.integer(0) != 0;
	}
	else {
		return Field::NotOwn;
	}
	return Field::Own;
}

// A kind's own field: its keyword and how it is read.
struct FieldReader {
	int key;
	void (*read)(Binding&, const Values&);
};

void readImages(Binding& b, const Values& v) { b.images = v.texts; }
void readFrame(Binding& b, const Values& v) { b.frame = v.text(0); }
void readWorld(Binding& b, const Values& v) { b.world = v.text(0); }
void readButtons(Binding& b, const Values& v) { b.buttons = {v.text(0), v.text(1)}; }

// The icon's sounds, then N edit sounds (the rest 0).
template <size_t N> void readEditSounds(Binding& b, const Values& v)
{
	readSounds(b.icon, v);
	b.hasEditSounds = v.integers.size() > 5;
	for (size_t i = 0; i < 4; i++) {
		b.editSounds[i] = i < N ? v.integer(5 + i) : 0;
	}
}

constexpr FieldReader ImageButtonFields[] = {{BindingImages, readImages}};
constexpr FieldReader HotspotFields[] = {{BindingImages, readWorld}}; // the highlight image
constexpr FieldReader SelectorFields[] = {
	{BindingButtons, readButtons},
	{BindingFrame, readFrame},
	{BindingNumbers, [](Binding& b, const Values& v) { b.unselectedInput = v.integer(0) != 0; }},
};
constexpr FieldReader RingFields[] = {
	{BindingNumbers,
		[](Binding& b, const Values& v) {
			b.numbers = {v.integer(0), v.integer(1), 0};
			b.step = v.reals.empty() ? 0 : v.reals[0];
		}},
	{BindingSlots,
		[](Binding& b, const Values& v) {
			for (size_t i = 0; i + 4 <= v.integers.size(); i += 4) {
				b.slots.push_back(rectAt(v.integers, i));
			}
		}},
	{BindingCamera, readCamera},
};
constexpr FieldReader OptionsFields[] = {
	{BindingNumbers,
		[](Binding& b, const Values& v) {
			b.steps = v.integer(0);
			b.value = v.integer(1);
		}},
	{BindingButtons, readButtons},
	{BindingTrack, [](Binding& b, const Values& v) { b.track = {v.text(0), v.text(1)}; }},
	{BindingImages, readImages},
	{BindingSounds, readEditSounds<2>},
};
constexpr FieldReader SceneViewFields[] = {
	{BindingWorld, readWorld},
	{BindingFrame, readFrame},
	{BindingCamera, readCamera},
	{BindingNumbers,
		[](Binding& b, const Values& v) {
			b.numbers[0] = v.integer(0);
			b.numbers[1] = v.integer(1);
		}},
	{BindingClearMode, [](Binding& b, const Values& v) { b.numbers[2] = v.integer(0); }},
};
constexpr FieldReader TextFieldFields[] = {
	{BindingFonts, [](Binding& b, const Values& v) { b.fonts = {v.text(0)}; }},
	{BindingNumbers, [](Binding& b, const Values& v) { b.maxLength = v.integer(0); }},
	{BindingSounds, readEditSounds<4>},
};
constexpr FieldReader SceneReferenceFields[] = {
	{BindingNumbers,
		[](Binding& b, const Values& v) {
			b.autoAdvance = v.integer(0) != 0;
			b.startFrame = v.integer(1);
		}},
	{BindingWorld, readWorld},
};
constexpr FieldReader TextButtonFields[] = {
	{BindingFonts, [](Binding& b, const Values& v) { b.fonts = v.texts; }},
	{BindingImages, readImages},
	{BindingColour, [](Binding& b, const Values& v) { b.colours = coloursOf(v.integers); }},
	{BindingNumbers, [](Binding& b, const Values& v) { b.wrap = v.integer(0); }},
};

// The field read by its kind's reader for the keyword; a keyword the kind has no reader for is `other`.
Field readField(std::span<const FieldReader> readers, Field other, Binding& b, int key, const Values& v)
{
	for (const FieldReader& r : readers) {
		if (r.key == key) {
			r.read(b, v);
			return Field::Own;
		}
	}
	return other;
}

// A field of the binding's kind (a kind the files don't have takes any field).
Field readOwnField(Binding& b, int key, const Values& v)
{
	using L = MenuLayout;
	switch (b.kind) {
	case L::Image:
	case L::Label:
	case L::Frame: return readPlainField(b, key, v);
	case L::ImageButton:
	case L::MultiState: return readField(ImageButtonFields, Field::NotOwn, b, key, v);
	case L::Hotspot: return readField(HotspotFields, Field::NotOwn, b, key, v);
	case L::Selector: return readField(SelectorFields, Field::NotOwn, b, key, v);
	case L::Ring: return readField(RingFields, Field::Bad, b, key, v);
	case L::Options: return readField(OptionsFields, Field::NotOwn, b, key, v);
	case L::SceneView: return readField(SceneViewFields, Field::Bad, b, key, v);
	case L::TextField: return readField(TextFieldFields, Field::NotOwn, b, key, v);
	case L::SceneReference: return readField(SceneReferenceFields, Field::Bad, b, key, v);
	case L::TextButton: return readField(TextButtonFields, Field::NotOwn, b, key, v);
	case L::Region: return Field::Bad;
	}
	return Field::Own;
}

// An icon's field on an interactive binding.
bool readIconField(Binding& b, int key, const Values& v)
{
	switch (key) {
	case IconEnabled: b.enabled = v.integer(0) != 0; break;
	case IconTreeAttach: b.iconAttach = v.integer(0) != 0; break;
	case IconName:
	case IconUnused: break; // a name and a number nothing reads
	case IconHelp: b.help = v.integer(0); break;
	case IconColours:
		b.icon.hasColours = true;
		for (size_t i = 0; i < 6; i++) {
			b.icon.colours[i] = colourAt(v.integers, i * 4);
		}
		break;
	case IconSounds: readSounds(b.icon, v); break;
	default: return false;
	}
	return true;
}

// A binding's fields (after the widget block's), its kind's own first, then an icon's.
bool readBindingFields(TokenReader& r, Binding& b, int32_t& nextId)
{
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		if (key == BindingWidget) {
			if (!readWidgetBlock(r, b, nextId)) {
				return false;
			}
			continue;
		}
		if (key == BindingButtons && b.kind == MenuLayout::Hotspot) { // the image button's body inside
			int32_t ignored = 0;
			Binding inner;
			inner.kind = MenuLayout::ImageButton;
			if (!readBindingFields(r, inner, ignored)) {
				return false;
			}
			b.images = inner.images;
			continue;
		}
		const Values v = valuesAfter(r);
		const Field field = readOwnField(b, key, v);
		if (field == Field::Bad || (field == Field::NotOwn && !(isIcon(b.kind) && readIconField(b, key, v)))) {
			return false;
		}
	}
	return !r.failed();
}

} // namespace

bool MenuLayout::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	int32_t nextId = 2;
	return forEachSection(bytes, capacity, [&](int section, const std::string& name, TokenReader& r) {
		Binding& b = bindings.emplace_back();
		b.kind = Kind(section);
		b.name = name;
		b.id = nextId++;
		const bool ok = b.kind == Region ? readWidgetBlock(r, b, nextId) : readBindingFields(r, b, nextId);
		return ok && section >= Region && section <= TextButton;
	});
}

} // namespace lr
