#pragma once

// LR_ASSERT(cond): the core's own invariants, such as an index it stored into a table it owns, checked in debug builds
// only (NDEBUG not defined; a failure aborts). Without it, it is nothing and cond isn't evaluated: cond must have no
// side effects. File input isn't for it: the readers validate that and fail.

#include <cassert>

#define LR_ASSERT(cond) assert(cond)
