#include "LRData/Look.h"
#include "LRData/Archive.h"

#include "Bytes.h"
#include "LRData/Image.h"
#include "LRData/Tokens.h"

namespace lr {

namespace {

// The material keys.
enum : int {
	KeyEntry = 0x27,
	KeyAmbient = 0x28,
	KeyDiffuse = 0x29,
	KeyFlat = 0x2a,
	KeyGouraud = 0x2b,
	KeyTexture = 0x2c,
	KeyModulate = 0x2d,
	KeyDecal = 0x2e,
	KeyAlphaTest = 0x2f,
	KeyTestAlways = 0x30,
	KeyTestNever = 0x36,
	KeyTestLast = 0x37,
	KeyBlend = 0x38,
	KeyFactorFirst = 0x39,
	KeyFactorLast = 0x43,
	KeyLinear = 0x44,
	KeyPoint = 0x45,
	KeyAlpha = 0x46,
	KeyRotate = 0x47,
	KeyWrap = 0x48,
	KeyClamp = 0x49,
	KeyIgnoredFirst = 0x4a,
	KeyIgnoredLast = 0x4c,
	KeyAlphaFirst = 0x4d, // three more spellings of the constant alpha
	KeyAlphaLast = 0x50,
};
// The texture list's.
enum : int {
	KeyFlip = 0x28,
	KeyMipLevels = 0x29,
	KeyBmp = 0x2a, // the source is the entry's name .BMP (decided from the data: no file name follows)
	KeyTga = 0x2b, // or .TGA
	KeyColourKey = 0x2c,
	KeyListFlagFirst = 0x2d,
	KeyListFlagLast = 0x2e,
};

std::string upTo8(std::string_view s) { return std::string(s.substr(0, 8)); }

std::array<uint8_t, 4> colour(TokenReader& r)
{
	std::array<uint8_t, 4> c;
	for (uint8_t& v : c) {
		v = uint8_t(r.integer());
	}
	return c;
}

} // namespace

int32_t MaterialLibrary::find(std::string_view name) const
{
	for (size_t i = 0; i < materials.size(); i++) {
		if (equalsIgnoreCase(materials[i].name, name)) {
			return int32_t(i);
		}
	}
	return -1;
}

const MaterialLibrary::Material* findMaterial(std::span<const MaterialLibrary* const> loaded, std::string_view name, size_t* library)
{
	for (size_t i = loaded.size(); i-- > 0;) {
		const int32_t m = loaded[i] ? loaded[i]->find(name) : -1;
		if (m >= 0) {
			if (library) {
				*library = i;
			}
			return &loaded[i]->materials[size_t(m)];
		}
	}
	return nullptr;
}

bool MaterialLibrary::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != KeyEntry) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != KeyEntry) {
			return false;
		}
		Material& m = materials.emplace_back();
		m.name = upTo8(r.text());
		bool textured = false;
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			const int key = r.keyword();
			if (key == KeyAmbient) {
				m.ambient = colour(r);
			}
			else if (key == KeyDiffuse) {
				m.diffuse = colour(r);
			}
			else if (key == KeyTexture) {
				m.texture = upTo8(r.text());
				textured = true;
			}
			else if (key == KeyAlphaTest) {
				const int function = r.keyword();
				if (function < KeyTestAlways || function > KeyTestLast) {
					return false;
				}
				if (function != KeyTestAlways && function != KeyTestNever) {
					r.integer();
				}
			}
			else if (key == KeyBlend) {
				for (uint8_t* factor : {&m.source, &m.destination}) {
					const int f = r.keyword();
					if (f < KeyFactorFirst || f > KeyFactorLast) {
						return false;
					}
					*factor = uint8_t(f - KeyFactorFirst);
				}
				m.blend = true;
				m.constantAlpha = false;
			}
			else if (key == KeyAlpha || (key >= KeyAlphaFirst && key <= KeyAlphaLast)) {
				m.alpha = uint8_t(r.integer());
				m.constantAlpha = true;
				m.blend = false;
			}
			else if (key == KeyPoint || key == KeyLinear) {
				m.point = key == KeyPoint;
			}
			else if (key == KeyClamp || key == KeyWrap) {
				m.clamp = key == KeyClamp;
			}
			else if (key == KeyRotate) {
				m.rotate = true;
			}
			else if (key == KeyFlat || key == KeyGouraud || key == KeyModulate || key == KeyDecal || (key >= KeyIgnoredFirst && key <= KeyIgnoredLast)) {
			}
			else {
				return false;
			}
		}
		if (!textured) {
			m.texture.clear();
		}
	}
	r.close();
	return !r.failed();
}

int32_t TextureList::find(std::string_view name) const
{
	for (size_t i = 0; i < textures.size(); i++) {
		if (equalsIgnoreCase(textures[i].name, name)) {
			return int32_t(i);
		}
	}
	return -1;
}

bool TextureList::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != KeyEntry) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != KeyEntry) {
			return false;
		}
		Entry& e = textures.emplace_back();
		e.name = upTo8(r.text());
		bool sourced = false;
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			const int key = r.keyword();
			if (key == KeyFlip) {
				e.flip = true;
			}
			else if (key == KeyMipLevels) {
				r.integer();
			}
			else if (key == KeyBmp || key == KeyTga) {
				e.targa = key == KeyTga;
				sourced = true;
			}
			else if (key == KeyColourKey) {
				for (uint8_t& v : e.key) {
					v = uint8_t(r.integer());
				}
				e.keyed = true;
			}
			else if (key >= KeyListFlagFirst && key <= KeyListFlagLast) {
				r.skipValues();
			}
			else {
				return false;
			}
		}
		if (!sourced) {
			return false;
		}
		e.file = e.name + (e.targa ? ".TGA" : ".BMP");
	}
	r.close();
	return !r.failed();
}

bool decodeTga(std::span<const uint8_t> bytes, Image& image)
{
	image = {};
	if (!fits(bytes, 0, 18)) {
		return false;
	}
	const uint8_t idSize = bytes[0], mapType = bytes[1], type = bytes[2], bits = bytes[16], descriptor = bytes[17];
	const uint32_t width = u16(bytes, 12), height = u16(bytes, 14);
	const uint64_t at = 18 + idSize + (mapType ? uint64_t(u16(bytes, 5)) * ((bytes[7] + 7) / 8) : 0);
	const uint32_t step = bits / 8;
	if (type != 2 || (bits != 24 && bits != 32) || !fits(bytes, at, uint64_t(width) * height * step)) {
		return false;
	}
	image.width = width;
	image.height = height;
	image.bits = bits;
	image.rgba.resize(size_t(width) * height * 4);
	const bool topFirst = descriptor & 0x20;
	for (uint32_t y = 0; y < height; y++) {
		const uint64_t row = at + uint64_t(topFirst ? y : height - 1 - y) * width * step;
		for (uint32_t x = 0; x < width; x++) {
			const uint8_t* p = &bytes[row + x * step];
			uint8_t* out = &image.rgba[(size_t(y) * width + x) * 4];
			out[0] = p[2], out[1] = p[1], out[2] = p[0], out[3] = 255; // (the game reads its targa textures opaque)
		}
	}
	return true;
}

void Texture::make(const Image& image, const TextureList::Entry& entry)
{
	width = image.width;
	height = image.height;
	rgba = image.rgba;
	const size_t row = size_t(width) * 4;
	// The colour key: texels of exactly the key's colour transparent, then their colour from their opaque neighbours.
	if (entry.keyed && !entry.targa) {
		for (size_t i = 0; i < rgba.size(); i += 4) {
			rgba[i + 3] = rgba[i] == entry.key[0] && rgba[i + 1] == entry.key[1] && rgba[i + 2] == entry.key[2] ? 0 : 255;
		}
		const std::vector<uint8_t> keyed = rgba;
		for (uint32_t y = 0; y < height; y++) {
			for (uint32_t x = 0; x < width; x++) {
				uint8_t* p = &rgba[y * row + x * 4];
				if (p[3]) {
					continue;
				}
				uint32_t sum[3] = {}, n = 0;
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = int(x) + dx, ny = int(y) + dy;
						if ((dx || dy) && nx >= 0 && ny >= 0 && nx < int(width) && ny < int(height)) {
							const uint8_t* q = &keyed[size_t(ny) * row + size_t(nx) * 4];
							if (q[3]) {
								sum[0] += q[0], sum[1] += q[1], sum[2] += q[2], n++;
							}
						}
					}
				}
				for (int c = 0; c < 3; c++) {
					p[c] = uint8_t(n ? (sum[c] + n / 2) / n : 0);
				}
			}
		}
	}
}

} // namespace lr
