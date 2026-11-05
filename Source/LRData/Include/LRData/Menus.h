#pragma once

// The menus' files, all in MENUDATA/:
//
// - String tables (.SRF): numbered strings of 16-bit characters (the menu text, the screen and binding names).
// - Name lists: a text file of a header line, a count, then that many names (the music list LEGOMSC, the sound bank
//   GENC0R0.SBK).
// - Image lists (.IDB) and font lists (.FDB): which .BMP images a screen uses (colour key, flip) and the fonts in the
//   language's folder (the characters in each font image, the spacing). A font's glyphs are read from its image.
// - Style tables (.MSB) and layouts (.MIB): named styles (an image, a font and colour, a button's state images and
//   sounds...) and named bindings, each a widget's place, parent and own settings; a screen makes a widget from a
//   binding and a style. Both are token streams of sections, one per kind.
// - The cinematics' scene and effects files (.CDB, .CEB).
// - The circuit and race tables (LEGORACE.CRB, .RCB).
//
// Colours are 0xAARRGGBB (the files write them alpha first).

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

struct Image;

struct LRDATA_API StringTable {
	std::vector<uint16_t> units;   // every string, each ending with a 0
	std::vector<uint32_t> starts;  // each string's first unit

	size_t size() const { return starts.size(); }
	std::u16string_view at(size_t index) const; // empty past the end
	// The string with each unit's low byte (the names are 8-bit text).
	std::string narrow(size_t index) const;
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const StringTable&) const = default;
};

struct LRDATA_API NameList {
	std::string header; // the first line: the list's folder, or nothing
	std::vector<std::string> names;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const NameList&) const = default;
};

struct LRDATA_API ImageList {
	struct Entry {
		std::string name;
		bool flip = false, targa = false;
		bool keyed = false;
		uint32_t key = 0;           // 0xffRRGGBB: texels of exactly this colour are transparent
		uint32_t tint = 0xffffffff;
		bool operator==(const Entry&) const = default;
	};
	std::vector<Entry> images;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const ImageList&) const = default;
};

struct LRDATA_API FontList {
	struct Font {
		std::string name;
		bool targa = false;
		bool keyed = false;
		uint32_t key = 0;
		std::vector<uint16_t> chars; // in the image, left to right
		int32_t spacing = 0;
		uint32_t colour = 0xffffffff;
		bool operator==(const Font&) const = default;
	};
	std::vector<Font> fonts;

	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const FontList&) const = default;
};

// A font's glyphs as its image holds them: one row as tall as the font, the glyphs between separator columns (a
// column equal to column 0, pixel by pixel). The leading separators are the space's width; the first glyph starts a
// column after them.
struct LRDATA_API FontGlyphs {
	struct Glyph {
		uint16_t code;
		uint16_t x, width; // in the image
		bool operator==(const Glyph&) const = default;
	};
	uint32_t height = 0;
	uint32_t space = 0;
	std::vector<Glyph> glyphs; // by code, the space (32: x 0, the space's width) among them

	// False if the image holds fewer glyphs than the font lists.
	bool scan(const Image& image, std::span<const uint16_t> chars);
	// The glyph for a code; a missing one is the space.
	const Glyph& find(uint16_t code) const;
};

struct MenuRect {
	int32_t left = 0, top = 0, right = 0, bottom = 0;
	int32_t width() const { return right - left; }
	int32_t height() const { return bottom - top; }
	bool operator==(const MenuRect&) const = default;
};

// What every interactive style and binding has: an icon's sounds, its visual states' rect deltas and colours.
struct MenuIcon {
	std::array<int32_t, 5> sounds = {}; // selected, deselected, pressed, released, unused: sound bank numbers from 1
	std::array<MenuRect, 6> stateRects = {};
	std::array<uint32_t, 6> colours = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
	int32_t transitionMs = 0;
	bool hasSounds = false, hasColours = false;
	bool operator==(const MenuIcon&) const = default;
};

struct LRDATA_API MenuStyles {
	enum Kind : uint8_t {
		Image = 0x32,
		Text = 0x33,
		Frame = 0x34,
		Button = 0x35,
		MultiState = 0x36,
		Hotspot = 0x37,
		Carousel = 0x38,
		Selector = 0x39,
		Options = 0x3b,
		Icon = 0x3d,
		TextButton = 0x3e,
	};
	struct Style {
		Kind kind = Image;
		std::string name;
		std::vector<std::string> images; // an image style's one, a frame's 8, a button's 6
		std::vector<uint32_t> colours;   // an image or text style's one, a frame's 2, a text button's 13
		std::string font;                // a text style's
		int32_t wrap = 0;
		bool centred = false;
		MenuIcon icon;
		std::array<std::string, 6> fonts; // a text button's
		int32_t maxWidth = 0;
		std::array<int32_t, 3> sounds = {}; // a carousel's next, previous, blocked; an options row's moved, blocked
		int32_t scrollMs = 0;
		bool pageMode = false, wrapAround = false;
		std::array<std::string, 2> buttons; // a selector's and an options row's previous and next button styles
		std::string frame;                  // a selector's frame style
		std::array<std::string, 2> track;   // an options row's track and thumb image styles
		std::vector<int32_t> regions;       // a hotspot's ids
		std::vector<MenuRect> regionRects;
		bool operator==(const Style&) const = default;
	};
	int32_t capacity = 0;
	std::vector<Style> styles;

	const Style* find(std::string_view name) const;
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const MenuStyles&) const = default;
};

struct LRDATA_API MenuLayout {
	enum Kind : uint8_t {
		Region = 0x37,
		Image = 0x38,
		Label = 0x39,
		Frame = 0x3a,
		ImageButton = 0x3b,
		MultiState = 0x3c,
		Hotspot = 0x3d,
		Selector = 0x3e,
		Ring = 0x3f,        // a 3D ring carousel
		Options = 0x40,
		SceneView = 0x42,
		TextField = 0x43,
		SceneReference = 0x45,
		TextButton = 0x46,
	};
	struct Binding {
		Kind kind = Region;
		std::string name;
		// The widget block.
		MenuRect rect;
		int32_t id = 0; // given in file order from 2 (an explicit one restarts the count)
		std::string parent;
		bool attach = true;
		bool hasColour = false;
		uint32_t colour = 0xffffffff;
		// An icon's.
		bool enabled = true, iconAttach = true;
		int32_t help = 0;
		MenuIcon icon;
		// The kind's own.
		std::vector<std::string> images;
		std::vector<uint32_t> colours; // a frame's 2, a text button's 13 (the widget block's colour is `colour`)
		std::vector<std::string> fonts;
		int32_t wrap = 0;        // a label's wrap width; a text button's max width
		bool hasFill = true;     // a frame's
		std::array<std::string, 2> buttons, track; // a selector's or options row's buttons; the row's track, thumb
		std::string frame;       // a selector's or framed scene view's frame binding
		bool unselectedInput = false;
		int32_t steps = 0, value = 0;        // an options row's
		std::array<int32_t, 4> editSounds = {}; // an options row's 2 step sounds, a text field's 4 edit sounds
		bool hasEditSounds = false;
		int32_t maxLength = 31;  // a text field's
		std::string world;       // a scene view's; a scene reference's cinematic
		std::array<float, 9> camera = {}; // eye, target, field of view, near, far
		std::array<int32_t, 3> numbers = {}; // a ring's slots, focused slot; a scene view's draw world, blended, clear
		float step = 0;          // a ring's
		std::vector<MenuRect> slots;
		bool autoAdvance = false;
		int32_t startFrame = 0;
		bool operator==(const Binding&) const = default;
	};
	int32_t capacity = 0;
	std::vector<Binding> bindings;

	const Binding* find(std::string_view name) const;
	// False on a malformed file.
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const MenuLayout&) const = default;
};

// A cinematic: its scenes (.CDB), each a timeline of cues (cameras, models, triggers, lights) with start and end
// frames, and its effects (.CEB): 2D overlays (texts, pictures, fades), sounds, particles and colour flashes, each
// started or stopped by links on the cues' names as they begin and end; the world databases (.WDB) the cues' cameras
// and entities come from.
struct LRDATA_API CinemaFile {
	struct Cue {
		enum Kind : uint8_t { Camera, Model, Trigger, Ambient, Light };
		Kind kind = Camera;
		std::string name;
		int32_t start = 0, end = 0; // frames
		// A camera's name in the worlds, a model's entity name (0x2f plain, 0x30 animated, 0x31 collision; or 0x32
		// world and entity: entityAt), the part either plays on its start (-1 none).
		std::string target;
		int32_t part = -1;
		uint8_t entityKind = 0; // 0x2f, 0x30, 0x31, 0x32; 0 none
		std::array<int32_t, 2> entityAt = {-1, -1};
		// A model's and a trigger's place, forward and up; a model's material tracks (world, animation, track, slot,
		// table).
		std::array<float, 3> position = {0, 0, 0}, forward = {1, 0, 0}, up = {0, 0, 1};
		std::vector<std::array<int32_t, 5>> tracks;
		// A light's colour (bytes), direction, blink (frames lit, dark; 0 0 none).
		std::array<int32_t, 3> colour = {};
		std::array<float, 3> direction = {};
		std::array<int32_t, 2> blink = {};
		bool operator==(const Cue&) const = default;
	};
	struct Scene {
		std::string name;
		int32_t rate = 30;
		int32_t frames = 0;
		std::vector<Cue> cues; // cameras, models, triggers, ambients, lights, each in file order
		bool operator==(const Scene&) const = default;
	};
	struct Effect {
		enum Kind : uint8_t { Colour = 0x2b, Sound = 0x2f, Stream = 0x36, Particles = 0x3c, Fade = 0x60, Text = 0x3f, Picture = 0x4d };
		Kind kind = Sound;
		std::string name;
		// A sound's: bank and index, volume, pan, frequency scale, looping.
		int32_t bank = 0, sound = 0;
		float volume = 1, pan = 0, frequency = 1;
		bool loops = false;
		// A fade's length and direction (in: from the colour; out: to it; neither: it never shows).
		int32_t ms = 0;
		bool in = false, out = false;
		uint32_t colour = 0xff000000; // a fade's, an overlay's tint (0xAARRGGBB)
		bool tinted = false;
		// An overlay's: a text (a string table's string, or a literal, in a font) or a picture; its place and size as
		// fractions of the screen, each kept only if given (else it is centred and sized to its content), its speeds.
		int32_t table = -1, string = -1;
		std::string text, font, picture;
		std::array<float, 4> place = {};        // left, top, width, height
		std::array<bool, 4> given = {};
		std::array<float, 4> speed = {};
		// The world entity it is attached to (0x5d animated, 0x5e plain, 0x5f collision; 0 none).
		std::string entity;
		uint8_t entityKind = 0;
		// A colour effect's: shift, start offset and offset speed per second, per channel R G B.
		std::array<int32_t, 3> shift = {}, offset = {};
		std::array<float, 3> offsetSpeed = {};
		bool operator==(const Effect&) const = default;
	};
	struct Link {
		uint8_t channel = 0x50; // 0x50 + 2·cue kind + (0 begins, 1 ends); 0x50/0x51 any kind
		std::string cue;
		bool start = true;
		int32_t effect = -1;    // in effects
		bool operator==(const Link&) const = default;
	};
	std::vector<std::string> worlds; // the world databases' base names, in order
	std::vector<Scene> scenes;
	std::vector<Effect> effects;
	std::vector<Link> links;
	std::vector<std::string> stringTables, soundBanks, imageLists;

	// The scenes (false on a malformed file); then the effects file, if there is one.
	bool decode(std::span<const uint8_t> bytes);
	bool decodeEffects(std::span<const uint8_t> bytes);
	bool operator==(const CinemaFile&) const = default;
};

struct LRDATA_API CircuitTable {
	struct Circuit {
		std::string name;
		std::vector<std::string> drivers; // slot 0..5: the champion is slot 1
		int32_t partSet = -1;
		std::string next;
		std::array<int32_t, 4> races = {-1, -1, -1, -1}; // by position: race table indices
		bool operator==(const Circuit&) const = default;
	};
	std::vector<Circuit> circuits;

	int32_t find(std::string_view name) const; // -1 none
	bool decode(std::span<const uint8_t> bytes);
	bool operator==(const CircuitTable&) const = default;
};

struct LRDATA_API RaceTable {
	struct Race {
		std::string name;
		std::string folder; // as written ("racec0r1")
		int32_t circuit = -1, position = 0;
		bool mirrored = false;
		int32_t display = 0; // CIRCUIT.SRF's index
		std::string theme, featured;
		bool operator==(const Race&) const = default;
	};
	std::vector<Race> races;

	int32_t find(std::string_view name) const; // -1 none
	// False on a malformed file; fills each circuit's races.
	bool decode(std::span<const uint8_t> bytes, CircuitTable& circuits);
	bool operator==(const RaceTable&) const = default;
};

} // namespace lr
