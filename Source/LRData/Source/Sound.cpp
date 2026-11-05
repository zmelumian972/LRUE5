#include "LRData/Sound.h"

#include "Bytes.h"

#include <algorithm>

namespace lr {

namespace {

// IMA's step sizes, but a sample moves by (nibble & 7) * step / 4 (IMA adds step / 8 more).
constexpr int16_t Steps[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
	73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
	796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
	5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
	29794, 32767};
constexpr int8_t StepChange[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

struct Channel {
	int sample = 0;
	int step = 0;

	int16_t next(uint8_t nibble)
	{
		const int delta = (nibble & 7) * Steps[step] >> 2;
		step = std::clamp(step + StepChange[nibble & 7], 0, 88);
		sample = std::clamp(nibble & 8 ? sample - delta : sample + delta, -32768, 32767);
		return int16_t(sample);
	}
};

} // namespace

bool Sound::decode(std::span<const uint8_t> bytes, bool music)
{
	if (!fits(bytes, 0, 16) || u32(bytes, 0) != 0x20504c41 || u32(bytes, 8) != 0x43504441) { // "ALP ", "ADPC"
		return false;
	}
	const uint64_t start = uint64_t(u32(bytes, 4)) + 8;
	channels = bytes[15];
	rate = music ? 22050 : start >= 20 ? u32(bytes, 16) : 11025;
	if (channels < 1 || channels > 2 || start > bytes.size() || rate == 0) {
		return false;
	}
	// A byte per channel holds two samples of each. Sounds play whole 4-byte words of it (the game drops the rest).
	const uint64_t size = bytes.size() - start;
	const std::span<const uint8_t> data = bytes.subspan(start, music ? size / channels * channels : size & ~uint64_t(3));
	samples.resize(data.size() * 2);
	Channel state[2];
	int16_t* out = samples.data();
	for (size_t i = 0; i < data.size(); i += channels) {
		for (uint32_t c = 0; c < channels; c++) {
			out[c] = state[c].next(data[i + c] >> 4);
			out[channels + c] = state[c].next(data[i + c] & 0x0f);
		}
		out += channels * 2;
	}
	return true;
}

} // namespace lr
