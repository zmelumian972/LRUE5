#include "LRData/Cache.h"

#include "CacheFields.h"
#include "LRData/Archive.h"

#include <algorithm>
#include <map>

namespace lr {

enum class Cache::Type : uint32_t {
	Directory = 1, // LEGO.JAM's paths
	Sizes, // and its files' sizes
	Languages, // the languages' folders
	Tunes, // the tunes' names
	Executable,
	Animation,
	BoundingVolume,
	CarSets,
	Champions,
	Chassis,
	Checkpoints,
	Cinema,
	Circuits,
	Collision,
	Drivers,
	Events,
	Fonts,
	Ghost,
	GhostMirrored,
	Hazards,
	Image,
	ImageList,
	Loading,
	MaterialAnimation,
	Materials,
	Layout,
	Styles,
	Model,
	NameList,
	Save,
	Parts,
	Pieces,
	Powerups,
	Race,
	Races,
	Route,
	Scene,
	Skeleton,
	Sound,
	Grid,
	Strings,
	Surfaces,
	Targets,
	TextureList,
	Timers,
	Touch,
	Texture,
	Emitters,
	Sky,
	Heads,
};

namespace {

using Type = Cache::Type;

constexpr uint32_t Magic = 0x4b43524c; // "LRCK"
constexpr size_t HeaderSize = 4 + 4 + 4 + 32 + 32 + 4;
constexpr size_t IndexRecord = 5 * 4; // type, path offset, path length, record offset, record size

CacheKind kindOf(Type type)
{
	switch (type) {
	case Type::Executable: return CacheKind::Executable;
	case Type::Model:
	case Type::Heads: return CacheKind::Models;
	case Type::Materials:
	case Type::TextureList: return CacheKind::Materials;
	case Type::Image:
	case Type::Texture: return CacheKind::Textures;
	case Type::Sound: return CacheKind::Sounds;
	default: return CacheKind::Tables;
	}
}

std::string key(Type type, std::string_view path) { return std::to_string(uint32_t(type)) + ":" + upper(path); }

std::string extensionOf(std::string_view path)
{
	const size_t slash = path.rfind('/'), dot = path.rfind('.');
	return dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash) ? std::string() : upper(path.substr(dot + 1));
}

std::string folderOf(std::string_view path) { return std::string(path.substr(0, path.rfind('/') + 1)); }
std::string stemOf(std::string_view path) { return std::string(path.substr(0, path.rfind('.'))); }

// One kind's file as it is built: its index and records.
struct KindFile {
	struct Item {
		Type type;
		std::string path;
		uint32_t offset, size;
	};
	std::vector<Item> items;
	std::vector<uint8_t> records;

	template <class T> void add(Type type, std::string_view path, T& value)
	{
		const size_t start = records.size();
		RecordWriter w{records};
		w(value);
		items.push_back({type, std::string(path), uint32_t(start), uint32_t(records.size() - start)});
	}

	std::vector<uint8_t> finish(CacheKind kind, const Digest& jam, const Digest& exe) const
	{
		std::vector<uint8_t> out;
		RecordWriter w{out};
		const uint32_t magic = Magic, format = CacheFormat, k = uint32_t(kind), count = uint32_t(items.size());
		w.raw(&magic, 4);
		w.raw(&format, 4);
		w.raw(&k, 4);
		w.raw(jam.data(), 32);
		w.raw(exe.data(), 32);
		w.raw(&count, 4);
		size_t pool = 0;
		for (const Item& item : items) {
			pool += item.path.size();
		}
		const uint32_t poolAt = uint32_t(HeaderSize + items.size() * IndexRecord), recordsAt = uint32_t(poolAt + pool);
		uint32_t at = poolAt;
		for (const Item& item : items) {
			const uint32_t fields5[5] = {uint32_t(item.type), at, uint32_t(item.path.size()), recordsAt + item.offset, item.size};
			w.raw(fields5, sizeof(fields5));
			at += uint32_t(item.path.size());
		}
		for (const Item& item : items) {
			w.raw(item.path.data(), item.path.size());
		}
		w.raw(records.data(), records.size());
		return out;
	}
};

// The languages' folders in language order: the executable's names, each a folder under MENUDATA/ with a MENUTEXT.SRF and
// one under GAMEDATA/COMMON/ (Finnish's folders exist but the list doesn't name them). Empty if one isn't there.
std::vector<std::string> languageFolders(const Archive& archive, const ExecutableTables& executable)
{
	std::vector<std::string> folders;
	for (const std::string& name : executable.languages) {
		const std::string folder = upper(name);
		if (!archive.find("MENUDATA/" + folder + "/MENUTEXT.SRF") || !archive.find("GAMEDATA/COMMON/" + folder + "/GAME.SRF")) {
			return {};
		}
		folders.push_back(folder);
	}
	return folders;
}

using KindFiles = std::array<KindFile, size_t(CacheKind::Count)>;

template <class T> void addTo(KindFiles& kinds, Type type, std::string_view path, T& value)
{
	kinds[size_t(kindOf(type))].add(type, path, value);
}

template <class T> void addIf(KindFiles& kinds, Type type, std::string_view path, T& value, bool decoded)
{
	if (decoded) {
		addTo(kinds, type, path, value);
	}
}

// The files that decode on their own, by extension.
template <class T, Type type> void addDecoded(KindFiles& kinds, const std::string& path, std::span<const uint8_t> bytes)
{
	T value;
	addIf(kinds, type, path, value, value.decode(bytes));
}

struct PlainFile {
	const char* ext;
	void (*add)(KindFiles& kinds, const std::string& path, std::span<const uint8_t> bytes);
};
constexpr PlainFile PlainFiles[] = {
	{"GDB", addDecoded<Model, Type::Model>},
	{"GCB", addDecoded<HeadLibrary, Type::Heads>},
	{"ADB", addDecoded<AnimationFile, Type::Animation>},
	{"SDB", addDecoded<SkeletonFile, Type::Skeleton>},
	{"BVB", addDecoded<BoundingVolume, Type::BoundingVolume>},
	{"TMB", addDecoded<SurfaceFile, Type::Surfaces>},
	{"RAB", addDecoded<RaceFile, Type::Race>},
	{"SPB", addDecoded<StartGrid, Type::Grid>},
	{"RRB", addDecoded<RouteFile, Type::Route>},
	{"CPB", addDecoded<CheckpointFile, Type::Checkpoints>},
	{"TRB", addDecoded<TouchFile, Type::Touch>},
	{"EVB", addDecoded<EventFile, Type::Events>},
	{"PWB", addDecoded<PowerupFile, Type::Powerups>},
	{"TGB", addDecoded<TargetFile, Type::Targets>},
	{"TIB", addDecoded<TimerFile, Type::Timers>},
	{"HZB", addDecoded<HazardFile, Type::Hazards>},
	{"LSB", addDecoded<LoadingScreen, Type::Loading>},
	{"MAB", addDecoded<MaterialAnimationFile, Type::MaterialAnimation>},
	{"EMB", addDecoded<EmitterFile, Type::Emitters>},
	{"SKB", addDecoded<SkyFile, Type::Sky>},
	{"CMB", addDecoded<ChassisTable, Type::Chassis>},
	{"CCB", addDecoded<ChampionTable, Type::Champions>},
	{"DDB", addDecoded<DriverTable, Type::Drivers>},
	{"SRF", addDecoded<StringTable, Type::Strings>},
	{"", addDecoded<NameList, Type::NameList>},
	{"SBK", addDecoded<NameList, Type::NameList>},
	{"IDB", addDecoded<ImageList, Type::ImageList>},
	{"FDB", addDecoded<FontList, Type::Fonts>},
	{"MSB", addDecoded<MenuStyles, Type::Styles>},
	{"MIB", addDecoded<MenuLayout, Type::Layout>},
	{"PCB", addDecoded<PartCatalog, Type::Parts>},
	{"MDB", addDecoded<MaterialLibrary, Type::Materials>},
};

// A texture list and each entry's texture: its file in the list's folder, else in GAMEDATA/COMMON/, else (two menu
// cars' tyres: decided from the data) in MENUDATA/PIECEDB/.
void addTextures(KindFiles& kinds, const std::string& p, std::span<const uint8_t> bytes, const std::map<std::string, Image>& images)
{
	TextureList list;
	if (!list.decode(bytes)) {
		return;
	}
	addTo(kinds, Type::TextureList, p, list);
	for (const TextureList::Entry& e : list.textures) {
		const std::string file = upper(e.file);
		auto image = images.find(upper(folderOf(p)) + file);
		for (const char* other : {"GAMEDATA/COMMON/", "MENUDATA/PIECEDB/"}) {
			image = image == images.end() ? images.find(other + file) : image;
		}
		if (image != images.end()) {
			Texture t;
			t.make(image->second, e);
			addTo(kinds, Type::Texture, p + "#" + e.name, t);
		}
	}
}

// A race table decodes against its circuit table and completes it: both as the pair leaves them.
void addCircuitsOrRaces(KindFiles& kinds, const Archive& archive, const std::string& ext, const std::string& p,
	std::span<const uint8_t> bytes)
{
	const Archive::File* races = archive.find(stemOf(p) + ".RCB");
	const Archive::File* circuitFile = archive.find(stemOf(p) + ".CRB");
	CircuitTable circuits;
	const bool ok = circuitFile && circuits.decode(archive.read(*circuitFile));
	if (ext == "RCB") {
		RaceTable r;
		addIf(kinds, Type::Races, p, r, ok && r.decode(bytes, circuits));
	}
	else {
		RaceTable r;
		addIf(kinds, Type::Circuits, p, circuits, ok && (!races || r.decode(archive.read(*races), circuits)));
	}
}

// An archive file's records, if it decodes (a file the cache doesn't keep has none).
void addFile(KindFiles& kinds, const Archive& archive, const Archive::File& f, const std::map<std::string, Image>& images)
{
	const std::string ext = extensionOf(f.path);
	const std::span<const uint8_t> bytes = archive.read(f);
	const std::string& p = f.path;
	for (const PlainFile& plain : PlainFiles) {
		if (ext == plain.ext) {
			plain.add(kinds, p, bytes);
			return;
		}
	}
	if (ext == "WDB") {
		SceneFile s;
		addIf(kinds, Type::Scene, p, s, s.decode(bytes));
		CollisionDatabase c;
		addIf(kinds, Type::Collision, p, c, c.decode(bytes));
	}
	else if (ext == "GHB") {
		GhostFile g, m;
		addIf(kinds, Type::Ghost, p, g, g.decode(bytes, false));
		addIf(kinds, Type::GhostMirrored, p, m, m.decode(bytes, true));
	}
	else if (ext == "CDB") {
		CinemaFile c;
		const Archive::File* effects = archive.find(stemOf(p) + ".CEB");
		addIf(kinds, Type::Cinema, p, c, c.decode(bytes) && (!effects || c.decodeEffects(archive.read(*effects))));
	}
	else if (ext == "CRB" || ext == "RCB") {
		addCircuitsOrRaces(kinds, archive, ext, p, bytes);
	}
	else if (ext == "LEB") {
		PieceLibrary l;
		const Archive::File* materials = archive.find(stemOf(p) + ".MDB");
		addIf(kinds, Type::Pieces, p, l, materials && l.decode(bytes, archive.read(*materials)));
	}
	else if (ext == "LRS") {
		OriginalSave s;
		addIf(kinds, Type::Save, p, s, s.decodeFile(bytes));
	}
	else if (ext == "TDB") {
		addTextures(kinds, p, bytes, images);
	}
	else if (ext == "PCM") {
		Sound s;
		addIf(kinds, Type::Sound, p, s, s.decode(bytes, false));
	}
}

} // namespace

bool buildCache(const CacheSource& source, std::array<std::vector<uint8_t>, size_t(CacheKind::Count)>& files, std::string& error,
	const std::function<void(float)>& progress)
{
	Archive archive;
	if (!archive.open(source.jam)) {
		error = "LEGO.JAM doesn't decode";
		return false;
	}
	ExecutableTables executable;
	if (!executable.locate(source.exe, error)) {
		return false;
	}
	const Digest jam = sha256(source.jam), exe = sha256(source.exe);
	KindFiles kinds;
	const auto add = [&](Type type, std::string_view path, auto& value) { addTo(kinds, type, path, value); };

	std::vector<std::string> paths;
	std::vector<uint32_t> sizes;
	for (const Archive::File& f : archive.files()) {
		paths.push_back(f.path);
		sizes.push_back(f.size);
	}
	add(Type::Directory, "", paths);
	add(Type::Sizes, "", sizes);
	std::vector<std::string> languages = languageFolders(archive, executable);
	if (languages.empty()) {
		error = "LEGO.JAM: the languages' folders aren't as expected";
		return false;
	}
	add(Type::Languages, "", languages);
	add(Type::Executable, "", executable);

	// The images first: the texture lists' entries use them.
	std::map<std::string, Image> images; // by upper path
	const size_t total = archive.files().size() + source.tunes.size();
	size_t done = 0;
	const auto step = [&] {
		if (progress && ++done % 64 == 0) {
			progress(float(done) / float(total * 2));
		}
	};
	for (const Archive::File& f : archive.files()) {
		const std::string ext = extensionOf(f.path);
		Image image;
		if ((ext == "BMP" && image.decodeBmp(archive.read(f))) || (ext == "TGA" && decodeTga(archive.read(f), image))) {
			add(Type::Image, f.path, image);
			images[upper(f.path)] = std::move(image);
		}
		step();
	}
	for (const Archive::File& f : archive.files()) {
		step();
		addFile(kinds, archive, f, images);
	}
	// The car sets, against the piece library.
	PieceLibrary library;
	CarSets sets;
	const std::string setsPath = "MENUDATA/PIECEDB/CRSTMGR.LEB";
	if (library.decode(archive.read("MENUDATA/PIECEDB/LPIECEHI.LEB"), archive.read("MENUDATA/PIECEDB/LPIECEHI.MDB")) &&
		sets.load(archive, library)) {
		add(Type::CarSets, setsPath, sets);
	}
	// The tunes, by their names.
	std::vector<std::string> tunes;
	for (const auto& [name, bytes] : source.tunes) {
		Sound s;
		if (s.decode(bytes, true)) {
			add(Type::Sound, name, s);
			tunes.push_back(name);
		}
		step();
	}
	add(Type::Tunes, "", tunes);
	for (size_t k = 0; k < kinds.size(); k++) {
		files[k] = kinds[k].finish(CacheKind(k), jam, exe);
	}
	if (progress) {
		progress(1);
	}
	return true;
}

bool cacheKey(std::span<const uint8_t> file, CacheKind& kind, Digest& jam, Digest& exe)
{
	if (!fits(file, 0, HeaderSize) || u32(file, 0) != Magic || u32(file, 4) != CacheFormat || u32(file, 8) >= uint32_t(CacheKind::Count)) {
		return false;
	}
	kind = CacheKind(u32(file, 8));
	std::memcpy(jam.data(), file.data() + 12, 32);
	std::memcpy(exe.data(), file.data() + 44, 32);
	return true;
}

bool Cache::open(const std::array<std::span<const uint8_t>, size_t(CacheKind::Count)>& files)
{
	*this = {};
	for (size_t k = 0; k < files.size(); k++) {
		const std::span<const uint8_t> f = files[k];
		CacheKind kind;
		Digest jam, exe;
		if (!cacheKey(f, kind, jam, exe) || kind != CacheKind(k) || (k > 0 && (jam != m_jam || exe != m_exe))) {
			return false;
		}
		m_jam = jam, m_exe = exe;
		const uint32_t count = u32(f, 76);
		if (!fits(f, HeaderSize, uint64_t(count) * IndexRecord)) {
			return false;
		}
		for (uint32_t i = 0; i < count; i++) {
			const size_t at = HeaderSize + size_t(i) * IndexRecord;
			const uint32_t type = u32(f, at), pathAt = u32(f, at + 4), pathSize = u32(f, at + 8), offset = u32(f, at + 12),
						   size = u32(f, at + 16);
			if (!fits(f, pathAt, pathSize) || !fits(f, offset, size) || kindOf(Type(type)) != CacheKind(k)) {
				return false;
			}
			const std::string_view path(reinterpret_cast<const char*>(f.data() + pathAt), pathSize);
			m_index[key(Type(type), path)] = {uint32_t(k), {offset, size}};
		}
	}
	m_files = files;
	std::vector<uint32_t> sizes;
	if (!read(Type::Directory, "", m_paths) || !read(Type::Sizes, "", sizes) || sizes.size() != m_paths.size() ||
		!read(Type::Languages, "", m_languages) || !read(Type::Tunes, "", m_tunes) || !read(Type::Executable, "", m_executable)) {
		return false;
	}
	for (size_t i = 0; i < m_paths.size(); i++) {
		m_sizes.emplace(upper(m_paths[i]), sizes[i]);
	}
	return true;
}

bool Cache::has(std::string_view path) const { return m_sizes.count(upper(path)) != 0; }

uint32_t Cache::size(std::string_view path) const
{
	const auto it = m_sizes.find(upper(path));
	return it == m_sizes.end() ? 0 : it->second;
}

template <class T> bool Cache::read(Type type, std::string_view path, T& out) const
{
	const auto it = m_index.find(key(type, path));
	out = {};
	if (it == m_index.end()) {
		return false;
	}
	RecordReader r{m_files[it->second.first].subspan(it->second.second.offset, it->second.second.size)};
	r(out);
	return !r.failed && r.at == r.in.size();
}

// clang-format off
bool Cache::get(std::string_view path, AnimationFile& out) const { return read(Type::Animation, path, out); }
bool Cache::get(std::string_view path, BoundingVolume& out) const { return read(Type::BoundingVolume, path, out); }
bool Cache::get(std::string_view path, CarSets& out) const { return read(Type::CarSets, path, out); }
bool Cache::get(std::string_view path, ChampionTable& out) const { return read(Type::Champions, path, out); }
bool Cache::get(std::string_view path, ChassisTable& out) const { return read(Type::Chassis, path, out); }
bool Cache::get(std::string_view path, CheckpointFile& out) const { return read(Type::Checkpoints, path, out); }
bool Cache::get(std::string_view path, CinemaFile& out) const { return read(Type::Cinema, path, out); }
bool Cache::get(std::string_view path, CircuitTable& out) const { return read(Type::Circuits, path, out); }
bool Cache::get(std::string_view path, CollisionDatabase& out) const { return read(Type::Collision, path, out); }
bool Cache::get(std::string_view path, DriverTable& out) const { return read(Type::Drivers, path, out); }
bool Cache::get(std::string_view path, EventFile& out) const { return read(Type::Events, path, out); }
bool Cache::get(std::string_view path, FontList& out) const { return read(Type::Fonts, path, out); }
bool Cache::get(std::string_view path, GhostFile& out, bool mirrored) const { return read(mirrored ? Type::GhostMirrored : Type::Ghost, path, out); }
bool Cache::get(std::string_view path, HazardFile& out) const { return read(Type::Hazards, path, out); }
bool Cache::get(std::string_view path, Image& out) const { return read(Type::Image, path, out); }
bool Cache::get(std::string_view path, ImageList& out) const { return read(Type::ImageList, path, out); }
bool Cache::get(std::string_view path, LoadingScreen& out) const { return read(Type::Loading, path, out); }
bool Cache::get(std::string_view path, MaterialAnimationFile& out) const { return read(Type::MaterialAnimation, path, out); }
bool Cache::get(std::string_view path, EmitterFile& out) const { return read(Type::Emitters, path, out); }
bool Cache::get(std::string_view path, SkyFile& out) const { return read(Type::Sky, path, out); }
bool Cache::get(std::string_view path, MaterialLibrary& out) const { return read(Type::Materials, path, out); }
bool Cache::get(std::string_view path, MenuLayout& out) const { return read(Type::Layout, path, out); }
bool Cache::get(std::string_view path, MenuStyles& out) const { return read(Type::Styles, path, out); }
bool Cache::get(std::string_view path, Model& out) const { return read(Type::Model, path, out); }
bool Cache::get(std::string_view path, HeadLibrary& out) const { return read(Type::Heads, path, out); }
bool Cache::get(std::string_view path, NameList& out) const { return read(Type::NameList, path, out); }
bool Cache::get(std::string_view path, OriginalSave& out) const { return read(Type::Save, path, out); }
bool Cache::get(std::string_view path, PartCatalog& out) const { return read(Type::Parts, path, out); }
bool Cache::get(std::string_view path, PieceLibrary& out) const { return read(Type::Pieces, path, out); }
bool Cache::get(std::string_view path, PowerupFile& out) const { return read(Type::Powerups, path, out); }
bool Cache::get(std::string_view path, RaceFile& out) const { return read(Type::Race, path, out); }
bool Cache::get(std::string_view path, RaceTable& out) const { return read(Type::Races, path, out); }
bool Cache::get(std::string_view path, RouteFile& out) const { return read(Type::Route, path, out); }
bool Cache::get(std::string_view path, SceneFile& out) const { return read(Type::Scene, path, out); }
bool Cache::get(std::string_view path, SkeletonFile& out) const { return read(Type::Skeleton, path, out); }
bool Cache::get(std::string_view path, Sound& out) const { return read(Type::Sound, path, out); }
bool Cache::get(std::string_view path, StartGrid& out) const { return read(Type::Grid, path, out); }
bool Cache::get(std::string_view path, StringTable& out) const { return read(Type::Strings, path, out); }
bool Cache::get(std::string_view path, SurfaceFile& out) const { return read(Type::Surfaces, path, out); }
bool Cache::get(std::string_view path, TargetFile& out) const { return read(Type::Targets, path, out); }
bool Cache::get(std::string_view path, TextureList& out) const { return read(Type::TextureList, path, out); }
bool Cache::get(std::string_view path, TimerFile& out) const { return read(Type::Timers, path, out); }
bool Cache::get(std::string_view path, TouchFile& out) const { return read(Type::Touch, path, out); }
bool Cache::texture(std::string_view list, std::string_view name, Texture& out) const { return read(Type::Texture, std::string(list) + "#" + std::string(name), out); }
// clang-format on

} // namespace lr
