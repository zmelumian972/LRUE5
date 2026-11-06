// Cinematics' scene files (.CDB) and effects files (.CEB).

#include "LRData/Menus.h"
#include "LRData/Archive.h"
#include "LRData/Tokens.h"
#include "MenuValues.h"

#include <algorithm>

namespace lr {

namespace {

// A cinematic's scene file (.CDB): its lists, a scene's fields and blocks of cues.
enum : int32_t {
	CinemaScenes = 0x27,
	CinemaWorlds = 0x28,
	SceneRate = 0x3b,
	SceneFrames = 0x2c,
	SceneCameras = 0x29,
	SceneModels = 0x2e,
	SceneTriggers = 0x37,
	SceneAmbients = 0x35,
	SceneLights = 0x3a,
};

// A cue's fields.
enum : int32_t {
	CueCamera = 0x2a,
	CueStart = 0x2b,
	CueDuration = 0x2c,
	CuePart = 0x2d,
	CuePlainEntity = 0x2f,
	CueAnimatedEntity = 0x30,
	CueCollisionEntity = 0x31,
	CueEntityAt = 0x32, // world database, entity index
	CuePosition = 0x33,
	CueAxes = 0x34, // forward, up
	CueTracks = 0x36,
	CueColour = 0x38,
	CueDirection = 0x39,
	CueBlink = 0x3c,
};

// One of a cue's fields, its key read. False if the key is bad.
bool readCueField(TokenReader& r, CinemaFile::Cue& cue, int32_t& duration, int cueKey)
{
	switch (cueKey) {
	case CueStart: cue.start = r.integer(); break;
	case CueDuration: duration = r.integer(); break;
	case CueCamera: cue.target = name8(r.text()); break;
	case CuePart: cue.part = r.integer(); break;
	case CuePlainEntity:
	case CueAnimatedEntity:
	case CueCollisionEntity:
		cue.entityKind = uint8_t(cueKey);
		cue.target = name8(r.text());
		break;
	case CueEntityAt:
		cue.entityKind = CueEntityAt;
		cue.entityAt = {r.integer(), r.integer()};
		break;
	case CuePosition:
		for (float& x : cue.position) {
			x = r.number();
		}
		break;
	case CueAxes:
		for (float& x : cue.forward) {
			x = r.number();
		}
		for (float& x : cue.up) {
			x = r.number();
		}
		break;
	case CueTracks:
		r.counted([&] {
			for (int32_t& x : cue.tracks.emplace_back()) {
				x = r.integer();
			}
		});
		break;
	case CueColour:
		for (int32_t& x : cue.colour) {
			x = r.truncated(r.number()) & 255;
		}
		break;
	case CueDirection:
		for (float& x : cue.direction) {
			x = r.number();
		}
		break;
	case CueBlink: cue.blink = {r.integer(), r.integer()}; break;
	default:
		if (cueKey < 0) {
			return false;
		}
		break;
	}
	return true;
}

// A cue's block "{ fields }": it ends its duration after its start. False if a key is bad or the reader failed.
bool readCue(TokenReader& r, CinemaFile::Cue& cue)
{
	int32_t duration = 0;
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		if (!readCueField(r, cue, duration, r.keyword())) {
			return false;
		}
		r.skipValues();
		if (!r.atEnd() && r.tokens().list[r.position()].kind == Token::OpenCount) {
			r.skipCounted();
		}
	}
	cue.end = cue.start + duration;
	return !r.failed();
}

// A scene's block: its rate, its frames (else its last camera's end) and its cue blocks. A bad key fails the reader;
// false if it has failed.
bool readScene(TokenReader& r, CinemaFile::Scene& s)
{
	using Cue = CinemaFile::Cue;
	int32_t lastCamera = 0;
	r.expect(Token::Open);
	// The cue blocks in any order: kept as cameras, models, triggers, ambients, lights.
	std::array<std::vector<Cue>, 5> byKind;
	while (!r.failed() && !r.next(Token::Close)) {
		const int field = r.keyword();
		if (field == SceneRate) {
			s.rate = r.integer();
			continue;
		}
		if (field == SceneFrames) {
			s.frames = r.integer();
			continue;
		}
		static const int Blocks[5] = {SceneCameras, SceneModels, SceneTriggers, SceneAmbients, SceneLights};
		const int* kind = std::find(std::begin(Blocks), std::end(Blocks), field);
		if (kind == std::end(Blocks)) {
			r.fail();
			return false;
		}
		r.counted([&] {
			r.keyword();
			Cue cue;
			cue.kind = Cue::Kind(kind - Blocks);
			cue.name = name8(r.text());
			if (!readCue(r, cue)) {
				return false;
			}
			if (cue.kind == Cue::Camera) {
				lastCamera = std::max(lastCamera, cue.end);
			}
			byKind[cue.kind].push_back(cue);
			return true;
		});
	}
	for (const std::vector<Cue>& k : byKind) {
		s.cues.insert(s.cues.end(), k.begin(), k.end());
	}
	if (s.frames == 0) {
		s.frames = lastCamera;
	}
	return !r.failed();
}

} // namespace

bool CinemaFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	while (!r.failed() && !r.atEnd()) {
		const int key = r.keyword();
		if (key == CinemaWorlds) {
			r.counted([&] { worlds.emplace_back(name8(r.text())); });
			continue;
		}
		if (key != CinemaScenes) {
			return false;
		}
		r.counted([&] {
			r.keyword();
			Scene& s = scenes.emplace_back();
			s.name = name8(r.text());
			return readScene(r, s);
		});
	}
	return !r.failed() && !scenes.empty();
}

namespace {

using Effect = CinemaFile::Effect;

// A cinematic's effects file (.CEB): its name lists, effect lists (by Effect::Kind) and link
// channels.
enum : int32_t {
	EffectsParticleSets = 0x27,
	EffectsStringTables = 0x28,
	EffectsFonts = 0x29,
	EffectsImageLists = 0x2a,
	EffectsSoundBanks = 0x5c,
	LinkFirst = 0x50, // ..0x5b: 0x50 + 2 * cue kind + (0 begins, 1 ends); 0x50/0x51 any kind
	LinkLast = 0x5b,
	LinkStart = 0x4e,
	LinkStop = 0x4f,
};

// An effect's fields.
enum : int32_t {
	ColourShift = 0x2c,
	ColourOffset = 0x2d,
	ColourOffsetSpeed = 0x2e,
	SoundBankAndIndex = 0x30,
	SoundVolume = 0x32,
	SoundFrequency = 0x33,
	SoundPan = 0x34,
	SoundLooping = 0x35,
	TextString = 0x40, // string table, string
	TextLiteral = 0x41,
	TextFont = 0x42,
	OverlayLeft = 0x43,
	OverlayTop = 0x44,
	OverlayCentreX = 0x45,
	OverlayCentreY = 0x46,
	OverlayWidth = 0x47,
	OverlayHeight = 0x48,
	OverlaySpeedFirst = 0x49, // ..0x4c: left, top, width, height
	OverlaySpeedLast = 0x4c,
	OverlayPicture = 0x4d,
	EntityAnimated = 0x5d,
	EntityPlain = 0x5e,
	EntityCollision = 0x5f,
	FadeMs = 0x61,
	FadeMode = 0x62, // then FadeIn or FadeOut
	FadeIn = 0x63,
	FadeOut = 0x64,
	EffectColour = 0x66, // a fade's colour, an overlay's tint
};

// A field given as values: sound settings, the text, font or picture, the colour, the entity, the colour shifts.
void readEffectValues(Effect& e, int field, const Values& v)
{
	const auto real = [&](size_t at, float none) { return at < v.reals.size() ? v.reals[at] : none; };
	const bool sound = e.kind == Effect::Sound || e.kind == Effect::Stream;
	switch (field) {
	case SoundBankAndIndex:
		if (sound) {
			e.bank = v.integer(0), e.sound = v.integer(1);
		}
		break;
	case SoundVolume: e.volume = sound ? real(0, 1) : e.volume; break;
	case SoundFrequency: e.frequency = sound ? real(0, 1) : e.frequency; break;
	case SoundPan: e.pan = sound ? real(0, 0) : e.pan; break;
	case SoundLooping: e.loops = sound ? true : e.loops; break;
	case TextString: e.table = v.integer(0), e.string = v.integer(1); break;
	case TextLiteral: e.text = v.text(0); break;
	case TextFont: e.font = v.text(0); break;
	case OverlayPicture: e.picture = v.text(0); break;
	case FadeMs: e.ms = v.integer(0); break;
	case EffectColour:
		e.colour = 0xff000000u | uint32_t(v.integer(0) & 255) << 16 | uint32_t(v.integer(1) & 255) << 8 | uint32_t(v.integer(2) & 255);
		e.tinted = true;
		break;
	case EntityAnimated:
	case EntityPlain:
	case EntityCollision:
		e.entity = name8(v.text(0));
		e.entityKind = uint8_t(field);
		break;
	case ColourShift:
	case ColourOffset:
	case ColourOffsetSpeed:
		if (e.kind == Effect::Colour) {
			for (size_t ch = 0; ch < 3; ch++) {
				if (field == ColourShift) {
					e.shift[ch] = v.integer(ch);
				}
				else if (field == ColourOffset) {
					e.offset[ch] = v.integer(ch);
				}
				else {
					e.offsetSpeed[ch] = real(ch, 0);
				}
			}
		}
		break;
	default: break; // what only the presentation uses
	}
}

// An overlay's place given (left, top, width, height), centring or speed. False if the field isn't one.
bool readOverlayField(TokenReader& r, Effect& e, int field)
{
	// The place's fields, in the order of Effect::place.
	static constexpr int Place[] = {OverlayLeft, OverlayTop, OverlayWidth, OverlayHeight};
	const int* place = std::find(std::begin(Place), std::end(Place), field);
	if (place != std::end(Place)) {
		const size_t at = size_t(place - std::begin(Place));
		e.place[at] = r.real();
		e.given[at] = true;
		return true;
	}
	if (field == OverlayCentreX) {
		e.given[0] = false;
		return true;
	}
	if (field == OverlayCentreY) {
		e.given[1] = false;
		return true;
	}
	if (field >= OverlaySpeedFirst && field <= OverlaySpeedLast) {
		e.speed[size_t(field - OverlaySpeedFirst)] = r.real();
		return true;
	}
	return false;
}

// An effect's block "{ fields }".
void readEffect(TokenReader& r, Effect& e)
{
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int field = r.keyword();
		if (field == FadeMode) {
			const int mode = r.keyword();
			e.in = mode == FadeIn;
			e.out = mode == FadeOut;
			continue;
		}
		if ((e.kind == Effect::Text || e.kind == Effect::Picture) && readOverlayField(r, e, field)) {
			continue;
		}
		readEffectValues(e, field, valuesAfter(r));
	}
}

// A counted list of one kind's effects (text and pictures white unless tinted).
void readEffects(TokenReader& r, Effect::Kind kind, std::vector<Effect>& effects)
{
	r.counted([&] {
		r.keyword();
		Effect e;
		e.kind = kind;
		e.name = name8(r.text());
		if (e.kind == Effect::Text || e.kind == Effect::Picture) {
			e.colour = 0xffffffff;
		}
		readEffect(r, e);
		effects.push_back(e);
	});
}

} // namespace

bool CinemaFile::decodeEffects(std::span<const uint8_t> bytes)
{
	TokenReader r(bytes);
	const auto names = [&](std::vector<std::string>& out) { r.counted([&] { out.push_back(std::string(r.text())); }); };
	// Links name their effects: resolved once every list is read.
	struct Named {
		uint8_t channel, kind;
		bool start;
		std::string cue, effect;
	};
	std::vector<Named> named;
	std::vector<std::string> unused;
	while (!r.failed() && !r.atEnd()) {
		const int key = r.keyword();
		switch (key) {
		case EffectsParticleSets: names(unused); continue; // particle sets
		case EffectsStringTables: names(stringTables); continue;
		case EffectsFonts: names(unused); continue; // fonts
		case EffectsImageLists: names(imageLists); continue;
		case EffectsSoundBanks: names(soundBanks); continue;
		default: break;
		}
		if (key >= LinkFirst && key <= LinkLast) {
			r.counted([&] {
				r.keyword();
				Named l{uint8_t(key), 0, true, name8(r.text()), {}};
				r.expect(Token::Open);
				l.kind = uint8_t(r.keyword());
				l.start = r.keyword() == LinkStart;
				l.effect = name8(r.text());
				r.close();
				named.push_back(l);
			});
			continue;
		}
		static const int Kinds[] = {Effect::Colour, Effect::Sound, Effect::Stream, Effect::Particles, Effect::Fade, Effect::Text,
			Effect::Picture};
		if (std::find(std::begin(Kinds), std::end(Kinds), key) == std::end(Kinds)) {
			return false;
		}
		readEffects(r, Effect::Kind(key), effects);
	}
	for (const Named& l : named) {
		Link& link = links.emplace_back();
		link.channel = l.channel;
		link.cue = l.cue;
		link.start = l.start;
		link.effect = indexWhere(effects, [&](const Effect& e) { return e.kind == l.kind && e.name == l.effect; });
		if (link.effect < 0) {
			return false;
		}
	}
	return !r.failed();
}

} // namespace lr
