#pragma once

// The race's heads-up displays, drawn by the menus' 2D rules: per player, in its viewport, the banner,
// the race info, the power-up frame and the gadget (RaceDraw.cpp).

#include "LRCore/Common/Api.h"

#include <array>
#include <cstdint>
#include <utility>

namespace lr {

struct Pen;
struct World;

struct LRCORE_API RaceHud {
	// The layers drawn, one per player's viewport: [first, end) of the draw list's elements each.
	std::array<std::pair<uint32_t, uint32_t>, 2> layers = {};
	void draw(const Pen& pen, const World& world);
};

} // namespace lr
