#include "LRData/Scene.h"

#include "LRData/Tokens.h"

namespace lr {

namespace {

// .WDB
enum : int {
	SceneModels = 0x2a, // [n] { "name" ... }
	SceneSkeletons = 0x2c,
	SceneAnimations = 0x29,
	SceneMaterialAnimations = 0x3d,
	SceneMaterialLibraries = 0x27,
	SceneTextureLists = 0x28,
	SceneUnplaced = 0x2d,

	SceneEntities = 0x2e, // [n] { K(0x2e) ["name"] { fields } ... }
	SceneAnimated = 0x2f,
	EntityModel = 0x2a, // i model, r distance
	EntityAnimatedModel = 0x33, // i model, i skeleton, i animation, r distance
	EntitySkeleton = 0x2c, // i skeleton, i animation, r distance (no model)
	EntityPosition = 0x31,
	EntityAxes = 0x32, // r r r forward, r r r up
	EntityPart = 0x35, // i part, or "part name"
	EntityScale = 0x36,
	EntityMaterialAnimation = 0x3e, // [n] { i file i track i slot i level ... }
	EntityScroll = 0x3f, // r u, r v (per second)
	SceneCameras = 0x43, // [n] { K(0x43) ["name"] { fields } ... }
	CameraTracks = 0x2f, // i entity, i node
	CameraTarget = 0x44, // r r r: forward = target - position
	CameraNear = 0x45,
	CameraFar = 0x46,
	CameraFov = 0x47,
	SceneAmbients = 0x48, // [n] { K(0x48) "name" { K(0x4a) r g b } ... }
	SceneLights = 0x49, // [n] { K(0x49) "name" { K(0x4a) r g b, K(0x4b) x y z } ... }
	LightColour = 0x4a,
	LightDirection = 0x4b,
	SceneSprites = 0x37, // [n] { K(0x37) { fields } ... }
	SpriteAxis = 0x38,
	SpriteMaterial = 0x39,
	SpriteWidth = 0x3a,
	SpriteHeight = 0x3b,
	SpriteDistance = 0x3c,
	SpriteAnimation = 0x3e, // i file, i track
};

// .SDB
enum : int {
	SkeletonNodes = 0x27, // [n] { K(0x27) "name" { fields } ... }
	NodePosition = 0x28,
	NodeRotation = 0x29,
	NodeParent = 0x2a,
};

// .MAB
enum : int {
	MaterialFrames = 0x27, // [n] { "texture" i ... }
	MaterialTracks = 0x28, // [n] { K(0x28) { K(0x27) i first i count, K(0x29) i duration, K(0x2a) i rate } ... }
	TrackFrames = 0x27,
	TrackDuration = 0x29,
	TrackRate = 0x2a,
};

// A value the file may write as an integer or a real.
float number(TokenReader& r) { return r.next(Token::Integer) ? float(r.tokens().list[r.position() - 1].integer) : r.real(); }

void names(TokenReader& r, std::vector<std::string>& into)
{
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		into.emplace_back(r.text());
	}
	r.close();
}

// Values, or a counted block, after a keyword this reader doesn't use.
void skipField(TokenReader& r)
{
	if (r.next(Token::OpenCount)) {
		r.seek(r.position() - 1);
		r.skipCounted();
	}
	else {
		r.skipValues();
	}
}

// "i part" or "part name".
void readPart(TokenReader& r, int32_t& part, std::string& partName)
{
	if (r.next(Token::Integer)) {
		part = r.tokens().list[r.position() - 1].integer;
	}
	else {
		partName = r.text();
	}
}

// An entity's model, animated model or skeleton (its first level of detail); a further level keeps only its distance.
void readEntityLevel(TokenReader& r, int key, SceneFile::Entity& e)
{
	if (e.model >= 0 || e.skeleton >= 0) {
		r.integer();
		if (key != EntityModel) {
			r.integer();
		}
		if (key == EntityAnimatedModel) {
			r.integer();
		}
		e.distances.push_back(r.real());
		return;
	}
	if (key != EntitySkeleton) {
		e.model = r.integer();
	}
	if (key != EntityModel) {
		e.skeleton = r.integer();
		e.animation = r.integer();
	}
	e.drawDistance = r.real();
	e.distances.push_back(e.drawDistance);
}

// An entity's fields after its "{" (section: SceneEntities or SceneAnimated).
bool readEntity(TokenReader& r, int section, SceneFile::Entity& e)
{
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		if (key < 0) {
			return false;
		}
		const bool level = (key == EntityModel && section == SceneEntities) || key == EntityAnimatedModel || (key == EntitySkeleton && section == SceneAnimated);
		if (level) {
			readEntityLevel(r, key, e);
		}
		else if (key == EntityScroll) {
			e.scroll = r.reals<2>();
		}
		else if (key == EntityMaterialAnimation) {
			const uint32_t n = r.openCounted();
			for (uint32_t b = 0; b < n && !r.failed(); b++) {
				e.materialBindings.push_back({r.integer(), r.integer(), r.integer(), r.integer()});
			}
			r.close();
			continue;
		}
		else if (key == EntityPosition) {
			e.position = r.reals<3>();
		}
		else if (key == EntityAxes) {
			e.forward = r.reals<3>();
			e.up = r.reals<3>();
		}
		else if (key == EntityPart) {
			readPart(r, e.part, e.partName);
		}
		else if (key == EntityScale) {
			e.scale = r.real();
		}
		skipField(r); // the other levels of detail, materials, scrolling, flags
	}
	return true;
}

// A camera's fields after its "{".
bool readCamera(TokenReader& r, SceneFile::Camera& c)
{
	bool aimed = false;
	std::array<float, 3> target = {};
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		if (key == CameraTracks) {
			c.entity = r.integer();
			c.node = r.integer();
		}
		else if (key == EntityPosition) {
			c.position = r.reals<3>();
		}
		else if (key == CameraTarget) {
			target = r.reals<3>();
			aimed = true;
		}
		else if (key == EntityAxes) {
			c.forward = r.reals<3>();
			c.up = r.reals<3>();
		}
		else if (key == CameraNear) {
			c.nearPlane = r.real();
		}
		else if (key == CameraFar) {
			c.farPlane = r.real();
		}
		else if (key == CameraFov) {
			c.fov = r.real();
		}
		else if (key == EntityPart) {
			readPart(r, c.part, c.partName);
		}
		else if (key < 0) {
			return false;
		}
		skipField(r);
	}
	if (aimed) {
		c.forward = {target[0] - c.position[0], target[1] - c.position[1], target[2] - c.position[2]};
	}
	return true;
}

// An ambient's or a light's fields after its "{".
bool readLight(TokenReader& r, SceneFile::Light& l)
{
	while (!r.failed() && !r.next(Token::Close)) {
		const int key = r.keyword();
		if (key == LightColour || key == LightDirection) {
			for (float& x : key == LightColour ? l.colour : l.direction) {
				x = number(r);
			}
		}
		else if (key < 0) {
			return false;
		}
		skipField(r);
	}
	return true;
}

// A sprite's fields after its "{".
bool readSprite(TokenReader& r, SceneFile::Sprite& s)
{
	while (!r.failed() && !r.next(Token::Close)) {
		switch (r.keyword()) {
		case EntityPosition: s.position = r.reals<3>(); break;
		case SpriteAxis: s.axis = r.reals<3>(); break;
		case SpriteWidth: s.width = r.real(); break;
		case SpriteHeight: s.height = r.real(); break;
		case SpriteDistance: s.distance = r.real(); break;
		case SpriteMaterial: s.material = r.text(); break;
		case SpriteAnimation:
			s.animationFile = r.integer();
			s.animationTrack = r.integer();
			break;
		case -1: return false;
		default: skipField(r);
		}
	}
	return true;
}

// A section's counted entries "K(section) ["name"] { fields }" (a sprite has no name), each's fields read by
// read(r, entry). False if one doesn't decode.
template <class T, class Read> bool readEntries(TokenReader& r, int section, std::vector<T>& list, Read read)
{
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != section) {
			return false;
		}
		T& entry = list.emplace_back();
		if constexpr (requires { entry.name; }) {
			if (!r.next(Token::Open)) {
				entry.name = r.text();
				r.expect(Token::Open);
			}
		}
		else {
			r.expect(Token::Open);
		}
		if (!read(r, entry)) {
			return false;
		}
	}
	r.close();
	return true;
}

} // namespace

bool SceneFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		const int section = r.keyword();
		bool ok = true;
		switch (section) {
		case SceneModels: names(r, models); break;
		case SceneSkeletons: names(r, skeletons); break;
		case SceneAnimations: names(r, animations); break;
		case SceneMaterialAnimations: names(r, materialAnimations); break;
		case SceneMaterialLibraries: names(r, materialLibraries); break;
		case SceneTextureLists: names(r, textureLists); break;
		case SceneUnplaced: names(r, unplaced); break;
		case SceneEntities:
		case SceneAnimated:
			ok = readEntries(r, section, section == SceneEntities ? entities : animated, [section](TokenReader& in, Entity& e) { return readEntity(in, section, e); });
			break;
		case SceneCameras: ok = readEntries(r, section, cameras, readCamera); break;
		case SceneAmbients:
		case SceneLights: ok = readEntries(r, section, section == SceneAmbients ? ambients : lights, readLight); break;
		case SceneSprites: ok = readEntries(r, section, sprites, readSprite); break;
		case -1: return false;
		default: skipField(r); // texture and material lists, the sky
		}
		if (!ok) {
			return false;
		}
	}
	return !r.failed();
}

bool SkeletonFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	if (r.keyword() != SkeletonNodes) {
		return false;
	}
	const uint32_t count = r.openCounted();
	for (uint32_t i = 0; i < count && !r.failed(); i++) {
		if (r.keyword() != SkeletonNodes) {
			return false;
		}
		Node& n = nodes.emplace_back();
		n.name = r.text();
		r.expect(Token::Open);
		while (!r.failed() && !r.next(Token::Close)) {
			switch (r.keyword()) {
			case NodePosition: n.position = r.reals<3>(); break;
			case NodeRotation: n.rotation = r.reals<4>(); break;
			case NodeParent: n.parent = r.text(); break;
			case -1: return false;
			default: r.skipValues();
			}
		}
	}
	r.close();
	return !r.failed();
}

bool MaterialAnimationFile::decode(std::span<const uint8_t> bytes)
{
	*this = {};
	Tokens tokens;
	if (!tokens.decode(bytes)) {
		return false;
	}
	TokenReader r(tokens);
	while (!r.atEnd() && !r.failed()) {
		const int section = r.keyword();
		if (section == MaterialFrames) {
			const uint32_t count = r.openCounted();
			for (uint32_t i = 0; i < count && !r.failed(); i++) {
				Frame& f = frames.emplace_back();
				f.material = r.text();
				f.start = r.integer();
			}
			r.close();
			continue;
		}
		if (section != MaterialTracks) {
			return false;
		}
		const uint32_t count = r.openCounted();
		for (uint32_t i = 0; i < count && !r.failed(); i++) {
			if (r.keyword() != MaterialTracks) {
				return false;
			}
			Track& t = tracks.emplace_back();
			r.expect(Token::Open);
			while (!r.failed() && !r.next(Token::Close)) {
				switch (r.keyword()) {
				case TrackFrames:
					t.firstFrame = r.integer();
					t.frames = r.integer();
					break;
				case TrackDuration: t.duration = r.integer(); break;
				case TrackRate: t.rate = r.integer(); break;
				case -1: return false;
				default: r.skipValues();
				}
			}
		}
		r.close();
	}
	return !r.failed();
}

} // namespace lr
