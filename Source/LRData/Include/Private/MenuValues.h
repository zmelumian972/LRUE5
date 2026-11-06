#pragma once

// What the menu files' readers share (Menus.cpp, MenuLayout.cpp, CinemaFiles.cpp): names kept to 8 characters, a
// keyword's mixed values, colours and rectangles from integers, and the sectioned files' walk.

#include "LRData/Menus.h"
#include "LRData/Tokens.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

// Names in the menu files are kept to 8 characters.
inline std::string name8(std::string_view text) { return std::string(text.substr(0, 8)); }

// A keyword's values: the integers, reals and texts after it (a list of mixed values, as the files have them).
struct Values {
	std::vector<int32_t> integers;
	std::vector<float> reals;
	std::vector<std::string> texts;
	// The i-th text or integer, else empty or 0.
	std::string text(size_t i) const { return i < texts.size() ? texts[i] : std::string(); }
	int32_t integer(size_t i) const { return i < integers.size() ? integers[i] : 0; }
};

// The values after a keyword, up to the next keyword or bracket.
Values valuesAfter(TokenReader& r);
// Four integers A R G B at `at` (white past the end); every whole four's.
uint32_t colourAt(const std::vector<int32_t>& v, size_t at);
std::vector<uint32_t> coloursOf(const std::vector<int32_t>& v);
// Four integers left top right bottom at `at` (empty past the end).
MenuRect rectAt(const std::vector<int32_t>& v, size_t at);

// The files' first keyword: a style table's and a layout's "K27 I capacity", a list's "K27 [n] { K27 T name ... }".
enum : int32_t {
	KeyCapacity = 0x27,
	ListEntry = 0x27,
};

// The icon block "{ 2b sounds | 2c state rects | 2a state colours | 2d transition }"; a binding's icon fields use
// the same sounds and colours.
enum : int32_t { IconSounds = 0x2b, IconRects = 0x2c, IconColours = 0x2a, IconTransition = 0x2d };

// "K27 I capacity, then K<s> [n] { K<s'> T name body } per section": calls entry(section, name, r) after each name.
template <typename Entry> bool forEachSection(std::span<const uint8_t> bytes, int32_t& capacity, Entry entry)
{
	TokenReader r(bytes);
	if (r.keyword() != KeyCapacity) {
		return false;
	}
	capacity = r.integer();
	while (!r.failed() && !r.atEnd()) {
		const int section = r.keyword();
		if (section < 0) {
			return false;
		}
		r.counted([&] { return r.keyword() >= 0 && entry(section, name8(r.text()), r); });
	}
	return !r.failed();
}

} // namespace lr
