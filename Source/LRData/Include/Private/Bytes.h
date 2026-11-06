#pragma once

// Little-endian reads from a byte span. Callers check bounds with fits() first.

#include <cstdint>
#include <cstring>
#include <span>

namespace lr {

inline bool fits(std::span<const uint8_t> bytes, uint64_t at, uint64_t size) { return at + size <= bytes.size(); }

inline uint16_t u16(std::span<const uint8_t> b, uint64_t at) { return uint16_t(b[at] | b[at + 1] << 8); }
inline uint32_t u32(std::span<const uint8_t> b, uint64_t at)
{
	return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | uint32_t(b[at + 3]) << 24;
}
inline float f32(std::span<const uint8_t> b, uint64_t at)
{
	const uint32_t bits = u32(b, at);
	float value;
	std::memcpy(&value, &bits, 4);
	return value;
}

} // namespace lr
