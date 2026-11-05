// A built car: the grid rules (resting, collisions, the height limit), adding and removing pieces, loading a stored
// car, the numbers the race takes, and the mesh.

#include "LRCore/Kart/Car.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <unordered_map>

namespace lr {

size_t CarMesh::materialCount() const
{
	size_t n = 0;
	for (size_t i = 0; i < triangles.size(); i++) {
		n += i == 0 || triangles[i].material != triangles[i - 1].material;
	}
	return n;
}

void Car::clear()
{
	entries.clear();
	grid = {};
}

int32_t Car::rest(const PieceLibrary& lib, const PieceLibrary::Piece& p, int32_t X, int32_t Y, int32_t rotation, bool floor) const
{
	const int32_t w = p.extentX(rotation), h = p.extentY(rotation);
	if (X < 0 || Y < 0) {
		return PastLeft;
	}
	if (X + w > Width) {
		return PastRight;
	}
	if (Y + h > Depth) {
		return PastFar;
	}
	// The highest a resting cell meets a studded top (a hole rests on nothing); a chassis may stand on the floor.
	int32_t r = NothingBelow;
	for (int32_t x = 0; x < w; x++) {
		for (int32_t y = 0; y < h; y++) {
			const PieceLibrary::Cell& c = lib.cells[p.cellAt(x, y, rotation)];
			const Cell& g = cell(X + x, Y + y);
			if (g.studded && c.rests) {
				r = std::max(r, g.height - c.base());
			}
			else if (floor && g.height == 0) {
				r = std::max(r, -int32_t(c.base()));
			}
		}
	}
	return r;
}

bool Car::collides(const PieceLibrary& lib, const PieceLibrary::Piece& p, int32_t X, int32_t Y, int32_t rotation, int32_t height) const
{
	if (height < 0) {
		return true;
	}
	for (int32_t x = 0; x < p.extentX(rotation); x++) {
		for (int32_t y = 0; y < p.extentY(rotation); y++) {
			const PieceLibrary::Cell& c = lib.cells[p.cellAt(x, y, rotation)];
			const Cell& g = cell(X + x, Y + y);
			const int32_t above = g.height - height;
			if (c.solid() && (above > c.base() || (above == c.base() && g.studded && !c.rests && !c.flush))) {
				return true;
			}
		}
	}
	return false;
}

int32_t Car::test(const PieceLibrary& lib, const PieceLibrary::Piece& p, int32_t x, int32_t y, int32_t rotation) const
{
	const int32_t r = rest(lib, p, x, y, rotation, p.chassis());
	if (r < 0) {
		return r;
	}
	if (collides(lib, p, x, y, rotation, r)) {
		return Collides;
	}
	if (r + p.tallest > HighestTop) {
		return TooHigh;
	}
	return int32_t(entries.size()) >= MostPieces ? TooMany : r;
}

void Car::stamp(const PieceLibrary& lib, size_t index, int32_t height, std::vector<Stud>* studs)
{
	const Entry& e = entries[index];
	const PieceLibrary::Piece& p = *lib.find(e.piece.type);
	for (int32_t x = 0; x < p.extentX(e.piece.rotation); x++) {
		for (int32_t y = 0; y < p.extentY(e.piece.rotation); y++) {
			const PieceLibrary::Cell& c = lib.cells[p.cellAt(x, y, e.piece.rotation)];
			if (!c.solid()) {
				continue;
			}
			Cell& g = grid[size_t((e.piece.x + x) * Depth + e.piece.y + y)];
			// A top stood on with a gap, or by a cell that leaves its studs showing.
			if (studs && g.studded && (g.height != height + c.base() || c.flush)) {
				studs->push_back({e.piece.x + x, e.piece.y + y, g.height, g.owner});
			}
			g.studded = c.studded;
			g.height = int8_t(height + c.top);
			g.owner = int8_t(index);
		}
	}
}

bool Car::add(const PieceLibrary& lib, const CarPiece& piece, bool floor)
{
	const PieceLibrary::Piece* p = lib.find(piece.type);
	if (!p) {
		return false;
	}
	const int32_t r = rest(lib, *p, piece.x, piece.y, piece.rotation, floor);
	if (r < 0 || collides(lib, *p, piece.x, piece.y, piece.rotation, r)) {
		return false;
	}
	entries.push_back({piece, r});
	stamp(lib, entries.size() - 1, r, nullptr);
	return true;
}

bool Car::place(const PieceLibrary& lib, CarPiece piece, Game& game)
{
	const PieceLibrary::Piece* p = lib.find(piece.type);
	const bool chassis = p && p->chassis();
	if (chassis) {
		piece.colour = uint8_t(game.draw() % 10);
	}
	return add(lib, piece, chassis);
}

bool Car::rebuildGrid(const PieceLibrary& lib, std::vector<Stud>* studs)
{
	grid = {};
	bool ok = true;
	for (size_t i = 0; i < entries.size(); i++) {
		Entry& e = entries[i];
		const PieceLibrary::Piece& p = *lib.find(e.piece.type);
		int32_t r = rest(lib, p, e.piece.x, e.piece.y, e.piece.rotation, false);
		if (r == NothingBelow) {
			r = rest(lib, p, e.piece.x, e.piece.y, e.piece.rotation, true); // QUIRK: any piece, not only a chassis
		}
		if (r < 0) {
			ok = false;
			continue;
		}
		if (e.height < 0) {
			e.height = r;
		}
		else if (e.height != r) {
			ok = false;
		}
		if (collides(lib, p, e.piece.x, e.piece.y, e.piece.rotation, r)) {
			ok = false;
			continue;
		}
		stamp(lib, i, r, studs);
	}
	return ok;
}

bool Car::rebuild(const PieceLibrary& lib) { return rebuildGrid(lib, nullptr); }

bool Car::remove(const PieceLibrary& lib, size_t index)
{
	// Only a piece nothing stands on.
	const Entry e = entries[index];
	const PieceLibrary::Piece& p = *lib.find(e.piece.type);
	for (int32_t x = 0; x < p.extentX(e.piece.rotation); x++) {
		for (int32_t y = 0; y < p.extentY(e.piece.rotation); y++) {
			if (lib.cells[p.cellAt(x, y, e.piece.rotation)].solid() && cell(e.piece.x + x, e.piece.y + y).owner != int8_t(index)) {
				return false;
			}
		}
	}
	entries.erase(entries.begin() + ptrdiff_t(index));
	if (rebuild(lib)) {
		return true;
	}
	entries.insert(entries.begin() + ptrdiff_t(index), e);
	rebuild(lib);
	return false;
}

void Car::load(const PieceLibrary& lib, std::span<const CarPiece> pieces)
{
	clear();
	if (pieces.size() > size_t(MostPieces)) {
		return;
	}
	for (const CarPiece& piece : pieces) {
		const PieceLibrary::Piece* p = lib.find(piece.type);
		if (!p) {
			continue;
		}
		const int32_t r = rest(lib, *p, piece.x, piece.y, piece.rotation, false);
		if (r >= 0 && r + p->tallest > HighestTop) {
			continue;
		}
		if (!add(lib, piece, false)) {
			add(lib, piece, true);
		}
	}
}

std::vector<CarPiece> Car::pieces() const
{
	std::vector<CarPiece> out;
	for (const Entry& e : entries) {
		out.push_back(e.piece);
	}
	return out;
}

void Car::chooseOffset(const PieceLibrary& lib)
{
	offset = {-5, -3, 0};
	const PieceLibrary::Piece* p = entries.empty() ? nullptr : lib.find(entries[0].piece.type);
	if (p && p->chassis() && p->anchor >= 0 && size_t(p->anchor) < lib.positions.size()) {
		const auto& a = lib.positions[size_t(p->anchor)];
		offset = {-float(a[0]) / 256.0f, -float(a[1]) / 256.0f, -float(a[2]) / 256.0f * 0.4f};
	}
}

Car::Numbers Car::numbers(const PieceLibrary& lib) const
{
	int32_t w = 0, mx = 0, my = 0;
	for (const Entry& e : entries) {
		const PieceLibrary::Piece* p = lib.find(e.piece.type);
		if (!p || p->chassis()) {
			continue;
		}
		for (int32_t x = 0; x < p->extentX(e.piece.rotation); x++) {
			for (int32_t y = 0; y < p->extentY(e.piece.rotation); y++) {
				const PieceLibrary::Cell& c = lib.cells[p->cellAt(x, y, e.piece.rotation)];
				const int32_t dz = c.top - c.base();
				w += dz;
				mx += dz * (e.piece.x + x);
				my += dz * (e.piece.y + y);
			}
		}
	}
	Numbers n;
	if (w) {
		n.volume = w;
		n.centroidX = float(mx) / float(w) - 4.5f;
		n.centroidY = float(my) / float(w) - 2.5f;
	}
	return n;
}

// The mesh.

namespace {

struct VertexHash {
	size_t operator()(const CarMesh::Vertex& v) const
	{
		uint32_t bits[6];
		std::memcpy(bits, v.position.data(), 12);
		std::memcpy(bits + 3, v.uv.data(), 8);
		bits[5] = uint32_t(uint8_t(v.normal[0])) | uint32_t(uint8_t(v.normal[1])) << 8 | uint32_t(uint8_t(v.normal[2])) << 16;
		size_t h = 0;
		for (uint32_t b : bits) {
			h = h * 0x9e3779b97f4a7c15ull + b;
		}
		return h;
	}
};
struct VertexBits {
	bool operator()(const CarMesh::Vertex& a, const CarMesh::Vertex& b) const
	{
		return std::memcmp(a.position.data(), b.position.data(), 12) == 0 && std::memcmp(a.uv.data(), b.uv.data(), 8) == 0 && a.normal == b.normal;
	}
};

// A triangle or quad (vertices 0..3: 0 and 3 opposite).
struct Primitive {
	std::array<uint32_t, 4> v = {};
	uint8_t corners = 3;
	int32_t material = 0;
	uint16_t flags = 0;
	int32_t part = 0;
	bool integral = true, parallelogram = false, dead = false;
	std::array<bool, 3> flat = {}; // every vertex shares that coordinate
};

struct MeshBuild {
	const PieceLibrary& lib;
	const std::array<int32_t, 10>& colourMaterials;
	Car::MeshOptions options;
	CarMesh mesh;
	std::unordered_map<CarMesh::Vertex, uint32_t, VertexHash, VertexBits> welded;
	std::vector<Primitive> primitives;
	int32_t lastNormal = -1; // QUIRK: carried from primitive to primitive and piece to piece

	void count(int32_t d)
	{
		mesh.primitives += d;
		if (mesh.primitives >= Car::PrimitiveBudget) {
			mesh.overflowed = true;
		}
	}
	uint32_t vertex(const CarMesh::Vertex& v)
	{
		const auto [it, added] = welded.try_emplace(v, uint32_t(mesh.vertices.size()));
		if (added) {
			mesh.vertices.push_back(v);
		}
		return it->second;
	}
	int32_t colourMaterial(int32_t colour) const { return colour >= 0 && colour < 10 ? colourMaterials[size_t(colour)] : -1; }

	void piece(const PieceLibrary::Piece& p, int32_t X, int32_t Y, int32_t Z, int32_t rotation, int32_t colour, int32_t part);
	void finishFlags(Primitive& q, const std::array<std::array<int16_t, 3>, 4>& library) const;
	void clip(int axis);
	uint32_t split(uint32_t a, uint32_t b, int axis, float at);
};

void MeshBuild::finishFlags(Primitive& q, const std::array<std::array<int16_t, 3>, 4>& library) const
{
	q.integral = true;
	for (uint8_t k = 0; k < q.corners; k++) {
		for (int16_t c : library[k]) {
			q.integral = q.integral && (c & 255) == 0;
		}
	}
	for (int a = 0; a < 3; a++) {
		q.flat[size_t(a)] = true;
		for (uint8_t k = 1; k < q.corners; k++) {
			q.flat[size_t(a)] = q.flat[size_t(a)] && mesh.vertices[q.v[k]].position[size_t(a)] == mesh.vertices[q.v[0]].position[size_t(a)];
		}
	}
	q.parallelogram = false;
	if (q.corners == 4) {
		q.parallelogram = true;
		for (size_t a = 0; a < 3; a++) {
			const auto pos = [&](size_t k) { return mesh.vertices[q.v[k]].position[a]; };
			q.parallelogram = q.parallelogram && pos(1) + pos(2) - pos(0) - pos(3) == 0.0f;
		}
	}
}

void MeshBuild::piece(const PieceLibrary::Piece& p, int32_t X, int32_t Y, int32_t Z, int32_t rotation, int32_t colour, int32_t part)
{
	const float W = float(p.width), H = float(p.height);
	const int32_t mat = colourMaterial(colour);
	const auto place = [&](uint32_t position, int32_t normal, int32_t uv, bool textured, uint16_t flags) {
		const auto& lp = lib.positions[position];
		const float px = float(lp[0]) / 256.0f, py = float(lp[1]) / 256.0f, pz = float(lp[2]) / 256.0f;
		const std::array<int8_t, 3> n0 = normal >= 0 ? lib.normals[size_t(normal)] : std::array<int8_t, 3>{127, 0, 0};
		CarMesh::Vertex v;
		switch (rotation & 3) {
		case 0: v.position = {px + float(X), py + float(Y), pz + float(Z)}, v.normal = n0; break;
		case 1: v.position = {py + float(X), (W - px) + float(Y), pz + float(Z)}, v.normal = {n0[1], int8_t(-n0[0]), n0[2]}; break;
		case 2: v.position = {(W - px) + float(X), (H - py) + float(Y), pz + float(Z)}, v.normal = {int8_t(-n0[0]), int8_t(-n0[1]), n0[2]}; break;
		default: v.position = {(H - py) + float(X), px + float(Y), pz + float(Z)}, v.normal = {int8_t(-n0[1]), n0[0], n0[2]}; break;
		}
		if (textured) {
			v.uv = {float(lib.uvs[size_t(uv)][0]) / 1024.0f, float(lib.uvs[size_t(uv)][1]) / 1024.0f};
		}
		else if (options.planarStuds && (flags == 2 || (flags & 0x800))) {
			v.uv = {(v.position[0] - float(X)) * 0.25f, (v.position[1] - float(Y)) * 0.25f};
		}
		else {
			v.uv = {0, 0};
		}
		return vertex(v);
	};
	const size_t start = primitives.size();
	size_t at = p.first;
	std::array<std::array<int16_t, 3>, 4> library = {};
	for (uint32_t k = 0; k < p.count && at < lib.commands.size(); k++) {
		const uint16_t c = lib.commands[at++];
		const auto next = [&]() -> int32_t { return at < lib.commands.size() ? lib.commands[at++] : 0; };
		if ((c & 0x3000) == 0x2000) {
			// A fourth corner for the triangle before.
			if (primitives.empty() || primitives.back().corners != 3) {
				continue;
			}
			Primitive& q = primitives.back();
			const bool own = q.flags & 0x4000, textured = (q.flags & 0x3000) == 0x1000;
			const int32_t position = next();
			if (own) {
				lastNormal = next();
			}
			const int32_t uv = textured ? next() : 0;
			q.v[3] = place(uint32_t(position), lastNormal, uv, textured, uint16_t(q.flags & 0xfff));
			library[3] = lib.positions[size_t(position)];
			q.corners = 4;
			finishFlags(q, library);
			continue;
		}
		Primitive q;
		q.flags = c; // the command's bits until the primitive is finished
		q.part = part;
		const bool textured = (c & 0x3000) == 0x1000, own = c & 0x4000, reuse = c & 0x8000;
		for (size_t i = 0; i < 3; i++) {
			const int32_t position = next();
			if (!reuse && (own || i == 0)) {
				lastNormal = next();
			}
			const int32_t uv = textured ? next() : 0;
			q.v[i] = place(uint32_t(position), lastNormal, uv, textured, uint16_t(c & 0xfff));
			library[i] = lib.positions[size_t(position)];
		}
		const int32_t m = c & 0x7ff;
		const bool textureBit = c & 0x800;
		q.material = m >= 3 ? m + (options.planarStuds && textureBit ? 1 : 0) : m == 2 && options.planarStuds ? mat + 1 : mat;
		finishFlags(q, library);
		primitives.push_back(q);
		count(1);
	}
	// The command bits were kept for the quad words; keep the flags only.
	for (size_t i = start; i < primitives.size(); i++) {
		primitives[i].flags &= 0xfff;
	}
}

// A vertex on the edge a-b where the axis' coordinate is `at`.
uint32_t MeshBuild::split(uint32_t a, uint32_t b, int axis, float at)
{
	const CarMesh::Vertex va = mesh.vertices[a], vb = mesh.vertices[b];
	const float t = (at - va.position[size_t(axis)]) / (vb.position[size_t(axis)] - va.position[size_t(axis)]);
	CarMesh::Vertex v;
	for (size_t i = 0; i < 3; i++) {
		v.position[i] = va.position[i] == vb.position[i] ? va.position[i] : va.position[i] + (vb.position[i] - va.position[i]) * t;
		v.normal[i] = int8_t(float(va.normal[i]) + float(vb.normal[i] - va.normal[i]) * t);
	}
	v.position[size_t(axis)] = at;
	for (size_t i = 0; i < 2; i++) {
		v.uv[i] = va.uv[i] + (vb.uv[i] - va.uv[i]) * t;
	}
	return vertex(v);
}

// Faces of different parts lying flat on each other in one plane of an axis are cut to their common rectangle, and
// it is dropped from both.
void MeshBuild::clip(int axis)
{
	std::vector<uint32_t> candidates;
	for (uint32_t i = 0; i < primitives.size(); i++) {
		const Primitive& q = primitives[i];
		if (!q.dead && q.corners == 4 && q.parallelogram && q.integral && q.flat[size_t(axis)]) {
			candidates.push_back(i);
		}
	}
	const auto range = [&](uint32_t q, int a) {
		std::array<float, 2> r = {1e30f, -1e30f};
		for (uint32_t v : primitives[q].v) {
			r[0] = std::min(r[0], mesh.vertices[v].position[size_t(a)]);
			r[1] = std::max(r[1], mesh.vertices[v].position[size_t(a)]);
		}
		return r;
	};
	// The in-plane axes, in cutting order.
	const std::array<int, 2> plane = axis == 2 ? std::array<int, 2>{1, 0} : axis == 1 ? std::array<int, 2>{2, 0} : std::array<int, 2>{2, 1};
	const auto overlaps = [&](uint32_t a, uint32_t b, int ax) {
		const auto ra = range(a, ax), rb = range(b, ax);
		return std::max(ra[0], rb[0]) < std::min(ra[1], rb[1]);
	};
	// Cuts quad q at `at` along ax: the part on the `low` side (or the high side) becomes a new quad; q keeps the rest.
	const auto cut = [&](uint32_t q, int ax, float at, bool lowOut) {
		// The corners in order round the quad: 0 1 3 2.
		static const size_t Ring[4] = {0, 1, 3, 2};
		const Primitive old = primitives[q];
		std::array<uint32_t, 4> ring, inside, outside;
		std::array<bool, 4> out;
		for (size_t k = 0; k < 4; k++) {
			ring[k] = old.v[Ring[k]];
			const float c = mesh.vertices[ring[k]].position[size_t(ax)];
			out[k] = lowOut ? c < at : c > at;
		}
		for (size_t k = 0; k < 4; k++) {
			const size_t prev = (k + 3) & 3, next = (k + 1) & 3;
			const size_t other = out[prev] != out[k] ? prev : next; // the neighbour across the cut
			const uint32_t s = out[other] != out[k] ? split(ring[k], ring[other], ax, at) : ring[k];
			outside[k] = out[k] ? ring[k] : s;
			inside[k] = out[k] ? s : ring[k];
		}
		Primitive piece = old;
		for (size_t k = 0; k < 4; k++) {
			primitives[q].v[Ring[k]] = inside[k];
			piece.v[Ring[k]] = outside[k];
		}
		candidates.push_back(uint32_t(primitives.size()));
		primitives.push_back(piece);
		count(1);
	};
	for (size_t i = 0; i < candidates.size();) {
		bool matched = false;
		for (size_t j = i + 1; j < candidates.size() && !matched; j++) {
			const uint32_t a = candidates[i], b = candidates[j];
			const float la = range(a, axis)[0], lb = range(b, axis)[0];
			const bool coplanar = primitives[a].part != primitives[b].part && (axis == 2 ? la - lb < 0.0049999999f && lb - la < 0.0049999999f : la == lb);
			if (!coplanar || !overlaps(a, b, plane[0]) || !overlaps(a, b, plane[1])) {
				continue;
			}
			for (int ax : plane) {
				const auto ra = range(a, ax), rb = range(b, ax);
				if (ra[0] != rb[0]) {
					ra[0] < rb[0] ? cut(a, ax, rb[0], true) : cut(b, ax, ra[0], true);
				}
				const auto sa = range(a, ax), sb = range(b, ax);
				if (sa[1] != sb[1]) {
					sa[1] > sb[1] ? cut(a, ax, sb[1], false) : cut(b, ax, sa[1], false);
				}
			}
			primitives[a].dead = primitives[b].dead = true;
			count(-2);
			candidates.erase(candidates.begin() + ptrdiff_t(j));
			candidates.erase(candidates.begin() + ptrdiff_t(i));
			matched = true;
		}
		if (!matched) {
			i++;
		}
	}
}

} // namespace

CarMesh Car::mesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials, MeshOptions options, size_t pieceCount) const
{
	MeshBuild b{lib, colourMaterials, options, {}, {}, {}};
	pieceCount = std::min(pieceCount, entries.size());
	for (size_t i = 0; i < pieceCount; i++) {
		const Entry& e = entries[i];
		if (const PieceLibrary::Piece* p = lib.find(e.piece.type)) {
			b.piece(*p, e.piece.x, e.piece.y, e.height, e.piece.rotation, e.piece.colour, int32_t(i));
		}
	}
	const PieceLibrary::Piece* stud = lib.find(PieceLibrary::StudType);
	if (options.realStuds && stud) {
		// The studs on top, then the ones that show under pieces (the grid rebuilt from the pieces drawn).
		Car built = *this;
		built.entries.resize(pieceCount);
		std::vector<Stud> studs;
		for (int32_t x = 0; x < Width; x++) {
			for (int32_t y = 0; y < Depth; y++) {
				const Cell& g = built.cell(x, y);
				if (g.studded) {
					studs.push_back({x, y, g.height, g.owner});
				}
			}
		}
		built.rebuildGrid(lib, &studs);
		for (const Stud& s : studs) {
			// In the colour of the piece under it; under a chassis cell with a raised bottom, the colour whose material
			// is that number (QUIRK).
			int32_t colour = 0;
			if (s.owner >= 0 && size_t(s.owner) < built.entries.size()) {
				const CarPiece& o = built.entries[size_t(s.owner)].piece;
				colour = o.colour;
				const PieceLibrary::Piece* op = lib.find(o.type);
				if (op && op->chassis()) {
					const PieceLibrary::Cell& c = lib.cells[op->cellAt(s.x - o.x, s.y - o.y, o.rotation)];
					const int32_t raised = c.bottom > c.top ? c.bottom - c.top : 0;
					if (raised) {
						const auto it = std::find(colourMaterials.begin(), colourMaterials.end(), raised);
						colour = it == colourMaterials.end() ? 0 : int32_t(it - colourMaterials.begin());
					}
				}
			}
			b.piece(*stud, s.x, s.y, s.height, 0, colour, 0);
		}
	}
	if (options.clip) {
		for (int axis : {2, 1, 0}) {
			b.clip(axis);
		}
	}
	for (Primitive& q : b.primitives) {
		if (!q.dead && options.cull && q.flags == 1) {
			q.dead = true;
			b.count(-1);
		}
	}
	// Quads become two triangles; by material.
	for (const Primitive& q : b.primitives) {
		if (q.dead) {
			continue;
		}
		b.mesh.triangles.push_back({{q.v[0], q.v[1], q.v[2]}, q.material});
		if (q.corners == 4) {
			b.mesh.triangles.push_back({{q.v[2], q.v[1], q.v[3]}, q.material});
			b.count(1);
		}
	}
	std::stable_sort(b.mesh.triangles.begin(), b.mesh.triangles.end(), [](const CarMesh::Triangle& x, const CarMesh::Triangle& y) { return x.material < y.material; });
	for (CarMesh::Vertex& v : b.mesh.vertices) {
		v.position = {v.position[0] + offset[0], v.position[1] + offset[1], v.position[2] * 0.4f + offset[2]};
	}
	return std::move(b.mesh);
}

CarMesh Car::itemMesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials, int32_t type, int32_t colour)
{
	Car c;
	c.offset = {0, 0, 0};
	const CarPiece piece = {uint16_t(type), 0, 0, 0, uint8_t(colour), 0};
	// The centre of the piece's own positions (its studs left out).
	MeshBuild b{lib, colourMaterials, {true, false, false, false}, {}, {}, {}};
	const PieceLibrary::Piece* p = lib.find(piece.type);
	if (!p) {
		return {};
	}
	b.piece(*p, 0, 0, 0, 0, piece.colour, 0);
	Vec3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
	for (const CarMesh::Vertex& v : b.mesh.vertices) {
		const Vec3 q = vec(v.position);
		lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
		hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
	}
	const Vec3 centre = b.mesh.vertices.empty() ? Vec3{} : (lo + hi) * 0.5f;
	c.offset = {-centre.x, -centre.y, -centre.z * 0.4f};
	return c.heldMesh(lib, colourMaterials, piece, 0);
}

CarMesh Car::heldMesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials, const CarPiece& piece, int32_t height) const
{
	// Its own primitives and a stud on each of its studded cells at its height plus that cell's top, nothing clipped or
	// culled.
	MeshBuild b{lib, colourMaterials, {true, false, false, false}, {}, {}, {}};
	const PieceLibrary::Piece* p = lib.find(piece.type);
	const PieceLibrary::Piece* stud = lib.find(PieceLibrary::StudType);
	if (!p) {
		return {};
	}
	b.piece(*p, piece.x, piece.y, height, piece.rotation, piece.colour, 0);
	for (int32_t x = 0; stud && x < p->extentX(piece.rotation); x++) {
		for (int32_t y = 0; y < p->extentY(piece.rotation); y++) {
			const PieceLibrary::Cell& c = lib.cells[p->cellAt(x, y, piece.rotation)];
			if (c.studded) {
				b.piece(*stud, piece.x + x, piece.y + y, height + c.top, 0, piece.colour, 0);
			}
		}
	}
	for (const Primitive& q : b.primitives) {
		if (q.dead) {
			continue;
		}
		b.mesh.triangles.push_back({{q.v[0], q.v[1], q.v[2]}, q.material});
		if (q.corners == 4) {
			b.mesh.triangles.push_back({{q.v[2], q.v[1], q.v[3]}, q.material});
		}
	}
	std::stable_sort(b.mesh.triangles.begin(), b.mesh.triangles.end(), [](const CarMesh::Triangle& x, const CarMesh::Triangle& y) { return x.material < y.material; });
	for (CarMesh::Vertex& v : b.mesh.vertices) {
		v.position = {v.position[0] + offset[0], v.position[1] + offset[1], v.position[2] * 0.4f + offset[2]};
	}
	return std::move(b.mesh);
}

Sphere meshBounds(const CarMesh& m)
{
	Vec3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
	for (const CarMesh::Vertex& v : m.vertices) {
		const Vec3 p = vec(v.position);
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	}
	if (m.vertices.empty()) {
		return {};
	}
	const Vec3 c = (hi + lo) * 0.5f;
	float r = 0;
	for (const CarMesh::Vertex& v : m.vertices) {
		r = std::max(r, lengthZYX(vec(v.position) - c));
	}
	return {c, r};
}

CarMesh Car::menuMesh(const PieceLibrary& lib, const std::array<int32_t, 10>& colourMaterials)
{
	// Each overflow gives up more: the real studs (printed ones instead), then those, then the last pieces.
	for (status = 0;; status++) {
		const MeshOptions o = {status == 0, status == 1, true, true};
		const size_t drawn = status <= 2 ? entries.size() : entries.size() - std::min(entries.size(), size_t(status - 2));
		CarMesh m = mesh(lib, colourMaterials, o, drawn);
		if (!m.overflowed || drawn == 0) {
			return m;
		}
	}
}

} // namespace lr
