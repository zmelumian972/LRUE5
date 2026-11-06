// The input the menus take: the presentation's USB key codes as the original's, and the frame's events queued with
// the key repeats.

#include "LRCore/Menus/Menus.h"

#include <algorithm>

namespace lr {

uint32_t menuKey(uint32_t usb)
{
	// USB usages 4.. (a..z, 1..0, Enter, Esc, Backspace, Tab, Space, - = [ ] \ # ; ' ` , . /, Caps Lock, F1..F12,
	// Print Screen, Scroll Lock, Pause, Insert, Home, Page Up, Delete, End, Page Down, Right, Left, Down, Up, Num Lock,
	// the keypad's / * - + Enter 1..9 0 .) as DirectInput scan codes.
	static const uint8_t Codes[100] = {0, 0, 0, 0, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18,
		0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x1c,
		0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a, 0x1b, 0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0xb7, 0x46, 0xc5, 0xd2, 0xc7, 0xc9, 0xd3, 0xcf, 0xd1, 0xcd, 0xcb, 0xd0, 0xc8, 0x45, 0xb5,
		0x37, 0x4a, 0x4e, 0x9c, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49, 0x52, 0x53};
	// The modifiers (224..231: left Ctrl, Shift, Alt, GUI, then the right ones) and the Menu key.
	static const uint8_t Modifiers[8] = {0x1d, 0x2a, 0x38, 0xdb, 0x9d, 0x36, 0xb8, 0xdc};
	const uint32_t code = usb < 100 ? Codes[usb] : usb >= 224 && usb < 232 ? Modifiers[usb - 224] : usb == 101 ? 0xdd : 0;
	return code ? SourceKeyboard | code : 0;
}

char16_t menuCharacter(uint32_t usb)
{
	static const char Punctuation[] = "-=[]\\;'`,./";
	return usb >= 4 && usb <= 29			  ? char16_t('A' + usb - 4)
		: usb >= 30 && usb <= 38			  ? char16_t('1' + usb - 30)
		: usb == 39							  ? u'0'
		: usb == 40							  ? u'\r'
		: usb == 42							  ? u'\b'
		: usb == 44							  ? u' '
		: usb >= 45 && usb <= 56 && usb != 50 ? char16_t(Punctuation[usb - 45 - (usb > 50)])
											  : 0;
}

void Ui::queueKey(MenuKey k)
{
	if (queued < queue.size()) {
		queue[queued++] = k;
	}
}

// A key or button going down (held, its device's repeat restarted) or up.
void Ui::holdKey(uint32_t key, bool down)
{
	const int device = (key >> 28) == 2 ? 1 : 0;
	if (down) {
		m_repeatMs[device] = 500;
		if (std::find(held.begin(), held.end(), key) == held.end()) {
			held.push_back(key);
		}
	}
	else {
		held.erase(std::remove(held.begin(), held.end(), key), held.end());
	}
	queueKey({key, down, false});
}

// The frame's events, then the repeats: each device's held keys again, in key order, 500 ms after a press and every
// 250 ms after that.
void Ui::queueInput(std::span<const MenuKey> keys, uint32_t buttons, int32_t dt)
{
	for (const MenuKey& k : keys) {
		if ((k.key >> 28) == (SourceCharacter >> 28)) {
			queueKey(k); // typed, not held
		}
		else {
			holdKey(k.key, k.down);
		}
	}
	for (uint32_t b = 0; b < 4; b++) {
		const bool was = mouseButtons & (1u << b), now = buttons & (1u << b);
		if (was != now) {
			holdKey(SourceMouse | b, now);
		}
	}
	mouseButtons = buttons;
	for (int device = 0; device < 2; device++) {
		m_repeatMs[device] -= dt;
		if (m_repeatMs[device] > 0) {
			continue;
		}
		std::vector<uint32_t> repeats;
		for (uint32_t key : held) {
			if (int((key >> 28) == 2) == device) {
				repeats.push_back(key);
			}
		}
		std::sort(repeats.begin(), repeats.end());
		for (uint32_t key : repeats) {
			queueKey({key, true, true});
		}
		m_repeatMs[device] = repeats.empty() ? 500 : 250;
	}
}

} // namespace lr
