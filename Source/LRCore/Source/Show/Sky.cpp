// The show's sky: its first state from the race file, the Sky events' blends and its keyframes'
// colours each frame.

#include "LRCore/Show/Sky.h"

#include "LRData/Cache.h"

#include <algorithm>

namespace lr {

void Sky::load(const Cache& cache, const std::string& dir, const RaceFile& race)
{
	if (!race.skyFile.empty() && cache.get(dir + binaryName(race.skyFile), file) && !file.states.empty()) {
		loaded = true;
		state = std::max(0, file.find(file.initial));
		update(0);
	}
}

// The sky's dome and world shown or hidden, and a blend to another sky state.
void Sky::event(const RaceEvent& e)
{
	if (!loaded) {
		return;
	}
	if (e.flags & 1) {
		domeShown = false;
	}
	if (e.flags & 2) {
		domeShown = true;
	}
	if (e.flags & 4) {
		worldShown = false;
	}
	if (e.flags & 8) {
		worldShown = true;
	}
	const int32_t to = e.name[0] != 0 ? file.find(e.name.data()) : -1;
	if (to >= 0 && to != state) {
		blendFrom = state;
		blendMs = 0;
		blendLength = std::max(0, e.value);
		state = to;
		key = 0;
		keyMs = 0;
	}
}

void Sky::update(int32_t ms)
{
	if (!loaded) {
		return;
	}
	const auto colour = [&](int32_t at, int32_t frame, int32_t elapsed, std::array<std::array<float, 3>, 3>& out) {
		const SkyFile::State& s = file.states[at];
		if (s.keyframes.empty()) {
			return;
		}
		const SkyFile::Keyframe& a = s.keyframes[size_t(frame) % s.keyframes.size()];
		const SkyFile::Keyframe& b = s.keyframes[size_t(frame + 1) % s.keyframes.size()];
		const float f = a.durationMs > 0 ? std::clamp(float(elapsed) / float(a.durationMs), 0.0f, 1.0f) : 0;
		for (size_t r = 0; r < 3; r++) {
			for (size_t c = 0; c < 3; c++) {
				out[r][c] = float(a.colours[r][c]) + (float(b.colours[r][c]) - float(a.colours[r][c])) * f;
			}
		}
	};
	const SkyFile::State& s = file.states[state];
	keyMs += ms;
	// A keyframe advances once its elapsed time exceeds its duration.
	while (!s.keyframes.empty() && keyMs > s.keyframes[size_t(key) % s.keyframes.size()].durationMs) {
		keyMs -= s.keyframes[size_t(key) % s.keyframes.size()].durationMs;
		key = int32_t((size_t(key) + 1) % s.keyframes.size());
	}
	colour(state, key, keyMs, colours);
	if (blendFrom >= 0) {
		blendMs += ms;
		if (blendMs >= blendLength) {
			blendFrom = -1;
		}
		else {
			std::array<std::array<float, 3>, 3> from = {};
			colour(blendFrom, 0, 0, from);
			const float f = float(blendMs) / float(std::max(1, blendLength));
			for (size_t r = 0; r < 3; r++) {
				for (size_t c = 0; c < 3; c++) {
					colours[r][c] = from[r][c] + (colours[r][c] - from[r][c]) * f;
				}
			}
		}
	}
}

} // namespace lr
