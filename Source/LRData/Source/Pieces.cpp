#include "LRData/Pieces.h"

#include "LRData/Archive.h"
#include "LRData/Tokens.h"

#include <algorithm>
#include <cctype>

namespace lr {

namespace {

// The piece library's material list (.MDB: K(0x27) [n] { K(0x27) "name" { ... } ... }).
constexpr int MaterialEntry = 0x27;
// The piece library (.LEB).
enum : int {
	PieceList = 0x27, // [n] { "name" type shape count offset ... }
	PieceCommands = 0x28, // [n] { i ... }: the index stream
	PiecePositions = 0x29, // [n] { x y z ... }
	PieceNormals = 0x2a, // [n] { x y z ... }
	PieceUvs = 0x2b, // [n] { u v ... }
	PieceShapes = 0x2c, // [n] { first second ... }: shape bytes in pairs
};
// The car sets: L_COLORS.LEB's colours, CRSTMGR.LEB's set files and each set file's fields.
enum : int {
	SetColours = 0x2d, // [10] { "name" ... }
	SetFiles = 0x2f, // [12] { "file" ... }
	SetPart = 0x2e, // i
	SetChoices = 0x30, // [n] { "piece name" "colour name" ... }
	SetChassis = 0x31, // "name"
};
// The driver part catalog (BODYPART.PCB).
enum : int {
	CatalogWorldFiles = 0x27,
	CatalogModelFolders = 0x28,
	CatalogHats = 0x29,
	CatalogFaces = 0x2a,
	CatalogTorsos = 0x2b,
	CatalogLegs = 0x2c,
	CatalogHatModels = 0x2d,
	CatalogBodies = 0x2e,
	CatalogRingNames = 0x2f,
};

// "[n] { ... }" of integers into a list, `per` at a time.
template <typename T, size_t N> void readTuples(TokenReader& r, std::vector<std::array<T, N>>& out)
{
	const uint32_t n = r.openCounted();
	out.resize(n);
	for (uint32_t i = 0; i < n && !r.failed(); i++) {
		for (T& v : out[i]) {
			v = T(r.integer());
		}
	}
	r.close();
}

// A piece as the piece file lists it: its name, type, shape and commands (count at offset).
struct ListedPiece {
	std::string name;
	int32_t type, shape, count, offset;
};

// A listed piece's shape: (width, height), its cells column by column, then a chassis' anchor (lo, hi). False if the
// listing points outside the shapes or the commands.
bool shapePiece(PieceLibrary& lib, const ListedPiece& l, const std::vector<std::array<int32_t, 2>>& shapes)
{
	PieceLibrary::Piece& p = lib.pieces.emplace_back();
	p.name = l.name;
	p.type = l.type;
	p.first = uint32_t(l.offset);
	p.count = uint32_t(l.count);
	if (l.shape < 0 || size_t(l.shape) >= shapes.size() || l.offset < 0 || size_t(l.offset) > lib.commands.size()) {
		return false;
	}
	p.width = uint8_t(shapes[size_t(l.shape)][0]);
	p.height = uint8_t(shapes[size_t(l.shape)][1]);
	const size_t n = size_t(p.width) * p.height;
	if (size_t(l.shape) + 1 + n + (p.chassis() ? 1 : 0) > shapes.size()) {
		return false;
	}
	p.cells = uint32_t(lib.cells.size());
	for (size_t c = 0; c < n; c++) {
		const auto [a, b] = shapes[size_t(l.shape) + 1 + c];
		PieceLibrary::Cell& cell = lib.cells.emplace_back();
		cell.top = uint8_t(a & 63);
		cell.studded = a & 128;
		cell.bottom = uint8_t(b & 63);
		cell.rests = b & 128;
		cell.flush = b & 64;
		p.tallest = std::max(p.tallest, cell.top);
	}
	if (p.chassis()) {
		const auto [lo, hi] = shapes[size_t(l.shape) + 1 + n];
		p.anchor = lo + 256 * hi;
	}
	return true;
}

} // namespace

uint32_t PieceLibrary::Piece::cellAt(int32_t x, int32_t y, int32_t rotation) const
{
	const int32_t W = width, H = height;
	switch (rotation & 3) {
	case 0: return cells + uint32_t(x * H + y);
	case 1: return cells + uint32_t(x + H * (W - y - 1));
	case 2: return cells + uint32_t((W - x - 1) * H + (H - y - 1));
	default: return cells + uint32_t(y * H + (H - x - 1));
	}
}

const PieceLibrary::Piece* PieceLibrary::find(int32_t type) const
{
	const auto it = std::lower_bound(pieces.begin(), pieces.end(), type, [](const Piece& p, int32_t t) { return p.type < t; });
	return it != pieces.end() && it->type == type ? &*it : nullptr;
}

const PieceLibrary::Piece* PieceLibrary::find(std::string_view name) const
{
	for (const Piece& p : pieces) {
		if (equalsIgnoreCase(p.name, name.substr(0, 8))) {
			return &p;
		}
	}
	return nullptr;
}

int32_t PieceLibrary::material(std::string_view name) const
{
	const auto it = std::find(materials.begin(), materials.end(), name);
	return it == materials.end() ? -1 : int32_t(it - materials.begin());
}

bool PieceLibrary::decode(std::span<const uint8_t> pieceFile, std::span<const uint8_t> materialFile)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(materialFile)) {
		return false;
	}
	// K27 [n] { K27 "name" { ... } ... }
	TokenReader mr(tokens);
	if (mr.keyword() != MaterialEntry) {
		return false;
	}
	const uint32_t materialCount = mr.openCounted();
	for (uint32_t i = 0; i < materialCount && !mr.failed(); i++) {
		if (mr.keyword() != MaterialEntry) {
			return false;
		}
		materials.emplace_back(mr.text().substr(0, 8));
		mr.skipBlock();
	}
	if (mr.failed() || !tokens.decode(pieceFile)) {
		return false;
	}
	TokenReader r(tokens);
	std::vector<std::array<int32_t, 2>> shapes;
	std::vector<ListedPiece> listed;
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case PieceShapes: readTuples(r, shapes); break;
		case PieceCommands: {
			const uint32_t n = r.openCounted();
			for (uint32_t i = 0; i < n && !r.failed(); i++) {
				commands.push_back(uint16_t(r.integer()));
			}
			r.close();
			break;
		}
		case PiecePositions: readTuples(r, positions); break;
		case PieceNormals: {
			std::vector<std::array<int32_t, 3>> n;
			readTuples(r, n);
			for (const auto& v : n) {
				normals.push_back({int8_t(v[0]), int8_t(v[1]), int8_t(v[2])});
			}
			break;
		}
		case PieceUvs: readTuples(r, uvs); break;
		case PieceList: {
			const uint32_t n = r.openCounted();
			for (uint32_t i = 0; i < n && !r.failed(); i++) {
				ListedPiece& l = listed.emplace_back();
				l.name = std::string(r.text().substr(0, 8));
				l.type = r.integer(), l.shape = r.integer(), l.count = r.integer(), l.offset = r.integer();
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
	for (const ListedPiece& l : listed) {
		if (!shapePiece(*this, l, shapes)) {
			return false;
		}
	}
	std::stable_sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.type < b.type; });
	return true;
}

int32_t CarSets::setOfPart(int32_t part) const
{
	for (size_t i = 0; i < sets.size(); i++) {
		if (sets[i].part == part) {
			return int32_t(i);
		}
	}
	return -1;
}

bool CarSets::load(const Archive& archive, const PieceLibrary& library)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(archive.read("MENUDATA/PIECEDB/L_COLORS.LEB"))) {
		return false;
	}
	TokenReader c(tokens);
	if (c.keyword() != SetColours || c.openCounted() != colours.size()) {
		return false;
	}
	for (size_t i = 0; i < colours.size(); i++) {
		colours[i] = std::string(c.text().substr(0, 8));
		materials[i] = library.material(colours[i]);
	}
	c.close();
	if (c.failed() || !tokens.decode(archive.read("MENUDATA/PIECEDB/CRSTMGR.LEB"))) {
		return false;
	}
	std::vector<std::string> files;
	TokenReader m(tokens);
	if (m.keyword() != SetFiles) {
		return false;
	}
	const uint32_t count = m.openCounted();
	for (uint32_t i = 0; i < count && !m.failed(); i++) {
		const std::string file = upper(m.text());
		files.push_back("MENUDATA/PIECEDB/" + file.substr(0, file.rfind('.')) + ".LEB");
	}
	m.close();
	if (m.failed()) {
		return false;
	}
	for (const std::string& file : files) {
		Tokens set;
		if (!set.decode(archive.read(file))) {
			return false;
		}
		TokenReader r(set);
		Set& s = sets.emplace_back();
		while (!r.atEnd() && !r.failed()) {
			switch (r.keyword()) {
			case SetPart: s.part = r.integer(); break;
			case SetChassis: s.chassis = std::string(r.text().substr(0, 16)); break;
			case SetChoices: {
				const uint32_t n = r.openCounted();
				for (uint32_t i = 0; i < n && !r.failed(); i++) {
					Choice& choice = s.choices.emplace_back();
					const PieceLibrary::Piece* piece = library.find(r.text());
					choice.type = piece ? piece->type : 0;
					const std::string_view colour = r.text().substr(0, 8);
					const auto it = std::find(colours.begin(), colours.end(), colour);
					choice.colour = it == colours.end() ? -1 : int32_t(it - colours.begin());
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
	}
	return true;
}

bool PartCatalog::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	// A ring's list: its model folders, then per part a name, a variant (torsos, legs) and the unlock value.
	const auto ring = [&](std::vector<Part>& out, int folders, bool variants) {
		const uint32_t n = r.openCounted();
		for (int f = 0; f < folders; f++) {
			r.text();
		}
		for (uint32_t i = 0; i < n && !r.failed(); i++) {
			Part& p = out.emplace_back();
			p.name = std::string(r.text().substr(0, 8));
			p.variant = variants ? uint8_t(r.integer()) : 0;
			p.unlock = uint8_t(r.integer());
		}
		r.close();
	};
	const auto names = [&] {
		std::vector<std::string> out;
		const uint32_t n = r.openCounted();
		for (uint32_t i = 0; i < n && !r.failed(); i++) {
			out.emplace_back(r.text().substr(0, 8));
		}
		r.close();
		return out;
	};
	while (!r.atEnd() && !r.failed()) {
		switch (r.keyword()) {
		case CatalogModelFolders:
		case CatalogWorldFiles:
		case CatalogRingNames: names(); break; // the model folders, world files and ring names
		case CatalogHats: ring(rings[0], 0, false); break;
		case CatalogFaces: ring(rings[1], 1, false); break;
		case CatalogTorsos: ring(rings[2], 2, true); break;
		case CatalogLegs: ring(rings[3], 2, true); break;
		case CatalogHatModels: hatModels = names(); break;
		case CatalogBodies: {
			const std::vector<std::string> b = names();
			std::copy_n(b.begin(), std::min(b.size(), bodies.size()), bodies.begin());
			break;
		}
		default: return false;
		}
	}
	return !r.failed();
}

} // namespace lr
