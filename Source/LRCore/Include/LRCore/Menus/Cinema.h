#pragma once

// The cinematic screens: the legal splash, the credits, the awards, the win cars and the circuits'
// intros. A player's state, and the screens that play one (Cinema.cpp: the clock, the cues' links, the overlays, the
// flow; CinemaView.cpp: what the scene shows in 3D).

#include "LRCore/Common/Game.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Menus/DrawList.h"
#include "LRCore/Menus/MenuData.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lr {

struct Menus;

// A cinematic screen's player: the scene's cues sorted by their start and end frames and the
// cursors through them, the cues begun (the presentation poses what they show), the 2D overlays they started (laid
// out at their first draw), the two fade slots, and this frame's effects started and stopped for the presentation
// (sounds, particles, colour flashes).
struct CinemaPlayer {
	std::vector<uint16_t> byStart, byEnd;
	size_t starts = 0, ends = 0;
	std::vector<uint8_t> active; // per cue
	std::vector<uint16_t> begun; // the active cues, newest first
	struct Overlay {
		bool shown = false, pending = false, disabled = false;
		std::array<float, 4> place = {}; // left, top, width, height: fractions of the screen
		int32_t width = 0, height = 0; // its content's
	};
	std::vector<Overlay> overlays; // per effect
	struct Fade {
		int16_t effect = -1;
		int32_t left = 0;
		bool expired = false;
	};
	Array<Fade, 2> fades;
	std::vector<std::pair<int16_t, bool>> effects; // this frame's: started (true) or stopped
	bool first = true; // no update yet (a skippable screen is skippable after its first)
	// The 3D side: the scene playing (a preview's switches), its clock when the screen
	// doesn't keep it (previews: scaled ms and frame), each cue's entity (world, animated 1 / plain 0, index; -1 none)
	// and ms run, each world's entities' placements and animated entities' clocks, each world camera's pose as last
	// updated, the camera stack (world, camera), the ambient cue, the directional light cues (oldest first) and each
	// light cue's blink (lit, ms to its next change); the view drawn last.
	size_t scene = 0;
	uint32_t scaled = 0;
	int32_t frame = 0;
	struct Clock {
		int32_t part = -1;
		float time = 0;
		bool on = false;
	};
	struct Placed {
		Vec3 position;
		Basis rows;
	};
	struct View {
		Vec3 eye, forward{1, 0, 0}, up{0, 0, 1};
		float fov = 65, nearPlane = 1, farPlane = 100;
	};
	List<std::array<int16_t, 3>> entityOf;
	std::vector<int32_t> cueMs;
	List<Array<List<Placed>, 2>> placed; // per world: plain, animated
	List<List<Clock>> clocks; // per world, per animated entity
	List<List<View>> views; // per world, per camera
	std::vector<std::array<int16_t, 2>> cameras;
	int32_t ambient = -1;
	std::vector<uint16_t> lights;
	std::vector<std::pair<bool, int32_t>> blinks;
	bool viewed = false;
	View view;
	// The model cues' material tracks: each track's cursor (world, animation, track; the ms it
	// has stepped while bound, never rewound), and the models' shared material tables as the tracks wrote them (world,
	// model, slot; the material: it stays when the cue ends).
	struct Cursor {
		std::array<int16_t, 3> track;
		int32_t ms = 0;
	};
	std::vector<Cursor> cursors;
	struct Written {
		std::array<int16_t, 3> slot;
		std::string material;
	};
	std::vector<Written> written;
	// The colour effects running: the effect, the model it transforms (world, model; shared by
	// every entity drawing it), its offsets.
	struct Colouring {
		int16_t effect = -1;
		std::array<int16_t, 2> model = {-1, -1};
		std::array<float, 3> offsets = {};
	};
	std::vector<Colouring> colourings;
};

struct LRCORE_API Cinema {
	// The screen's state (made afresh with each screen): its clock and player; its model swaps:
	// whether it swaps, the minifig's looks, the racer whose car it shows.
	struct State {
		uint32_t scaled = 0;
		int32_t cinemaFrame = 0, cinemaFrames = 0, rate = 30;
		bool complete = false, finished = false, skippable = false;
		CinemaPlayer cinema;
		bool cinemaSwap = false;
		std::array<uint8_t, 5> cinemaLooks = {};
		RacerRecord cinemaRacer;
	} state;

	// The player started from the screen's scene, its update; the screen drawn; the award screens' flow.
	void startCinema(Menus& m);
	void updateCinema(Menus& m, int32_t dt);
	void drawCinema(Menus& m);
	void leaveCinema(Menus& m);
	// A player made ready for a scene of a screen's cinema, a cue begun or ended and the cues' ticks, a scene played
	// (looping or not; its links fired or not), what the scene shows.
	static void cinemaStart3d(CinemaPlayer& c, const MenuData::Screen& s, size_t scene);
	static void cue3d(CinemaPlayer& c, const MenuData::Screen& s, size_t cue, bool begins);
	static void cueTicks(CinemaPlayer& c, const MenuData::Screen& s, int32_t ms);
	void playScene(Menus& m, CinemaPlayer& c, const MenuData::Screen& s, int32_t ms, int32_t ticks, bool loop, bool links);
	void describeCinema(const Menus& m, const CinemaPlayer& c, const MenuData::Screen& s, MenuScene& scene,
		const std::array<uint8_t, 5>* looks, const RacerRecord* car) const;

private:
	// A cue begun or ended (its links fired), the cues between two frames; an effect started or stopped (cue: the model
	// cue whose link starts it, channels 0x54/0x55).
	void cueChanged(Menus& m, size_t cue, bool begins);
	void cueWindow(Menus& m, CinemaPlayer& c, int32_t a, int32_t b);
	void effect(Menus& m, size_t effect, bool start, int32_t cue = -1);
};

// A cinematic entity's nodes as posed (its scene's thing, from the menus' data).
LRCORE_API void entityNodes(const MenuData& data, const MenuScene& scene, const MenuScene::Thing& t, std::vector<NodeMatrix>& out);

} // namespace lr
