#pragma once

// The menus' rule for placing one rect in another.

#include "LRData/Menus.h"

namespace lr {

// "Centre a within b": b's sides move to a's, inset by half the difference where a is the larger (sizes compared as
// unsigned, so a negative size counts as huge). QUIRK: an odd difference leaves b a pixel larger than it was.
inline void centre(const MenuRect& a, MenuRect& b)
{
	const int32_t wa = a.width(), wb = b.width(), ha = a.height(), hb = b.height();
	const int32_t dx = uint32_t(wa) > uint32_t(wb) ? (wa - wb) >> 1 : 0, dy = uint32_t(ha) > uint32_t(hb) ? (ha - hb) >> 1 : 0;
	b = {a.left + dx, a.top + dy, a.right - dx, a.bottom - dy};
}

} // namespace lr
