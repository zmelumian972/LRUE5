#pragma once

// What the models are drawn with: material libraries (.MDB), texture lists (.TDB) and the textures
// they name (.BMP, .TGA), the colour key applied and dilated.

#include "LRData/Api.h"
#include "LRData/Index.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lr {

struct Image;

struct LRDATA_API MaterialLibrary {
	struct Material {
		std::string name; // at most 8 characters: what a model's sections name
		std::array<uint8_t, 4> ambient = {255, 255, 255, 255}, diffuse = {255, 255, 255, 255}; // r g b a
		std::string texture; // a name of the texture list; empty: untextured
		bool blend = false;
		// The blend's source and destination factors (key − 0x39 in the original's order: 0 zero, 1 one,
		// 2 src colour, 3 dst colour, 4 inv src colour, 5 inv dst colour, 6 src alpha, 7 dst alpha, 8 inv src alpha,
		// 9 inv dst alpha, 10 src alpha saturate).
		enum Factor : uint8_t {
			Zero,
			One,
			SourceColour,
			DestinationColour,
			InverseSourceColour,
			InverseDestinationColour,
			SourceAlpha,
			DestinationAlpha,
			InverseSourceAlpha,
			InverseDestinationAlpha,
			SourceAlphaSaturate
		};
		uint8_t source = SourceAlpha, destination = InverseSourceAlpha;
		bool constantAlpha = false;
		uint8_t alpha = 255; // the constant alpha
		bool point = false; // point filter (else linear)
		bool clamp = false; // clamp addressing (else wrap)
		bool rotate = false; // the rotate-vertices flag (no drawing effect)
		// Drawn additively: blended with the factors (src alpha, one); decided by the factors, not the name.
		bool additive() const { return blend && source == SourceAlpha && destination == One; }
		bool operator==(const Material&) const = default;
	};
	List<Material> materials;

	int32_t find(std::string_view name) const; // -1 none
	[[nodiscard]] bool decode(std::span<const uint8_t> bytes);
	bool operator==(const MaterialLibrary&) const = default;
};

// A material by name across the libraries loaded, `loaded` oldest first, searched newest first; which
// library it came from (its texture is that library's list's). Null: found nowhere (a load error in the original).
LRDATA_API const MaterialLibrary::Material* findMaterial(std::span<const MaterialLibrary* const> loaded, std::string_view name,
	size_t* library = nullptr);

struct LRDATA_API TextureList {
	struct Entry {
		std::string name; // what a material names
		std::string file; // its .BMP or .TGA: the name and the kind's extension
		bool targa = false;
		bool flip = false; // nearly every entry has it: the drawing applies it (v), the pixels stay as stored
		bool keyed = false;
		std::array<uint8_t, 3> key = {}; // r g b
		bool operator==(const Entry&) const = default;
	};
	List<Entry> textures;

	int32_t find(std::string_view name) const; // -1 none
	[[nodiscard]] bool decode(std::span<const uint8_t> bytes);
	bool operator==(const TextureList&) const = default;
};

// A texture as UE gets it: RGBA8, rows top first; keyed texels alpha 0 with the average colour of
// their opaque 8-neighbours (black if none), the others alpha 255.
struct LRDATA_API Texture {
	uint32_t width = 0, height = 0;
	std::vector<uint8_t> rgba;
	bool operator==(const Texture&) const = default;

	// From a decoded image and its list entry (its key).
	void make(const Image& image, const TextureList::Entry& entry);
};

// An uncompressed true-colour .TGA (24 or 32 bits, rows bottom first unless its descriptor says top).
LRDATA_API bool decodeTga(std::span<const uint8_t> bytes, Image& image);

} // namespace lr
