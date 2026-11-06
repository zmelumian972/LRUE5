#pragma once

// What the menus hand the presentation each frame: 2D elements in the game's 640x480 screen, their glyph quads, the 3D
// views among them and what each shows, and the reference painter for the 2D.

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Show/View.h"
#include "LRData/Menus.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lr {

struct MenuData;

// The game's screen as a rect.
constexpr MenuRect ScreenRect = {0, 0, ScreenWidth, ScreenHeight};

// What the presentation draws, in order, in 640x480 space.
struct MenuElement {
	enum Kind : uint8_t { Image, Glyphs, Quad, View3D, Triangle };
	Kind kind = Image;
	int16_t image = -1; // MenuData::images; a Glyphs' font (the language's)
	uint32_t colour = 0; // 0xAARRGGBB: an image's and the text's tint (alpha ignored), a quad's top-left
	std::array<float, 4> dest = {}; // x0 y0 x1 y1
	std::array<float, 4> source = {}; // u0 v0 u1 v1 in image pixels
	std::array<uint32_t, 3> corners = {}; // a quad's other colours: top-right, bottom-left, bottom-right
	int32_t alpha = -1; // a quad blended at this alpha (0..255); -1 opaque
	// Glyphs: the glyph quads (dest, source) and the text with its box, for a renderer that lays text out itself.
	uint32_t firstGlyph = 0, glyphCount = 0;
	std::u16string text;
	std::array<int32_t, 4> box = {}; // x y w h
	bool wrapped = false;
	bool scaled = false; // drawn at its own size times `scale` from (x, y), not fitted to the box
	std::array<float, 2> scale = {1, 1};
	int32_t widget = -1; // View3D: the scene view's (or ring's) widget id, and its scene in Menus::scenes
	int32_t scene = -1;
	// Triangle: its corners (x, y, u, v; u and v 0..1 of the image, or -1: flat), coloured `colour`, corners[0] and
	// corners[1] (blended by their alpha, in between interpolated), drawn inside the clip rect (left, top, right,
	// bottom).
	std::array<std::array<float, 4>, 3> vertices = {};
	std::array<int32_t, 4> clip = {0, 0, ScreenWidth, ScreenHeight};
};

// The device's lighting as the menus leave it: an ambient (0xAARRGGBB, alpha 0 none:
// the material's own ambient, `state` 255) and up to 7 directional lights (the way they travel, normalised; colour).
struct MenuLighting {
	uint32_t ambient = 0;
	std::vector<std::pair<Vec3, uint32_t>> lights;
	bool operator==(const MenuLighting&) const = default;
};

// What a 3D view shows, for the presentation to pose and draw: its world, camera and light, and what stands in it.
struct MenuScene {
	std::string world; // the world's name ("garage", "cbset"); empty for a ring
	// The camera (the world's first camera when it has one; a cinematic's from its camera stack):
	// eye, a point ahead, screen up; the lens (vertical, degrees; model units); aspect = the viewport's width / height.
	std::array<float, 3> eye = {}, target = {}, up = {0, 0, 1};
	float fov = 45, nearPlane = 1, farPlane = 1000, aspect = 4.0f / 3;
	bool camera = true; // false: a cinematic with no camera yet (nothing drawn)
	MenuLighting lighting; // what its models are lit with (a ring's items: per item, below)
	// A cinematic's (the world is its scene's name; a setup preview's too): its clock in scene frames and the cues
	// begun, newest first; the screen (its MenuData::Screen holds the cinema and its worlds).
	float time = 0;
	std::vector<uint16_t> cues;
	uint16_t cinemaScreen = 0;
	// A scene view's flags (MU): its world drawn; its folder's BLENDED.WDB drawn after it, its materials found by name in
	// the libraries its world loaded (the driver builder's spotlight cone).
	bool drawWorld = true, blendedWorld = false;
	float blendedFrame = 0; // the frame its blended world's animated entities play their part at (the turntable's clock)
	bool sliding = false; // a ring's items sliding between slots (it scrolls)
	struct Thing {
		enum Kind : uint8_t {
			Car, // Menus::carMesh, its chassis and the seated driver (at the chassis' driver mount)
			Piece, // a library piece: a ring's item (in its slot, spun), or the builder's held piece
			Minifig, // looks and expression; standing or seated; an animation part (CBANIM's, CMAMAN's) and its frame
			Part, // a driver ring's item: the ring (hat, face, torso, legs) and the part
			Mark, // a cell under the held piece where it hits something (one 1x1 quad, car frame)
			Chassis, // the menus' chassis model (`model`) under a car, playing its first part at `frame`
			Entity, // a cinematic's entity (cinema world `entity[0]`, animated `entity[1]` 1 or plain 0, index
			// `entity[2]`) at its cue's place, playing `animation` at `frame` (-1 at rest); `cue` its model cue
		};
		Kind kind = Car;
		std::array<float, 3> at = {};
		float angle = 0; // about z; a ring's item: its spin about the tilted axis
		int32_t type = 0, colour = 0, rotation = 0, slot = 0; // a piece's; a ring item's slot; a part's ring
		uint8_t alpha = 255;
		bool lit = false; // brighter (hovered)
		std::array<uint8_t, 5> looks = {}; // face, hat, legs, torso, expression
		bool seated = false;
		int32_t animation = -1;
		float frame = 0;
		// Where it is drawn: its world matrix, the model's scale in the rows (a car's mesh, a
		// mark's quad: scale 1; a ring item's and a minifig's: their models'). A minifig's nodes are posed from it by the
		// shared sampler (View.h) with its part file's part and frame.
		NodeMatrix world;
		enum PartFile : uint8_t { NoParts, Legoman, Showcase, Builder, Photo }; // LEGOMAN, RSANIM, CBANIM, CMAMAN
		PartFile parts = NoParts;
		std::string model; // a Part item's model (a ring's body part, or a hat's head library entry); a Chassis' model
		// A minifig's skeleton (its .SDB) and, in a cinematic or preview, its part file (guy1's
		// .ADB) in place of `parts`; its body (MENUPART's catalog body 0..4).
		std::string skeleton, partFile;
		int32_t body = 0;
		std::array<int32_t, 3> entity = {-1, -1, -1};
		int32_t cue = -1, cueMs = 0; // a cinematic thing's model cue and the ms it has run
		// A cinematic entity's model's shared material table as the material tracks left it (slot, material; the other
		// slots the model's own), and its colour transform while a colour effect runs on the
		// model (per channel red, green, blue: (byte >> shift) + offset, capped at 255).
		std::vector<std::pair<int32_t, std::string>> materials;
		bool coloured = false;
		std::array<int32_t, 3> colourShift = {}, colourOffset = {};
		// A ring item's lighting: ambient = primary, its one light's colour = secondary,
		// along the scene's first light.
		uint32_t primary = 0xffffff, secondary = 0xffffff;
	};
	std::vector<Thing> things;
};

struct MenuGlyph {
	std::array<int32_t, 4> dest;
	std::array<float, 4> source;
};

// The draw list's 2D elements painted into a 640x480 target (0xAARRGGBB, rows top first) as the original's device
// draws them: pixel centres at +0.5, the top and left edges in, point sampling, keyed texels left
// out, texels times the colour; quads opaque or blended at their alpha. A pixel painted is opaque; one blended over a
// clear pixel keeps the quad's alpha (a layer over a 3D view). View3D elements paint nothing.
LRCORE_API void paint(const MenuData& data, size_t language, std::span<const MenuElement> elements, std::span<const MenuGlyph> glyphs,
	uint32_t* target);

} // namespace lr
