#pragma once

// The executable's tables the rules read (the random table, the cheat codes, the setup screens'
// frame colours, circuit points, the curse and lava hazards' spots, the car builder's eyes). They come from the data
// cache, never from the source: MenuData::load (or a test) hands them over once, before anything runs.

#include "LRCore/Common/Api.h"
#include "LRData/Executable.h"

namespace lr {

LRCORE_API void useExecutableTables(const ExecutableTables& tables);
LRCORE_API const ExecutableTables& executableTables();

} // namespace lr
