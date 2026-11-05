#include "LRData/Menus.h"

#include "Bytes.h"
#include "LRData/Image.h"
#include "LRData/Tokens.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace lr {

namespace {

// Names in the menu files are kept to 8 characters.
std::string name8(std::string_view text) { return std::string(text.substr(0, 8)); }

// A keyword's values: the integers, reals and texts after it (a list of mixed values, as the files have them).
struct Values {
	std::vector<int32_t> integers;
	std::vector<float> reals;
	std::vector<std::string> texts;
	// The i-th text or integer, else empty or 0.
	std::string text(size_t i) const { return i < texts.size() ? texts[i] : std::string(); }
	int32_t integer(size_t i) const { return i < integers.size() ? integers[i] : 0; }
};

Values valuesAfter(TokenReader& r)
{
	Values v;
	for (;;) {
		if (r.atEnd() || r.failed()) {
			return v;
		}
		const Token& t = r.tokens().list[r.position()];
		if (t.kind == Token::Integer) {
			v.integers.push_back(r.integer());
		}
		else if (t.kind == Token::Real) {
			v.reals.push_back(r.real());
		}
		else if (t.kind == Token::Text) {
			v.texts.push_back(name8(r.text()));
		}
		else {
			return v;
		}
	}
}

// Four integers A R G B.
uint32_t colourAt(const std::vector<int32_t>& v, size_t at)
{
	if (at + 4 > v.size()) {
		return 0xffffffff;
	}
	return uint32_t(v[at] & 255) << 24 | uint32_t(v[at + 1] & 255) << 16 | uint32_t(v[at + 2] & 255) << 8 | uint32_t(v[at + 3] & 255);
}
std::vector<uint32_t> coloursOf(const std::vector<int32_t>& v)
{
	std::vector<uint32_t> colours;
	for (size_t i = 0; i + 4 <= v.size(); i += 4) {
		colours.push_back(colourAt(v, i));
	}
	return colours;
}
uint32_t rgbKey(const std::vector<int32_t>& v) { return v.size() < 3 ? 0 : 0xff000000u | uint32_t(v[0] & 255) << 16 | uint32_t(v[1] & 255) << 8 | uint32_t(v[2] & 255); }
MenuRect rectAt(const std::vector<int32_t>& v, size_t at) { return at + 4 > v.size() ? MenuRect{} : MenuRect{v[at], v[at + 1], v[at + 2], v[at + 3]}; }

// The files' first keyword: a style table's and a layout's "K27 I capacity", a list's "K27 [n] { K27 T name ... }".
enum : int {
	KeyCapacity = 0x27,
	ListEntry = 0x27,
};

// The image list's (.IDB).
enum : int {
	ImageFlip = 0x28,
	ImageBmp = 0x29,
	ImageTga = 0x2a,
	ImageColourKey = 0x2b, // I r, I g, I b
	ImageTint = 0x2c, // I r, I g, I b
};

// The font list's (.FDB).
enum : int {
	FontBmp = 0x28,
	FontTga = 0x29,
	FontColourKey = 0x2a, // I r, I g, I b
	FontChars = 0x2b, // [ chars ]
	FontSpacing = 0x2c,
	FontColour = 0x2d, // I r, I g, I b
};

// A style's fields (.MSB).
enum : int {
	StyleImages = 0x28,
	StyleFonts = 0x29,
	StyleColours = 0x2a,
	StyleSounds = 0x2b,
	StyleRects = 0x2c, // a text button's two rects and a multi-state's: unused
	StyleScrollMs = 0x2d,
	StyleWrap = 0x2e, // a text style's wrap width, a text button's max text width
	StyleCentred = 0x2f, // a text style's centring, a carousel's page mode and wrap, a hotspot's region count
	StyleRegionRects = 0x30,
	StyleRegionIds = 0x31,
	StyleTrack = 0x32, // the track and thumb image styles
	StyleFrame = 0x34,
	StyleButtons = 0x35, // the previous and next button styles
	StyleIcon = 0x3a, // the icon block
};

// The icon block "{ 2b sounds | 2c state rects | 2a state colours | 2d transition }"; a binding's icon fields use
// the same sounds and colours.
enum : int { IconSounds = 0x2b, IconRects = 0x2c, IconColours = 0x2a, IconTransition = 0x2d };

bool readIcon(TokenReader& r, MenuIcon& icon)
{
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		const Values v = valuesAfter(r);
		switch (key) {
		case IconSounds:
			icon.hasSounds = true;
			for (size_t i = 0; i < 5 && i < v.integers.size(); i++) {
				icon.sounds[i] = v.integers[i];
			}
			break;
		case IconRects:
			for (size_t i = 0; i < 6; i++) {
				icon.stateRects[i] = rectAt(v.integers, i * 4);
			}
			break;
		case IconColours:
			icon.hasColours = true;
			for (size_t i = 0; i < 6; i++) {
				icon.colours[i] = colourAt(v.integers, i * 4);
			}
			break;
		case IconTransition: icon.transitionMs = v.integers.empty() ? 0 : v.integers[0]; break;
		default: return false;
		}
	}
	return !r.failed();
}

// "K27 I capacity, then K<s> [n] { K<s'> T name body } per section": calls entry(section, name, r) after each name.
template <typename Entry> bool forEachSection(const Tokens& tokens, int32_t& capacity, Entry entry)
{
	TokenReader r(tokens);
	if (r.keyword() != KeyCapacity) {
		return false;
	}
	capacity = r.integer();
	while (!r.failed() && !r.atEnd()) {
		const int section = r.keyword();
		if (section < 0) {
			return false;
		}
		const uint32_t count = r.openCounted();
		for (uint32_t i = 0; i < count && !r.failed(); i++) {
			if (r.keyword() < 0) {
				return false;
			}
			const std::string name = name8(r.text());
			if (!entry(section, name, r)) {
				return false;
			}
		}
		r.close();
	}
	return !r.failed();
}

// "K27 [n] { K27 T name { fields } }": the image and font lists.
template <typename Entry> bool forEachListed(std::span<const uint8_t> bytes, Entry entry)
{
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != ListEntry) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != ListEntry) {
			return false;
		}
		const std::string name = name8(r.text());
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			const int key = r.keyword();
			if (key < 0 || !entry(i, name, key, r)) {
				return false;
			}
		}
	}
	r.close();
	return !r.failed() && r.atEnd();
}

} // namespace

std::u16string_view StringTable::at(size_t index) const
{
	if (index >= starts.size()) {
		return {};
	}
	const char16_t* first = reinterpret_cast<const char16_t*>(units.data() + starts[index]);
	return std::u16string_view(first);
}

std::string StringTable::narrow(size_t index) const
{
	std::string text;
	for (char16_t c : at(index)) {
		text += char(c & 255);
	}
	return text;
}

bool StringTable::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	if (!fits(bytes, 0, 4)) {
		return false;
	}
	const uint32_t count = u16(bytes, 0), length = u16(bytes, 2);
	if (!fits(bytes, 4, (count + uint64_t(length)) * 2)) {
		return false;
	}
	units.resize(length + 1); // a 0 past the end, so a string can't run off it
	for (uint32_t i = 0; i < length; i++) {
		units[i] = u16(bytes, 4 + 2 * (count + uint64_t(i)));
	}
	for (uint32_t i = 0; i < count; i++) {
		const uint32_t start = u16(bytes, 4 + 2 * uint64_t(i));
		if (start >= length) {
			return false;
		}
		starts.push_back(start);
	}
	return true;
}

bool NameList::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	std::vector<std::string> lines;
	std::string line;
	for (uint8_t c : bytes) {
		if (c == '\n') {
			lines.push_back(line);
			line.clear();
		}
		else if (c != '\r') {
			line += char(c);
		}
	}
	if (!line.empty()) {
		lines.push_back(line);
	}
	if (lines.size() < 2) {
		return false;
	}
	header = lines[0];
	const int count = std::atoi(lines[1].c_str());
	if (count < 0 || size_t(count) + 2 > lines.size()) {
		return false;
	}
	names.assign(lines.begin() + 2, lines.begin() + 2 + count);
	return true;
}

bool ImageList::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	return forEachListed(bytes, [&](uint32_t i, const std::string& name, int key, TokenReader& r) {
		if (i >= images.size()) {
			images.emplace_back().name = name;
		}
		Entry& e = images.back();
		const Values v = valuesAfter(r);
		switch (key) {
		case ImageFlip: e.flip = true; break;
		case ImageBmp: e.targa = false; break;
		case ImageTga: e.targa = true; break;
		case ImageColourKey:
			e.keyed = true;
			e.key = rgbKey(v.integers);
			break;
		case ImageTint: e.tint = rgbKey(v.integers); break;
		default: return false;
		}
		return true;
	});
}

bool FontList::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	return forEachListed(bytes, [&](uint32_t i, const std::string& name, int key, TokenReader& r) {
		if (i >= fonts.size()) {
			fonts.emplace_back().name = name;
		}
		Font& f = fonts.back();
		switch (key) {
		case FontBmp: f.targa = false; break;
		case FontTga: f.targa = true; break;
		case FontColourKey:
			f.keyed = true;
			f.key = rgbKey(valuesAfter(r).integers);
			break;
		case FontChars: // [ chars ]: one-character texts or codes
			r.expect(Token::OpenCount);
			while (!r.failed() && !r.next(Token::CloseCount)) {
				if (r.tokens().list[r.position()].kind == Token::Text) {
					const std::string_view c = r.text();
					f.chars.push_back(c.empty() ? 0 : uint8_t(c[0]));
				}
				else {
					f.chars.push_back(uint16_t(r.integer()));
				}
			}
			break;
		case FontSpacing: f.spacing = r.integer(); break;
		case FontColour: f.colour = rgbKey(valuesAfter(r).integers); break;
		default: return false;
		}
		return true;
	});
}

bool FontGlyphs::scan(const Image& image, std::span<const uint16_t> chars)
{
	*this = {};
	height = image.height;
	const auto pixel = [&](uint32_t x, uint32_t y) {
		uint32_t value;
		std::memcpy(&value, &image.rgba[(size_t(y) * image.width + x) * 4], 4);
		return value;
	};
	// QUIRK: in a 4-bit image of odd width the last column is never a separator (the original reads the row's
	// padding there), so a glyph touching it is a column wider, and an empty last column is a glyph.
	const uint32_t last = image.bits == 4 && image.width % 2 ? image.width - 1 : image.width;
	const auto separator = [&](uint32_t x) {
		if (x == last) {
			return false;
		}
		for (uint32_t y = 0; y < height; y++) {
			if (pixel(x, y) != pixel(0, y)) {
				return false;
			}
		}
		return true;
	};
	if (image.width == 0 || height == 0) {
		return false;
	}
	while (space < image.width && separator(space)) {
		space++;
	}
	glyphs.push_back({32, 0, uint16_t(space)});
	uint32_t x = space + 1; // the column after the separators is skipped: the first glyph loses it
	for (size_t i = 0; i < chars.size(); i++) {
		if (i > 0) {
			while (x < image.width && separator(x)) {
				x++;
			}
		}
		const uint32_t start = x;
		while (x < image.width && !separator(x)) {
			x++;
		}
		if (x == start) {
			return false;
		}
		glyphs.push_back({chars[i], uint16_t(start), uint16_t(x - start)});
	}
	std::stable_sort(glyphs.begin(), glyphs.end(), [](const Glyph& a, const Glyph& b) { return a.code < b.code; });
	return true;
}

const FontGlyphs::Glyph& FontGlyphs::find(uint16_t code) const
{
	// Halving the range (a code listed twice is found where the halving lands, as the original's lookup).
	int32_t low = 0, high = int32_t(glyphs.size()) - 1;
	while (low <= high) {
		const int32_t middle = (low + high) / 2;
		if (glyphs[size_t(middle)].code == code) {
			return glyphs[size_t(middle)];
		}
		if (glyphs[size_t(middle)].code < code) {
			low = middle + 1;
		}
		else {
			high = middle - 1;
		}
	}
	return *std::find_if(glyphs.begin(), glyphs.end(), [](const Glyph& g) { return g.code == 32; });
}

const MenuStyles::Style* MenuStyles::find(std::string_view name) const
{
	for (const Style& s : styles) {
		if (s.name == name) {
			return &s;
		}
	}
	return nullptr;
}

bool MenuStyles::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	return tokens.decode(bytes) && forEachSection(tokens, capacity, [&](int section, const std::string& name, TokenReader& r) {
		Style& s = styles.emplace_back();
		s.kind = Kind(section);
		s.name = name;
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			const int key = r.keyword();
			if (key == StyleIcon) {
				if (!readIcon(r, s.icon)) {
					return false;
				}
				continue;
			}
			const Values v = valuesAfter(r);
			switch (key) {
			case StyleImages: s.images = v.texts; break;
			case StyleFonts:
				s.font = v.texts.empty() ? std::string() : v.texts[0];
				for (size_t i = 0; i < 6 && i < v.texts.size(); i++) {
					s.fonts[i] = v.texts[i];
				}
				break;
			case StyleColours: s.colours = coloursOf(v.integers); break;
			case StyleSounds:
				for (size_t i = 0; i < 3 && i < v.integers.size(); i++) {
					s.sounds[i] = v.integers[i];
				}
				break;
			case StyleRects: break; // a text button's two rects and a multi-state's: unused
			case StyleScrollMs: s.scrollMs = v.integers.empty() ? 0 : v.integers[0]; break;
			case StyleWrap: s.wrap = s.maxWidth = v.integers.empty() ? 0 : v.integers[0]; break;
			case StyleCentred:
				if (section == Carousel && v.integers.size() >= 2) {
					s.pageMode = v.integers[0] != 0;
					s.wrapAround = v.integers[1] != 0;
				}
				else if (!v.integers.empty()) {
					s.centred = v.integers[0] != 0;
				}
				break;
			case StyleRegionRects:
				for (size_t i = 0; i + 4 <= v.integers.size(); i += 4) {
					s.regionRects.push_back(rectAt(v.integers, i));
				}
				break;
			case StyleRegionIds: s.regions = v.integers; break;
			case StyleTrack:
				for (size_t i = 0; i < 2 && i < v.texts.size(); i++) {
					s.track[i] = v.texts[i];
				}
				break;
			case StyleFrame: s.frame = v.texts.empty() ? std::string() : v.texts[0]; break;
			case StyleButtons:
				for (size_t i = 0; i < 2 && i < v.texts.size(); i++) {
					s.buttons[i] = v.texts[i];
				}
				break;
			default: return false;
			}
		}
		return true;
	});
}

const MenuLayout::Binding* MenuLayout::find(std::string_view name) const
{
	for (const Binding& b : bindings) {
		if (b.name == name) {
			return &b;
		}
	}
	return nullptr;
}

namespace {

// A binding's widget block "{ 2f rect | 30 id | 31 parent | 32 attach | 2a colour }" (.MIB).
enum : int {
	WidgetColour = 0x2a,
	WidgetRect = 0x2f,
	WidgetId = 0x30,
	WidgetParent = 0x31,
	WidgetAttach = 0x32,
};

// A binding's own fields, by its kind (the sections table).
enum : int {
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
enum : int {
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
		case WidgetParent: b.parent = v.texts.empty() ? std::string() : v.texts[0]; break;
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
	return k == L::ImageButton || k == L::MultiState || k == L::Hotspot || k == L::Selector || k == L::Options || k == L::TextField || k == L::TextButton;
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

Field readSelectorField(Binding& b, int key, const Values& v)
{
	if (key == BindingButtons) {
		b.buttons = {v.text(0), v.text(1)};
	}
	else if (key == BindingFrame) {
		b.frame = v.text(0);
	}
	else if (key == BindingNumbers) {
		b.unselectedInput = v.integer(0) != 0;
	}
	else {
		return Field::NotOwn;
	}
	return Field::Own;
}

Field readRingField(Binding& b, int key, const Values& v)
{
	if (key == BindingNumbers) {
		b.numbers = {v.integer(0), v.integer(1), 0};
		b.step = v.reals.empty() ? 0 : v.reals[0];
	}
	else if (key == BindingSlots) {
		for (size_t i = 0; i + 4 <= v.integers.size(); i += 4) {
			b.slots.push_back(rectAt(v.integers, i));
		}
	}
	else if (key == BindingCamera) {
		readCamera(b, v);
	}
	else {
		return Field::Bad;
	}
	return Field::Own;
}

Field readOptionsField(Binding& b, int key, const Values& v)
{
	if (key == BindingNumbers) {
		b.steps = v.integer(0);
		b.value = v.integer(1);
	}
	else if (key == BindingButtons) {
		b.buttons = {v.text(0), v.text(1)};
	}
	else if (key == BindingTrack) {
		b.track = {v.text(0), v.text(1)};
	}
	else if (key == BindingImages) {
		b.images = v.texts;
	}
	else if (key == BindingSounds) {
		readSounds(b.icon, v);
		b.hasEditSounds = v.integers.size() > 5;
		b.editSounds = {v.integer(5), v.integer(6), 0, 0};
	}
	else {
		return Field::NotOwn;
	}
	return Field::Own;
}

Field readSceneViewField(Binding& b, int key, const Values& v)
{
	if (key == BindingWorld) {
		b.world = v.text(0);
	}
	else if (key == BindingFrame) {
		b.frame = v.text(0);
	}
	else if (key == BindingCamera) {
		readCamera(b, v);
	}
	else if (key == BindingNumbers) {
		b.numbers[0] = v.integer(0);
		b.numbers[1] = v.integer(1);
	}
	else if (key == BindingClearMode) {
		b.numbers[2] = v.integer(0);
	}
	else {
		return Field::Bad;
	}
	return Field::Own;
}

Field readTextFieldField(Binding& b, int key, const Values& v)
{
	if (key == BindingFonts) {
		b.fonts = {v.text(0)};
	}
	else if (key == BindingNumbers) {
		b.maxLength = v.integer(0);
	}
	else if (key == BindingSounds) {
		readSounds(b.icon, v);
		b.hasEditSounds = v.integers.size() > 5;
		b.editSounds = {v.integer(5), v.integer(6), v.integer(7), v.integer(8)};
	}
	else {
		return Field::NotOwn;
	}
	return Field::Own;
}

Field readSceneReferenceField(Binding& b, int key, const Values& v)
{
	if (key == BindingNumbers) {
		b.autoAdvance = v.integer(0) != 0;
		b.startFrame = v.integer(1);
	}
	else if (key == BindingWorld) {
		b.world = v.text(0);
	}
	else {
		return Field::Bad;
	}
	return Field::Own;
}

Field readTextButtonField(Binding& b, int key, const Values& v)
{
	if (key == BindingFonts) {
		b.fonts = v.texts;
	}
	else if (key == BindingImages) {
		b.images = v.texts;
	}
	else if (key == BindingColour) {
		b.colours = coloursOf(v.integers);
	}
	else if (key == BindingNumbers) {
		b.wrap = v.integer(0);
	}
	else {
		return Field::NotOwn;
	}
	return Field::Own;
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
	case L::MultiState:
		if (key != BindingImages) {
			return Field::NotOwn;
		}
		b.images = v.texts;
		return Field::Own;
	case L::Hotspot: // the highlight image
		if (key != BindingImages) {
			return Field::NotOwn;
		}
		b.world = v.text(0);
		return Field::Own;
	case L::Selector: return readSelectorField(b, key, v);
	case L::Ring: return readRingField(b, key, v);
	case L::Options: return readOptionsField(b, key, v);
	case L::SceneView: return readSceneViewField(b, key, v);
	case L::TextField: return readTextFieldField(b, key, v);
	case L::SceneReference: return readSceneReferenceField(b, key, v);
	case L::TextButton: return readTextButtonField(b, key, v);
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
	Tokens tokens;
	int32_t nextId = 2;
	return tokens.decode(bytes) && forEachSection(tokens, capacity, [&](int section, const std::string& name, TokenReader& r) {
		Binding& b = bindings.emplace_back();
		b.kind = Kind(section);
		b.name = name;
		b.id = nextId++;
		const bool ok = b.kind == Region ? readWidgetBlock(r, b, nextId) : readBindingFields(r, b, nextId);
		return ok && section >= Region && section <= TextButton;
	});
}

namespace {

// A cinematic's scene file (.CDB): its lists, a scene's fields and blocks of cues.
enum : int {
	CinemaScenes = 0x27,
	CinemaWorlds = 0x28,
	SceneRate = 0x3b,
	SceneFrames = 0x2c,
	SceneCameras = 0x29,
	SceneModels = 0x2e,
	SceneTriggers = 0x37,
	SceneAmbients = 0x35,
	SceneLights = 0x3a,
};

// A cue's fields.
enum : int {
	CueCamera = 0x2a,
	CueStart = 0x2b,
	CueDuration = 0x2c,
	CuePart = 0x2d,
	CuePlainEntity = 0x2f,
	CueAnimatedEntity = 0x30,
	CueCollisionEntity = 0x31,
	CueEntityAt = 0x32, // world database, entity index
	CuePosition = 0x33,
	CueAxes = 0x34, // forward, up
	CueTracks = 0x36,
	CueColour = 0x38,
	CueDirection = 0x39,
	CueBlink = 0x3c,
};

// One of a cue's fields, its key read. False if the key is bad.
bool readCueField(TokenReader& r, CinemaFile::Cue& cue, int32_t& duration, int cueKey)
{
	const auto number = [&r] { return r.next(Token::Integer) ? float(r.tokens().list[r.position() - 1].integer) : r.real(); };
	switch (cueKey) {
	case CueStart: cue.start = r.integer(); break;
	case CueDuration: duration = r.integer(); break;
	case CueCamera: cue.target = name8(r.text()); break;
	case CuePart: cue.part = r.integer(); break;
	case CuePlainEntity:
	case CueAnimatedEntity:
	case CueCollisionEntity:
		cue.entityKind = uint8_t(cueKey);
		cue.target = name8(r.text());
		break;
	case CueEntityAt:
		cue.entityKind = CueEntityAt;
		cue.entityAt = {r.integer(), r.integer()};
		break;
	case CuePosition:
		for (float& x : cue.position) {
			x = number();
		}
		break;
	case CueAxes:
		for (float& x : cue.forward) {
			x = number();
		}
		for (float& x : cue.up) {
			x = number();
		}
		break;
	case CueTracks: {
		const uint32_t k = r.openCounted();
		for (uint32_t t = 0; t < k && !r.failed(); t++) {
			std::array<int32_t, 5>& track = cue.tracks.emplace_back();
			for (int32_t& x : track) {
				x = r.integer();
			}
		}
		r.close();
		break;
	}
	case CueColour:
		for (int32_t& x : cue.colour) {
			x = int32_t(number()) & 255;
		}
		break;
	case CueDirection:
		for (float& x : cue.direction) {
			x = number();
		}
		break;
	case CueBlink: cue.blink = {r.integer(), r.integer()}; break;
	default:
		if (cueKey < 0) {
			return false;
		}
		break;
	}
	return true;
}

// A cue's block "{ fields }": it ends its duration after its start. False if a key is bad.
bool readCue(TokenReader& r, CinemaFile::Cue& cue)
{
	int32_t duration = 0;
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		if (!readCueField(r, cue, duration, r.keyword())) {
			return false;
		}
		r.skipValues();
		if (!r.atEnd() && r.tokens().list[r.position()].kind == Token::OpenCount) {
			r.skipCounted();
		}
	}
	cue.end = cue.start + duration;
	return true;
}

// A scene's block: its rate, its frames (else its last camera's end) and its cue blocks. False if a key is bad.
bool readScene(TokenReader& r, CinemaFile::Scene& s)
{
	using Cue = CinemaFile::Cue;
	int32_t lastCamera = 0;
	r.expect(Token::Open);
	// The cue blocks in any order: kept as cameras, models, triggers, ambients, lights.
	std::array<std::vector<Cue>, 5> byKind;
	while (!r.failed() && !r.next(Token::Close)) {
		const int field = r.keyword();
		if (field == SceneRate) {
			s.rate = r.integer();
			continue;
		}
		if (field == SceneFrames) {
			s.frames = r.integer();
			continue;
		}
		static const int Blocks[5] = {SceneCameras, SceneModels, SceneTriggers, SceneAmbients, SceneLights};
		const int* kind = std::find(std::begin(Blocks), std::end(Blocks), field);
		if (kind == std::end(Blocks)) {
			return false;
		}
		const uint32_t cues = r.openCounted();
		for (uint32_t c = 0; c < cues && !r.failed(); c++) {
			r.keyword();
			Cue cue;
			cue.kind = Cue::Kind(kind - Blocks);
			cue.name = name8(r.text());
			if (!readCue(r, cue)) {
				return false;
			}
			if (cue.kind == Cue::Camera) {
				lastCamera = std::max(lastCamera, cue.end);
			}
			byKind[cue.kind].push_back(cue);
		}
		r.close();
	}
	for (const std::vector<Cue>& k : byKind) {
		s.cues.insert(s.cues.end(), k.begin(), k.end());
	}
	if (s.frames == 0) {
		s.frames = lastCamera;
	}
	return true;
}

} // namespace

bool CinemaFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.failed() && !r.atEnd()) {
		const int key = r.keyword();
		if (key == CinemaWorlds) {
			const uint32_t n = r.openCounted();
			for (uint32_t i = 0; i < n && !r.failed(); i++) {
				worlds.emplace_back(name8(r.text()));
			}
			r.close();
			continue;
		}
		if (key != CinemaScenes) {
			return false;
		}
		const uint32_t count = r.openCounted();
		for (uint32_t i = 0; i < count && !r.failed(); i++) {
			r.keyword();
			Scene& s = scenes.emplace_back();
			s.name = name8(r.text());
			if (!readScene(r, s)) {
				return false;
			}
		}
		r.close();
	}
	return !r.failed() && !scenes.empty();
}

namespace {

using Effect = CinemaFile::Effect;

// A cinematic's effects file (.CEB): its name lists, effect lists (by Effect::Kind) and link
// channels.
enum : int {
	EffectsParticleSets = 0x27,
	EffectsStringTables = 0x28,
	EffectsFonts = 0x29,
	EffectsImageLists = 0x2a,
	EffectsSoundBanks = 0x5c,
	LinkFirst = 0x50, // ..0x5b: 0x50 + 2 * cue kind + (0 begins, 1 ends); 0x50/0x51 any kind
	LinkLast = 0x5b,
	LinkStart = 0x4e,
	LinkStop = 0x4f,
};

// An effect's fields.
enum : int {
	ColourShift = 0x2c,
	ColourOffset = 0x2d,
	ColourOffsetSpeed = 0x2e,
	SoundBankAndIndex = 0x30,
	SoundVolume = 0x32,
	SoundFrequency = 0x33,
	SoundPan = 0x34,
	SoundLooping = 0x35,
	TextString = 0x40, // string table, string
	TextLiteral = 0x41,
	TextFont = 0x42,
	OverlayLeft = 0x43,
	OverlayTop = 0x44,
	OverlayCentreX = 0x45,
	OverlayCentreY = 0x46,
	OverlayWidth = 0x47,
	OverlayHeight = 0x48,
	OverlaySpeedFirst = 0x49, // ..0x4c: left, top, width, height
	OverlaySpeedLast = 0x4c,
	OverlayPicture = 0x4d,
	EntityAnimated = 0x5d,
	EntityPlain = 0x5e,
	EntityCollision = 0x5f,
	FadeMs = 0x61,
	FadeMode = 0x62, // then FadeIn or FadeOut
	FadeIn = 0x63,
	FadeOut = 0x64,
	EffectColour = 0x66, // a fade's colour, an overlay's tint
};

// A field given as values: sound settings, the text, font or picture, the colour, the entity, the colour shifts.
void readEffectValues(Effect& e, int field, const Values& v)
{
	const auto real = [&](size_t at, float none) { return at < v.reals.size() ? v.reals[at] : none; };
	const bool sound = e.kind == Effect::Sound || e.kind == Effect::Stream;
	switch (field) {
	case SoundBankAndIndex:
		if (sound) {
			e.bank = v.integer(0), e.sound = v.integer(1);
		}
		break;
	case SoundVolume: e.volume = sound ? real(0, 1) : e.volume; break;
	case SoundFrequency: e.frequency = sound ? real(0, 1) : e.frequency; break;
	case SoundPan: e.pan = sound ? real(0, 0) : e.pan; break;
	case SoundLooping: e.loops = sound ? true : e.loops; break;
	case TextString: e.table = v.integer(0), e.string = v.integer(1); break;
	case TextLiteral: e.text = v.text(0); break;
	case TextFont: e.font = v.text(0); break;
	case OverlayPicture: e.picture = v.text(0); break;
	case FadeMs: e.ms = v.integer(0); break;
	case EffectColour:
		e.colour = 0xff000000u | uint32_t(v.integer(0) & 255) << 16 | uint32_t(v.integer(1) & 255) << 8 | uint32_t(v.integer(2) & 255);
		e.tinted = true;
		break;
	case EntityAnimated:
	case EntityPlain:
	case EntityCollision:
		e.entity = name8(v.text(0));
		e.entityKind = uint8_t(field);
		break;
	case ColourShift:
	case ColourOffset:
	case ColourOffsetSpeed:
		if (e.kind == Effect::Colour) {
			for (size_t ch = 0; ch < 3; ch++) {
				if (field == ColourShift) {
					e.shift[ch] = v.integer(ch);
				}
				else if (field == ColourOffset) {
					e.offset[ch] = v.integer(ch);
				}
				else {
					e.offsetSpeed[ch] = real(ch, 0);
				}
			}
		}
		break;
	default: break; // what only the presentation uses
	}
}

// An overlay's place given (left, top, width, height), centring or speed. False if the field isn't one.
bool readOverlayField(TokenReader& r, Effect& e, int field)
{
	// The place's fields, in the order of Effect::place.
	static constexpr int Place[] = {OverlayLeft, OverlayTop, OverlayWidth, OverlayHeight};
	const int* place = std::find(std::begin(Place), std::end(Place), field);
	if (place != std::end(Place)) {
		const size_t at = size_t(place - std::begin(Place));
		e.place[at] = r.real();
		e.given[at] = true;
		return true;
	}
	if (field == OverlayCentreX) {
		e.given[0] = false;
		return true;
	}
	if (field == OverlayCentreY) {
		e.given[1] = false;
		return true;
	}
	if (field >= OverlaySpeedFirst && field <= OverlaySpeedLast) {
		e.speed[size_t(field - OverlaySpeedFirst)] = r.real();
		return true;
	}
	return false;
}

// An effect's block "{ fields }".
void readEffect(TokenReader& r, Effect& e)
{
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int field = r.keyword();
		if (field == FadeMode) {
			const int mode = r.keyword();
			e.in = mode == FadeIn;
			e.out = mode == FadeOut;
			continue;
		}
		if ((e.kind == Effect::Text || e.kind == Effect::Picture) && readOverlayField(r, e, field)) {
			continue;
		}
		readEffectValues(e, field, valuesAfter(r));
	}
}

// A counted list of one kind's effects (text and pictures white unless tinted).
void readEffects(TokenReader& r, Effect::Kind kind, std::vector<Effect>& effects)
{
	const uint32_t n = r.openCounted();
	for (uint32_t i = 0; i < n && !r.failed(); i++) {
		r.keyword();
		Effect e;
		e.kind = kind;
		e.name = name8(r.text());
		if (e.kind == Effect::Text || e.kind == Effect::Picture) {
			e.colour = 0xffffffff;
		}
		readEffect(r, e);
		effects.push_back(e);
	}
	r.close();
}

} // namespace

bool CinemaFile::decodeEffects(std::span<const uint8_t> bytes)
{
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	const auto names = [&](std::vector<std::string>& out) {
		const uint32_t n = r.openCounted();
		for (uint32_t i = 0; i < n && !r.failed(); i++) {
			out.push_back(std::string(r.text()));
		}
		r.close();
	};
	// Links name their effects: resolved once every list is read.
	struct Named {
		uint8_t channel, kind;
		bool start;
		std::string cue, effect;
	};
	std::vector<Named> named;
	std::vector<std::string> unused;
	while (!r.failed() && !r.atEnd()) {
		const int key = r.keyword();
		switch (key) {
		case EffectsParticleSets: names(unused); continue; // particle sets
		case EffectsStringTables: names(stringTables); continue;
		case EffectsFonts: names(unused); continue; // fonts
		case EffectsImageLists: names(imageLists); continue;
		case EffectsSoundBanks: names(soundBanks); continue;
		default: break;
		}
		if (key >= LinkFirst && key <= LinkLast) {
			const uint32_t n = r.openCounted();
			for (uint32_t i = 0; i < n && !r.failed(); i++) {
				r.keyword();
				Named l{uint8_t(key), 0, true, name8(r.text()), {}};
				r.expect(Token::Open);
				l.kind = uint8_t(r.keyword());
				l.start = r.keyword() == LinkStart;
				l.effect = name8(r.text());
				r.close();
				named.push_back(l);
			}
			r.close();
			continue;
		}
		static const int Kinds[] = {Effect::Colour, Effect::Sound, Effect::Stream, Effect::Particles, Effect::Fade, Effect::Text, Effect::Picture};
		if (std::find(std::begin(Kinds), std::end(Kinds), key) == std::end(Kinds)) {
			return false;
		}
		readEffects(r, Effect::Kind(key), effects);
	}
	for (const Named& l : named) {
		Link& link = links.emplace_back();
		link.channel = l.channel;
		link.cue = l.cue;
		link.start = l.start;
		for (size_t e = 0; e < effects.size(); e++) {
			if (effects[e].kind == l.kind && effects[e].name == l.effect) {
				link.effect = int32_t(e);
				break;
			}
		}
		if (link.effect < 0) {
			return false;
		}
	}
	return !r.failed();
}

namespace {

// The circuit list's fields (LEGORACE.CRB).
enum : int {
	CircuitDrivers = 0x28,
	CircuitNumber = 0x29,
	CircuitPartSet = 0x2a, // (a string index, the circuit's number again)
	CircuitNext = 0x2b,
};

// The race list's (LEGORACE.RCB).
enum : int {
	RacePosition = 0x28,
	RaceFolder = 0x29,
	RaceCircuit = 0x2a,
	RaceDisplay = 0x2b,
	RaceMirrored = 0x2c,
	RaceTheme = 0x2d,
	RaceFeatured = 0x2e,
};

} // namespace

int32_t CircuitTable::find(std::string_view name) const
{
	for (size_t i = 0; i < circuits.size(); i++) {
		if (circuits[i].name == name) {
			return int32_t(i);
		}
	}
	return -1;
}

bool CircuitTable::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	return forEachListed(bytes, [&](uint32_t i, const std::string& name, int key, TokenReader& r) {
		if (i >= circuits.size()) {
			circuits.emplace_back().name = name;
		}
		Circuit& c = circuits.back();
		switch (key) {
		case CircuitDrivers: {
			const uint32_t count = r.openCounted();
			for (uint32_t d = 0; d < count && !r.failed(); d++) {
				c.drivers.push_back(name8(r.text()));
			}
			r.close();
			break;
		}
		case CircuitNumber: r.integer(); break;
		case CircuitPartSet: c.partSet = r.integer(); break;
		case CircuitNext: c.next = name8(r.text()); break;
		default: return false;
		}
		return true;
	});
}

int32_t RaceTable::find(std::string_view name) const
{
	for (size_t i = 0; i < races.size(); i++) {
		if (races[i].name == name) {
			return int32_t(i);
		}
	}
	return -1;
}

bool RaceTable::decode(std::span<const uint8_t> bytes, CircuitTable& table)
{
	*this = {};
	std::vector<std::string> circuitNames;
	const bool ok = forEachListed(bytes, [&](uint32_t i, const std::string& name, int key, TokenReader& r) {
		if (i >= races.size()) {
			races.emplace_back().name = name;
			circuitNames.emplace_back();
		}
		Race& race = races.back();
		switch (key) {
		case RacePosition: race.position = r.integer(); break;
		case RaceFolder: race.folder = r.text(); break;
		case RaceCircuit: circuitNames.back() = name8(r.text()); break;
		case RaceDisplay: race.display = r.integer(); break;
		case RaceMirrored: race.mirrored = true; break;
		case RaceTheme: race.theme = name8(r.text()); break;
		case RaceFeatured: race.featured = name8(r.text()); break;
		default: return false;
		}
		return true;
	});
	if (!ok) {
		return false;
	}
	for (size_t i = 0; i < races.size(); i++) {
		if (circuitNames[i].empty()) {
			continue;
		}
		races[i].circuit = table.find(circuitNames[i]);
		if (races[i].circuit < 0 || races[i].position < 0 || races[i].position > 3) {
			return false;
		}
		table.circuits[size_t(races[i].circuit)].races[size_t(races[i].position)] = int32_t(i);
	}
	return true;
}

} // namespace lr
