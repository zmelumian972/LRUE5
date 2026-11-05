#pragma once

// The data cache: everything the game reads from the player's install (LEGO.JAM,
// LEGORacers.exe, the *.tun music), decoded once into one file per kind and read back at runtime with no parsing.
//
// Each file: "LRCK", the format number, the kind, the SHA-256 of the JAM and of the exe, then an index of records (the
// JAM path, matched case-insensitively, and the record's type) and the records: flat little-endian forms of the
// structs LRData decodes into (CacheFields.h).

#include "LRData/Api.h"
#include "LRData/Digest.h"
#include "LRData/Executable.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lr {

struct AnimationFile;
struct BoundingVolume;
struct CarSets;
struct ChampionTable;
struct ChassisTable;
struct CheckpointFile;
struct CinemaFile;
struct CircuitTable;
struct CollisionDatabase;
struct DriverTable;
struct EmitterFile;
struct EventFile;
struct FontList;
struct GhostFile;
struct HeadLibrary;
struct HazardFile;
struct Image;
struct ImageList;
struct LoadingScreen;
struct MaterialAnimationFile;
struct MaterialLibrary;
struct MenuLayout;
struct MenuStyles;
struct Model;
struct NameList;
struct OriginalSave;
struct PartCatalog;
struct PieceLibrary;
struct PowerupFile;
struct RaceFile;
struct RaceTable;
struct RouteFile;
struct SceneFile;
struct SkeletonFile;
struct Sound;
struct StartGrid;
struct SkyFile;
struct StringTable;
struct SurfaceFile;
struct TargetFile;
struct Texture;
struct TextureList;
struct TimerFile;
struct TouchFile;

enum class CacheKind : uint32_t { Tables, Executable, Models, Materials, Textures, Sounds, Count };
inline constexpr uint32_t CacheFormat = 12; // 5: the head libraries (.GCB); 6: per-vertex parts, menu lights, cinematic cues; 7: engine pitch, driver voice; 8: blend factors, colour effects; 9: the builder keypad steps; 10: node transforms' worlds, model distances' hide flag; 11: textures without the relief's steepness; 12: the worlds' sprites
// Each kind's file name in the cache folder.
inline constexpr const char* CacheFileNames[size_t(CacheKind::Count)] = {"tables.lrc", "executable.lrc", "models.lrc", "materials.lrc",
	"textures.lrc", "sounds.lrc"};

struct CacheSource {
	std::span<const uint8_t> jam, exe;
	std::vector<std::pair<std::string, std::span<const uint8_t>>> tunes; // the install's *.tun files by name
};

// Decodes the install into the cache's files (by kind). False with why in `error` (the JAM doesn't decode, the exe isn't
// a supported version). `progress` hears the fraction done now and then.
LRDATA_API bool buildCache(const CacheSource& source, std::array<std::vector<uint8_t>, size_t(CacheKind::Count)>& files, std::string& error,
	const std::function<void(float)>& progress = {});

// A cache file's key: its format and kind, and the digests it was built from. False if it isn't a cache file.
LRDATA_API bool cacheKey(std::span<const uint8_t> file, CacheKind& kind, Digest& jam, Digest& exe);

class LRDATA_API Cache {
public:
	// The kinds' files by kind (the cache views the bytes: keep them alive). False if one isn't a cache file of this
	// format, isn't its kind, or they weren't built from the same install.
	bool open(const std::array<std::span<const uint8_t>, size_t(CacheKind::Count)>& files);

	const Digest& jamDigest() const { return m_jam; }
	const Digest& exeDigest() const { return m_exe; }

	// LEGO.JAM's files (paths as listed) and whether one exists (case-insensitive).
	const std::vector<std::string>& paths() const { return m_paths; }
	bool has(std::string_view path) const;
	uint32_t size(std::string_view path) const; // its size in bytes; 0 if none
	const ExecutableTables& executable() const { return m_executable; }
	// The languages' folders (under MENUDATA/ and GAMEDATA/COMMON/), in the language screen's order.
	const std::vector<std::string>& languages() const { return m_languages; }

	// A file's decoded form. False if the file isn't in the JAM or didn't decode as that.
	bool get(std::string_view path, AnimationFile& out) const;
	bool get(std::string_view path, BoundingVolume& out) const;
	bool get(std::string_view path, CarSets& out) const; // MENUDATA/PIECEDB/CRSTMGR.LEB, against LPIECEHI's library
	bool get(std::string_view path, ChampionTable& out) const;
	bool get(std::string_view path, ChassisTable& out) const;
	bool get(std::string_view path, CheckpointFile& out) const;
	bool get(std::string_view path, CinemaFile& out) const; // with its .CEB's effects
	bool get(std::string_view path, CircuitTable& out) const; // a .CRB as its .RCB's decode leaves it
	bool get(std::string_view path, CollisionDatabase& out) const;
	bool get(std::string_view path, DriverTable& out) const;
	bool get(std::string_view path, EmitterFile& out) const;
	bool get(std::string_view path, EventFile& out) const;
	bool get(std::string_view path, FontList& out) const;
	bool get(std::string_view path, GhostFile& out, bool mirrored) const;
	bool get(std::string_view path, HazardFile& out) const;
	bool get(std::string_view path, HeadLibrary& out) const; // a .GCB
	bool get(std::string_view path, Image& out) const; // a .BMP or .TGA as decoded
	bool get(std::string_view path, ImageList& out) const;
	bool get(std::string_view path, LoadingScreen& out) const;
	bool get(std::string_view path, MaterialAnimationFile& out) const;
	bool get(std::string_view path, MaterialLibrary& out) const;
	bool get(std::string_view path, MenuLayout& out) const;
	bool get(std::string_view path, MenuStyles& out) const;
	bool get(std::string_view path, Model& out) const;
	bool get(std::string_view path, NameList& out) const;
	bool get(std::string_view path, OriginalSave& out) const;
	bool get(std::string_view path, PartCatalog& out) const;
	bool get(std::string_view path, PieceLibrary& out) const; // a .LEB with the .MDB beside it
	bool get(std::string_view path, PowerupFile& out) const;
	bool get(std::string_view path, RaceFile& out) const;
	bool get(std::string_view path, RaceTable& out) const; // with the .CRB beside it
	bool get(std::string_view path, RouteFile& out) const;
	bool get(std::string_view path, SceneFile& out) const;
	bool get(std::string_view path, SkeletonFile& out) const;
	bool get(std::string_view path, Sound& out) const; // a .PCM, or a tune by its file name ("THEME.TUN")
	bool get(std::string_view path, SkyFile& out) const;
	bool get(std::string_view path, StartGrid& out) const;
	bool get(std::string_view path, StringTable& out) const;
	bool get(std::string_view path, SurfaceFile& out) const;
	bool get(std::string_view path, TargetFile& out) const;
	bool get(std::string_view path, TextureList& out) const;
	bool get(std::string_view path, TimerFile& out) const;
	bool get(std::string_view path, TouchFile& out) const;
	// A texture list's entry as UE draws it: `list` the .TDB's path, `name` the entry's.
	bool texture(std::string_view list, std::string_view name, Texture& out) const;
	// The names of the tunes in the cache (as the install spells them).
	const std::vector<std::string>& tunes() const { return m_tunes; }

	// The records' types (part of the format).
	enum class Type : uint32_t;

private:
	struct Entry {
		uint32_t offset, size;
	};
	template <class T>
	bool read(Type type, std::string_view path, T& out) const;

	std::array<std::span<const uint8_t>, size_t(CacheKind::Count)> m_files;
	std::unordered_map<std::string, std::pair<uint32_t, Entry>> m_index; // "type:PATH" -> kind, record
	Digest m_jam = {}, m_exe = {};
	std::vector<std::string> m_paths, m_languages, m_tunes;
	std::unordered_map<std::string, uint32_t> m_sizes; // by upper path
	ExecutableTables m_executable;
};

} // namespace lr
