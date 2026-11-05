#pragma once

// What the race view's files share: the common folder, model and library paths, a lit channel, an entity's pose.

#include "LRCoreModelMesh.h"
#include "LRData/Scene.h"

namespace LRUe
{
inline const std::string Common = "GAMEDATA/COMMON/";

// A model's cache path: its folder's own, else COMMON's.
inline FString ModelPath(const lr::Cache& Cache, const std::string& Name, const std::string& Folder)
{
	const std::string Own = AssetPath(Folder, Name, ".GDB");
	return FString(UTF8_TO_TCHAR((Cache.has(Own) ? Own : AssetPath(Common, Name, ".GDB")).c_str()));
}

// A table entry's material library and texture list in COMMON (the list named as the library when it names none).
inline TArray<std::pair<std::string, std::string>> Libs(const std::string& Mdb, const std::string& Tdb)
{
	TArray<std::pair<std::string, std::string>> L;
	L.Add({AssetPath(Common, Mdb, ".MDB"), AssetPath(Common, Tdb.empty() ? Mdb : Tdb, ".TDB")});
	return L;
}

// A lit vertex's colour channel. The base is the world's ambient times the material's ambient (a black built car is
// dark, which 255 in place of the material's would not give).
inline uint8 LitChannel(int32 Ambient, int32 Diffuse, int32 Light, float NdotL, int32 MaterialAmbient)
{
	const int32 Base = (Ambient * MaterialAmbient) >> 8;
	const int32 P = (Diffuse * Light) >> 8;
	return uint8(FMath::Clamp(Base - int32(float(P) * NdotL), Base, 255));
}
} // namespace LRUe

// ---- Building ----

namespace LRUe
{
// A plain entity's frame (forward, up and the left between them; the model's and the entity's scale), mirrored as the
// track is (the mesh mirrored too: f' = Sf, s' = -Ss, u' = Su, p' = Sp with S the y flip).
inline lr::Pose EntityPose(const lr::SceneFile::Entity& E, bool bMirror)
{
	lr::Vec3 F = lr::normalize(lr::vec(E.forward)), U = lr::normalize(lr::vec(E.up));
	lr::Vec3 S = lr::cross(U, F);
	lr::Vec3 P = lr::vec(E.position);
	if (bMirror) {
		F.y = -F.y, U.y = -U.y, S = {-S.x, S.y, -S.z}, P.y = -P.y;
	}
	return {P, {F, S, U}};
}

} // namespace LRUe
