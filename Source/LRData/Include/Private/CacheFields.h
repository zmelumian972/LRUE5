#pragma once

// The data cache's records: every decoded struct as a flat little-endian record. fields() lists each
// struct's members in order; Writer and Reader walk them. Numbers are stored as they are in memory (every target is
// little-endian), strings and vectors as a u32 count then their items, arrays item by item.

#include "Bytes.h"
#include "LRData/Cars.h"
#include "LRData/Collision.h"
#include "LRData/Executable.h"
#include "LRData/Image.h"
#include "LRData/Look.h"
#include "LRData/Menus.h"
#include "LRData/Model.h"
#include "LRData/Pieces.h"
#include "LRData/Race.h"
#include "LRData/Saves.h"
#include "LRData/Scene.h"
#include "LRData/Sound.h"

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace lr {

// clang-format off
template <class V> void fields(V& v, ChassisTable::Chassis::Variant& x) { v(x.materials, x.textures, x.model, x.skeleton, x.animation); }
template <class V> void fields(V& v, ChassisTable::Chassis& x) { v(x.name, x.model, x.animation, x.skeleton, x.materials, x.textures, x.second, x.driverMount, x.centerOfMass, x.mass, x.shadowWidth, x.shadowLength, x.enginePitch, x.skidWidths, x.skidPoints, x.wheels, x.handling, x.topSpeed, x.acceleration); }
template <class V> void fields(V& v, ChassisTable& x) { v(x.chassis); }
template <class V> void fields(V& v, ChampionTable::Champion& x) { v(x.name, x.model, x.materials, x.textures, x.chassis, x.mass, x.centerOfMass); }
template <class V> void fields(V& v, ChampionTable& x) { v(x.champions); }
template <class V> void fields(V& v, DriverTable::Driver& x) { v(x.name, x.champion, x.stats, x.chargeColour, x.chargeTarget, x.nameIndex, x.looks, x.voice, x.materials, x.textures, x.model); }
template <class V> void fields(V& v, DriverTable& x) { v(x.drivers); }
template <class V> void fields(V& v, AnimationFile::Part& x) { v(x.name, x.frames, x.loopFrames, x.rate, x.bounds, x.track); }
template <class V> void fields(V& v, AnimationFile::Track& x) { v(x.rotationFrame, x.rotationKey, x.rotationKeys, x.positionFrame, x.positionKey, x.positionKeys); }
template <class V> void fields(V& v, AnimationFile& x) { v(x.parts, x.positions, x.rotations, x.keys, x.tracks); }

template <class V> void fields(V& v, CollisionDatabase::Entity& x) { v(x.name, x.volume, x.position, x.forward, x.up); }
template <class V> void fields(V& v, CollisionDatabase& x) { v(x.volumes, x.entities); }
template <class V> void fields(V& v, BoundingVolume::Triangle& x) { v(x.v, x.material); }
template <class V> void fields(V& v, BoundingVolume::Node& x) { v(x.child, x.normal, x.first, x.count); }
template <class V> void fields(V& v, BoundingVolume& x) { v(x.materials, x.vertices, x.triangles, x.nodes); }
template <class V> void fields(V& v, SurfaceFile::Surface& x) { v(x.name, x.flags, x.touchEvent, x.projectileEvent, x.push, x.slopeHold, x.friction, x.grip, x.rolling, x.particle, x.sound); }
template <class V> void fields(V& v, SurfaceFile& x) { v(x.surfaces); }

template <class V> void fields(V& v, Image& x) { v(x.width, x.height, x.bits, x.rgba); }

template <class V> void fields(V& v, StringTable& x) { v(x.units, x.starts); }
template <class V> void fields(V& v, NameList& x) { v(x.header, x.names); }
template <class V> void fields(V& v, ImageList::Entry& x) { v(x.name, x.flip, x.targa, x.keyed, x.key, x.tint); }
template <class V> void fields(V& v, ImageList& x) { v(x.images); }
template <class V> void fields(V& v, FontList::Font& x) { v(x.name, x.targa, x.keyed, x.key, x.chars, x.spacing, x.colour); }
template <class V> void fields(V& v, FontList& x) { v(x.fonts); }
template <class V> void fields(V& v, MenuRect& x) { v(x.left, x.top, x.right, x.bottom); }
template <class V> void fields(V& v, MenuIcon& x) { v(x.sounds, x.stateRects, x.colours, x.transitionMs, x.hasSounds, x.hasColours); }
template <class V> void fields(V& v, MenuStyles::Style& x) { v(x.kind, x.name, x.images, x.colours, x.font, x.wrap, x.centred, x.icon, x.fonts, x.maxWidth, x.sounds, x.scrollMs, x.pageMode, x.wrapAround, x.buttons, x.frame, x.track, x.regions, x.regionRects); }
template <class V> void fields(V& v, MenuStyles& x) { v(x.capacity, x.styles); }
template <class V> void fields(V& v, MenuLayout::Binding& x) { v(x.kind, x.name, x.rect, x.id, x.parent, x.attach, x.hasColour, x.colour, x.enabled, x.iconAttach, x.help, x.icon, x.images, x.colours, x.fonts, x.wrap, x.hasFill, x.buttons, x.track, x.frame, x.unselectedInput, x.steps, x.value, x.editSounds, x.hasEditSounds, x.maxLength, x.world, x.camera, x.numbers, x.step, x.slots, x.autoAdvance, x.startFrame); }
template <class V> void fields(V& v, MenuLayout& x) { v(x.capacity, x.bindings); }
template <class V> void fields(V& v, CinemaFile::Cue& x) { v(x.kind, x.name, x.start, x.end, x.target, x.part, x.entityKind, x.entityAt, x.position, x.forward, x.up, x.tracks, x.colour, x.direction, x.blink); }
template <class V> void fields(V& v, CinemaFile::Scene& x) { v(x.name, x.rate, x.frames, x.cues); }
template <class V> void fields(V& v, CinemaFile::Effect& x) { v(x.kind, x.name, x.bank, x.sound, x.volume, x.pan, x.frequency, x.loops, x.ms, x.in, x.out, x.colour, x.tinted, x.table, x.string, x.text, x.font, x.picture, x.place, x.given, x.speed, x.entity, x.entityKind, x.shift, x.offset, x.offsetSpeed); }
template <class V> void fields(V& v, CinemaFile::Link& x) { v(x.channel, x.cue, x.start, x.effect); }
template <class V> void fields(V& v, CinemaFile& x) { v(x.worlds, x.scenes, x.effects, x.links, x.stringTables, x.soundBanks, x.imageLists); }
template <class V> void fields(V& v, CircuitTable::Circuit& x) { v(x.name, x.drivers, x.partSet, x.next, x.races); }
template <class V> void fields(V& v, CircuitTable& x) { v(x.circuits); }
template <class V> void fields(V& v, RaceTable::Race& x) { v(x.name, x.folder, x.circuit, x.position, x.mirrored, x.display, x.theme, x.featured); }
template <class V> void fields(V& v, RaceTable& x) { v(x.races); }

template <class V> void fields(V& v, Model::Section& x) { v(x.material, x.part, x.indices); }
template <class V> void fields(V& v, Model& x) { v(x.kind, x.positions, x.uvs, x.colors, x.normals, x.sections, x.vertexParts, x.materials, x.scale, x.faceCount); }
template <class V> void fields(V& v, HeadLibrary::Head& x) { v(x.name, x.model); }
template <class V> void fields(V& v, HeadLibrary& x) { v(x.heads); }

template <class V> void fields(V& v, PieceLibrary::Cell& x) { v(x.top, x.bottom, x.studded, x.rests, x.flush); }
template <class V> void fields(V& v, PieceLibrary::Piece& x) { v(x.name, x.type, x.width, x.height, x.cells, x.first, x.count, x.anchor, x.tallest); }
template <class V> void fields(V& v, PieceLibrary& x) { v(x.pieces, x.cells, x.commands, x.positions, x.normals, x.uvs, x.materials); }
template <class V> void fields(V& v, CarSets::Choice& x) { v(x.type, x.colour); }
template <class V> void fields(V& v, CarSets::Set& x) { v(x.part, x.chassis, x.choices); }
template <class V> void fields(V& v, CarSets& x) { v(x.colours, x.materials, x.sets); }
template <class V> void fields(V& v, PartCatalog::Part& x) { v(x.name, x.variant, x.unlock); }
template <class V> void fields(V& v, PartCatalog& x) { v(x.rings, x.hatModels, x.bodies); }

template <class V> void fields(V& v, RaceFile& x) { v(x.name, x.collisionFile, x.trackVolume, x.checkpointFile, x.checkpointVolume, x.surfaceFile, x.startGridFile, x.touchFile, x.eventFile, x.introCamera, x.startCamera, x.startLook, x.startDown, x.scene, x.sharedScene, x.powerupFile, x.powerupScene, x.positionalFile, x.timerFile, x.hazardFile, x.targetFile, x.emitterFiles, x.effectsBank, x.voiceBank, x.ambientBank, x.mapBounds, x.skyFile, x.skyWorld, x.globalWorld, x.localWorld); }
template <class V> void fields(V& v, StartGrid::Place& x) { v(x.position, x.forward, x.up); }
template <class V> void fields(V& v, StartGrid& x) { v(x.places); }
template <class V> void fields(V& v, RouteFile::Point& x) { v(x.dx, x.dy, x.dz, x.rotation, x.left, x.right, x.kind); }
template <class V> void fields(V& v, RouteFile& x) { v(x.start, x.loop, x.startRotation, x.loopRotation, x.loopTime, x.loopPoint, x.points); }
template <class V> void fields(V& v, CheckpointFile::Checkpoint& x) { v(x.normal, x.offset, x.next, x.position); }
template <class V> void fields(V& v, CheckpointFile& x) { v(x.checkpoints); }
template <class V> void fields(V& v, TouchFile::Sphere& x) { v(x.centre, x.radius, x.event, x.playersOnly, x.projectilesOnly, x.world, x.lapGated, x.lap); }
template <class V> void fields(V& v, TouchFile& x) { v(x.spheres); }
template <class V> void fields(V& v, EventFile::Resource& x) { v(x.kind, x.id, x.onEnd, x.noEnd, x.chains, x.durationMs, x.holdEvent, x.zone, x.force, x.channel, x.armingEvent, x.forced, x.position, x.atEvent, x.shared, x.entity, x.node, x.emitter, x.axes, x.sound, x.volume, x.pitch, x.nearDistance, x.farDistance, x.probability, x.looping, x.sky, x.skyMs, x.skyFlags, x.parts, x.clear, x.world, x.hide); }
template <class V> void fields(V& v, EventFile& x) { v(x.resources); }
template <class V> void fields(V& v, PowerupFile::Brick& x) { v(x.position, x.colour, x.respawnMs); }
template <class V> void fields(V& v, PowerupFile& x) { v(x.colourBricks, x.whiteBricks); }
template <class V> void fields(V& v, TargetFile::Point& x) { v(x.position, x.disabledBy); }
template <class V> void fields(V& v, TargetFile& x) { v(x.points); }
template <class V> void fields(V& v, TimerFile::Timer& x) { v(x.onMs, x.offMs, x.randomOn, x.randomOff, x.event, x.delayMs); }
template <class V> void fields(V& v, TimerFile& x) { v(x.timers); }
template <class V> void fields(V& v, HazardFile::Hazard& x) { v(x.type, x.trigger, x.entity, x.entities, x.surface, x.source, x.target, x.triggerPoint, x.triggerRadius, x.launches, x.targets, x.scroll, x.startFrame, x.size, x.periodMs, x.sweep, x.intervalMs, x.material, x.strength, x.low, x.high); }
template <class V> void fields(V& v, HazardFile& x) { v(x.hazards); }
template <class V> void fields(V& v, GhostFile& x) { v(x.laps, x.position, x.rotation, x.positions, x.rotations); }
template <class V> void fields(V& v, LoadingScreen& x) { v(x.picture, x.name, x.dots); }

template <class V> void fields(V& v, OriginalSave::Piece& x) { v(x.type, x.x, x.y, x.rotation, x.colour, x.part); }
template <class V> void fields(V& v, OriginalSave::Record& x) { v(x.name, x.looks, x.expression, x.stockCar, x.chassis, x.pieces, x.trophies); }
template <class V> void fields(V& v, OriginalSave::Best& x) { v(x.lapMs, x.raceMs, x.lapHolder, x.raceHolder); }
template <class V> void fields(V& v, OriginalSave::Pick& x) { v(x.record, x.list, x.slot, x.layout); }
template <class V> void fields(V& v, OriginalSave::Layout& x) { v(x.kind, x.subKind, x.device, x.bindings); }
template <class V> void fields(V& v, OriginalSave& x) { v(x.parts, x.circuits, x.races, x.best, x.opponents, x.device, x.kept, x.music, x.sound, x.stereo, x.language, x.laps, x.picks, x.layouts, x.records); }

template <class V> void fields(V& v, SceneFile::Entity& x) { v(x.name, x.model, x.skeleton, x.animation, x.drawDistance, x.distances, x.position, x.forward, x.up, x.scale, x.part, x.partName, x.scroll, x.materialBindings); }
template <class V> void fields(V& v, SceneFile::Camera& x) { v(x.name, x.entity, x.node, x.position, x.forward, x.up, x.nearPlane, x.farPlane, x.fov, x.part, x.partName); }
template <class V> void fields(V& v, SceneFile& x) { v(x.models, x.skeletons, x.animations, x.materialAnimations, x.materialLibraries, x.textureLists, x.unplaced, x.entities, x.animated, x.cameras, x.ambients, x.lights, x.sprites); }
template <class V> void fields(V& v, SceneFile::Sprite& x) { v(x.position, x.axis, x.width, x.height, x.distance, x.material, x.animationFile, x.animationTrack); }
template <class V> void fields(V& v, SceneFile::Light& x) { v(x.name, x.colour, x.direction); }
template <class V> void fields(V& v, SkeletonFile::Node& x) { v(x.name, x.position, x.rotation, x.parent); }
template <class V> void fields(V& v, SkeletonFile& x) { v(x.nodes); }
template <class V> void fields(V& v, MaterialAnimationFile::Track& x) { v(x.firstFrame, x.frames, x.duration, x.rate); }
template <class V> void fields(V& v, MaterialAnimationFile::Frame& x) { v(x.material, x.start); }
template <class V> void fields(V& v, MaterialAnimationFile& x) { v(x.frames, x.tracks); }
template <class V> void fields(V& v, EmitterFile::Emitter& x) { v(x.name, x.intervalMs, x.chance, x.acceleration, x.launches, x.width, x.height, x.growWidth, x.growHeight, x.track, x.lifeMs, x.durationMs, x.radius, x.material, x.priority); }
template <class V> void fields(V& v, EmitterFile& x) { v(x.emitters); }
template <class V> void fields(V& v, SkyFile::Keyframe& x) { v(x.durationMs, x.colours); }
template <class V> void fields(V& v, SkyFile::State& x) { v(x.name, x.keyframes); }
template <class V> void fields(V& v, SkyFile& x) { v(x.states, x.initial, x.height); }

template <class V> void fields(V& v, Sound& x) { v(x.channels, x.rate, x.samples); }

template <class V> void fields(V& v, MaterialLibrary::Material& x) { v(x.name, x.ambient, x.diffuse, x.texture, x.blend, x.source, x.destination, x.constantAlpha, x.alpha, x.point, x.clamp, x.rotate); }
template <class V> void fields(V& v, MaterialLibrary& x) { v(x.materials); }
template <class V> void fields(V& v, TextureList::Entry& x) { v(x.name, x.file, x.targa, x.flip, x.keyed, x.key); }
template <class V> void fields(V& v, TextureList& x) { v(x.textures); }
template <class V> void fields(V& v, Texture& x) { v(x.width, x.height, x.rgba); }

template <class V> void fields(V& v, ExecutableTables& x) { v(x.random, x.cheats, x.languages, x.frameColours, x.frameMap, x.placePoints, x.curseDrop, x.lavaSpots, x.lavaStarts, x.lavaEnds, x.builderEyes, x.builderStepX, x.builderStepY); }
// clang-format on

template <class T>
inline constexpr bool IsPlain = std::is_arithmetic_v<T> || std::is_enum_v<T>;

template <class T>
struct IsVector : std::false_type {};
template <class T, class A>
struct IsVector<std::vector<T, A>> : std::true_type {};
template <class T>
struct IsArray : std::false_type {};
template <class T, size_t N>
struct IsArray<std::array<T, N>> : std::true_type {};
template <class T>
struct IsString : std::false_type {};
template <class C>
struct IsString<std::basic_string<C>> : std::true_type {};

struct RecordWriter {
	std::vector<uint8_t>& out;

	template <class... T>
	void operator()(T&... items)
	{
		(put(items), ...);
	}
	void raw(const void* data, size_t size)
	{
		const uint8_t* p = static_cast<const uint8_t*>(data);
		out.insert(out.end(), p, p + size);
	}
	void count(size_t n)
	{
		const uint32_t c = uint32_t(n);
		raw(&c, 4);
	}
	template <class T>
	void put(T& x)
	{
		if constexpr (IsPlain<T>) {
			raw(&x, sizeof(T));
		}
		else if constexpr (IsString<T>::value) {
			count(x.size());
			raw(x.data(), x.size() * sizeof(typename T::value_type));
		}
		else if constexpr (IsVector<T>::value) {
			count(x.size());
			if constexpr (IsPlain<typename T::value_type>) {
				raw(x.data(), x.size() * sizeof(typename T::value_type));
			}
			else {
				for (auto& item : x) {
					put(item);
				}
			}
		}
		else if constexpr (IsArray<T>::value || std::is_array_v<T>) {
			for (auto& item : x) {
				put(item);
			}
		}
		else {
			fields(*this, x);
		}
	}
};

struct RecordReader {
	std::span<const uint8_t> in;
	size_t at = 0;
	bool failed = false;

	template <class... T>
	void operator()(T&... items)
	{
		(get(items), ...);
	}
	bool raw(void* data, size_t size)
	{
		if (failed || !fits(in, at, size)) {
			failed = true;
			return false;
		}
		if (size) {
			std::memcpy(data, in.data() + at, size);
		}
		at += size;
		return true;
	}
	size_t count()
	{
		uint32_t c = 0;
		raw(&c, 4);
		if (!fits(in, at, c)) { // (every item is at least a byte)
			failed = true;
			return 0;
		}
		return c;
	}
	template <class T>
	void get(T& x)
	{
		if constexpr (IsPlain<T>) {
			raw(&x, sizeof(T));
		}
		else if constexpr (IsString<T>::value) {
			x.resize(count());
			raw(x.data(), x.size() * sizeof(typename T::value_type));
		}
		else if constexpr (IsVector<T>::value) {
			x.resize(count());
			if constexpr (IsPlain<typename T::value_type>) {
				raw(x.data(), x.size() * sizeof(typename T::value_type));
			}
			else {
				for (auto& item : x) {
					get(item);
				}
			}
		}
		else if constexpr (IsArray<T>::value || std::is_array_v<T>) {
			for (auto& item : x) {
				get(item);
			}
		}
		else {
			fields(*this, x);
		}
	}
};

} // namespace lr
