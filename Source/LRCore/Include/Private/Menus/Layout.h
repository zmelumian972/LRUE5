#pragma once

// The menus' rules for rects: placing one in another, a point inside one, one cut to another.

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

// (x, y) in r, its edges in.
inline bool inside(const MenuRect& r, int32_t x, int32_t y) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }

// d cut to clip, and s (what d shows: sx, sy of d's pixels to one of s's) cut with it.
inline void cutTo(const MenuRect& clip, MenuRect& d, MenuRect& s, float sx, float sy)
{
	if (d.left < clip.left) {
		s.left += int32_t(float(clip.left - d.left) / sx);
		d.left = clip.left;
	}
	if (d.right > clip.right) {
		s.right -= int32_t(float(d.right - clip.right) / sx);
		d.right = clip.right;
	}
	if (d.top < clip.top) {
		s.top += int32_t(float(clip.top - d.top) / sy);
		d.top = clip.top;
	}
	if (d.bottom > clip.bottom) {
		s.bottom -= int32_t(float(d.bottom - clip.bottom) / sy);
		d.bottom = clip.bottom;
	}
}

} // namespace lr
