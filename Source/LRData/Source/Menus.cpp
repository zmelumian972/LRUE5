#include "LRData/Menus.h"
#include "LRData/Archive.h"

#include "Bytes.h"
#include "LRData/Image.h"
#include "LRData/Tokens.h"
#include "MenuValues.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

namespace lr {

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
MenuRect rectAt(const std::vector<int32_t>& v, size_t at)
{
	return at + 4 > v.size() ? MenuRect{} : MenuRect{v[at], v[at + 1], v[at + 2], v[at + 3]};
}

namespace {

uint32_t rgbKey(const std::vector<int32_t>& v)
{
	return v.size() < 3 ? 0 : 0xff000000u | uint32_t(v[0] & 255) << 16 | uint32_t(v[1] & 255) << 8 | uint32_t(v[2] & 255);
}

// The image list's (.IDB).
enum : int32_t {
	ImageFlip = 0x28,
	ImageBmp = 0x29,
	ImageTga = 0x2a,
	ImageColourKey = 0x2b, // I r, I g, I b
	ImageTint = 0x2c, // I r, I g, I b
};

// The font list's (.FDB).
enum : int32_t {
	FontBmp = 0x28,
	FontTga = 0x29,
	FontColourKey = 0x2a, // I r, I g, I b
	FontChars = 0x2b, // [ chars ]
	FontSpacing = 0x2c,
	FontColour = 0x2d, // I r, I g, I b
};

// A style's fields (.MSB).
enum : int32_t {
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
		case IconTransition: icon.transitionMs = v.integer(0); break;
		default: return false;
		}
	}
	return !r.failed();
}

// "K27 [n] { K27 T name { fields } }": the image and font lists.
template <typename Entry> bool forEachListed(std::span<const uint8_t> bytes, Entry entry)
{
	TokenReader r(bytes);
	uint32_t i = 0;
	r.table(ListEntry, [&] {
		const std::string name = name8(r.text());
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			const int key = r.keyword();
			if (key < 0 || !entry(i, name, key, r)) {
				return false;
			}
		}
		i++;
		return true;
	});
	return !r.failed() && r.atEnd();
}

} // namespace

std::u16string_view StringTable::at(size_t index) const
{
	if (index >= starts.size()) {
		return {};
	}
	return std::u16string_view(units.data() + starts[index]);
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
		units[i] = char16_t(u16(bytes, 4 + 2 * (count + uint64_t(i))));
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
	// The count: a number after any blanks (what follows it ignored).
	const std::string& countLine = lines[1];
	const char* digits = countLine.data() + std::min(countLine.find_first_not_of(" \t"), countLine.size());
	int count = 0;
	if (std::from_chars(digits, countLine.data() + countLine.size(), count).ec != std::errc() || count < 0 ||
		size_t(count) + 2 > lines.size()) {
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
		if (glyphs[middle].code == code) {
			return glyphs[middle];
		}
		if (glyphs[middle].code < code) {
			low = middle + 1;
		}
		else {
			high = middle - 1;
		}
	}
	// Else the space (none: an empty glyph).
	static const Glyph none{};
	const auto blank = std::find_if(glyphs.begin(), glyphs.end(), [](const Glyph& g) { return g.code == 32; });
	return blank != glyphs.end() ? *blank : none;
}

const MenuStyles::Style* MenuStyles::find(std::string_view name) const
{
	return findWhere(styles, [&](const Style& s) { return s.name == name; });
}

bool MenuStyles::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	return forEachSection(bytes, capacity, [&](int section, const std::string& name, TokenReader& r) {
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
				s.font = v.text(0);
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
			case StyleScrollMs: s.scrollMs = v.integer(0); break;
			case StyleWrap: s.wrap = s.maxWidth = v.integer(0); break;
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
			case StyleFrame: s.frame = v.text(0); break;
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

namespace {

// The circuit list's fields (LEGORACE.CRB).
enum : int32_t {
	CircuitDrivers = 0x28,
	CircuitNumber = 0x29,
	CircuitPartSet = 0x2a, // (a string index, the circuit's number again)
	CircuitNext = 0x2b,
};

// The race list's (LEGORACE.RCB).
enum : int32_t {
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
	return indexWhere(circuits, [&](const Circuit& c) { return c.name == name; });
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
		case CircuitDrivers: r.counted([&] { c.drivers.push_back(name8(r.text())); }); break;
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
	return indexWhere(races, [&](const Race& r) { return r.name == name; });
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
		table.circuits[races[i].circuit].races[races[i].position] = int32_t(i);
	}
	return true;
}

} // namespace lr
