#pragma once

// The race's sounds (Sounds.cpp): the four banks, the sounds held, the 32 voices, the rules' sounds
// and those the show derives. Its operations take the show (its stream, the effects volume, the chassis' engine pitch).

#include "LRCore/Common/Api.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Show/Show.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lr {

class Cache;
class Show;
struct World;

// A race sound the show plays or holds: a rule's one-shot or loop, or one the show derives (the
// engines, the loops that follow the race view, the voices). A 3D sound is heard from the players' cars; a loop that
// is off or can't be heard keeps no voice and waits.
struct ShowSound {
	enum Role : uint8_t { Rule, Engine, Brake, Others, Skid, Spin, Shield, Turbo, Warp, Fuse, Flight, Crackle, Retrigger, Surface };
	int32_t id = -1; // the race's sound id
	uint32_t serial = 0; // unique for the race (a voice names its sound by it)
	Owner owner;
	Role role = Rule;
	uint8_t slot = 0; // the role's: an engine's layer, a turbo's level, a flight's kind
	bool looping = false, positional = true;
	bool standing = false; // made at the load (never asked again: turned on and off)
	bool on = true; // a loop that wants to play
	bool asked = true; // a one-shot not heard yet (it plays at its first hearing or never)
	float near = 30, far = 300, volume = 1, pitch = 1;
	Vec3 place, velocity;
	float playedMs = 0; // a one-shot's, at its pitch
	int8_t voice = -1; // the voice playing it
	// As last heard: volume, pan (negative: to the left), pitch (with the Doppler), the priority.
	float heard = 0, pan = 0, heardPitch = 1;
	int32_t priority = 0;
};

// The four banks (effects, the two voice banks, the track's), the sounds held, the 32 voices (the serial of the sound
// each plays, 0 free), and what this frame asked for and started.
struct LRCORE_API RaceSounds {
	static constexpr size_t Voices = 32;
	struct Bank {
		std::string folder; // where its samples are: "GAMEDATA/COMMON/"
		List<std::string> names; // its samples' files, as the cache names them: "321.PCM"
		List<float> lengthsMs; // each one's length at pitch 1 (0: none, plays nothing)
	};
	std::array<Bank, 4> banks;
	std::vector<ShowSound> held;
	Array<uint32_t, Voices> voices = {};
	struct Started {
		uint32_t serial;
		int32_t id;
	};
	std::vector<Started> asked, started, stopped; // in order (stopped: a loop's voice let go)
	// A sound id's sample (the cache's path), empty if it has none.
	std::string sample(int32_t id) const;
	const ShowSound* find(uint32_t serial) const;
	float lengthOf(int32_t id) const; // its sample's ms at pitch 1
	// The device's law: a heard volume to a gain (linear; 0 silent), and a pan's far channel's.
	static float gainOf(float volume);
	static float panGain(float pan);

	// The banks, the racers' voices and the standing loops, for a world just set up (after the show's chassis).
	void load(Show& show, const Cache& cache, const World& world);
	// The frame's sounds, after the show's other parts: the early events', the hearing and the voices, the later
	// events', the derived loops.
	void update(Show& show, const World& world, int32_t ms);

private:
	void standingLoops(const World& world); // (load's)
	void soundEvent(Show& show, const World& world, const RaceEvent& e);
	void ruleSound(const Show& show, const RaceEvent& e, bool racer, size_t i); // soundEvent's parts
	void voiceLine(Show& show, const World& world, const RaceEvent& e, size_t i);
	ShowSound& play(const Show& show, int32_t id, Owner owner, const Vec3* at, float near = 30, float far = 300, bool looping = false);
	ShowSound* heldFor(ShowSound::Role role, Owner owner, uint8_t slot = 0);
	void stopSound(ShowSound& s); // off: its voice freed
	void letGo(ShowSound& s); // its voice freed (a loop waits for another)
	void hear(const Show& show, const World& world);
	void allocate(bool paused);
	void deriveSounds(Show& show, const World& world, int32_t ms);
	void othersEngine(const Show& show, const World& world, size_t n, bool one, int8_t listener);
	void kartLoops(const Show& show, const World& world, size_t n);
	void actionLoops(Show& show, const World& world, size_t n, int32_t ms);
	void flights(const World& world, int8_t listener);
	void ghostWails(Show& show, const World& world, int32_t ms);
	void retrigger(Show& show, const World& world, int32_t ms);
	void engines(const Show& show, const World& world, int32_t ms);
	Vec3 placeOf(const World& world, const ShowSound& s, Vec3& velocity) const;
	struct Voicing {
		int32_t base = -1; // the racer's first voice line's sound id (-1 silent)
		int32_t reactionMs = 0, scrapeMs = 0; // cooldowns (the shield-hit one gates nothing: Absorb)
		bool engineOn = false; // a player's engine (from the countdown)
		std::array<float, 3> layers = {}; // idle, drive, coast
		bool braking = false;
	};
	std::vector<Voicing> m_voicing; // by racer
	std::vector<int32_t> m_delays; // the track's retriggered sounds' delays, by event resource
	std::vector<uint8_t> m_retriggering; // and whether each was on as the last frame ended
	std::vector<int32_t> m_crackleMs; // by action: a lightning's next crackle
	std::vector<int32_t> m_wailMs; // by hazard: a ghost's wail
	std::vector<float> m_wail; // its depth
	uint32_t m_serial = 0;
	bool m_othersMade = false;
	bool m_racing = false;
};

} // namespace lr
