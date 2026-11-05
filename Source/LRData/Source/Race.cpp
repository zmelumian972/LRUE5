#include "LRData/Race.h"

#include "LRData/Tokens.h"

#include <algorithm>
#include <cctype>

namespace lr {

namespace {

// .RAB
enum : int {
	KeyRace = 0x35,
	KeyCollision = 0x2b,  // "database" "track entity" "another trigger entity"
	KeySurfaces = 0x43,   // "surface table"
	KeyCheckpoints = 0x48, // "checkpoint file" "checkpoint entity"
	KeyStartGrid = 0x41,
	KeyTouch = 0x39,
	KeyEvents = 0x2c,
	KeyIntroCamera = 0x49,
	KeyStartCamera = 0x29,     // r r r: the cameras' place before the race
	KeyStartCameraAxes = 0x28, // r r r look, r r r down
	KeyScene = 0x2e,
	KeySharedScene = 0x3b,
	KeyPowerups = 0x33,   // "bricks" "material animations" "models"
	KeyPositional = 0x37,
	KeyTimers = 0x38,
	KeyHazards = 0x44,
	KeyTargets = 0x4a,
	KeyEmitters = 0x3a,    // "race set" "track set"
	KeyEffectsBank = 0x3c,
	KeyVoiceBank = 0x3d,
	KeyAmbientBank = 0x3f,
	KeyMapBounds = 0x46,   // r r r r: min x, max y, max x, min y
	KeySky = 0x42,
	KeySkyWorld = 0x27,
	KeyGlobalWorld = 0x40,
	KeyLocalWorld = 0x45,
};

// .SPB
enum : int {
	GridPlaces = 0x27,   // [n] { K(0x27) i place { fields } ... }
	PlacePosition = 0x28, // r r r
	PlaceAxes = 0x29,    // r r r (forward) r r r (up)
};

// .RRB
enum : int {
	RoutePoints = 0x27, // [n] { 10 integers ... }
	RouteStartRotation = 0x28,
	RouteStart = 0x29,
	RouteLoop = 0x2a,
	RouteLoopRotation = 0x2b,
	RouteLoopTime = 0x2c,
	RouteLoopPoint = 0x2d,
};

// .CPB
enum : int {
	CheckpointList = 0x27, // [n] { K(0x27) { fields } ... }
	CheckpointPlane = 0x28, // r r r r
	CheckpointNext = 0x29, // i i i i
	CheckpointPosition = 0x2a, // r r r
};

// .TRB
enum : int {
	SphereList = 0x27,  // [n] { K(0x27) { fields } ... }
	SphereCentre = 0x29,
	SphereRadius = 0x2a,
	SphereEvent = 0x2b,
	SphereProjectilesOnly = 0x2c,
	SphereWorld = 0x2d,
	SphereLapGate = 0x2e,
	SpherePlayersOnly = 0x2f,
};

// .EVB: sections K(kind) [n] { K(0x27) i id [K(0x3c)] { fields } ... }
enum : int {
	EventRecord = 0x27,       // also a chain inside the fields: K(0x27) K(slot) i event
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
	NodeWorld = 0x4a,    // a node transform's collision entity
	DistanceHide = 0x46, // a model distance's "hide while active"
};

// .PWB
enum : int {
	ColourBricks = 0x27, // [n] { K(0x27) { fields } ... }
	WhiteBricks = 0x2f,
	BrickPosition = 0x28,
	BrickRed = 0x2a,     // ..0x2d: red, yellow, blue, green
	BrickRespawn = 0x2e,
};

// .TGB, .TIB
enum : int {
	TargetList = 0x27,
	TargetPosition = 0x28,
	TargetDisabledBy = 0x29,
	TimerList = 0x27,
	TimerOn = 0x28,      // K(0x2b) i (random) or i
	TimerOff = 0x29,
	TimerEvent = 0x2a,
	TimerRandom = 0x2b,
	TimerDelay = 0x2d,
};

// .HZB: K(0x27) [n] { K(type) [body] ... }
enum : int {
	HazardList = 0x27,
	HazardLauncher = 0x33,
	HazardOpener = 0x34,     // a triggered animation
	HazardOscillator = 0x36,
	HazardLaunchers = 0x3d,
	HazardRock = 0x3e,
	HazardSweep = 0x40,
	HazardRain = 0x43,
	HazardGrabber = 0x48,
	FieldSource = 0x37,      // also a multi-launcher's launch list
	FieldTarget = 0x38,      // also its target list
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
template <typename Entry>
bool forEachEntry(const Tokens& tokens, int key, Entry entry)
{
	TokenReader r(tokens);
	if (r.keyword() != key) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != key) {
			return false;
		}
		if (!entry(r)) {
			return false;
		}
	}
	r.close();
	return !r.failed() && r.atEnd();
}

} // namespace

bool RaceFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
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
	std::string name(sourceName);
	for (char& c : name) {
		c = char(std::toupper(uint8_t(c)));
	}
	if (name.find('.') != std::string::npos) {
		name.back() = 'B';
	}
	return name;
}

bool StartGrid::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	std::vector<uint8_t> seen;
	const bool ok = tokens.decode(bytes) && forEachEntry(tokens, GridPlaces, [&](TokenReader& r) {
		const int32_t index = r.integer();
		if (index < 0 || index > 255) {
			return false;
		}
		if (size_t(index) >= places.size()) {
			places.resize(size_t(index) + 1);
			seen.resize(size_t(index) + 1);
		}
		Place& p = places[size_t(index)];
		seen[size_t(index)] = 1;
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
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case RouteStart: start = r.reals<3>(); break;
		case RouteStartRotation: startRotation = r.reals<4>(); break;
		case RouteLoop: loop = r.reals<3>(); break;
		case RouteLoopRotation: loopRotation = r.reals<4>(); break;
		case RouteLoopTime: loopTime = r.integer(); break;
		case RouteLoopPoint: loopPoint = r.integer(); break;
		case RoutePoints: {
			const uint32_t count = r.openCounted();
			points.resize(count);
			for (Point& p : points) {
				int32_t v[10];
				for (int32_t& x : v) {
					x = r.integer();
				}
				// Each value is truncated to its field's width.
				p = {int16_t(v[0]), int16_t(v[1]), int8_t(v[2]), {int8_t(v[3]), int8_t(v[4]), int8_t(v[5]), int8_t(v[6])}, int8_t(v[7]),
					int8_t(v[8]), uint8_t(v[9])};
			}
			r.close();
			break;
		}
		case -1: return false;
		default:
			r.skipValues(); // unknown blocks' values
		}
	}
	return !r.failed() && loopPoint >= 0 && size_t(loopPoint) < points.size();
}

bool CheckpointFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	return tokens.decode(bytes) && forEachEntry(tokens, CheckpointList, [&](TokenReader& r) {
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
	Tokens tokens;
	return tokens.decode(bytes) && forEachEntry(tokens, SphereList, [&](TokenReader& r) {
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
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	static const std::pair<int, Kind> sections[] = {{0x2a, Sounds}, {0x28, PartAnimations}, {0x29, MaterialAnimations},
		{0x3d, Particles}, {0x42, SkyStates}, {0x4b, Timers}, {0x52, NodeTransforms}, {0x4d, Colours}, {0x51, LapMarks},
		{0x53, ModelDistances}, {0x55, LookTargets}, {0x59, Forces}, {0x39, Links}};
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		const int section = r.keyword();
		const auto it = std::find_if(std::begin(sections), std::end(sections), [&](const auto& s) { return s.first == section; });
		if (it == std::end(sections)) {
			return false;
		}
		const Kind kind = it->second;
		const uint32_t count = r.openCounted();
		for (uint32_t i = 0; i < count && !r.failed(); i++) {
			if (r.keyword() != EventRecord) {
				return false;
			}
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
					if (slot == ChainStart || slot == ChainActive || slot == ChainEnd) {
						e.chains[slot == ChainStart ? 0 : slot == ChainActive ? 1 : 2] = event;
					}
					continue;
				}
				if (key == EventNoEnd && kind != LapMarks && kind != LookTargets) {
					e.noEnd = true;
				}
				else if (kind == Timers && key == TimerDuration) {
					e.durationMs = r.integer();
				}
				else if (kind == Timers && key == TimerHold) {
					e.holdEvent = r.integer();
				}
				else if (kind == LapMarks && (key == MarkOpen || key == MarkPast)) {
					e.zone = key == MarkOpen ? Open : Past;
				}
				else if (kind == Forces && key == ForceVector) {
					e.force = r.reals<3>();
				}
				else if (kind == Forces && key == ForceChannel) {
					e.channel = r.integer();
				}
				else if (kind == Forces && key == ForceArming) {
					e.armingEvent = r.integer();
				}
				else if (kind == Links && key == LinkForced) {
					e.forced = r.integer();
				}
				else if ((kind == Sounds || kind == Particles) && key == ResourcePosition) {
					e.position = r.reals<3>();
				}
				else if ((kind == Sounds || kind == Particles || kind == PartAnimations) && key == ResourceAtEvent) {
					e.atEvent = true;
				}
				else if (kind == Colours && key == ColourClear) {
					e.clear = true;
				}
				else if ((kind == Sounds || kind == Particles || kind == PartAnimations || kind == Colours || kind == NodeTransforms ||
							 kind == ModelDistances) &&
					key == ResourceEntity) {
					e.entity = r.text();
				}
				else if ((kind == Particles || kind == PartAnimations || kind == ModelDistances) && key == ResourceShared) {
					// (QUIRK: the entity is looked up as it is read, so the shared database only if this came first.)
					e.shared = kind == Particles || e.entity.empty();
				}
				else if ((kind == Sounds || kind == Particles || kind == NodeTransforms) && key == ResourceNode) {
					e.node = r.integer();
				}
				else if (kind == NodeTransforms && key == NodeWorld) {
					e.world = r.text();
				}
				else if (kind == ModelDistances && key == DistanceHide) {
					e.hide = true;
				}
				else if (kind == Particles && key == ParticleEmitter) {
					e.emitter = r.text();
				}
				else if (kind == Particles && key == ParticleAxes) {
					e.axes = r.reals<6>();
				}
				else if (kind == Sounds && key == SoundId) {
					e.sound = r.integer();
				}
				else if ((kind == Sounds || kind == PartAnimations) && key == ResourceLooping) {
					e.looping = true;
				}
				else if (kind == Sounds && key >= SoundVolume && key <= SoundFar) {
					const float value = r.real();
					(key == SoundVolume ? e.volume : key == SoundPitch ? e.pitch : key == SoundNear ? e.nearDistance : e.farDistance) = value;
				}
				else if (kind == Sounds && key == SoundProbability) {
					e.probability = r.real();
				}
				else if (kind == SkyStates && key == SkyName) {
					e.sky = r.text();
				}
				else if (kind == SkyStates && key == SkyMs) {
					e.skyMs = r.integer();
				}
				else if (kind == SkyStates && key >= SkyHideDome && key <= SkyHideDome + 3) {
					e.skyFlags |= uint8_t(1u << (key - SkyHideDome));
				}
				else if (kind == PartAnimations && key >= ChainActive && key <= ChainEnd) {
					// (alone, not after K(0x27): the parts) active 0x34, idle 0x35, start 0x36, end 0x37
					e.parts[size_t(key - ChainActive)] = r.integer();
				}
				r.skipValues(); // the presentation's fields (sounds, models, particles, tints...)
			}
		}
		r.close();
	}
	return !r.failed();
}

bool PowerupFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		const int section = r.keyword();
		if (section != ColourBricks && section != WhiteBricks) {
			return false;
		}
		const uint32_t count = r.openCounted();
		for (uint32_t i = 0; i < count && !r.failed(); i++) {
			if (r.keyword() != section) {
				return false;
			}
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
		}
		r.close();
	}
	return !r.failed();
}

bool TargetFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	return tokens.decode(bytes) && forEachEntry(tokens, TargetList, [&](TokenReader& r) {
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
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != TimerList) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != TimerList) {
			return false;
		}
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
	}
	r.close();
	return !r.failed();
}

bool HazardFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != HazardList) {
		return false;
	}
	auto points = [&](std::vector<std::array<float, 4>>& into) {
		const uint32_t n = r.openCounted();
		for (uint32_t i = 0; i < n && !r.failed(); i++) {
			const std::array<float, 3> p = r.reals<3>();
			into.push_back({p[0], p[1], p[2], float(r.integer())});
		}
		r.close();
	};
	// A body of keyword fields (the launcher's, the triggered animation's, the sweep cannon's, the rain's, the grabber's).
	auto fields = [&](Hazard& h) {
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
	};
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		Hazard& h = hazards.emplace_back();
		h.type = r.keyword();
		switch (h.type) {
		case -1: return false;
		case HazardLauncher:
		case HazardOpener:
		case HazardSweep:
		case HazardRain:
		case HazardGrabber: fields(h); break;
		case HazardOscillator:
			h.entity = r.text();
			h.scroll = r.reals<2>();
			break;
		case HazardLaunchers:
			r.expect(Token::Open);
			while (!r.failed()) {
				const int key = r.keyword();
				if (key == FieldSource) {
					points(h.launches);
				}
				else if (key == FieldTarget) {
					points(h.targets);
				}
				else if (key == HazardLauncher) {
					break;
				}
				else if (key < 0 && !r.next(Token::Text) && !r.next(Token::Real) && !r.next(Token::Integer)) {
					r.fail(); // (other keywords and values before the launcher block are skipped)
				}
			}
			fields(h);
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
	}
	r.close();
	return !r.failed() && r.atEnd();
}

bool GhostFile::decode(std::span<const uint8_t> bytes, bool mirrored)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != 0x2c) {
		return false;
	}
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		switch (r.keyword()) {
		case 0x2a:
			for (int32_t& lap : laps) {
				lap = r.integer();
			}
			break;
		case 0x28: position = r.reals<3>(); break;
		case 0x29: rotation = r.reals<4>(); break;
		case 0x2b: r.integer(); break;
		case 0x27: {
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
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != 0x27) {
		return false;
	}
	r.expect(Token::Open);
	while (!r.failed() && !r.next(Token::Close)) {
		switch (r.keyword()) {
		case 0x28: picture = r.text(); break;
		case 0x2a: name = r.integer(); break;
		case 0x29: {
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
enum : int {
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
enum : int {
	SkyStates = 0x2c,   // [n] { K(0x27) [k] "name" { K(0x27) { keyframe } ... } ... }
	SkyState = 0x27,
	SkyDuration = 0x28,
	SkyHorizon = 0x29,  // r g b (the three rings: 0x29 the horizon's, 0x2a the middle's, 0x2b the high one's)
	SkyInitial = 0x2d,
	SkyHeight = 0x2e,
};

// A number written either way (the files mix integers and reals).
float number(TokenReader& r)
{
	if (r.next(Token::Integer)) {
		return float(r.tokens().list[r.position() - 1].integer);
	}
	return r.real();
}

} // namespace

int32_t EmitterFile::find(std::string_view name) const
{
	name = name.substr(0, std::min<size_t>(name.size(), 8));
	for (size_t i = 0; i < emitters.size(); i++) {
		if (std::string_view(emitters[i].name).substr(0, 8) == name) {
			return int32_t(i);
		}
	}
	return -1;
}

bool EmitterFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != EmitterList) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != EmitterList) {
			return false;
		}
		Emitter& e = emitters.emplace_back();
		e.name = r.text();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case EmitterRate: e.intervalMs = int32_t(1000.0f / number(r)); break;
			case EmitterChance: e.chance = uint8_t(int32_t(number(r) * 256.0f) & 255); break;
			case EmitterAcceleration: e.acceleration = {number(r), number(r), number(r)}; break;
			case EmitterLaunches: {
				const uint32_t n = r.openCounted();
				for (uint32_t k = 0; k < n && !r.failed(); k++) {
					e.launches.push_back({number(r), number(r), number(r)});
				}
				r.close();
				break;
			}
			case EmitterWidth: e.width = number(r); break;
			case EmitterHeight: e.height = number(r); break;
			case EmitterTrack: e.track = int32_t(number(r)); break;
			case EmitterLife: e.lifeMs = int32_t(number(r)); break;
			case EmitterDuration: e.durationMs = int32_t(number(r)); break;
			case EmitterGrowWidth: e.growWidth = number(r); break;
			case EmitterGrowHeight: e.growHeight = number(r); break;
			case EmitterRadius: e.radius = number(r); break;
			case EmitterMaterial: e.material = r.text(); break;
			case EmitterPriority: e.priority = uint8_t(int32_t(number(r)) & 255); break;
			default: return false; // (an unknown key is an error)
			}
		}
	}
	r.close();
	return !r.failed();
}

int32_t SkyFile::find(std::string_view name) const
{
	for (size_t i = 0; i < states.size(); i++) {
		if (states[i].name == name) {
			return int32_t(i);
		}
	}
	return -1;
}

bool SkyFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	const auto colour = [&] {
		std::array<uint8_t, 3> c;
		for (uint8_t& x : c) {
			x = uint8_t(std::clamp(int32_t(number(r)), 0, 255));
		}
		return c;
	};
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case SkyStates: {
			const uint32_t count = r.openCounted();
			for (uint32_t i = 0; i < count && !r.failed(); i++) {
				if (r.keyword() != SkyState) {
					return false;
				}
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
							k.durationMs = int32_t(number(r));
						}
						else if (key >= SkyHorizon && key <= SkyHorizon + 2) {
							k.colours[size_t(key - SkyHorizon)] = colour();
						}
						else {
							return false;
						}
					}
				}
			}
			r.close();
			break;
		}
		case SkyInitial: initial = r.text(); break;
		case SkyHeight: height = number(r); break;
		default: return false;
		}
	}
	return !r.failed();
}

} // namespace lr
