// The reference painter: the draw list's 2D elements into a 640x480 target as the original's device
// draws them.

#include "LRCore/Menus/DrawList.h"
#include "LRCore/Menus/MenuData.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

// Colours are sRGB; tinting and blending happen in linear light (a texel times the item colour looks right
// only so).
uint32_t encoded(float l)
{
	const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
	return uint32_t(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct Linear {
	std::array<float, 256> of;
	std::vector<uint8_t> tinted; // [texel][colour]
	Linear()
		: tinted(256 * 256)
	{
		for (size_t i = 0; i < 256; i++) {
			const float c = float(i) / 255.0f;
			of[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}
		for (size_t t = 0; t < 256; t++) {
			for (size_t c = 0; c < 256; c++) {
				tinted[t * 256 + c] = uint8_t(encoded(of[t] * of[c]));
			}
		}
	}
	uint32_t tint(uint8_t texel, uint32_t colour) const { return tinted[size_t(texel) * 256 + colour]; }
};
const Linear linear;

// Texels from (u0, v0)..(u1, v1) over the pixels whose centres lie in [x0, x1) x [y0, y1), keyed texels left out,
// times the colour.
void paintTexels(const uint8_t* rgba, int32_t width, int32_t height, bool keyed, uint32_t key, std::array<float, 4> d,
	std::array<float, 4> s, uint32_t colour, uint32_t* target)
{
	if (!rgba || d[2] <= d[0] || d[3] <= d[1]) {
		return;
	}
	const int32_t px0 = std::max(0, int32_t(std::ceil(d[0] - 0.5f))), px1 = std::min(ScreenWidth, int32_t(std::ceil(d[2] - 0.5f)));
	const int32_t py0 = std::max(0, int32_t(std::ceil(d[1] - 0.5f))), py1 = std::min(ScreenHeight, int32_t(std::ceil(d[3] - 0.5f)));
	const float du = (s[2] - s[0]) / (d[2] - d[0]), dv = (s[3] - s[1]) / (d[3] - d[1]);
	const uint32_t cr = colour >> 16 & 255, cg = colour >> 8 & 255, cb = colour & 255;
	const uint32_t keyRgb = key & 0xffffff;
	for (int32_t py = py0; py < py1; py++) {
		const int32_t tv = std::clamp(int32_t(std::floor(s[1] + (float(py) + 0.5f - d[1]) * dv)), 0, height - 1);
		const uint8_t* row = rgba + size_t(tv) * size_t(width) * 4;
		uint32_t* out = target + size_t(py) * ScreenWidth;
		for (int32_t px = px0; px < px1; px++) {
			const int32_t tu = std::clamp(int32_t(std::floor(s[0] + (float(px) + 0.5f - d[0]) * du)), 0, width - 1);
			const uint8_t* t = row + size_t(tu) * 4;
			if (keyed && (uint32_t(t[0]) << 16 | uint32_t(t[1]) << 8 | t[2]) == keyRgb) {
				continue;
			}
			out[px] = (colour & 0xffffff) == 0xffffff
				? 0xff000000u | uint32_t(t[0]) << 16 | uint32_t(t[1]) << 8 | t[2]
				: 0xff000000u | linear.tint(t[0], cr) << 16 | linear.tint(t[1], cg) << 8 | linear.tint(t[2], cb);
		}
	}
}

// A pixel blended over what is there at an alpha (in linear light; a clear pixel takes the colour at the alpha). At
// alpha 0 it is left as it is, unless zeroBlends (a quad's pixel is blended all the same).
void blendPixel(uint32_t& out, uint32_t colour, uint32_t a, bool zeroBlends = false)
{
	if (a >= 255) {
		out = 0xff000000u | (colour & 0xffffff);
		return;
	}
	if (a == 0 && !zeroBlends) {
		return;
	}
	const float fa = float(a) / 255.0f, da = float(out >> 24) / 255.0f, oa = fa + da * (1 - fa);
	uint32_t mixed = uint32_t(oa * 255.0f + 0.5f) << 24;
	for (int shift = 0; shift <= 16; shift += 8) {
		const float c = linear.of[colour >> shift & 255], d = linear.of[out >> shift & 255];
		mixed |= (oa > 0 ? encoded((fa * c + da * (1 - fa) * d) / oa) : 0) << shift;
	}
	out = mixed;
}

// A triangle: the pixels whose centres are inside it (and its clip), its corners' colours interpolated, times the
// image's texel (point sampled) if it has one, blended at the result's alpha.
void paintTriangle(const MenuData& data, const MenuElement& e, uint32_t* target)
{
	const auto& v = e.vertices;
	const float area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[2][0] - v[0][0]) * (v[1][1] - v[0][1]);
	if (area == 0) {
		return;
	}
	const MenuData::ImageInfo* image = e.image >= 0 ? &data.images[e.image] : nullptr;
	const uint32_t colours[3] = {e.colour, e.corners[0], e.corners[1]};
	const float x0 = std::min({v[0][0], v[1][0], v[2][0]}), x1 = std::max({v[0][0], v[1][0], v[2][0]});
	const float y0 = std::min({v[0][1], v[1][1], v[2][1]}), y1 = std::max({v[0][1], v[1][1], v[2][1]});
	const int32_t px0 = std::max({0, e.clip[0], int32_t(std::ceil(x0 - 0.5f))}),
				  px1 = std::min({ScreenWidth, e.clip[2], int32_t(std::ceil(x1 - 0.5f))});
	const int32_t py0 = std::max({0, e.clip[1], int32_t(std::ceil(y0 - 0.5f))}),
				  py1 = std::min({ScreenHeight, e.clip[3], int32_t(std::ceil(y1 - 0.5f))});
	for (int32_t py = py0; py < py1; py++) {
		for (int32_t px = px0; px < px1; px++) {
			const float x = float(px) + 0.5f, y = float(py) + 0.5f;
			float b[3];
			for (size_t k = 0; k < 3; k++) {
				const auto& a = v[(k + 1) % 3];
				const auto& c = v[(k + 2) % 3];
				b[k] = ((c[0] - a[0]) * (y - a[1]) - (x - a[0]) * (c[1] - a[1])) / area;
			}
			if (b[0] < 0 || b[1] < 0 || b[2] < 0) {
				continue;
			}
			float channel[4] = {};
			for (int k = 0; k < 3; k++) {
				for (int c = 0; c < 4; c++) {
					channel[c] += b[k] * float(colours[k] >> (8 * c) & 255);
				}
			}
			uint32_t colour = 0;
			for (int c = 0; c < 3; c++) {
				colour |= uint32_t(std::clamp(channel[c] + 0.5f, 0.0f, 255.0f)) << (8 * c);
			}
			uint32_t alpha = uint32_t(std::clamp(channel[3] + 0.5f, 0.0f, 255.0f));
			if (image && !image->rgba.empty()) {
				const float u = b[0] * v[0][2] + b[1] * v[1][2] + b[2] * v[2][2], w = b[0] * v[0][3] + b[1] * v[1][3] + b[2] * v[2][3];
				const int32_t tu = std::clamp(int32_t(std::floor(u * float(image->width))), 0, int32_t(image->width) - 1);
				const int32_t tv = std::clamp(int32_t(std::floor(w * float(image->height))), 0, int32_t(image->height) - 1);
				const uint8_t* t = &image->rgba[(size_t(tv) * image->width + size_t(tu)) * 4];
				colour = linear.tint(t[0], colour >> 16 & 255) << 16 | linear.tint(t[1], colour >> 8 & 255) << 8 |
					linear.tint(t[2], colour & 255);
				alpha = alpha * t[3] / 255;
			}
			blendPixel(target[size_t(py) * ScreenWidth + size_t(px)], colour, alpha);
		}
	}
}

} // namespace

void paint(const MenuData& data, size_t language, std::span<const MenuElement> elements, std::span<const MenuGlyph> glyphs,
	uint32_t* target)
{
	for (const MenuElement& e : elements) {
		switch (e.kind) {
		case MenuElement::Image: {
			const MenuData::ImageInfo& i = data.images[e.image];
			paintTexels(i.rgba.data(), int32_t(i.width), int32_t(i.height), i.keyed, i.key, e.dest, e.source, e.colour, target);
			break;
		}
		case MenuElement::Glyphs: {
			const MenuData::Font& f = data.languages[language].fonts[e.image];
			for (uint32_t g = e.firstGlyph; g < e.firstGlyph + e.glyphCount; g++) {
				const MenuGlyph& q = glyphs[g];
				paintTexels(f.rgba.data(), int32_t(f.width), int32_t(f.glyphs.height), f.keyed, f.key,
					{float(q.dest[0]), float(q.dest[1]), float(q.dest[2]), float(q.dest[3])}, q.source, e.colour, target);
			}
			break;
		}
		case MenuElement::Quad: {
			// (A rect turned inside out covers its span.)
			const float x0 = std::min(e.dest[0], e.dest[2]), x1 = std::max(e.dest[0], e.dest[2]);
			const float y0 = std::min(e.dest[1], e.dest[3]), y1 = std::max(e.dest[1], e.dest[3]);
			const int32_t px0 = std::max(0, int32_t(std::ceil(x0 - 0.5f))), px1 = std::min(ScreenWidth, int32_t(std::ceil(x1 - 0.5f)));
			const int32_t py0 = std::max(0, int32_t(std::ceil(y0 - 0.5f))), py1 = std::min(ScreenHeight, int32_t(std::ceil(y1 - 0.5f)));
			const uint32_t a = e.alpha < 0 ? 255 : uint32_t(std::clamp(e.alpha, 0, 255));
			for (int32_t py = py0; py < py1; py++) {
				for (int32_t px = px0; px < px1; px++) {
					blendPixel(target[size_t(py) * ScreenWidth + size_t(px)], e.colour, a, true);
				}
			}
			break;
		}
		case MenuElement::Triangle: paintTriangle(data, e, target); break;
		case MenuElement::View3D: break;
		}
	}
}

} // namespace lr
