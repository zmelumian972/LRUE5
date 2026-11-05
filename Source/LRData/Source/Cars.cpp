#include "LRData/Cars.h"

#include "LRData/Tokens.h"

#include <algorithm>

namespace lr {

namespace {

// All three tables: K(0x27) [n] { K(0x27) "name" { fields } ... }
constexpr int TableEntry = 0x27;

// .CMB
enum : int {
	ChassisVariant = 0x28,   // { K(0x34..0x38) "name" ... }; 0x29 is the second variant
	ChassisVariant2 = 0x29,
	VariantMaterials = 0x34,
	VariantTextures = 0x35,
	VariantModel = 0x36,
	VariantSkeleton = 0x37,
	VariantAnimation = 0x38,
	ChassisShadowTexture = 0x39,
	ChassisCenterOfMass = 0x2a,
	ChassisDriverMount = 0x2b,
	ChassisMass = 0x2c,
	ChassisUnused2d = 0x2d,  // r r
	ChassisShadowSize = 0x2e, // r r
	ChassisEnginePitch = 0x2f,
	ChassisSkids = 0x30,     // { r r, 4 * (r r r) }
	ChassisWheels = 0x31,    // { 4 * (r r r) }
	ChassisUnused32 = 0x32,
	ChassisUnused33 = 0x33,
	ChassisHandling = 0x3a,
	ChassisTopSpeed = 0x3b,
	ChassisAcceleration = 0x3c,
};

// .CCB
enum : int {
	ChampionMaterials = 0x28,
	ChampionTextures = 0x29,
	ChampionModel = 0x2a,
	ChampionChassis = 0x2b,
	ChampionMass = 0x2c,
	ChampionCenterOfMass = 0x2d,
};

// .DDB
enum : int {
	DriverMaterials = 0x28,
	DriverTextures = 0x29,
	DriverModel = 0x2a,
	DriverChampion = 0x2b,
	DriverStats = 0x2c,  // ..0x31, one each
	DriverVoice = 0x34,
	DriverCharge = 0x3a, // i colour, i target
};

// .ADB
enum : int {
	AnimationFrames = 0x27,   // { K(0x28) positions, K(0x29) rotations, K(0x2a) key times }
	FramePositions = 0x28,    // [n] { (r r r) ... }
	FrameRotations = 0x29,    // [n] { (r r r r) ... }
	FrameKeys = 0x2a,         // [n] { i ... }
	AnimationTracks = 0x2b,   // [n] { 6 i ... }
	PartList = 0x2c,    // [n] { K(0x2c) "name" { fields } ... }
	PartFrames = 0x2d,
	PartLoopFrames = 0x2e,
	PartRate = 0x2f,
	PartVelocity = 0x30,
	PartBounds = 0x31,
	PartTrack = 0x2b,
};

// A real that may be stored as an integer.
float number(TokenReader& r) { return r.next(Token::Integer) ? float(r.tokens().list[r.position() - 1].integer) : r.real(); }

// A table of named entries; field(r, key, entry) reads one field's values, false for a malformed one.
template <typename T, typename Field>
bool decodeTable(std::span<const uint8_t> bytes, std::vector<T>& out, Field field)
{
	out.clear();
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != TableEntry) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != TableEntry) {
			return false;
		}
		T& entry = out.emplace_back();
		entry.name = r.text();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			const int key = r.keyword();
			if (key < 0 || !field(r, key, entry)) {
				return false;
			}
		}
	}
	r.close();
	return !r.failed() && r.atEnd();
}

template <typename T>
const T* findNamed(const std::vector<T>& list, std::string_view name)
{
	for (const T& entry : list) {
		if (entry.name == name) {
			return &entry;
		}
	}
	return nullptr;
}

} // namespace

const ChassisTable::Chassis* ChassisTable::find(std::string_view name) const { return findNamed(chassis, name); }
const ChampionTable::Champion* ChampionTable::find(std::string_view name) const { return findNamed(champions, name); }
const DriverTable::Driver* DriverTable::find(std::string_view name) const { return findNamed(drivers, name); }

bool ChassisTable::decode(std::span<const uint8_t> bytes)
{
	return decodeTable(bytes, chassis, [](TokenReader& r, int key, Chassis& c) {
		switch (key) {
		case ChassisVariant:
		case ChassisVariant2: {
			ChassisTable::Chassis::Variant first;
			ChassisTable::Chassis::Variant& v = key == ChassisVariant ? first : c.second;
			r.expect(Token::Open);
			while (!r.failed() && !r.next(Token::Close)) {
				const int part = r.keyword();
				std::string* into = part == VariantMaterials ? &v.materials
					: part == VariantTextures                 ? &v.textures
					: part == VariantModel                    ? &v.model
					: part == VariantSkeleton                 ? &v.skeleton
					: part == VariantAnimation                ? &v.animation
															  : nullptr;
				if (part < 0) {
					return false;
				}
				if (into) {
					*into = r.text();
				}
				r.skipValues();
			}
			if (key == ChassisVariant) {
				c.model = first.model;
				c.animation = first.animation;
				c.skeleton = first.skeleton;
				c.materials = first.materials;
				c.textures = first.textures;
			}
			return true;
		}
		case ChassisDriverMount: c.driverMount = r.reals<3>(); return true;
		case ChassisSkids:
			r.expect(Token::Open);
			c.skidWidths = r.reals<2>();
			for (std::array<float, 3>& point : c.skidPoints) {
				point = r.reals<3>();
			}
			r.close();
			return true;
		case ChassisWheels:
			r.expect(Token::Open);
			for (std::array<float, 3>& w : c.wheels) {
				w = r.reals<3>();
			}
			r.close();
			return true;
		case ChassisCenterOfMass: c.centerOfMass = r.reals<3>(); return true;
		case ChassisMass: c.mass = r.real(); return true;
		case ChassisShadowSize:
			c.shadowWidth = r.real();
			c.shadowLength = r.real();
			return true;
		// Stats are stored as bytes.
		case ChassisHandling: c.handling = uint8_t(r.integer()); return true;
		case ChassisTopSpeed: c.topSpeed = uint8_t(r.integer()); return true;
		case ChassisAcceleration: c.acceleration = uint8_t(r.integer()); return true;
		case ChassisEnginePitch: c.enginePitch = r.real(); return true;
		case ChassisShadowTexture:
		case ChassisUnused2d:
		case ChassisUnused32:
		case ChassisUnused33: r.skipValues(); return true;
		default: return false;
		}
	});
}

bool ChampionTable::decode(std::span<const uint8_t> bytes)
{
	return decodeTable(bytes, champions, [](TokenReader& r, int key, Champion& c) {
		switch (key) {
		case ChampionChassis: c.chassis = r.text(); return true;
		case ChampionMass: c.mass = r.real(); return true;
		case ChampionCenterOfMass: c.centerOfMass = r.reals<3>(); return true;
		case ChampionModel: c.model = r.text(); return true;
		case ChampionMaterials: c.materials = r.text(); return true;
		case ChampionTextures: c.textures = r.text(); return true;
		default: return false;
		}
	});
}

bool DriverTable::decode(std::span<const uint8_t> bytes)
{
	return decodeTable(bytes, drivers, [](TokenReader& r, int key, Driver& d) {
		if (key == DriverChampion) {
			d.champion = r.text();
		}
		else if (key >= DriverStats && key < DriverStats + 6) {
			d.stats[size_t(key - DriverStats)] = r.integer();
		}
		else if (key == DriverCharge) {
			d.chargeColour = r.integer();
			d.chargeTarget = r.integer();
		}
		else if (key == 0x33) {
			d.nameIndex = r.integer();
		}
		else if (key == DriverVoice) {
			d.voice = r.integer();
		}
		else if (key >= 0x35 && key <= 0x39) {
			d.looks[size_t(key - 0x35)] = r.integer();
		}
		else if (key == DriverMaterials || key == DriverTextures || key == DriverModel) {
			(key == DriverMaterials ? d.materials : key == DriverTextures ? d.textures : d.model) = r.text();
		}
		else {
			r.skipValues(); // the other names
		}
		return true;
	});
}

bool AnimationFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case AnimationFrames:
			r.expect(Token::Open);
			while (!r.failed() && !r.next(Token::Close)) {
				const int key = r.keyword();
				const uint32_t n = r.openCounted();
				for (uint32_t i = 0; i < n && !r.failed(); i++) {
					if (key == FramePositions) {
						positions.push_back({number(r), number(r), number(r)});
					}
					else if (key == FrameRotations) {
						rotations.push_back({number(r), number(r), number(r), number(r)});
					}
					else if (key == FrameKeys) {
						keys.push_back(r.integer());
					}
					else {
						r.fail();
					}
				}
				r.close();
			}
			break;
		case AnimationTracks: {
			const uint32_t n = r.openCounted();
			for (uint32_t i = 0; i < n && !r.failed(); i++) {
				Track& t = tracks.emplace_back();
				for (int32_t* v : {&t.rotationFrame, &t.rotationKey, &t.rotationKeys, &t.positionFrame, &t.positionKey, &t.positionKeys}) {
					*v = r.integer();
				}
			}
			r.close();
			break;
		}
		case PartList: {
			const uint32_t count = r.openCounted();
			for (uint32_t i = 0; i < count && !r.failed(); i++) {
				if (r.keyword() != PartList) {
					return false;
				}
				Part& p = parts.emplace_back();
				p.name = r.text();
				r.expect(Token::Open);
				while (!r.failed() && !r.next(Token::Close)) {
					switch (r.keyword()) {
					case PartFrames: p.frames = r.integer(); break;
					case PartLoopFrames: p.loopFrames = r.integer(); break;
					case PartRate: p.rate = r.integer(); break;
					case PartBounds: p.bounds = r.reals<4>(); break;
					case PartTrack: p.track = r.integer(); break;
					case PartVelocity: r.skipValues(); break;
					default: return false;
					}
				}
			}
			r.close();
			break;
		}
		default: return false;
		}
	}
	for (const Track& t : tracks) {
		if (t.rotationFrame < 0 || t.positionFrame < 0 || t.rotationKey < 0 || t.positionKey < 0 ||
			size_t(t.rotationFrame) + size_t(std::max(0, t.rotationKeys)) > rotations.size() ||
			size_t(t.positionFrame) + size_t(std::max(0, t.positionKeys)) > positions.size() ||
			size_t(t.rotationKey) + size_t(std::max(0, t.rotationKeys)) > keys.size() ||
			size_t(t.positionKey) + size_t(std::max(0, t.positionKeys)) > keys.size()) {
			return false;
		}
	}
	return !r.failed();
}

} // namespace lr
