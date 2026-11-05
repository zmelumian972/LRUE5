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
uint32_t rgbKey(const std::vector<int32_t>& v)
{
	return v.size() < 3 ? 0 : 0xff000000u | uint32_t(v[0] & 255) << 16 | uint32_t(v[1] & 255) << 8 | uint32_t(v[2] & 255);
}
MenuRect rectAt(const std::vector<int32_t>& v, size_t at)
{
	return at + 4 > v.size() ? MenuRect{} : MenuRect{v[at], v[at + 1], v[at + 2], v[at + 3]};
}

// The icon block "{ 2b sounds | 2c state rects | 2a state colours | 2d transition }".
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
template <typename Entry>
bool forEachSection(const Tokens& tokens, int32_t& capacity, Entry entry)
{
	TokenReader r(tokens);
	if (r.keyword() != 0x27) {
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
template <typename Entry>
bool forEachListed(std::span<const uint8_t> bytes, Entry entry)
{
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != 0x27) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != 0x27) {
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
		case 0x28: e.flip = true; break;
		case 0x29: e.targa = false; break;
		case 0x2a: e.targa = true; break;
		case 0x2b:
			e.keyed = true;
			e.key = rgbKey(v.integers);
			break;
		case 0x2c: e.tint = rgbKey(v.integers); break;
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
		case 0x28: f.targa = false; break;
		case 0x29: f.targa = true; break;
		case 0x2a:
			f.keyed = true;
			f.key = rgbKey(valuesAfter(r).integers);
			break;
		case 0x2b: // [ chars ]: one-character texts or codes
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
		case 0x2c: f.spacing = r.integer(); break;
		case 0x2d: f.colour = rgbKey(valuesAfter(r).integers); break;
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
			if (key == 0x3a) {
				if (!readIcon(r, s.icon)) {
					return false;
				}
				continue;
			}
			const Values v = valuesAfter(r);
			switch (key) {
			case 0x28: s.images = v.texts; break;
			case 0x29:
				s.font = v.texts.empty() ? std::string() : v.texts[0];
				for (size_t i = 0; i < 6 && i < v.texts.size(); i++) {
					s.fonts[i] = v.texts[i];
				}
				break;
			case 0x2a: s.colours = coloursOf(v.integers); break;
			case 0x2b:
				for (size_t i = 0; i < 3 && i < v.integers.size(); i++) {
					s.sounds[i] = v.integers[i];
				}
				break;
			case 0x2c: break; // a text button's two rects and a multi-state's: unused
			case 0x2d: s.scrollMs = v.integers.empty() ? 0 : v.integers[0]; break;
			case 0x2e:
				s.wrap = s.maxWidth = v.integers.empty() ? 0 : v.integers[0];
				break;
			case 0x2f:
				if (section == Carousel && v.integers.size() >= 2) {
					s.pageMode = v.integers[0] != 0;
					s.wrapAround = v.integers[1] != 0;
				}
				else if (!v.integers.empty()) {
					s.centred = v.integers[0] != 0;
				}
				break;
			case 0x30:
				for (size_t i = 0; i + 4 <= v.integers.size(); i += 4) {
					s.regionRects.push_back(rectAt(v.integers, i));
				}
				break;
			case 0x31: s.regions = v.integers; break;
			case 0x32:
				for (size_t i = 0; i < 2 && i < v.texts.size(); i++) {
					s.track[i] = v.texts[i];
				}
				break;
			case 0x34: s.frame = v.texts.empty() ? std::string() : v.texts[0]; break;
			case 0x35:
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

// The widget block "{ 2f rect | 30 id | 31 parent | 32 attach | 2a colour }".
bool readWidgetBlock(TokenReader& r, MenuLayout::Binding& b, int32_t& nextId)
{
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		const Values v = valuesAfter(r);
		switch (key) {
		case 0x2f: b.rect = rectAt(v.integers, 0); break;
		case 0x30:
			if (!v.integers.empty()) {
				b.id = v.integers[0];
				nextId = b.id + 1;
			}
			break;
		case 0x31: b.parent = v.texts.empty() ? std::string() : v.texts[0]; break;
		case 0x32: b.attach = v.integers.empty() || v.integers[0] != 0; break;
		case 0x2a:
			b.hasColour = true;
			b.colour = colourAt(v.integers, 0);
			break;
		default: return false;
		}
	}
	return !r.failed();
}

bool isIcon(MenuLayout::Kind k)
{
	using L = MenuLayout;
	return k == L::ImageButton || k == L::MultiState || k == L::Hotspot || k == L::Selector || k == L::Options ||
		k == L::TextField || k == L::TextButton;
}

// A binding's fields (after the widget block's), its kind's own first, then an icon's.
bool readBindingFields(TokenReader& r, MenuLayout::Binding& b, int32_t& nextId)
{
	using L = MenuLayout;
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		if (key == 0x36) {
			if (!readWidgetBlock(r, b, nextId)) {
				return false;
			}
			continue;
		}
		if (key == 0x3b && b.kind == L::Hotspot) { // the image button's body inside
			int32_t ignored = 0;
			L::Binding inner;
			inner.kind = L::ImageButton;
			if (!readBindingFields(r, inner, ignored)) {
				return false;
			}
			b.images = inner.images;
			continue;
		}
		const Values v = valuesAfter(r);
		const auto text = [&](size_t i) { return i < v.texts.size() ? v.texts[i] : std::string(); };
		const auto integer = [&](size_t i) { return i < v.integers.size() ? v.integers[i] : 0; };
		bool own = true;
		switch (b.kind) {
		case L::Image:
			if (key == 0x28) {
				b.images = v.texts;
			}
			else if (key == 0x2a) {
				b.hasColour = true;
				b.colour = colourAt(v.integers, 0);
			}
			else {
				own = false;
			}
			break;
		case L::Label:
			if (key == 0x2a) {
				b.hasColour = true;
				b.colour = colourAt(v.integers, 0);
			}
			else if (key == 0x29) { // the font, then always a colour
				b.fonts = {text(0)};
				b.hasColour = true;
				b.colour = colourAt(v.integers, 0);
			}
			else if (key == 0x33) {
				b.wrap = integer(0);
			}
			else {
				own = false;
			}
			break;
		case L::Frame:
			if (key == 0x33) {
				b.hasFill = integer(0) != 0;
			}
			else if (key == 0x28) {
				b.images = v.texts;
			}
			else if (key == 0x2a) {
				b.hasColour = true;
				b.colours = coloursOf(v.integers);
			}
			else {
				own = false;
			}
			break;
		case L::ImageButton:
		case L::MultiState: own = key == 0x28; b.images = own ? v.texts : b.images; break;
		case L::Hotspot: own = key == 0x28; b.world = own ? text(0) : b.world; break; // the highlight image
		case L::Selector:
			if (key == 0x3b) {
				b.buttons = {text(0), text(1)};
			}
			else if (key == 0x3a) {
				b.frame = text(0);
			}
			else if (key == 0x33) {
				b.unselectedInput = integer(0) != 0;
			}
			else {
				own = false;
			}
			break;
		case L::Ring:
			if (key == 0x33) {
				b.numbers = {integer(0), integer(1), 0};
				b.step = v.reals.empty() ? 0 : v.reals[0];
			}
			else if (key == 0x2f) {
				for (size_t i = 0; i + 4 <= v.integers.size(); i += 4) {
					b.slots.push_back(rectAt(v.integers, i));
				}
			}
			else if (key == 0x2e) {
				for (size_t i = 0; i < 9 && i < v.reals.size(); i++) {
					b.camera[i] = v.reals[i];
				}
			}
			else {
				return false;
			}
			break;
		case L::Options:
			if (key == 0x33) {
				b.steps = integer(0);
				b.value = integer(1);
			}
			else if (key == 0x3b) {
				b.buttons = {text(0), text(1)};
			}
			else if (key == 0x38) {
				b.track = {text(0), text(1)};
			}
			else if (key == 0x28) {
				b.images = v.texts;
			}
			else if (key == 0x2b) {
				b.icon.hasSounds = true;
				for (size_t i = 0; i < 5; i++) {
					b.icon.sounds[i] = integer(i);
				}
				b.hasEditSounds = v.integers.size() > 5;
				b.editSounds = {integer(5), integer(6), 0, 0};
			}
			else {
				own = false;
			}
			break;
		case L::SceneView:
			if (key == 0x2d) {
				b.world = text(0);
			}
			else if (key == 0x3a) {
				b.frame = text(0);
			}
			else if (key == 0x2e) {
				for (size_t i = 0; i < 9 && i < v.reals.size(); i++) {
					b.camera[i] = v.reals[i];
				}
			}
			else if (key == 0x33) {
				b.numbers[0] = integer(0);
				b.numbers[1] = integer(1);
			}
			else if (key == 0x35) {
				b.numbers[2] = integer(0);
			}
			else {
				return false;
			}
			break;
		case L::TextField:
			if (key == 0x29) {
				b.fonts = {text(0)};
			}
			else if (key == 0x33) {
				b.maxLength = integer(0);
			}
			else if (key == 0x2b) {
				b.icon.hasSounds = true;
				for (size_t i = 0; i < 5; i++) {
					b.icon.sounds[i] = integer(i);
				}
				b.hasEditSounds = v.integers.size() > 5;
				b.editSounds = {integer(5), integer(6), integer(7), integer(8)};
			}
			else {
				own = false;
			}
			break;
		case L::SceneReference:
			if (key == 0x33) {
				b.autoAdvance = integer(0) != 0;
				b.startFrame = integer(1);
			}
			else if (key == 0x2d) {
				b.world = text(0);
			}
			else {
				return false;
			}
			break;
		case L::TextButton:
			if (key == 0x29) {
				b.fonts = v.texts;
			}
			else if (key == 0x28) {
				b.images = v.texts;
			}
			else if (key == 0x2a) {
				b.colours = coloursOf(v.integers);
			}
			else if (key == 0x33) {
				b.wrap = integer(0);
			}
			else {
				own = false;
			}
			break;
		case L::Region: return false;
		}
		if (own) {
			continue;
		}
		if (!isIcon(b.kind)) {
			return false;
		}
		switch (key) {
		case 0x33: b.enabled = integer(0) != 0; break;
		case 0x32: b.iconAttach = integer(0) != 0; break;
		case 0x31:
		case 0x34: break; // a name and a number nothing reads
		case 0x2c: b.help = integer(0); break;
		case 0x2a:
			b.icon.hasColours = true;
			for (size_t i = 0; i < 6; i++) {
				b.icon.colours[i] = colourAt(v.integers, i * 4);
			}
			break;
		case 0x2b:
			b.icon.hasSounds = true;
			for (size_t i = 0; i < 5; i++) {
				b.icon.sounds[i] = integer(i);
			}
			break;
		default: return false;
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
		if (key == 0x28) {
			const uint32_t n = r.openCounted();
			for (uint32_t i = 0; i < n && !r.failed(); i++) {
				worlds.emplace_back(name8(r.text()));
			}
			r.close();
			continue;
		}
		if (key != 0x27) {
			return false;
		}
		const uint32_t count = r.openCounted();
		for (uint32_t i = 0; i < count && !r.failed(); i++) {
			r.keyword();
			Scene& s = scenes.emplace_back();
			s.name = name8(r.text());
			int32_t lastCamera = 0;
			r.expect(Token::Open);
			// The cue blocks in any order: kept as cameras, models, triggers, ambients, lights.
			std::array<std::vector<Cue>, 5> byKind;
			while (!r.failed() && !r.next(Token::Close)) {
				const int field = r.keyword();
				if (field == 0x3b || field == 0x2c) {
					(field == 0x3b ? s.rate : s.frames) = r.integer();
					continue;
				}
				static const int Blocks[5] = {0x29, 0x2e, 0x37, 0x35, 0x3a};
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
					int32_t duration = 0;
					r.expect(Token::Open);
					const auto number = [&r] { return r.next(Token::Integer) ? float(r.tokens().list[r.position() - 1].integer) : r.real(); };
					while (!r.failed() && !r.next(Token::Close)) {
						const int cueKey = r.keyword();
						switch (cueKey) {
						case 0x2b: cue.start = r.integer(); break;
						case 0x2c: duration = r.integer(); break;
						case 0x2a: cue.target = name8(r.text()); break;
						case 0x2d: cue.part = r.integer(); break;
						case 0x2f:
						case 0x30:
						case 0x31:
							cue.entityKind = uint8_t(cueKey);
							cue.target = name8(r.text());
							break;
						case 0x32:
							cue.entityKind = 0x32;
							cue.entityAt = {r.integer(), r.integer()};
							break;
						case 0x33:
							for (float& x : cue.position) {
								x = number();
							}
							break;
						case 0x34:
							for (float& x : cue.forward) {
								x = number();
							}
							for (float& x : cue.up) {
								x = number();
							}
							break;
						case 0x36: {
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
						case 0x38:
							for (int32_t& x : cue.colour) {
								x = int32_t(number()) & 255;
							}
							break;
						case 0x39:
							for (float& x : cue.direction) {
								x = number();
							}
							break;
						case 0x3c: cue.blink = {r.integer(), r.integer()}; break;
						default:
							if (cueKey < 0) {
								return false;
							}
							break;
						}
						r.skipValues();
						if (!r.atEnd() && r.tokens().list[r.position()].kind == Token::OpenCount) {
							r.skipCounted();
						}
					}
					cue.end = cue.start + duration;
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
		}
		r.close();
	}
	return !r.failed() && !scenes.empty();
}

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
		case 0x27: names(unused); continue; // particle sets
		case 0x28: names(stringTables); continue;
		case 0x29: names(unused); continue; // fonts
		case 0x2a: names(imageLists); continue;
		case 0x5c: names(soundBanks); continue;
		default: break;
		}
		if (key >= 0x50 && key <= 0x5b) {
			const uint32_t n = r.openCounted();
			for (uint32_t i = 0; i < n && !r.failed(); i++) {
				r.keyword();
				Named l{uint8_t(key), 0, true, name8(r.text()), {}};
				r.expect(Token::Open);
				l.kind = uint8_t(r.keyword());
				l.start = r.keyword() == 0x4e;
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
		const uint32_t n = r.openCounted();
		for (uint32_t i = 0; i < n && !r.failed(); i++) {
			r.keyword();
			Effect e;
			e.kind = Effect::Kind(key);
			e.name = name8(r.text());
			if (e.kind == Effect::Text || e.kind == Effect::Picture) {
				e.colour = 0xffffffff;
			}
			r.expect(Token::Open);
			while (!r.failed() && !r.next(Token::Close)) {
				const int field = r.keyword();
				if (field == 0x62) {
					const int mode = r.keyword();
					e.in = mode == 0x63;
					e.out = mode == 0x64;
					continue;
				}
				// Overlays: a place given (left, top, width, height), centring, speeds.
				if (e.kind == Effect::Text || e.kind == Effect::Picture) {
					if (field >= 0x43 && field <= 0x48 && field != 0x45 && field != 0x46) {
						const size_t at = field == 0x43 ? 0 : field == 0x44 ? 1 : field == 0x47 ? 2 : 3;
						e.place[at] = r.real();
						e.given[at] = true;
						continue;
					}
					if (field == 0x45 || field == 0x46) {
						e.given[field == 0x45 ? 0 : 1] = false;
						continue;
					}
					if (field >= 0x49 && field <= 0x4c) {
						e.speed[size_t(field - 0x49)] = r.real();
						continue;
					}
				}
				const Values v = valuesAfter(r);
				const auto integer = [&](size_t at) { return at < v.integers.size() ? v.integers[at] : 0; };
				const auto real = [&](size_t at, float none) { return at < v.reals.size() ? v.reals[at] : none; };
				const std::string text = v.texts.empty() ? std::string() : v.texts[0];
				switch (field) {
				case 0x30:
					if (e.kind == Effect::Sound || e.kind == Effect::Stream) {
						e.bank = integer(0), e.sound = integer(1);
					}
					break;
				case 0x32: e.volume = e.kind == Effect::Sound || e.kind == Effect::Stream ? real(0, 1) : e.volume; break;
				case 0x33: e.frequency = e.kind == Effect::Sound || e.kind == Effect::Stream ? real(0, 1) : e.frequency; break;
				case 0x34: e.pan = e.kind == Effect::Sound || e.kind == Effect::Stream ? real(0, 0) : e.pan; break;
				case 0x35: e.loops = e.kind == Effect::Sound || e.kind == Effect::Stream ? true : e.loops; break;
				case 0x40: e.table = integer(0), e.string = integer(1); break;
				case 0x41: e.text = text; break;
				case 0x42: e.font = text; break;
				case 0x4d: e.picture = text; break;
				case 0x61: e.ms = integer(0); break;
				case 0x66:
					e.colour = 0xff000000u | uint32_t(integer(0) & 255) << 16 | uint32_t(integer(1) & 255) << 8 | uint32_t(integer(2) & 255);
					e.tinted = true;
					break;
				case 0x5d:
				case 0x5e:
				case 0x5f:
					e.entity = name8(text);
					e.entityKind = uint8_t(field);
					break;
				case 0x2c:
				case 0x2d:
				case 0x2e:
					if (e.kind == Effect::Colour) {
						for (size_t ch = 0; ch < 3; ch++) {
							if (field == 0x2c) {
								e.shift[ch] = integer(ch);
							}
							else if (field == 0x2d) {
								e.offset[ch] = integer(ch);
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
			effects.push_back(e);
		}
		r.close();
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
		case 0x28: {
			const uint32_t count = r.openCounted();
			for (uint32_t d = 0; d < count && !r.failed(); d++) {
				c.drivers.push_back(name8(r.text()));
			}
			r.close();
			break;
		}
		case 0x29: r.integer(); break;
		case 0x2a: c.partSet = r.integer(); break;
		case 0x2b: c.next = name8(r.text()); break;
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
		case 0x28: race.position = r.integer(); break;
		case 0x29: race.folder = r.text(); break;
		case 0x2a: circuitNames.back() = name8(r.text()); break;
		case 0x2b: race.display = r.integer(); break;
		case 0x2c: race.mirrored = true; break;
		case 0x2d: race.theme = name8(r.text()); break;
		case 0x2e: race.featured = name8(r.text()); break;
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
