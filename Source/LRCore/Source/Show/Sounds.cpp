// The show's sounds: the banks, the rules' one-shots and loops from the frame's events, the sounds
// the show derives from the race view (engines, brakes, skids, shields, turbos, fuses, flights, crackles, retriggered
// track sounds), the voices' reactions, how the players' cars hear each sound, and the 32 voices.

#include "LRCore/Show/ShowState.h"
#include "LRData/Archive.h"

#include "Common/Assert.h"
#include "LRCore/Race/World.h"
#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Menus.h"
#include "LRData/Sound.h"
#include "Powers/PowerParts.h"
#include "Show/SoundIds.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace lr {

namespace {

// The banks' first sound ids (the effects', the first voice's, the second voice's, the track's), a voice's lines (six
// negative, then six positive), a time race's second voice bank (VOICE29).
constexpr int32_t BankStarts[4] = {0, 1000, 1100, 3000}, VoiceLines = 12, TimeRaceVoice = 29;

// The sounds the show turns on and off rather than asking for (made at the race's load): the engines' layers, the
// turbo and warp loops, the projectiles' flights, the lightning's hum.
bool standingId(int32_t id)
{
	const auto among = [id](const auto& ids) { return std::ranges::find(ids, id) != std::end(ids); };
	return among(EngineSounds) || (id >= TurboLoopSounds && id <= WarpSound) || among(FlightSounds) || id == BoltHumSound;
}

// A sound id's index in its bank (negative: none).
int32_t bankIndex(int32_t id, size_t& bank)
{
	bank = 3;
	while (bank > 0 && id < BankStarts[bank]) {
		bank--;
	}
	return id - BankStarts[bank];
}

// The taunt's horn by the racer's voice base.
int32_t horn(int32_t base)
{
	switch (base) {
	case 1000: return 26;
	case 1100: return 28;
	case 1112: return 27;
	case 1124: return 29;
	case 1136: return 30;
	case 1148:
	case 1160: return 31;
	default: return -1;
	}
}

// The show hears and gives voices once a frame, after the frame's power-ups, track events, hazards and track timers and
// before the players' controls and the steps: a sound asked later is first heard in the next frame (a brick taken
// in the steps plays a frame after it is asked for, a retriggered track sound in its own).
bool early(RaceEvent::Stage stage) { return stage <= RaceEvent::Hazards || stage == RaceEvent::Session; }

} // namespace

std::string RaceSounds::sample(int32_t id) const
{
	size_t b;
	const int32_t index = bankIndex(id, b);
	if (!inRange(banks[b].names, index) || banks[b].lengthsMs[index] <= 0) {
		return {};
	}
	return banks[b].folder + banks[b].names[index];
}

const ShowSound* RaceSounds::find(uint32_t serial) const
{
	for (const ShowSound& s : held) {
		if (s.serial == serial) {
			return &s;
		}
	}
	return nullptr;
}

float RaceSounds::gainOf(float volume)
{
	if (volume < 0.005f) {
		return 0;
	}
	const float db = -30 + float(int32_t(3000 * volume)) / 100;
	return std::pow(10.0f, db / 20);
}

float RaceSounds::lengthOf(int32_t id) const
{
	size_t b;
	const int32_t index = bankIndex(id, b);
	return inRange(banks[b].lengthsMs, index) ? banks[b].lengthsMs[index] : 0;
}

float RaceSounds::panGain(float pan) { return std::pow(10.0f, -std::fabs(pan) * 25 / 20); }

void RaceSounds::load(Show& show, const Cache& cache, const World& world)
{
	const std::string& track = world.session.brief.track;
	const std::string dir = "GAMEDATA/" + track + "/";
	RaceFile race;
	(void)cache.get(dir + track + ".RAB", race); // (Show::load has read it already)
	// The banks: effects (the track's folder, else COMMON), the first voice (the human player's face), the second
	// (a time race's VOICE29; two players: player 2's face; else the race file's), the track's.
	const auto bankFile = [&](const std::string& name) {
		const std::string file = upper(name) + ".SBK";
		return cache.has(dir + file) ? dir + file : "GAMEDATA/COMMON/" + file;
	};
	const auto voiceFile = [](int face) {
		char name[32];
		std::snprintf(name, sizeof name, "GAMEDATA/VOICES/VOICE%02d.SBK", face);
		return std::string(name);
	};
	const std::vector<RaceBrief::Slot>& slots = world.session.brief.slots;
	const int face1 = slots.empty() ? 0 : slots[0].face;
	const std::string files[4] = {race.effectsBank.empty() ? std::string() : bankFile(race.effectsBank), voiceFile(face1),
		world.session.brief.mode == RaceBrief::Mode::TimeRace	 ? voiceFile(TimeRaceVoice)
			: world.session.brief.twoPlayers && slots.size() > 1 ? voiceFile(slots[1].face)
			: race.voiceBank.empty()							 ? std::string()
																 : "GAMEDATA/VOICES/" + upper(race.voiceBank) + ".SBK",
		race.ambientBank.empty() ? std::string() : dir + upper(race.ambientBank) + ".SBK"};
	for (size_t b = 0; b < 4; b++) {
		NameList list;
		if (files[b].empty() || !cache.get(files[b], list)) {
			continue;
		}
		Bank& bank = banks[b];
		bank.folder = files[b].substr(0, files[b].rfind('/') + 1) + upper(list.header);
		for (const std::string& n : list.names) {
			bank.names.push_back(upper(n));
			Sound pcm;
			const bool found = cache.get(bank.folder + bank.names.back(), pcm) && pcm.rate > 0 && pcm.channels > 0 && !pcm.samples.empty();
			bank.lengthsMs.push_back(found ? float(pcm.samples.size() / pcm.channels) * 1000 / float(pcm.rate) : 0);
		}
	}
	// Each racer's voice base: a built racer's bank by its index (player 1 the first, player 2 the second); a
	// champion's line k of the second bank's group (QUIRK kept: usually someone else's voice). Then its reaction
	// cooldown (a draw at its creation, in racer order).
	DriverTable drivers;
	(void)cache.get("GAMEDATA/COMMON/DRIVERS.DDB", drivers); // (World::setup has read it already)
	const size_t n = world.racers.karts.size();
	m_voicing.assign(n, {});
	for (size_t i = 0; i < n && i < slots.size(); i++) {
		const DriverTable::Driver* d = slots[i].driver.empty() ? nullptr : drivers.find(slots[i].driver);
		m_voicing[i].base = d ? BankStarts[2] + VoiceLines * d->voice : i < 2 ? BankStarts[i + 1] : -1;
	}
	for (size_t i = 0; i < n; i++) {
		m_voicing[i].reactionMs = 5000 + 4 * int32_t(show.draw(Show::Draw::ReactionCooldown, int16_t(i)));
	}
	// The track's sounds' first retrigger delays (every sound resource, in order).
	m_delays.assign(world.track.events.size(), 0);
	for (size_t i = 0; i < world.track.events.size(); i++) {
		if (world.track.events[i].kind == EventResource::Sound) {
			m_delays[i] = 500 + int32_t(show.draw(Show::Draw::SoundDelayLoad) % 1000);
		}
	}
	m_crackleMs.assign(world.powerUps.actions.size(), 0);
	m_wailMs.assign(world.scenery.hazards.size(), 0);
	m_wail.assign(world.scenery.hazards.size(), 0);
	standingLoops(world);
}

// The loops made at the load, off: each player's engine layers (idle, drive, coast) and turbo loops (the three
// levels' and the warp's); with one player the projectiles' flights (cannonball, hook flying, hook attached, missile).
void RaceSounds::standingLoops(const World& world)
{
	const auto standing = [&](int32_t id, ShowSound::Role role, Owner owner, uint8_t slot, float near, float far) {
		ShowSound& s = held.emplace_back();
		s.id = id, s.serial = ++m_serial, s.owner = owner, s.role = role, s.slot = slot;
		s.looping = true, s.standing = true, s.on = false, s.asked = false;
		s.near = near, s.far = far;
	};
	for (size_t i = 0; i < world.racers.karts.size(); i++) {
		if (!world.racers.race[i].player) {
			continue;
		}
		const Owner car{Owner::Racer, int16_t(i)};
		for (uint8_t layer = 0; layer < 3; layer++) {
			standing(EngineSounds[layer], ShowSound::Engine, car, layer, 30, 300);
		}
		for (uint8_t level = 0; level < 3; level++) {
			standing(TurboLoopSounds + level, ShowSound::Turbo, car, level, 30, 300);
		}
		standing(WarpSound, ShowSound::Warp, car, 0, 30, 300);
	}
	if (world.raceView.cameras.size() == 1) {
		for (uint8_t k = 0; k < 4; k++) {
			standing(FlightSounds[k], ShowSound::Flight, {}, k, 200, 500);
		}
	}
}

ShowSound& RaceSounds::play(const Show& show, int32_t id, Owner owner, const Vec3* at, float near, float far, bool looping)
{
	ShowSound& s = held.emplace_back();
	s.id = id, s.serial = ++m_serial, s.owner = owner;
	s.positional = at != nullptr;
	s.place = at ? *at : Vec3{};
	s.near = near, s.far = far;
	s.looping = looping;
	s.standing = standingId(id);
	if (!s.standing) {
		asked.push_back({s.serial, s.id});
	}
	// A 2D one-shot asks for a voice at once and is dropped if none is free.
	if (!s.positional && !looping) {
		s.heard = show.effectsVolume, s.heardPitch = 1, s.priority = 4096;
		for (size_t v = 0; v < Voices; v++) {
			if (voices[v] == 0) {
				voices[v] = s.serial, s.voice = int8_t(v);
				started.push_back({s.serial, s.id});
				break;
			}
		}
		s.on = s.voice >= 0;
		s.asked = false; // (heard now or never)
	}
	return s;
}

ShowSound* RaceSounds::heldFor(ShowSound::Role role, Owner owner, uint8_t slot)
{
	for (ShowSound& s : held) {
		if (s.role == role && s.owner == owner && s.slot == slot && s.on) {
			return &s;
		}
	}
	return nullptr;
}

void RaceSounds::letGo(ShowSound& s)
{
	if (s.voice >= 0) {
		LR_ASSERT(inRange(voices, s.voice));
		voices[s.voice] = 0;
		s.voice = -1;
		if (s.looping) {
			stopped.push_back({s.serial, s.id});
		}
	}
}

void RaceSounds::stopSound(ShowSound& s)
{
	letGo(s);
	s.on = false;
}

// A rule's sound: a loop stopped, or played (a player's engine on with the countdown's "start").
void RaceSounds::ruleSound(const Show& show, const RaceEvent& e, bool racer, size_t i)
{
	if (e.stop) {
		for (ShowSound& s : held) {
			if (s.role == ShowSound::Rule && s.looping && s.owner == e.owner && s.id == e.sound) {
				stopSound(s);
			}
		}
		return;
	}
	// The countdown starts each player's engine with its "start".
	if (e.sound == EngineStartSound && racer) {
		m_voicing[i].engineOn = true;
	}
	// A retriggered track sound only arms the retriggers (below).
	if (e.owner.kind == Owner::TrackEvent && e.value < 255) {
		return;
	}
	if (sample(e.sound).empty()) {
		return;
	}
	ShowSound& s = play(show, e.sound, e.owner, e.positional ? &e.place : nullptr, e.near, e.far, e.looping);
	s.volume = e.volume, s.pitch = e.pitch;
}

// A racer's voice: its horn for a taunt, else a reaction line (drawn) unless one played lately.
void RaceSounds::voiceLine(Show& show, const World& world, const RaceEvent& e, size_t i)
{
	Voicing& v = m_voicing[i];
	const Vec3 at = world.racers.visible[i].position;
	if (e.value == RaceEvent::Taunt) {
		const int32_t id = horn(v.base);
		if (id >= 0 && !sample(id).empty()) {
			play(show, id, e.owner, &at);
		}
		return;
	}
	if (v.reactionMs > 0) {
		return;
	}
	const int32_t line = int32_t(show.draw(Show::Draw::ReactionSound, int16_t(i)) % 6) + (e.value == RaceEvent::Positive ? 6 : 0);
	v.reactionMs = 5000 + 4 * int32_t(show.draw(Show::Draw::ReactionCooldown, int16_t(i)));
	if (v.base >= 0 && !sample(v.base + line).empty()) {
		play(show, v.base + line, e.owner, &at, 100, 400);
	}
}

void RaceSounds::soundEvent(Show& show, const World& world, const RaceEvent& e)
{
	const size_t n = m_voicing.size();
	const bool racer = e.owner.kind == Owner::Racer && e.owner.index >= 0 && size_t(e.owner.index) < n;
	const size_t i = racer ? size_t(e.owner.index) : 0;
	switch (e.kind) {
	case RaceEvent::Sound: ruleSound(show, e, racer, i); break;
	case RaceEvent::Voice:
		if (racer) {
			voiceLine(show, world, e, i);
		}
		break;
	case RaceEvent::Scrape:
		if (racer && m_voicing[i].scrapeMs <= 0) {
			const int32_t id = ScrapeSounds[show.draw(Show::Draw::Scrape, int16_t(i)) & 1];
			m_voicing[i].scrapeMs = 250;
			if (!sample(id).empty()) {
				play(show, id, e.owner, &e.place);
			}
		}
		break;
	case RaceEvent::Bump: {
		const size_t j = size_t(e.value);
		if (racer && j < n && m_voicing[i].scrapeMs <= 0 && m_voicing[j].scrapeMs <= 0) {
			const int32_t id = BumpSounds[show.draw(Show::Draw::Bump, int16_t(i)) & 1];
			m_voicing[i].scrapeMs = m_voicing[j].scrapeMs = 250;
			if (!sample(id).empty()) {
				play(show, id, e.owner, &e.place);
			}
		}
		break;
	}
	case RaceEvent::Absorb:
		if (racer) {
			// (Every absorb plays: its cooldown gates nothing.)
			const int32_t id = BlockSounds[show.draw(Show::Draw::ShieldHit, int16_t(i)) % 3];
			if (!sample(id).empty()) {
				play(show, id, e.owner, &e.place, 200, 600);
			}
		}
		break;
	case RaceEvent::StopEffects: // (a restart: the engines and the show's loops off)
		for (ShowSound& s : held) {
			if (s.role != ShowSound::Rule && s.role != ShowSound::Flight) {
				stopSound(s);
			}
		}
		for (Voicing& v : m_voicing) {
			v.engineOn = false, v.layers = {}, v.braking = false;
		}
		m_racing = false;
		break;
	default: break;
	}
}

// Where a sound is now, and its velocity (per ms): a loop follows what it belongs to, a one-shot stays put.
Vec3 RaceSounds::placeOf(const World& world, const ShowSound& s, Vec3& velocity) const
{
	velocity = s.velocity;
	if (!s.looping) {
		return s.place;
	}
	const int16_t owner = s.owner.index;
	switch (s.owner.kind) {
	case Owner::Racer:
		if (inRange(world.racers.visible, owner) && s.role != ShowSound::Warp) {
			velocity = world.racers.karts[owner].velocity * (s.role == ShowSound::Others ? 2.0f : 1.0f);
			return world.racers.visible[owner].position;
		}
		break;
	case Owner::Action:
		if (inRange(world.powerUps.actions, owner)) {
			const Action& a = world.powerUps.actions[owner];
			if (a.kind == Action::Bolt && a.owner >= 0) {
				velocity = world.racers.karts[a.owner].velocity;
				return world.racers.powers[a.owner].sourcePosition;
			}
			if (a.kind == Action::Shield && a.owner >= 0) {
				velocity = world.racers.karts[a.owner].velocity;
				return world.racers.visible[a.owner].position;
			}
			if (a.kind == Action::Dynamite || s.role == ShowSound::Flight) {
				velocity = a.shot.velocity;
				return a.shot.at;
			}
		}
		break;
	case Owner::TrackEvent:
		if (inRange(world.scenery.events, owner)) {
			// Riding an animated entity's node: at its origin, as the entity was last drawn.
			const EventResource& r = world.track.events[owner];
			if (inRange(world.track.animated, r.entity)) {
				return nodeWorld(world, size_t(r.entity), r.node, {}, world.scenery.clocks[r.entity].drawn);
			}
			return world.scenery.events[owner].at;
		}
		break;
	default: break;
	}
	return s.place;
}

// Each sound as the players' cars hear it: its volume by squared distance between its two
// distances, its pan by the side axis, its pitch with the Doppler; two listeners take the louder volume and the
// volume-weighted pan and pitch.
void RaceSounds::hear(const Show& show, const World& world)
{
	for (ShowSound& s : held) {
		const float volume = s.volume * show.effectsVolume;
		if (!s.positional) {
			s.heard = volume, s.pan = 0, s.heardPitch = s.pitch, s.priority = 4096;
			continue;
		}
		Vec3 velocity;
		s.place = placeOf(world, s, velocity);
		s.velocity = velocity;
		float loudest = 0, weight = 0, pan = 0, pitch = 0;
		for (const PlayerCamera& c : world.raceView.cameras) {
			if (!inRange(world.racers.visible, c.racer)) {
				continue;
			}
			const Pose& car = world.racers.visible[c.racer];
			const Vec3 offset = s.place - car.position;
			const float d2 = lengthSquared(offset), min2 = s.near * s.near, max2 = s.far * s.far;
			const float heard = d2 <= min2 ? volume : d2 >= max2 ? 0 : volume * (1 - (d2 - min2) / (max2 - min2));
			if (heard <= 0) {
				continue;
			}
			const Vec3 side = cross(car.rotation.u, car.rotation.f);
			const float across = dot(side, offset);
			const float heardPan = d2 > 0 ? (across > 0 ? -0.7f : 0.7f) * across * across / d2 : 0;
			float doppler = 1;
			if (d2 > 0) {
				const Vec3 toListener = offset * (-1 / std::sqrt(d2));
				const float closing = std::min(171.5f, dot(s.velocity - world.racers.karts[c.racer].velocity, toListener));
				doppler = 343 / (343 - closing);
			}
			loudest = std::max(loudest, heard);
			weight += heard, pan += heard * heardPan, pitch += heard * s.pitch * doppler;
		}
		s.heard = loudest;
		s.pan = weight > 0 ? pan / weight : 0;
		s.heardPitch = weight > 0 ? pitch / weight : 1;
		s.priority = int32_t(loudest * 2048);
	}
}

// The voices: the finished and the stopped let go, the inaudible give theirs up (a loop waits, a
// one-shot is gone), then the waiting start loudest first, taking the quietest playing 3D sound's voice when none is
// free; a one-shot given none now never plays.
void RaceSounds::allocate(bool paused)
{
	for (ShowSound& s : held) {
		const bool finished = !s.looping && s.voice >= 0 && s.playedMs >= lengthOf(s.id);
		if (finished || (s.looping && !s.on) || (s.voice >= 0 && s.positional && (s.priority == 0 || paused))) {
			letGo(s);
			s.asked = false;
		}
	}
	const auto gone = [](const ShowSound& s) { return s.voice < 0 && !s.asked && (!s.looping || (!s.on && !s.standing)); };
	std::erase_if(held, gone);
	if (!paused) {
		std::vector<ShowSound*> waiting;
		for (ShowSound& s : held) {
			if (s.voice < 0 && s.priority > 0 && (s.looping ? s.on : s.asked)) {
				waiting.push_back(&s);
			}
		}
		std::stable_sort(waiting.begin(), waiting.end(), [](const ShowSound* a, const ShowSound* b) { return a->priority > b->priority; });
		for (ShowSound* s : waiting) {
			size_t v = 0;
			while (v < Voices && voices[v] != 0) {
				v++;
			}
			if (v == Voices) {
				ShowSound* victim = nullptr;
				for (ShowSound& p : held) {
					if (p.voice >= 0 && p.positional && (!victim || p.priority < victim->priority)) {
						victim = &p;
					}
				}
				if (!victim || victim->priority >= s->priority) {
					continue;
				}
				v = size_t(victim->voice);
				letGo(*victim);
			}
			voices[v] = s->serial;
			s->voice = int8_t(v);
			s->asked = false;
			started.push_back({s->serial, s->id});
		}
	}
	for (ShowSound& s : held) {
		s.asked = false;
	}
	std::erase_if(held, gone);
}

void RaceSounds::update(Show& show, const World& world, int32_t ms)
{
	asked.clear();
	stopped.clear();
	started.clear();
	const bool paused = world.session.paused();
	const size_t n = std::min(m_voicing.size(), world.racers.race.size());
	// The cooldowns run in the racers' timers (not on the grid).
	for (size_t i = 0; i < n; i++) {
		if (!(world.racers.race[i].flags & RacerOnGrid)) {
			Voicing& v = m_voicing[i];
			v.reactionMs = std::max(0, v.reactionMs - ms);
			v.scrapeMs = std::max(0, v.scrapeMs - ms);
		}
	}
	// The one-shots' play.
	for (ShowSound& s : held) {
		if (!s.looping && s.voice >= 0) {
			s.playedMs += float(ms) * s.heardPitch;
		}
	}
	const std::vector<RaceEvent>& events = world.raceView.raceEvents;
	for (const RaceEvent& e : events) {
		if (early(e.stage)) {
			soundEvent(show, world, e);
		}
	}
	if (!paused) {
		retrigger(show, world, ms);
	}
	hear(show, world);
	allocate(paused);
	for (const RaceEvent& e : events) {
		if (!early(e.stage)) {
			soundEvent(show, world, e);
		}
	}
	if (!paused) {
		deriveSounds(show, world, ms);
	}
}

// A player's engine: three layers crossfading toward the one the throttle and the speed ask for,
// each pitched by the speed and the chassis; the brake's squeal while braking forward.
void RaceSounds::engines(const Show& show, const World& world, int32_t ms)
{
	for (size_t i = 0; i < m_voicing.size(); i++) {
		Voicing& v = m_voicing[i];
		if (!v.engineOn) {
			continue;
		}
		const Racer& r = world.racers.race[i];
		const Kart& k = world.racers.karts[i];
		// (The throttle as the driver asks for it: a route kart has none; an AI on the physics, its own.)
		const float throttle = world.racers.playback[i].active ? 0 : world.racers.drivers[i].thrust;
		const bool boosting = (r.flags & RacerTurbo) != 0;
		const float speed = k.speed;
		const size_t active = throttle != 0 || boosting ? 1 : speed > 0.015f ? 2 : 0;
		const float target = r.flags & RacerFinished ? 0.5f : 0.7f;
		const float step = float(ms) * 0.03000003f * 0.06f;
		const float P = i < show.chassis.size() ? show.chassis[i].enginePitch : 0;
		const float airborne = k.airborneMs > 50 ? 0.1f : 0;
		for (size_t layer = 0; layer < 3; layer++) {
			float& x = v.layers[layer];
			x = layer == active ? std::min(target, x + (layer == 0 ? 1.5f * step : step)) : std::max(0.0f, x - step);
			ShowSound* s = nullptr;
			for (ShowSound& c : held) {
				if (c.role == ShowSound::Engine && c.owner.index == int16_t(i) && c.slot == layer) {
					s = &c;
				}
			}
			if (!s) {
				continue;
			}
			s->on = layer == active || x > 0;
			s->volume = x >= target ? target : target * std::sin(Tau * float(int32_t(x * 2.2439947f * 162.97466f)) / 1024);
			const float pitch = layer == 0 ? P
				: layer == 1			   ? 0.4f + (speed / 0.17f) * 0.5f * P + airborne
										   : 0.4f + (speed / 0.17f) * 0.6f * P;
			s->pitch = std::clamp(pitch, 0.0f, 1.0f);
		}
		// The brake: a squeal, then its loop, while the player holds the throttle back and the kart still rolls forward
		// (not while boosting: an AI reversing off the grid with the start boost doesn't).
		const float forward = k.headingSpeed;
		const Owner car{Owner::Racer, int16_t(i)};
		if (!v.braking && forward > 0.01f && throttle < 0 && !boosting) {
			const Vec3 at = world.racers.visible[i].position;
			play(show, BrakeSquealSound, car, &at);
			ShowSound& loop = play(show, BrakeLoopSound, car, &at, 30, 300, true);
			loop.role = ShowSound::Brake;
			v.braking = true;
		}
		else if (v.braking && (forward < 0.01f || throttle >= 0)) {
			if (ShowSound* loop = heldFor(ShowSound::Brake, car)) {
				stopSound(*loop);
			}
			v.braking = false;
		}
		if (ShowSound* loop = v.braking ? heldFor(ShowSound::Brake, car) : nullptr) {
			loop->pitch = 1 - (0.17f - forward);
		}
	}
}

// The track's retriggered sounds, while their events are on, in resource order. They update in
// the frame's event stage, before its steps: an event its sphere started in the steps counts from the next frame.
void RaceSounds::retrigger(Show& show, const World& world, int32_t ms)
{
	m_retriggering.resize(m_delays.size());
	for (size_t i = 0; i < world.track.events.size() && i < m_delays.size(); i++) {
		const EventResource& r = world.track.events[i];
		const bool was = m_retriggering[i];
		m_retriggering[i] = world.scenery.events[i].phase != EventState::Idle;
		if (r.kind != EventResource::Sound || r.chance >= 255 || !was) {
			continue;
		}
		m_delays[i] = std::max(0, m_delays[i] - ms);
		if (m_delays[i] > 0) {
			continue;
		}
		m_delays[i] = 500 + int32_t(show.draw(Show::Draw::SoundDelay) % 1000);
		if (int32_t(show.draw(Show::Draw::SoundChance) % 255) < r.chance && !sample(r.sound).empty()) {
			ShowSound& s = play(show, r.sound, {Owner::TrackEvent, int16_t(i)}, &world.scenery.events[i].at, r.nearDistance, r.farDistance);
			s.volume = r.volume, s.pitch = r.pitch, s.role = ShowSound::Retrigger;
		}
	}
}

// The loops the show derives from the race view each frame, and the sounds it paces itself.
void RaceSounds::deriveSounds(Show& show, const World& world, int32_t ms)
{
	const size_t n = std::min(m_voicing.size(), world.racers.race.size());
	if (world.session.phase >= Session::Racing && world.session.phase <= Session::Finished && !m_racing) {
		m_racing = true;
	}
	engines(show, world, ms);
	const bool one = world.raceView.cameras.size() == 1;
	const int8_t listener = world.raceView.cameras.empty() ? -1 : world.raceView.cameras[0].racer;
	othersEngine(show, world, n, one, listener);
	kartLoops(show, world, n);
	actionLoops(show, world, n, ms);
	if (one && listener >= 0) {
		flights(world, listener);
	}
	ghostWails(show, world, ms);
}

// Everyone else's engine: one loop, made as the racing starts (in split screen too, where it never plays), at the kart
// nearest player 1's car.
void RaceSounds::othersEngine(const Show& show, const World& world, size_t n, bool one, int8_t listener)
{
	if (listener >= 0 && m_racing && !m_othersMade) {
		m_othersMade = true;
		ShowSound& s = play(show, OthersEngineSound, {}, &world.racers.visible[listener].position, 30, 200, true);
		s.role = ShowSound::Others;
		s.standing = true; // (from now on turned on and off)
		s.on = false;
		s.volume = 0.8f;
	}
	if (one && listener >= 0 && m_racing) {
		size_t nearest = n;
		float best = 0;
		for (size_t i = 0; i < n; i++) {
			const float d2 = lengthSquared(world.racers.visible[i].position - world.racers.visible[listener].position);
			if (int8_t(i) != listener && (nearest == n || d2 < best)) {
				nearest = i, best = d2;
			}
		}
		for (ShowSound& s : held) {
			if (s.role == ShowSound::Others && nearest < n) {
				const Kart& k = world.racers.karts[nearest];
				const float P = nearest < show.chassis.size() ? show.chassis[nearest].enginePitch : 0;
				s.owner = {Owner::Racer, int16_t(nearest)}; // (followed: heard where it is when heard)
				s.place = world.racers.visible[nearest].position;
				s.velocity = k.velocity * 2;
				s.pitch = std::clamp(0.4f + (k.speed / 0.17f) * 0.5f * P, 0.0f, 1.0f) + (k.airborneMs > 50 ? 0.2f : 0);
				s.on = best < 200 * 200;
			}
		}
	}
}

// Each kart's own loops. The skid (physics karts, from the start): on as steering begins off the walls while
// powersliding or sliding sideways; off when the steering stops. The spin: while a physics kart spins (its spin, not
// the spin-out).
void RaceSounds::kartLoops(const Show& show, const World& world, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		const Kart& k = world.racers.karts[i];
		const Owner car{Owner::Racer, int16_t(i)};
		const bool steering = (k.flags & KartSteering) != 0;
		if (m_racing && !world.racers.playback[i].active) {
			ShowSound* skid = heldFor(ShowSound::Skid, car);
			if (!skid && steering && k.wallHits == 0 &&
				((k.flags & KartPowerslide) || dot(k.travelDir, world.racers.visible[i].rotation.f) < 0.9f)) {
				skid = &play(show, SkidSound, car, &world.racers.visible[i].position, 30, 300, true);
				skid->role = ShowSound::Skid;
				skid->volume = 0.8f;
			}
			if (skid && !steering) {
				stopSound(*skid);
				skid = nullptr;
			}
			if (skid) {
				skid->pitch = 1.4f - 4 * (0.17f - k.headingSpeed) + (k.flags & KartPowerslide ? 0.5f * k.slideFactor : 0);
			}
		}
		ShowSound* spin = heldFor(ShowSound::Spin, car);
		const bool spinning = m_racing && !world.racers.playback[i].active && (k.flags & KartSpinning) != 0;
		if (spinning && !spin) {
			play(show, SpinSound, car, &world.racers.visible[i].position, 30, 300, true).role = ShowSound::Spin;
		}
		else if (!spinning && spin) {
			stopSound(*spin);
		}
		// The ground's own loop (key 0x2e) under a physics kart's wheels (the first wheel whose last surface has
		// one): asked for each frame it isn't playing, and stopped while the kart is slower than 0.009 ft/ms (a slow kart
		// on grass asks and stops it every frame).
		if (!world.racers.playback[i].active) {
			int32_t id = -1;
			for (const Wheel& w : k.wheels) {
				if (id < 0 && inRange(world.track.surfaces, w.surface)) {
					id = world.track.surfaces[w.surface].sound;
				}
			}
			ShowSound* ground = heldFor(ShowSound::Surface, car);
			if (ground && ground->id != id) {
				stopSound(*ground);
				ground = nullptr;
			}
			if (!ground && id >= 0 && !sample(id).empty()) {
				ground = &play(show, id, car, &world.racers.visible[i].position, 30, 300, true);
				ground->role = ShowSound::Surface;
			}
			if (ground && k.speed < 0.009f) {
				stopSound(*ground);
			}
		}
	}
}

// The actions' loops: shields, turbos and the warp, dynamite's fuse, the lightning's hum and crackle.
void RaceSounds::actionLoops(Show& show, const World& world, size_t n, int32_t ms)
{
	List<uint8_t> turbo(n, 0xff);
	std::vector<bool> shielded(world.powerUps.actions.size()), fusing(world.powerUps.actions.size()), ghost(n);
	for (const uint8_t index : world.powerUps.actionList) {
		const Action& a = world.powerUps.actions[index];
		if (a.kind == Action::Shield && a.owner >= 0 && (a.state == ShieldActive || a.state == ShieldFading)) {
			shielded[index] = true;
		}
		if (a.kind == Action::Turbo && a.owner >= 0 && a.state >= TurboIgnite && a.state <= TurboFading) {
			turbo[a.owner] = a.level;
		}
		if (a.kind == Action::Dynamite && a.state == DynamiteThrown) {
			fusing[index] = true;
		}
		if (a.kind == Action::Bolt && a.owner >= 0) {
			int32_t& next = m_crackleMs[index];
			if (a.state == BoltSustain) {
				next -= ms;
				if (next < 0) {
					const Power& pw = world.racers.powers[a.owner];
					const Vec3 at = pw.sourcePosition + pw.sourceForward * float(show.draw(Show::Draw::Crackle) % 50);
					next = 200 + int32_t(show.draw(Show::Draw::Crackle) % 300);
					play(show, CrackleSound, {Owner::Action, int16_t(index)}, &at).role = ShowSound::Crackle;
				}
			}
			else {
				next = 0;
			}
			const float t = float(a.ms);
			for (ShowSound& s : held) {
				if (s.role == ShowSound::Rule && s.id == BoltHumSound && s.owner == Owner{Owner::Action, int16_t(index)}) {
					s.pitch = a.state == BoltRamp ? 1 - 0.0002f * t : a.state == BoltFade ? 1 - 0.0002f * (500 - t) : 1;
				}
			}
		}
	}
	for (size_t i = 0; i < n; i++) {
		ghost[i] = (world.racers.race[i].flags & RacerGhost) != 0;
	}
	for (ShowSound& s : held) {
		const size_t i = size_t(s.owner.index);
		if (s.role == ShowSound::Turbo) {
			s.on = turbo[i] == s.slot;
		}
		else if (s.role == ShowSound::Warp) {
			s.on = ghost[i]; // (QUIRK kept: never placed, so heard from the world's origin)
		}
	}
	// Each shield its loop by level, from its start through its fade; each thrown stick its fuse.
	for (size_t a = 0; a < world.powerUps.actions.size(); a++) {
		const Owner owner{Owner::Action, int16_t(a)};
		const Action& action = world.powerUps.actions[a];
		const bool shielding = shielded[a];
		ShowSound* shield = heldFor(ShowSound::Shield, owner);
		if (shielding && !shield) {
			play(show, ShieldSounds[action.level & 3], owner, &world.racers.visible[action.owner].position, 30, 300, true).role =
				ShowSound::Shield;
		}
		else if (!shielding && shield) {
			stopSound(*shield);
		}
		ShowSound* fuse = heldFor(ShowSound::Fuse, owner);
		if (fusing[a] && !fuse) {
			play(show, FuseSound, owner, &action.shot.at, 30, 300, true).role = ShowSound::Fuse;
		}
		else if (!fusing[a] && fuse) {
			stopSound(*fuse);
		}
	}
}

// The projectiles in flight (one player): per kind, the one nearest player 1's car within 500.
void RaceSounds::flights(const World& world, int8_t listener)
{
	const Vec3 ear = world.racers.visible[listener].position;
	Array<int16_t, 4> nearest = {-1, -1, -1, -1};
	Array<float, 4> best = {};
	for (const uint8_t index : world.powerUps.actionList) {
		const Action& a = world.powerUps.actions[index];
		const int k = a.kind == Action::Cannon && a.state == ShotFlying ? 0
			: a.kind == Action::Hook && a.state == ShotFlying			? 1
			: a.kind == Action::Hook && a.state == HookPulling			? 2
			: a.kind == Action::Missile && a.state == ShotFlying		? 3
																		: -1;
		const float d2 = k >= 0 ? lengthSquared(a.shot.at - ear) : 0;
		if (k >= 0 && d2 < 500 * 500 && (nearest[k] < 0 || d2 < best[k])) {
			nearest[k] = int16_t(index), best[k] = d2;
		}
	}
	for (ShowSound& s : held) {
		if (s.role == ShowSound::Flight) {
			s.on = nearest[s.slot] >= 0;
			if (s.on) {
				s.owner = {Owner::Action, nearest[s.slot]}; // (followed: heard where it is when heard)
				s.place = world.powerUps.actions[nearest[s.slot]].shot.at;
			}
		}
	}
}

// The ghost's wail: every 500 ms a new depth.
void RaceSounds::ghostWails(Show& show, const World& world, int32_t ms)
{
	for (size_t h = 0; h < world.scenery.hazards.size() && h < m_wailMs.size(); h++) {
		if (world.scenery.hazards[h].type != Hazard::Ghost || world.scenery.hazards[h].state == Hazard::Idle) {
			m_wailMs[h] = 500;
			continue;
		}
		m_wailMs[h] += ms;
		if (m_wailMs[h] >= 500) {
			m_wailMs[h] = 0;
			m_wail[h] = float(show.draw(Show::Draw::Wail) % 100) * 0.01f * 0.4f;
		}
		for (ShowSound& s : held) {
			if (s.owner == Owner{Owner::Hazard, int16_t(h)} && s.looping) {
				s.pitch = 1 - std::cos(Tau * float(m_wailMs[h]) / 500) * m_wail[h];
			}
		}
	}
}

} // namespace lr
