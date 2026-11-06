// The game mode's input as the core numbers it (LRCoreRun.h): UE's keys as USB usages, a pad's buttons as SDL's.

#include "LRCoreRun.h"

// UE's keys as USB usages.
uint32 UsbOf(const FKey& Key)
{
	static const TMap<FKey, uint32> Codes = [] {
		TMap<FKey, uint32> M;
		const FKey Letters[26] = {EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J,
			EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V,
			EKeys::W, EKeys::X, EKeys::Y, EKeys::Z};
		for (uint32 i = 0; i < 26; i++) {
			M.Add(Letters[i], 4 + i);
		}
		const FKey Digits[10] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight,
			EKeys::Nine, EKeys::Zero};
		for (uint32 i = 0; i < 10; i++) {
			M.Add(Digits[i], 30 + i);
		}
		const FKey Functions[12] = {EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9,
			EKeys::F10, EKeys::F11, EKeys::F12};
		for (uint32 i = 0; i < 12; i++) {
			M.Add(Functions[i], 58 + i);
		}
		const FKey Pad[10] = {EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive,
			EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine, EKeys::NumPadZero};
		for (uint32 i = 0; i < 10; i++) {
			M.Add(Pad[i], 89 + i);
		}
		const TPair<FKey, uint32> Others[] = {{EKeys::Enter, 40}, {EKeys::Escape, 41}, {EKeys::BackSpace, 42}, {EKeys::Tab, 43},
			{EKeys::SpaceBar, 44}, {EKeys::Hyphen, 45}, {EKeys::Equals, 46}, {EKeys::LeftBracket, 47}, {EKeys::RightBracket, 48},
			{EKeys::Backslash, 49}, {EKeys::Semicolon, 51}, {EKeys::Apostrophe, 52}, {EKeys::Tilde, 53}, {EKeys::Comma, 54},
			{EKeys::Period, 55}, {EKeys::Slash, 56}, {EKeys::CapsLock, 57}, {EKeys::ScrollLock, 71}, {EKeys::Pause, 72},
			{EKeys::Insert, 73}, {EKeys::Home, 74}, {EKeys::PageUp, 75}, {EKeys::Delete, 76}, {EKeys::End, 77}, {EKeys::PageDown, 78},
			{EKeys::Right, 79}, {EKeys::Left, 80}, {EKeys::Down, 81}, {EKeys::Up, 82}, {EKeys::NumLock, 83}, {EKeys::Divide, 84},
			{EKeys::Multiply, 85}, {EKeys::Subtract, 86}, {EKeys::Add, 87}, {EKeys::Decimal, 99}, {EKeys::LeftControl, 224},
			{EKeys::LeftShift, 225}, {EKeys::LeftAlt, 226}, {EKeys::LeftCommand, 227}, {EKeys::RightControl, 228}, {EKeys::RightShift, 229},
			{EKeys::RightAlt, 230}, {EKeys::RightCommand, 231}};
		for (const auto& [K, Code] : Others) {
			M.Add(K, Code);
		}
		return M;
	}();
	const uint32* Code = Codes.Find(Key);
	return Code ? *Code : 0;
}

// A pad's buttons as SDL numbers them (the bindings'); -1 not a button.
int32 PadButtonOf(const FKey& Key)
{
	static const FKey Buttons[15] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left,
		EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_Special_Left, FKey(), EKeys::Gamepad_Special_Right, EKeys::Gamepad_LeftThumbstick,
		EKeys::Gamepad_RightThumbstick, EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_DPad_Up,
		EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right};
	for (int32 i = 0; i < 15; i++) {
		if (Buttons[i].IsValid() && Buttons[i] == Key) {
			return i;
		}
	}
	return -1;
}
