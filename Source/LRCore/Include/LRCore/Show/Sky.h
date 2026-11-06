#pragma once

// The show's sky (Sky.cpp): the race file's sky states, the one shown and its keyframe, a blend
// toward another, the rings' colours now, whether the dome and the sky's world are shown.

#include "LRCore/Common/Api.h"
#include "LRCore/Show/Show.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"

#include <array>
#include <cstdint>
#include <string>

namespace lr {

class Cache;

// The sky: its state, keyframe and clock, a blend toward another state, the rings' colours now (horizon, middle,
// high; 0..255); whether a sky was loaded and the dome and the sky's world are shown.
struct LRCORE_API Sky {
	SkyFile file;
	bool loaded = false, domeShown = true, worldShown = true;
	int32_t state = 0, key = 0, keyMs = 0;
	int32_t blendFrom = -1, blendMs = 0, blendLength = 0;
	std::array<std::array<float, 3>, 3> colours = {};

	// The race file's sky (in the track's folder `dir`), at its first state.
	void load(const Cache& cache, const std::string& dir, const RaceFile& race);
	// A Sky event: the dome and world shown or hidden, a blend to another state.
	void event(const RaceEvent& e);
	void update(int32_t ms);
};

} // namespace lr
