#include "LRData/Image.h"

#include "Bytes.h"

namespace lr {

namespace {

// The pixels come in sections: sizes (uncompressed, compressed; equal if stored as is), then the bytes. A compressed
// section is LZ77: a literal byte, then groups of eight items under a flag byte (high bit first): a literal, or a
// back-reference (a distance of 12 bits, a length of 3..17 or, with a 0 nibble, 18 + a byte); distance 0 ends it.
bool unpack(std::span<const uint8_t> in, size_t at, std::vector<uint8_t>& out, size_t size)
{
	out.clear();
	out.reserve(size);
	while (out.size() < size && at < in.size()) {
		if (!fits(in, at, 4)) {
			return false;
		}
		const uint16_t raw = u16(in, at), packed = u16(in, at + 2);
		at += 4;
		if (raw == packed) {
			if (!fits(in, at, raw)) {
				return false;
			}
			out.insert(out.end(), in.begin() + at, in.begin() + at + raw);
			at += raw;
			continue;
		}
		if (!fits(in, at, 1)) {
			return false;
		}
		out.push_back(in[at++]);
		for (bool done = false; !done;) {
			if (!fits(in, at, 1)) {
				return false;
			}
			uint8_t flags = in[at++];
			for (int i = 0; i < 8 && !done; i++, flags <<= 1) {
				if (!(flags & 0x80)) {
					if (!fits(in, at, 1)) {
						return false;
					}
					out.push_back(in[at++]);
					continue;
				}
				if (!fits(in, at, 2)) {
					return false;
				}
				const uint8_t head = in[at];
				const size_t distance = (head & 0xf0) << 4 | in[at + 1];
				size_t length = 18 - (head & 0x0f);
				at += 2;
				if ((head & 0x0f) == 0) {
					if (distance == 0) {
						done = true;
						break;
					}
					if (!fits(in, at, 1)) {
						return false;
					}
					length = 18 + in[at++];
				}
				// Byte by byte: a reference can overlap what it writes (a run).
				for (size_t j = 0; j < length; j++) {
					out.push_back(distance <= out.size() ? out[out.size() - distance] : 0);
				}
			}
		}
	}
	if (out.size() < size) {
		return false;
	}
	out.resize(size);
	return true;
}

} // namespace

bool Image::decodeBmp(std::span<const uint8_t> bytes)
{
	if (!fits(bytes, 0, 6)) {
		return false;
	}
	const uint8_t depth = bytes[0];
	const uint32_t colors = bytes[1] + 1u;
	width = u16(bytes, 2);
	height = u16(bytes, 4);
	const bool paletted = depth == 4 || depth == 8;
	if (!paletted && depth != 0x98) {
		return false;
	}
	bits = paletted ? depth : 24;
	size_t at = 6;
	uint8_t palette[256][3] = {};
	if (paletted) {
		if (!fits(bytes, at, colors * 3)) {
			return false;
		}
		for (uint32_t i = 0; i < colors; i++, at += 3) { // stored BGR
			palette[i][0] = bytes[at + 2];
			palette[i][1] = bytes[at + 1];
			palette[i][2] = bytes[at];
		}
	}
	// 4-bit rows end on a whole byte (an odd width leaves a nibble over).
	const size_t pixels = size_t(width) * height, stride = depth == 4 ? (width + 1) & ~1u : width;
	const size_t size = depth == 4 ? stride / 2 * height : depth == 8 ? pixels : pixels * 3;
	std::vector<uint8_t> raw;
	if (!unpack(bytes, at, raw, size)) {
		return false;
	}

	rgba.resize(pixels * 4);
	for (size_t i = 0; i < pixels; i++) {
		uint8_t* out = &rgba[i * 4];
		if (paletted) {
			const size_t p = i / width * stride + i % width;
			const uint8_t index = depth == 8 ? raw[p] : (raw[p / 2] >> (p & 1 ? 0 : 4)) & 0x0f;
			out[0] = palette[index][0];
			out[1] = palette[index][1];
			out[2] = palette[index][2];
		}
		else {
			out[0] = raw[i * 3 + 2];
			out[1] = raw[i * 3 + 1];
			out[2] = raw[i * 3];
		}
		out[3] = 255;
	}
	return true;
}

} // namespace lr
