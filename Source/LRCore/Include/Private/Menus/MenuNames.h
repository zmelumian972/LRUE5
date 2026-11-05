#pragma once

// The numbers the screens name things by: MENUNAME.SRF's bindings and styles, MENUTEXT.SRF's strings.

#include <cstdint>

namespace lr {

// MENUNAME.SRF's numbers for the bindings and styles the screens use.
enum : uint32_t {
	StyleMenuFont = 55,
	StyleBanner = 58,
	StylePicker = 59,
	NameTextBox = 61,
	NameImageBox = 62,
	NameBack = 63,
	StyleItem = 66,
	StyleLeftArrow = 67,
	StyleCancel = 69,
	StyleCheck = 70,
	NameBackdrop = 73,
	StyleTextSelector = 76,
	StyleImageSelector = 77,
	NameLogo = 82,
	NameContext = 150,
	NameDialog = 153,
	NamePlatform = 208,
	StyleSoundSlider = 236,
};

constexpr int32_t TextOk = 114, TextYes = 115, TextNo = 116, TextCancel = 31, TextContinue = 32;

} // namespace lr
