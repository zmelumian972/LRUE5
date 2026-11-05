// The game's random numbers: one fixed table, read from the executable.

#include "LRCore/Common/Tables.h"
#include "LRCore/Race/World.h"

namespace lr {

namespace {

ExecutableTables g_tables;

} // namespace

void useExecutableTables(const ExecutableTables& tables) { g_tables = tables; }
const ExecutableTables& executableTables() { return g_tables; }

uint16_t randomTable(uint32_t index) { return g_tables.random[index & 1023]; }

} // namespace lr
