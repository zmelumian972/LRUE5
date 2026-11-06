#include "LRData/Race.h"
#include "LRData/Archive.h"

#include "LRData/Tokens.h"

#include <algorithm>
#include <cctype>

namespace lr {

namespace {

// .RAB
enum : int32_t {
	KeyRace = 0x35,
	KeyCollision = 0x2b, // "database" "track entity" "another trigger entity"
	KeySurfaces = 0x43, // "surface table"
	KeyCheckpoints = 0x48, // "checkpoint file" "checkpoint entity"
	KeyStartGrid = 0x41,
	KeyTouch = 0x39,
	KeyEvents = 0x2c,
	KeyIntroCamera = 0x49,
	KeyStartCamera = 0x29, // r r r: the cameras' place before the race
	KeyStartCameraAxes = 0x28, // r r r look, r r r down
	KeyScene = 0x2e,
	KeySharedScene = 0x3b,
	KeyPowerups = 0x33, // "bricks" "material animations" "models"
	KeyPositional = 0x37,
	KeyTimers = 0x38,
	KeyHazards = 0x44,
	KeyTargets = 0x4a,
	KeyEmitters = 0x3a, // "race set" "track set"
	KeyEffectsBank = 0x3c,
	KeyVoiceBank = 0x3d,
	KeyAmbientBank = 0x3f,
	KeyMapBounds = 0x46, // r r r r: min x, max y, max x, min y
	KeySky = 0x42,
	KeySkyWorld = 0x27,
	KeyGlobalWorld = 0x40,
	KeyLocalWorld = 0x45,
};

// .SPB
enum : int32_t {
	GridPlaces = 0x27, // [n] { K(0x27) i place { fields } ... }
	PlacePosition = 0x28, // r r r
	PlaceAxes = 0x29, // r r r (forward) r r r (up)
};

// .RRB
enum : int32_t {
	RoutePoints = 0x27, // [n] { 10 integers ... }
	RouteStartRotation = 0x28,
	RouteStart = 0x29,
	RouteLoop = 0x2a,
	RouteLoopRotation = 0x2b,
	RouteLoopTime = 0x2c,
	RouteLoopPoint = 0x2d,
};

// .CPB
enum : int32_t {
	CheckpointList = 0x27, // [n] { K(0x27) { fields } ... }
	CheckpointPlane = 0x28, // r r r r
	CheckpointNext = 0x29, // i i i i
	CheckpointPosition = 0x2a, // r r r
};

// .TRB
enum : int32_t {
	SphereList = 0x27, // [n] { K(0x27) { fields } ... }
	SphereCentre = 0x29,
	SphereRadius = 0x2a,
	SphereEvent = 0x2b,
	SphereProjectilesOnly = 0x2c,
	SphereWorld = 0x2d,
	SphereLapGate = 0x2e,
	SpherePlayersOnly = 0x2f,
};

// .EVB: sections K(kind) [n] { K(0x27) i id [K(0x3c)] { fields } ... }
enum : int32_t {
	SectionSounds = 0x2a,
	SectionPartAnimations = 0x28,
	SectionMaterialAnimations = 0x29,
	SectionParticles = 0x3d,
	SectionSkyStates = 0x42,
	SectionTimers = 0x4b,
	SectionNodeTransforms = 0x52,
	SectionColours = 0x4d,
	SectionLapMarks = 0x51,
	SectionModelDistances = 0x53,
	SectionLookTargets = 0x55,
	SectionForces = 0x59,
	SectionLinks = 0x39,
	EventRecord = 0x27, // also a chain inside the fields: K(0x27) K(slot) i event
	EventOnEnd = 0x3c,
	EventNoEnd = 0x3a,
	ChainStart = 0x36,
	ChainActive = 0x34,
	ChainEnd = 0x37,
	TimerDuration = 0x49,
	TimerHold = 0x4c,
	MarkOpen = 0x36,
	MarkPast = 0x37,
	ForceVector = 0x3e,
	ForceChannel = 0x4c,
	ForceArming = 0x5a,
	LinkForced = 0x39,
	SoundId = 0x2c,
	ResourceLooping = 0x2d,
	SoundVolume = 0x2f,
	SoundPitch = 0x30,
	SoundNear = 0x31,
	SoundFar = 0x32,
	ResourceEntity = 0x33,
	ResourcePosition = 0x3b,
	ParticleEmitter = 0x3d,
	ParticleAxes = 0x3e,
	ResourceAtEvent = 0x3f,
	SoundProbability = 0x40,
	ResourceShared = 0x41,
	SkyName = 0x43,
	SkyMs = 0x44,
	SkyHideDome = 0x45,
	ResourceNode = 0x54,
	ColourClear = 0x50,
	NodeWorld = 0x4a, // a node transform's collision entity
	DistanceHide = 0x46, // a model distance's "hide while active"
};

// An event record's field: its key (or a run of keys, first..last), the kinds whose records read it (eventKindBit) and
// how. A record's key is read by the first entry that matches it.
struct EventField {
	int first, last;
	uint16_t kinds;
	void (*read)(TokenReader& r, EventFile::Resource& e, int key);
};

constexpr uint16_t eventKindBit(EventFile::Kind kind) { return uint16_t(1u << kind); }

using EK = EventFile;
constexpr uint16_t SoundBit = eventKindBit(EK::Sounds), PartBit = eventKindBit(EK::PartAnimations),
				   ParticleBit = eventKindBit(EK::Particles), SkyBit = eventKindBit(EK::SkyStates), TimerBit = eventKindBit(EK::Timers),
				   NodeBit = eventKindBit(EK::NodeTransforms), ColourBit = eventKindBit(EK::Colours),
				   LapMarkBit = eventKindBit(EK::LapMarks), DistanceBit = eventKindBit(EK::ModelDistances),
				   LookBit = eventKindBit(EK::LookTargets), ForceBit = eventKindBit(EK::Forces), LinkBit = eventKindBit(EK::Links);
constexpr uint16_t AllEventKinds = uint16_t((1u << (EK::Links + 1)) - 1);

constexpr EventField EventFields[] = {
	{EventNoEnd, EventNoEnd, uint16_t(AllEventKinds & ~(LapMarkBit | LookBit)), [](TokenReader&, EK::Resource& e, int) { e.noEnd = true; }},
	{TimerDuration, TimerDuration, TimerBit, [](TokenReader& r, EK::Resource& e, int) { e.durationMs = r.integer(); }},
	{TimerHold, TimerHold, TimerBit, [](TokenReader& r, EK::Resource& e, int) { e.holdEvent = r.integer(); }},
	{MarkOpen, MarkPast, LapMarkBit, [](TokenReader&, EK::Resource& e, int key) { e.zone = key == MarkOpen ? EK::Open : EK::Past; }},
	{ForceVector, ForceVector, ForceBit, [](TokenReader& r, EK::Resource& e, int) { e.force = r.reals<3>(); }},
	{ForceChannel, ForceChannel, ForceBit, [](TokenReader& r, EK::Resource& e, int) { e.channel = r.integer(); }},
	{ForceArming, ForceArming, ForceBit, [](TokenReader& r, EK::Resource& e, int) { e.armingEvent = r.integer(); }},
	{LinkForced, LinkForced, LinkBit, [](TokenReader& r, EK::Resource& e, int) { e.forced = r.integer(); }},
	{ResourcePosition, ResourcePosition, SoundBit | ParticleBit, [](TokenReader& r, EK::Resource& e, int) { e.position = r.reals<3>(); }},
	{ResourceAtEvent, ResourceAtEvent, SoundBit | ParticleBit | PartBit, [](TokenReader&, EK::Resource& e, int) { e.atEvent = true; }},
	{ColourClear, ColourClear, ColourBit, [](TokenReader&, EK::Resource& e, int) { e.clear = true; }},
	{ResourceEntity, ResourceEntity, SoundBit | ParticleBit | PartBit | ColourBit | NodeBit | DistanceBit,
		[](TokenReader& r, EK::Resource& e, int) { e.entity = r.text(); }},
	// (QUIRK: the entity is looked up as it is read, so the shared database only if this came first.)
	{ResourceShared, ResourceShared, ParticleBit | PartBit | DistanceBit,
		[](TokenReader&, EK::Resource& e, int) { e.shared = e.kind == EK::Particles || e.entity.empty(); }},
	{ResourceNode, ResourceNode, SoundBit | ParticleBit | NodeBit, [](TokenReader& r, EK::Resource& e, int) { e.node = r.integer(); }},
	{NodeWorld, NodeWorld, NodeBit, [](TokenReader& r, EK::Resource& e, int) { e.world = r.text(); }},
	{DistanceHide, DistanceHide, DistanceBit, [](TokenReader&, EK::Resource& e, int) { e.hide = true; }},
	{ParticleEmitter, ParticleEmitter, ParticleBit, [](TokenReader& r, EK::Resource& e, int) { e.emitter = r.text(); }},
	{ParticleAxes, ParticleAxes, ParticleBit, [](TokenReader& r, EK::Resource& e, int) { e.axes = r.reals<6>(); }},
	{SoundId, SoundId, SoundBit, [](TokenReader& r, EK::Resource& e, int) { e.sound = r.integer(); }},
	{ResourceLooping, ResourceLooping, SoundBit | PartBit, [](TokenReader&, EK::Resource& e, int) { e.looping = true; }},
	{SoundVolume, SoundFar, SoundBit,
		[](TokenReader& r, EK::Resource& e, int key) {
			float* const fields[] = {&e.volume, &e.pitch, &e.nearDistance, &e.farDistance}; // by key, SoundVolume..SoundFar
			*fields[key - SoundVolume] = r.real();
		}},
	{SoundProbability, SoundProbability, SoundBit, [](TokenReader& r, EK::Resource& e, int) { e.probability = r.real(); }},
	{SkyName, SkyName, SkyBit, [](TokenReader& r, EK::Resource& e, int) { e.sky = r.text(); }},
	{SkyMs, SkyMs, SkyBit, [](TokenReader& r, EK::Resource& e, int) { e.skyMs = r.integer(); }},
	{SkyHideDome, SkyHideDome + 3, SkyBit,
		[](TokenReader&, EK::Resource& e, int key) { e.skyFlags |= uint8_t(1u << (key - SkyHideDome)); }},
	// (Alone, not after K(0x27): the parts) active 0x34, idle 0x35, start 0x36, end 0x37.
	{ChainActive, ChainEnd, PartBit, [](TokenReader& r, EK::Resource& e, int key) { e.parts[size_t(key - ChainActive)] = r.integer(); }},
};

// .PWB
enum : int32_t {
	ColourBricks = 0x27, // [n] { K(0x27) { fields } ... }
	WhiteBricks = 0x2f,
	BrickPosition = 0x28,
	BrickRed = 0x2a, // ..0x2d: red, yellow, blue, green
	BrickRespawn = 0x2e,
};

// .TGB, .TIB
enum : int32_t {
	TargetList = 0x27,
	TargetPosition = 0x28,
	TargetDisabledBy = 0x29,
	TimerList = 0x27,
	TimerOn = 0x28, // K(0x2b) i (random) or i
	TimerOff = 0x29,
	TimerEvent = 0x2a,
	TimerRandom = 0x2b,
	TimerDelay = 0x2d,
};

// .HZB: K(0x27) [n] { K(type) [body] ... }
enum : int32_t {
	HazardList = 0x27,
	HazardLauncher = 0x33,
	HazardOpener = 0x34, // a triggered animation
	HazardOscillator = 0x36,
	HazardLaunchers = 0x3d,
	HazardRock = 0x3e,
	HazardSweep = 0x40,
	HazardRain = 0x43,
	HazardGrabber = 0x48,
	FieldSource = 0x37, // also a multi-launcher's launch list
	FieldTarget = 0x38, // also its target list
	FieldTriggerPoint = 0x39,
	FieldTriggerRadius = 0x3a,
	FieldTrigger = 0x3b,
	FieldSurface = 0x41,
	FieldEntity = 0x42,
	FieldInterval = 0x44,
	FieldMaterial = 0x45,
	FieldPeriod = 0x46,
	FieldSweep = 0x47,
};

// "K(key) [n] { K(key) ... { fields } ... }": calls entry(r) n times, after each entry's keyword (and before its
// optional value and brace); entry reads up to and including the entry's closing brace.
template <typename Entry> bool forEachEntry(std::span<const uint8_t> bytes, int key, Entry entry)
{
	TokenReader r(bytes);
	r.table(key, [&] { return entry(r); });
	return !r.failed() && r.atEnd();
}

} // namespace

bool RaceFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	if (r.keyword() != KeyRace) {
		return false;
	}
	name = r.text();
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		if (key < 0) {
			return false;
		}
		switch (key) {
		case KeyCollision:
			collisionFile = r.text();
			trackVolume = r.text();
			break;
		case KeySurfaces: surfaceFile = r.text(); break;
		case KeyCheckpoints:
			checkpointFile = r.text();
			checkpointVolume = r.text();
			break;
		case KeyStartGrid: startGridFile = r.text(); break;
		case KeyTouch: touchFile = r.text(); break;
		case KeyEvents: eventFile = r.text(); break;
		case KeyIntroCamera: introCamera = r.text(); break;
		case KeyScene: scene = r.text(); break;
		case KeySharedScene: sharedScene = r.text(); break;
		case KeyPowerups:
			powerupFile = r.text();
			r.text();
			powerupScene = r.text();
			break;
		case KeyPositional: positionalFile = r.text(); break;
		case KeyTimers: timerFile = r.text(); break;
		case KeyHazards: hazardFile = r.text(); break;
		case KeyTargets: targetFile = r.text(); break;
		case KeyStartCamera: startCamera = r.reals<3>(); break;
		case KeyEmitters:
			emitterFiles[0] = r.text();
			emitterFiles[1] = r.text();
			break;
		case KeyEffectsBank: effectsBank = r.text(); break;
		case KeyVoiceBank: voiceBank = r.text(); break;
		case KeyAmbientBank: ambientBank = r.text(); break;
		case KeyMapBounds: mapBounds = r.reals<4>(); break;
		case KeySky: skyFile = r.text(); break;
		case KeySkyWorld: skyWorld = r.text(); break;
		case KeyGlobalWorld: globalWorld = r.text(); break;
		case KeyLocalWorld: localWorld = r.text(); break;
		case KeyStartCameraAxes:
			startLook = r.reals<3>();
			startDown = r.reals<3>();
			break;
		}
		r.skipValues(); // the rest of the entry's values (and every other entry's)
	}
	return !r.failed() && r.atEnd() && !collisionFile.empty();
}

std::string binaryName(std::string_view sourceName)
{
	std::string name = upper(sourceName);
	if (name.find('.') != std::string::npos) {
		name.back() = 'B';
	}
	return name;
}

bool StartGrid::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	List<uint8_t> seen;
	const bool ok = forEachEntry(bytes, GridPlaces, [&](TokenReader& r) {
		const int32_t index = r.integer();
		if (index < 0 || index > 255) {
			return false;
		}
		if (!inRange(places, index)) {
			places.resize(size_t(index) + 1);
			seen.resize(size_t(index) + 1);
		}
		Place& p = places[index];
		seen[index] = 1;
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case PlacePosition: p.position = r.reals<3>(); break;
			case PlaceAxes:
				p.forward = r.reals<3>();
				p.up = r.reals<3>();
				break;
			default: return false;
			}
		}
		return true;
	});
	for (uint8_t s : seen) {
		if (!s) {
			return false;
		}
	}
	return ok;
}

bool RouteFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case RouteStart: start = r.reals<3>(); break;
		case RouteStartRotation: startRotation = r.reals<4>(); break;
		case RouteLoop: loop = r.reals<3>(); break;
		case RouteLoopRotation: loopRotation = r.reals<4>(); break;
		case RouteLoopTime: loopTime = r.integer(); break;
		case RouteLoopPoint: loopPoint = r.integer(); break;
		case RoutePoints: {
			// (Read until the count or the stream runs out: a broken file's count can be any size.)
			r.counted([&] {
				int32_t v[10];
				for (int32_t& x : v) {
					x = r.integer();
				}
				// Each value is truncated to its field's width.
				points.push_back({int16_t(v[0]), int16_t(v[1]), int8_t(v[2]), {int8_t(v[3]), int8_t(v[4]), int8_t(v[5]), int8_t(v[6])},
					int8_t(v[7]), int8_t(v[8]), uint8_t(v[9])});
			});
			break;
		}
		case -1: return false;
		default: r.skipValues(); // unknown blocks' values
		}
	}
	return !r.failed() && inRange(points, loopPoint);
}

bool CheckpointFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	return forEachEntry(bytes, CheckpointList, [&](TokenReader& r) {
		Checkpoint& c = checkpoints.emplace_back();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case CheckpointPlane: {
				const std::array<float, 4> plane = r.reals<4>();
				c.normal = {plane[0], plane[1], plane[2]};
				c.offset = plane[3];
				break;
			}
			case CheckpointNext:
				for (uint8_t& n : c.next) {
					n = uint8_t(r.integer());
				}
				break;
			case CheckpointPosition: c.position = r.reals<3>(); break;
			default: return false;
			}
		}
		return checkpoints.size() <= 254;
	});
}

bool TouchFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	return forEachEntry(bytes, SphereList, [&](TokenReader& r) {
		Sphere& s = spheres.emplace_back();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case SphereCentre: s.centre = r.reals<3>(); break;
			case SphereRadius: s.radius = r.real(); break;
			case SphereEvent: s.event = r.integer(); break;
			case SphereProjectilesOnly: s.projectilesOnly = true; break;
			case SphereWorld: s.world = r.text(); break;
			case SphereLapGate:
				s.lapGated = true;
				s.lap = r.integer();
				break;
			case SpherePlayersOnly: s.playersOnly = true; break;
			default: return false;
			}
		}
		return true;
	});
}

bool EventFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	static const std::pair<int, Kind> sections[] = {{SectionSounds, Sounds}, {SectionPartAnimations, PartAnimations},
		{SectionMaterialAnimations, MaterialAnimations}, {SectionParticles, Particles}, {SectionSkyStates, SkyStates},
		{SectionTimers, Timers}, {SectionNodeTransforms, NodeTransforms}, {SectionColours, Colours}, {SectionLapMarks, LapMarks},
		{SectionModelDistances, ModelDistances}, {SectionLookTargets, LookTargets}, {SectionForces, Forces}, {SectionLinks, Links}};
	TokenReader r(bytes);
	while (!r.atEnd() && !r.failed()) {
		const int section = r.keyword();
		const auto it = std::find_if(std::begin(sections), std::end(sections), [&](const auto& s) { return s.first == section; });
		if (it == std::end(sections)) {
			return false;
		}
		const Kind kind = it->second;
		r.entries(EventRecord, [&] {
			Resource& e = resources.emplace_back();
			e.kind = kind;
			e.id = r.integer();
			if (r.keyword() == EventOnEnd) {
				e.onEnd = true;
				e.noEnd = kind == Colours; // QUIRK: a colour change that starts on the end has no end
			}
			r.expect(Token::Open);
			while (!r.failed() && !r.next(Token::Close)) {
				const int key = r.keyword();
				if (key < 0) {
					return false;
				}
				if (key == EventRecord && kind != Sounds && kind != Links) {
					const int slot = r.keyword();
					const int32_t event = r.integer();
					// The chains in order: as it starts, while active, as it ends.
					static constexpr int ChainSlots[] = {ChainStart, ChainActive, ChainEnd};
					const auto chain = std::find(std::begin(ChainSlots), std::end(ChainSlots), slot);
					if (chain != std::end(ChainSlots)) {
						e.chains[chain - std::begin(ChainSlots)] = event;
					}
					continue;
				}
				const auto field = std::find_if(std::begin(EventFields), std::end(EventFields),
					[&](const EventField& f) { return key >= f.first && key <= f.last && (f.kinds & eventKindBit(kind)); });
				if (field != std::end(EventFields)) {
					field->read(r, e, key);
				}
				r.skipValues(); // the presentation's fields (sounds, models, particles, tints...)
			}
			return true;
		});
	}
	return !r.failed();
}

bool PowerupFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	while (!r.atEnd() && !r.failed()) {
		const int section = r.keyword();
		if (section != ColourBricks && section != WhiteBricks) {
			return false;
		}
		r.entries(section, [&] {
			Brick b;
			r.expect(Token::Open);
			while (!r.failed() && !r.next(Token::Close)) {
				const int key = r.keyword();
				if (key == BrickPosition) {
					b.position = r.reals<3>();
				}
				else if (key >= BrickRed && key <= BrickRed + 3) {
					static constexpr uint8_t colours[] = {1, 4, 2, 3}; // red, yellow, blue, green
					b.colour = colours[key - BrickRed];
				}
				else if (key == BrickRespawn) {
					b.respawnMs = r.integer();
				}
				else if (key < 0) {
					return false;
				}
				r.skipValues(); // two unused fields
			}
			if (section == ColourBricks) {
				colourBricks.push_back(b);
			}
			else {
				whiteBricks.push_back(b.position);
			}
			return true;
		});
	}
	return !r.failed();
}

bool TargetFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	return forEachEntry(bytes, TargetList, [&](TokenReader& r) {
		Point& p = points.emplace_back();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case TargetPosition: p.position = r.reals<3>(); break;
			case TargetDisabledBy: p.disabledBy = r.integer(); break;
			default: return false;
			}
		}
		return true;
	});
}

bool TimerFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	r.table(TimerList, [&] {
		Timer& t = timers.emplace_back();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case TimerOn:
				t.randomOn = r.keyword() == TimerRandom; // (a plain length isn't a keyword: nothing is taken)
				t.onMs = r.integer();
				break;
			case TimerOff:
				t.randomOff = r.keyword() == TimerRandom;
				t.offMs = r.integer();
				break;
			case TimerEvent: t.event = r.integer(); break;
			case TimerDelay: t.delayMs = r.integer(); break;
			default: return false;
			}
		}
		return true;
	});
	return !r.failed();
}

namespace {

// A multi-launcher's counted list of points, each (x, y, z) and an integer.
void readHazardPoints(TokenReader& r, std::vector<std::array<float, 4>>& into)
{
	r.counted([&] {
		const std::array<float, 3> p = r.reals<3>();
		into.push_back({p[0], p[1], p[2], float(r.integer())});
	});
}

// A body of keyword fields (the launcher's, the triggered animation's, the sweep cannon's, the rain's, the grabber's).
void readHazardFields(TokenReader& r, HazardFile::Hazard& h)
{
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		switch (r.keyword()) {
		case FieldSource: h.source = r.reals<3>(); break;
		case FieldTarget: h.target = r.reals<3>(); break;
		case FieldTriggerPoint: h.triggerPoint = r.reals<3>(); break;
		case FieldTriggerRadius: h.triggerRadius = r.real(); break;
		case FieldTrigger: h.trigger = r.integer(); break;
		case FieldSurface: h.surface = r.text(); break;
		case FieldEntity:
			h.entity = r.text();
			h.entities.push_back(h.entity);
			if (h.type == HazardGrabber) {
				h.strength = r.real();
				h.low = r.real();
				h.high = r.real();
			}
			break;
		case FieldInterval: h.intervalMs = r.integer(); break;
		case FieldMaterial: h.material = r.text(); break;
		case FieldPeriod: h.periodMs = r.integer(); break;
		case FieldSweep: h.sweep = r.reals<3>(); break;
		default: r.fail();
		}
	}
}

} // namespace

bool HazardFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	if (r.keyword() != HazardList) {
		return false;
	}
	r.counted([&] {
		Hazard& h = hazards.emplace_back();
		h.type = r.keyword();
		switch (h.type) {
		case -1: return false;
		case HazardLauncher:
		case HazardOpener:
		case HazardSweep:
		case HazardRain:
		case HazardGrabber: readHazardFields(r, h); break;
		case HazardOscillator:
			h.entity = r.text();
			h.scroll = r.reals<2>();
			break;
		case HazardLaunchers:
			r.expect(Token::Open);
			while (!r.failed()) {
				const int key = r.keyword();
				if (key == FieldSource) {
					readHazardPoints(r, h.launches);
				}
				else if (key == FieldTarget) {
					readHazardPoints(r, h.targets);
				}
				else if (key == HazardLauncher) {
					break;
				}
				else if (key < 0 && !r.next(Token::Text) && !r.next(Token::Real) && !r.next(Token::Integer)) {
					r.fail(); // (other keywords and values before the launcher block are skipped)
				}
			}
			readHazardFields(r, h);
			r.close();
			break;
		case HazardRock:
			r.expect(Token::Open);
			h.entity = r.text();
			h.trigger = r.integer();
			h.startFrame = r.real();
			h.size = r.reals<3>();
			r.close();
			break;
		default: break; // the types that read nothing more
		}
		return true;
	});
	return !r.failed() && r.atEnd();
}

namespace {

// .GHB: K(0x2c) { fields }
enum : int32_t {
	GhostRun = 0x2c,
	GhostSamples = 0x27, // [n] { 7 i ... }
	GhostPosition = 0x28, // r r r
	GhostRotation = 0x29, // r r r r
	GhostLaps = 0x2a, // i i i
	GhostUnused = 0x2b, // i (0 in every file)
};
// .LSB: K(0x27) { fields }
enum : int32_t {
	LoadingScreenBlock = 0x27,
	LoadingPicture = 0x28,
	LoadingDots = 0x29, // [n] { r r ... }
	LoadingName = 0x2a,
};

} // namespace

bool GhostFile::decode(std::span<const uint8_t> bytes, bool mirrored)
{
	*this = {};
	TokenReader r(bytes);
	if (r.keyword() != GhostRun) {
		return false;
	}
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		switch (r.keyword()) {
		case GhostLaps:
			for (int32_t& lap : laps) {
				lap = r.integer();
			}
			break;
		case GhostPosition: position = r.reals<3>(); break;
		case GhostRotation: rotation = r.reals<4>(); break;
		case GhostUnused: r.integer(); break;
		case GhostSamples: {
			// Seven integers a sample.
			r.openCounted();
			while (!r.failed() && !r.next(Token::Close)) {
				std::array<int16_t, 3>& p = positions.emplace_back();
				std::array<int8_t, 4>& q = rotations.emplace_back();
				for (int16_t& v : p) {
					v = int16_t(r.integer());
				}
				for (int8_t& v : q) {
					v = int8_t(uint8_t(r.integer()));
				}
			}
			break;
		}
		default: return false;
		}
	}
	if (mirrored) {
		position[1] = -position[1];
		rotation[1] = -rotation[1];
		rotation[3] = -rotation[3];
		for (size_t i = 0; i < positions.size(); i++) {
			positions[i][1] = int16_t(-positions[i][1]);
			rotations[i][1] = int8_t(-rotations[i][1]);
			rotations[i][3] = int8_t(-rotations[i][3]);
		}
	}
	return !r.failed() && r.atEnd();
}

bool LoadingScreen::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	if (r.keyword() != LoadingScreenBlock) {
		return false;
	}
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		switch (r.keyword()) {
		case LoadingPicture: picture = r.text(); break;
		case LoadingName: name = r.integer(); break;
		case LoadingDots: {
			r.openCounted();
			while (!r.failed() && !r.next(Token::Close)) {
				dots.push_back(r.reals<2>());
			}
			break;
		}
		default: return false;
		}
	}
	return !r.failed() && r.atEnd();
}

namespace {

// .EMB
enum : int32_t {
	EmitterList = 0x27, // [n] { K(0x27) "name" { properties } ... }
	EmitterRate = 0x28,
	EmitterChance = 0x29,
	EmitterAcceleration = 0x2a,
	EmitterLaunches = 0x2b, // [k] { r r r ... }
	EmitterWidth = 0x2c,
	EmitterHeight = 0x2d,
	EmitterTrack = 0x2e,
	EmitterLife = 0x2f,
	EmitterDuration = 0x30,
	EmitterGrowWidth = 0x31,
	EmitterGrowHeight = 0x32,
	EmitterRadius = 0x33,
	EmitterMaterial = 0x34,
	EmitterPriority = 0x35,
};

// .SKB
enum : int32_t {
	SkyStates = 0x2c, // [n] { K(0x27) [k] "name" { K(0x27) { keyframe } ... } ... }
	SkyState = 0x27,
	SkyDuration = 0x28,
	SkyHorizon = 0x29, // r g b (the three rings: 0x29 the horizon's, 0x2a the middle's, 0x2b the high one's)
	SkyInitial = 0x2d,
	SkyHeight = 0x2e,
};

} // namespace

int32_t EmitterFile::find(std::string_view name) const
{
	return indexWhere(emitters, [&](const Emitter& e) { return std::string_view(e.name).substr(0, 8) == name.substr(0, 8); });
}

bool EmitterFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	r.table(EmitterList, [&] {
		Emitter& e = emitters.emplace_back();
		e.name = r.text();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case EmitterRate: e.intervalMs = r.truncated(1000.0f / r.number()); break;
			case EmitterChance: e.chance = uint8_t(r.truncated(r.number() * 256.0f) & 255); break;
			case EmitterAcceleration: e.acceleration = {r.number(), r.number(), r.number()}; break;
			case EmitterLaunches: r.counted([&] { e.launches.push_back({r.number(), r.number(), r.number()}); }); break;
			case EmitterWidth: e.width = r.number(); break;
			case EmitterHeight: e.height = r.number(); break;
			case EmitterTrack: e.track = r.truncated(r.number()); break;
			case EmitterLife: e.lifeMs = r.truncated(r.number()); break;
			case EmitterDuration: e.durationMs = r.truncated(r.number()); break;
			case EmitterGrowWidth: e.growWidth = r.number(); break;
			case EmitterGrowHeight: e.growHeight = r.number(); break;
			case EmitterRadius: e.radius = r.number(); break;
			case EmitterMaterial: e.material = r.text(); break;
			case EmitterPriority: e.priority = uint8_t(r.truncated(r.number()) & 255); break;
			default: return false; // (an unknown key is an error)
			}
		}
		return true;
	});
	return !r.failed();
}

int32_t SkyFile::find(std::string_view name) const
{
	return indexWhere(states, [&](const State& s) { return s.name == name; });
}

bool SkyFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	const auto colour = [&] {
		std::array<uint8_t, 3> c;
		for (uint8_t& x : c) {
			x = uint8_t(std::clamp(r.truncated(r.number()), 0, 255));
		}
		return c;
	};
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case SkyStates:
			r.entries(SkyState, [&] {
				State& s = states.emplace_back();
				// "[k] "name" { K(0x27) { keyframe } × k }"
				r.expect(Token::OpenCount);
				r.integer();
				r.expect(Token::CloseCount);
				s.name = r.text();
				r.expect(Token::Open);
				while (!r.failed() && !r.next(Token::Close)) {
					if (r.keyword() != SkyState) {
						return false;
					}
					Keyframe& k = s.keyframes.emplace_back();
					r.expect(Token::Open);
					while (!r.failed() && !r.next(Token::Close)) {
						const int key = r.keyword();
						if (key == SkyDuration) {
							k.durationMs = r.truncated(r.number());
						}
						else if (key >= SkyHorizon && key <= SkyHorizon + 2) {
							k.colours[size_t(key - SkyHorizon)] = colour();
						}
						else {
							return false;
						}
					}
				}
				return true;
			});
			break;
		case SkyInitial: initial = r.text(); break;
		case SkyHeight: height = r.number(); break;
		default: return false;
		}
	}
	return !r.failed();
}

} // namespace lr
