#include "LRData/Collision.h"

#include "LRData/Tokens.h"

namespace lr {

namespace {

// .WDB
enum : int {
	DbModels = 0x28, // [n] { "name" ... }: the display models (not collision)
	DbMaterialTables = 0x2b, // [n] { "name" ... }: material-table overrides (no track has them)
	DbVolumes = 0x40, // [n] { "bvb name" ... }
	DbEntities = 0x41, // [n] { K(0x41) ["name"] { fields } ... }
	EntityVolume = 0x40, // i
	EntityPlace = 0x31, // r r r
	EntityFacing = 0x32, // r r r (forward) r r r (up)
	EntityMaterialTable = 0x2b, // i
};

// .BVB
enum : int {
	VolumeMaterials = 0x27, // [n] { "name" ... }
	VolumeVertices = 0x34, // [n] { r r r ... }
	VolumeTriangles = 0x2d, // [n] { i v0, i v1, i v2, i material ... }
	VolumeRealNodes = 0x8b, // [n] { child0 child1 r nx r ny r nz r d i first i count ... }; a child is i or K(0x8c/0x8d)
	VolumeFixedNodes = 0x8e, // [n] { i child0 i child1 i nx i ny i nz i first i count ... }; normal * 2^30
	ChildNone = 0x8c,
	ChildLeaf = 0x8d,
};

// .TMB
enum : int {
	TableSurfaces = 0x27, // [n] { K(0x27) "name" { properties } ... }
	PropFirst = 0x28, // properties 0x28..0x39 set flag bit (key - 0x27)
	PropTouchEvent = 0x2a,
	PropProjectileEvent = 0x2b,
	PropUnusedVector = 0x2c,
	PropPush = 0x2d,
	PropParticle = 0x31,
	PropSound = 0x2e,
	PropSlopeHold = 0x32,
	PropFriction = 0x33,
	PropGrip = 0x34,
	PropUnusedNumber = 0x35,
	PropRolling = 0x36,
	PropNonSolid = 0x37,
	PropLast = 0x39,
};

void names(TokenReader& r, std::vector<std::string>* into)
{
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		const std::string_view name = r.text();
		if (into) {
			into->emplace_back(name);
		}
	}
	r.close();
}

// A node child: -1 or -2, or 0xfffe and up as 16 bits, or the keywords, mean none.
int32_t child(TokenReader& r)
{
	const int key = r.keyword();
	if (key == ChildNone || key == ChildLeaf) {
		return -1;
	}
	if (key >= 0) {
		r.fail();
		return -1;
	}
	const int32_t index = r.integer();
	return index < 0 || index >= 0xfffe ? -1 : index;
}

} // namespace

bool CollisionDatabase::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case DbModels: names(r, nullptr); break;
		case DbVolumes: names(r, &volumes); break;
		case DbEntities: {
			const uint32_t count = r.openCounted();
			for (uint32_t i = 0; i < count && !r.failed(); i++) {
				if (r.keyword() != DbEntities) {
					return false;
				}
				Entity& e = entities.emplace_back();
				if (!r.next(Token::Open)) {
					e.name = r.text();
					r.expect(Token::Open);
				}
				while (!r.failed() && !r.next(Token::Close)) {
					switch (r.keyword()) {
					case EntityVolume: e.volume = uint32_t(r.integer()); break;
					case EntityPlace: e.position = r.reals<3>(); break;
					case EntityFacing:
						e.forward = r.reals<3>();
						e.up = r.reals<3>();
						break;
					default: return false; // including material-table overrides
					}
				}
			}
			r.close();
			break;
		}
		default: return false;
		}
	}
	if (r.failed()) {
		return false;
	}
	for (const Entity& e : entities) {
		if (e.volume >= volumes.size()) {
			return false;
		}
	}
	return true;
}

bool BoundingVolume::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		const int key = r.keyword();
		if (key == VolumeMaterials) {
			names(r, &materials);
			continue;
		}
		const uint32_t count = r.openCounted();
		switch (key) {
		case VolumeVertices:
			for (uint32_t i = 0; i < count && !r.failed(); i++) {
				vertices.push_back(r.reals<3>());
			}
			break;
		case VolumeTriangles:
			for (uint32_t i = 0; i < count && !r.failed(); i++) {
				Triangle& t = triangles.emplace_back();
				for (uint16_t& v : t.v) {
					v = uint16_t(r.integer());
				}
				t.material = uint16_t(r.integer());
			}
			break;
		case VolumeRealNodes:
		case VolumeFixedNodes:
			for (uint32_t i = 0; i < count && !r.failed(); i++) {
				Node& n = nodes.emplace_back();
				n.child[0] = child(r);
				n.child[1] = child(r);
				if (key == VolumeRealNodes) {
					n.normal = r.reals<3>();
					r.real(); // the plane offset: the game takes the plane through the first triangle instead
				}
				else {
					for (float& x : n.normal) {
						x = float(r.integer()) * (1.0f / 1073741824.0f);
					}
				}
				n.first = uint32_t(r.integer());
				n.count = uint32_t(r.integer());
			}
			break;
		default: return false;
		}
		r.close();
	}
	if (r.failed()) {
		return false;
	}
	for (const Triangle& t : triangles) {
		if (t.v[0] >= vertices.size() || t.v[1] >= vertices.size() || t.v[2] >= vertices.size() || t.material >= materials.size()) {
			return false;
		}
	}
	for (const Node& n : nodes) {
		// Every node needs a triangle: its plane passes through the first one's vertex 0.
		if (n.count == 0 || uint64_t(n.first) + n.count > triangles.size() || n.child[0] >= int32_t(nodes.size()) || n.child[1] >= int32_t(nodes.size())) {
			return false;
		}
	}
	return true;
}

bool SurfaceFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != TableSurfaces) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != TableSurfaces) {
			return false;
		}
		Surface& s = surfaces.emplace_back();
		s.name = r.text();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			const int key = r.keyword();
			if (key < PropFirst || key > PropLast) {
				return false;
			}
			s.flags |= 1u << (key - TableSurfaces);
			switch (key) {
			case PropTouchEvent: s.touchEvent = r.integer(); break;
			case PropProjectileEvent: s.projectileEvent = r.integer(); break;
			case PropUnusedVector: r.reals<3>(); break;
			case PropPush: s.push = r.reals<3>(); break;
			case PropParticle: s.particle = r.text(); break;
			case PropSound: s.sound = r.integer(); break;
			case PropSlopeHold: s.slopeHold = r.real(); break;
			case PropFriction: s.friction = r.real(); break;
			case PropGrip: s.grip = r.real(); break;
			case PropUnusedNumber: r.real(); break;
			case PropRolling: s.rolling = r.real(); break;
			default:
				if (key < PropNonSolid) {
					r.integer(); // events and sounds
				}
			}
		}
	}
	r.close();
	return !r.failed() && r.atEnd();
}

} // namespace lr
