#include "LRData/Executable.h"

#include "Bytes.h"
#include "LRData/Digest.h"

#include <vector>

namespace lr {

namespace {

// A table: where the known executable has it, its length, its bytes' SHA-256.
struct Place {
	const char* name;
	uint32_t offset;
	uint32_t size;
	const char* digest;
};
constexpr Place Random = {"random table", 0xbefec, 2048, "dc18033635bc1006b0e41d0bb8bed342d554ee0bbf29589d50b35aa000a952d5"};
// 13 NUL-padded, 4-byte aligned strings, the last cheat first (the 8 bytes after them are not a cheat).
constexpr Place Cheats = {"cheat codes", 0xc2028, 112, "e5e43dd9977b41bc81c2c643b5960f6a4b17916bd85592898f41becd9d5cd50b"};
// 9 NUL-padded, 4-byte aligned folder names in language order.
constexpr Place Languages = {"language folders", 0xb05dc, 72, "2f91b1274c442bafd1a6107a66e4170e9908bcc6fbad4881102cac2040df06a6"};
constexpr Place Frames = {"frame colours", 0xc2278, 45, "3ebc12378eb5daffc475948b22dd9656e414284e5391319d6f0c10cef82f13db"}; // 5 RGBA, 25-byte map
constexpr Place Points = {"place points", 0xb0b58, 24, "e5bb9b3f055ee6f0925bb587ae2396c1747daf1cdd6245c371230c2157f790d2"};
constexpr Place Curse = {"curse drop", 0xb4208, 36, "c0ec3c6078cab6784c57baf840bd3533d9c5b2ab90d6c7eb0dc4d629adb0854c"};
constexpr Place LavaSpots = {"lava spots", 0xc22d8, 36, "7004157b23c47d909a8f50b4e4f27fd2402d9b62435836077ff7bdefaa7933df"};
constexpr Place LavaFrames = {"lava frames", 0xb44c8, 24, "ef78fa0d0918e7ef289df86920c089a0e856e5a44a2174eb2e11675eb8915803"};
constexpr Place Eyes = {"builder eyes", 0xb2e80, 36, "4cd28108cb68960a5ecdcfec76f8c90c44e0a8d331fd01793442a6a3d1e3b638"};
// 8 x 8 i32 x steps by view slot and direction, then the 8 x 8 y steps.
constexpr Place Steps = {"builder keypad steps", 0xc1d78, 512, "a11d9029c470d268ea594e7e0f5406c37d7889376a1641232b6fe51947593de7"};

// The initialised data sections' file ranges (.data, .rdata) from the PE section table.
std::vector<std::pair<uint32_t, uint32_t>> dataSections(std::span<const uint8_t> exe)
{
	std::vector<std::pair<uint32_t, uint32_t>> ranges;
	if (!fits(exe, 0x3c, 4)) {
		return ranges;
	}
	const uint32_t pe = u32(exe, 0x3c);
	if (!fits(exe, pe, 24) || u32(exe, pe) != 0x4550) { // "PE\0\0"
		return ranges;
	}
	const uint32_t count = u16(exe, pe + 6), optional = u16(exe, pe + 20);
	const uint64_t table = uint64_t(pe) + 24 + optional;
	for (uint32_t i = 0; i < count && fits(exe, table + i * 40, 40); i++) {
		const uint64_t at = table + i * 40;
		std::string name;
		for (uint32_t k = 0; k < 8 && exe[at + k]; k++) {
			name += char(exe[at + k]);
		}
		const uint32_t size = u32(exe, at + 16), offset = u32(exe, at + 20);
		if ((name == ".data" || name == ".rdata") && fits(exe, offset, size)) {
			ranges.emplace_back(offset, size);
		}
	}
	return ranges;
}

// The table's bytes: at its known offset if they match, else the first window that does in the data sections.
std::span<const uint8_t> find(std::span<const uint8_t> exe, const Place& place, std::string& error)
{
	Digest want;
	parseHex(place.digest, want);
	if (fits(exe, place.offset, place.size) && sha256(exe.subspan(place.offset, place.size)) == want) {
		return exe.subspan(place.offset, place.size);
	}
	for (const auto& [offset, size] : dataSections(exe)) {
		for (uint64_t at = offset; at + place.size <= uint64_t(offset) + size; at += 4) {
			if (sha256(exe.subspan(at, place.size)) == want) {
				return exe.subspan(at, place.size);
			}
		}
	}
	error = std::string("LEGORacers.exe isn't a supported version (no ") + place.name + ")";
	return {};
}

// NUL-padded strings, each starting 4-byte aligned, in order.
template <size_t N>
void readStrings(std::span<const uint8_t> b, std::array<std::string, N>& out, bool reversed)
{
	size_t n = 0;
	for (size_t at = 0; at < b.size() && n < N; n++) {
		std::string s;
		while (at < b.size() && b[at]) {
			s += char(b[at++]);
		}
		out[reversed ? N - 1 - n : n] = s;
		at = (at + 4) & ~size_t(3);
	}
}

template <size_t N>
void readFloats(std::span<const uint8_t> b, std::array<std::array<float, 3>, N>& out)
{
	for (size_t i = 0; i < N * 3; i++) {
		out[i / 3][i % 3] = f32(b, i * 4);
	}
}

} // namespace

bool ExecutableTables::locate(std::span<const uint8_t> exe, std::string& error)
{
	*this = {};
	const std::span<const uint8_t> r = find(exe, Random, error), c = find(exe, Cheats, error), f = find(exe, Frames, error),
		p = find(exe, Points, error), cu = find(exe, Curse, error), ls = find(exe, LavaSpots, error), lf = find(exe, LavaFrames, error),
		e = find(exe, Eyes, error), l = find(exe, Languages, error), s = find(exe, Steps, error);
	if (r.empty() || c.empty() || l.empty() || f.empty() || p.empty() || cu.empty() || ls.empty() || lf.empty() || e.empty() || s.empty()) {
		return false;
	}
	for (size_t i = 0; i < random.size(); i++) {
		random[i] = u16(r, i * 2);
	}
	readStrings(c, cheats, true); // (the last cheat first)
	readStrings(l, languages, false);
	for (size_t i = 0; i < frameColours.size(); i++) {
		frameColours[i] = uint32_t(f[i * 4 + 3]) << 24 | f[i * 4] << 16 | f[i * 4 + 1] << 8 | f[i * 4 + 2];
	}
	for (size_t i = 0; i < frameMap.size(); i++) {
		frameMap[i] = f[20 + i];
	}
	for (size_t i = 0; i < placePoints.size(); i++) {
		placePoints[i] = int32_t(u32(p, i * 4));
	}
	readFloats(cu, curseDrop);
	readFloats(ls, lavaSpots);
	readFloats(e, builderEyes);
	for (size_t i = 0; i < 64; i++) {
		builderStepX[i / 8][i % 8] = int32_t(u32(s, i * 4));
		builderStepY[i / 8][i % 8] = int32_t(u32(s, 256 + i * 4));
	}
	for (size_t i = 0; i < 3; i++) {
		lavaStarts[i] = f32(lf, i * 4);
		lavaEnds[i] = f32(lf, 12 + i * 4);
	}
	return true;
}

} // namespace lr
