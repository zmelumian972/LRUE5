#include "LRData/Model.h"
#include "LRData/Archive.h"
#include "LRData/Index.h"

#include "LRData/Tokens.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace lr {

namespace {

// Top-level keywords.
enum : int32_t {
	KeyMaterials = 0x27,
	KeyPlainVertices = 0x28, // position, uv
	KeyLitVertices = 0x29, // position, uv, normal
	KeyColoredVertices = 0x2a, // position, uv, rgba
	KeyTriangles = 0x2d, // three cache slots each
	KeyRecords = 0x2e, // what to draw, in order
	KeyScale = 0x33,
	KeyPositions = 0x34, // position only
};
// The draw records' keywords.
enum : int32_t {
	RecordMaterial = 0x27, // material index
	RecordDraw = 0x2d, // first triangle, count
	RecordPushPart = 0x2f, // part index
	RecordPopPart = 0x30,
	RecordLoad = 0x31, // cache slot, first vertex, count
	RecordSetPart = 0x32, // part index
};
// A head library's (.GCB, Model.h).
enum : int32_t {
	HeadPool = 0x28, // [n] { x y z u v nx ny nz ... }
	HeadEntries = 0x2c, // [m] { K(0x2c) "name" { fields } ... }
	HeadScale = 0x2d,
	HeadGroups = 0x2b, // [g] { K(0x2b) { K(0x27) "material" K(0x2a) [t] { indices } } ... }
	GroupMaterial = 0x27,
	GroupTriangles = 0x2a,
};
constexpr uint32_t CacheSize = 64;

bool readVertices(TokenReader& r, Model& m, Model::Vertices kind)
{
	const uint32_t count = r.openCounted();
	m.kind = kind;
	m.positions.reserve(count * 3);
	if (kind != Model::Vertices::PositionsOnly) {
		m.uvs.reserve(count * 2);
		m.colors.assign(size_t(count) * 4, 255);
	}
	if (kind == Model::Vertices::Lit) {
		m.normals.reserve(count * 3);
	}
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		for (int j = 0; j < 3; j++) {
			m.positions.push_back(r.real());
		}
		if (kind == Model::Vertices::PositionsOnly) {
			continue;
		}
		m.uvs.push_back(r.real());
		m.uvs.push_back(r.real());
		if (kind == Model::Vertices::Colored) {
			for (int j = 0; j < 4; j++) {
				m.colors[i * 4 + j] = uint8_t(r.integer());
			}
		}
		else if (kind == Model::Vertices::Lit) {
			for (int j = 0; j < 3; j++) {
				m.normals.push_back(r.real());
			}
		}
	}
	r.close();
	return !r.failed();
}

// Plays the draw records through the vertex cache into sections.
bool readRecords(TokenReader& r, Model& m, const std::vector<uint8_t>& slots)
{
	const uint32_t count = r.openCounted();
	uint32_t cache[CacheSize];
	bool loaded[CacheSize] = {};
	uint16_t material = 0, part = 0;
	std::vector<uint16_t> parts; // the pushed parts
	Model::Section* section = nullptr;
	const uint32_t vertexTotal = uint32_t(m.positions.size() / 3);
	m.vertexParts.assign(vertexTotal, 0);
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		switch (r.keyword()) {
		case RecordMaterial:
			material = uint16_t(r.integer());
			section = nullptr;
			break;
		case RecordSetPart:
			part = uint16_t(r.integer());
			section = nullptr;
			break;
		case RecordPushPart:
			parts.push_back(part);
			part = uint16_t(r.integer());
			section = nullptr;
			break;
		case RecordPopPart:
			if (parts.empty()) {
				return false;
			}
			part = parts.back();
			parts.pop_back();
			section = nullptr;
			break;
		case RecordLoad: {
			const uint32_t slot = uint32_t(r.integer()), first = uint32_t(r.integer()), n = uint32_t(r.integer());
			if (uint64_t(slot) + n > CacheSize || uint64_t(first) + n > vertexTotal) {
				return false;
			}
			for (uint32_t j = 0; j < n; j++) {
				cache[slot + j] = first + j;
				loaded[slot + j] = true;
				m.vertexParts[first + j] = part;
			}
			break;
		}
		case RecordDraw: {
			const uint32_t first = uint32_t(r.integer()), n = uint32_t(r.integer());
			if ((uint64_t(first) + n) * 3 > slots.size()) {
				return false;
			}
			if (!section) {
				section = &m.sections.emplace_back(Model::Section{material, part, {}});
			}
			for (uint32_t j = first * 3; j < (first + n) * 3; j++) {
				const uint8_t slot = slots[j];
				if (slot >= CacheSize || !loaded[slot]) {
					return false;
				}
				section->indices.push_back(cache[slot]);
			}
			m.faceCount += n;
			break;
		}
		default: return false;
		}
	}
	r.close();
	return !r.failed();
}

} // namespace

bool Model::decode(std::span<const uint8_t> bytes)
{
	TokenReader r(bytes);
	if (r.failed()) {
		return false;
	}
	*this = Model();
	std::vector<uint8_t> slots; // the triangles: three cache slots each
	// The records can come before the triangles they draw, so they're read last.
	size_t records = SIZE_MAX;
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case KeyMaterials: r.counted([&] { materials.emplace_back(r.text()); }); break;
		case KeyPlainVertices: readVertices(r, *this, Vertices::Plain); break;
		case KeyColoredVertices: readVertices(r, *this, Vertices::Colored); break;
		case KeyLitVertices: readVertices(r, *this, Vertices::Lit); break;
		case KeyPositions: readVertices(r, *this, Vertices::PositionsOnly); break;
		case KeyTriangles: {
			const uint32_t count = r.openCounted();
			slots.reserve(count * 3);
			for (uint32_t i = 0; i < count * 3 && !r.failed(); i++) {
				slots.push_back(uint8_t(r.integer()));
			}
			r.close();
			break;
		}
		case KeyRecords:
			records = r.position();
			r.skipCounted();
			break;
		case KeyScale: scale = r.real(); break;
		default: return false;
		}
	}
	if (r.failed()) {
		return false;
	}
	if (records == SIZE_MAX) {
		return true;
	}
	r.seek(records);
	return readRecords(r, *this, slots);
}

namespace {
// A head's pool vertex: position, uv and normal, shared by every head that names it.
struct HeadVertex {
	float p[3], uv[2], n[3];
};

// The pool of vertices the heads index.
void readHeadPool(TokenReader& r, List<HeadVertex>& pool)
{
	const uint32_t n = r.openCounted();
	pool.resize(n);
	for (HeadVertex& v : pool) {
		// (Signed 16-bit positions and uvs)
		const auto signed16 = [](float x) { return x > 32767 ? x - 65536 : x; };
		for (float& x : v.p) {
			x = signed16(r.number());
		}
		for (float& x : v.uv) {
			x = signed16(r.number()) / 4096.0f;
		}
		for (float& x : v.n) {
			const int32_t b = r.truncated(r.number());
			x = float(b > 127 ? b - 256 : b) / 127.0f;
		}
	}
	r.close();
}

// A head's triangles: each pool vertex it uses is copied into the model once (remap: pool index to the model's).
void readHeadTriangles(TokenReader& r, const List<HeadVertex>& pool, List<int32_t>& remap, Model& model, Model::Section& section)
{
	const uint32_t t = r.openCounted();
	for (uint32_t c = 0; c < t * 3 && !r.failed(); c++) {
		const int32_t v = r.integer();
		if (!inRange(pool, v)) {
			r.fail();
			break;
		}
		if (remap[v] < 0) {
			remap[v] = int32_t(model.positions.size() / 3);
			const HeadVertex& pv = pool[v];
			model.positions.insert(model.positions.end(), pv.p, pv.p + 3);
			model.uvs.insert(model.uvs.end(), pv.uv, pv.uv + 2);
			model.normals.insert(model.normals.end(), pv.n, pv.n + 3);
			model.colors.insert(model.colors.end(), {255, 255, 255, 255});
			model.vertexParts.push_back(0);
		}
		section.indices.push_back(uint32_t(remap[v]));
	}
	r.close();
}

// A head's counted groups, each a section: its material and its triangles.
void readHeadGroups(TokenReader& r, const List<HeadVertex>& pool, List<int32_t>& remap, Model& model)
{
	r.entries(HeadGroups, [&] {
		r.expect(Token::Open);
		Model::Section section{0, 0, {}};
		while (!r.failed() && !r.next(Token::Close)) {
			const int gk = r.keyword();
			if (gk == GroupMaterial) {
				const std::string material(r.text());
				auto it = std::find(model.materials.begin(), model.materials.end(), material);
				section.material = uint16_t(it - model.materials.begin());
				if (it == model.materials.end()) {
					model.materials.push_back(material);
				}
			}
			else if (gk == GroupTriangles) {
				readHeadTriangles(r, pool, remap, model, section);
			}
			else {
				r.fail();
			}
		}
		model.faceCount += uint32_t(section.indices.size() / 3);
		model.sections.push_back(std::move(section));
	});
}

} // namespace

const HeadLibrary::Head* HeadLibrary::find(std::string_view name) const
{
	return findWhere(heads, [&](const Head& h) { return equalsIgnoreCase(h.name, name); });
}

bool HeadLibrary::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	TokenReader r(bytes);
	List<HeadVertex> pool;
	while (!r.atEnd() && !r.failed()) {
		const int key = r.keyword();
		if (key == HeadPool) {
			readHeadPool(r, pool);
		}
		else if (key == HeadEntries) {
			r.entries(HeadEntries, [&] {
				Head& h = heads.emplace_back();
				h.name = std::string(r.text());
				r.expect(Token::Open);
				Model& model = h.model;
				model.kind = Model::Vertices::Lit;
				List<int32_t> remap(pool.size(), -1);
				while (!r.failed() && !r.next(Token::Close)) {
					const int k = r.keyword();
					if (k == HeadScale) {
						model.scale = r.number();
					}
					else if (k == HeadGroups) {
						readHeadGroups(r, pool, remap, model);
					}
					else {
						r.fail();
					}
				}
			});
		}
		else if (key < 0) {
			r.fail();
		}
		else {
			// (Other keys are skipped.)
			r.skipValues();
			if (!r.atEnd() && r.tokens().list[r.position()].kind == Token::OpenCount) {
				r.skipCounted();
			}
		}
	}
	return !r.failed() && !heads.empty();
}

} // namespace lr
