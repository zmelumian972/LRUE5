#pragma once

// The menus' minifig animations, timed by their part files: a part plays at its rate from frame 0 and has played to
// its end once its position (in frames, summed each frame in float) reaches its last frame.

#include "LRData/Cars.h"

#include <algorithm>
#include <string_view>

namespace lr {

inline int32_t partIndex(const AnimationFile& file, std::string_view name)
{
	for (size_t i = 0; i < file.parts.size(); i++) {
		if (file.parts[i].name == name) {
			return int32_t(i);
		}
	}
	return -1;
}

inline bool partEnded(const AnimationFile& file, int32_t part, float position)
{
	return part >= 0 && position >= float(file.parts[size_t(part)].frames - 1);
}

inline float advance(const AnimationFile& file, int32_t part, float position, int32_t dt)
{
	return part >= 0 ? position + float(dt * file.parts[size_t(part)].rate) / 1000.0f : position;
}

// Where a part is, in frames, ms after it started blending in over 200 ms (blended: not from the first frame).
inline float blendedFrame(const AnimationFile& file, int32_t part, int32_t ms, bool blended)
{
	return float(std::max(0, ms - (blended ? 200 : 0)) * file.parts[size_t(part)].rate) / 1000.0f;
}

} // namespace lr
