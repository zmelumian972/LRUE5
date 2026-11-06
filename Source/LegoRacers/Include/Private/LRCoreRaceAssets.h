#pragma once

// The race view's asset store (LRCoreRaceAssets.cpp): the Classic look's base materials, and the cache's
// material libraries, texture lists, textures, materials, models as UE meshes, figures and animations, each made once;
// the race's libraries.

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "LRCore/Race/Track.h"
#include "LRCore/Show/View.h"
#include "LRCoreModelMesh.h"
#include "LRData/Look.h"
#include "LRData/Model.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class FReferenceCollector;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

namespace lr
{
class Cache;
struct RaceFile;
} // namespace lr

struct FLRRaceAssets
{
	const lr::Cache* Cache = nullptr;
	UObject* Outer = nullptr; // the materials' outer (the race view)

	// The race's libraries: the track world's, the effects' (COMMON), the power-ups', those two joined (the effects'
	// billboards and decals), and the local world's each with its own name's .TDB (the track's particles).
	TArray<std::pair<std::string, std::string>> TrackLibraries, CommonLibraries, PowerupLibraries, SharedLibraries, EmitterLibraries;
	void LoadRaceLibraries(const lr::RaceFile& Race, const std::string& Dir);

	// The Classic look's three base materials (masked, translucent, additive), loaded once.
	bool LoadBases();
	// A material of the libraries for a section (the texture's flip applied to its uvs by the caller), made once.
	UMaterialInstanceDynamic* MaterialFor(const std::string& Name, const TArray<std::pair<std::string, std::string>>& Libraries,
		float Alpha, bool& bFlip, uint8 Diffuse[4], bool bForceTranslucent = false, uint8* Ambient = nullptr);
	UTexture2D* Texture(const std::string& List, const std::string& Name, bool bPoint, bool bClamp);
	// An image file's texture (flipped upside down if asked), made once.
	UTexture2D* ImageTexture(const std::string& Path, bool bFlip);
	// A minifig picture (MENUDATA/PARTDB/<file>.BMP) as a material, made once.
	UMaterialInterface* Picture(const std::string& File, bool bFlip, float Alpha = 1);
	// Each material library and texture list decoded once (MaterialFor runs per particle per frame); none if it doesn't
	// decode.
	const lr::MaterialLibrary* LibraryAt(const std::string& Path);
	const lr::TextureList* TexturesAt(const std::string& Path);
	// The cache's model as UE sections (made once per path and mirroring); a model in hand, made once per key.
	const FLRModelMesh* Model(const FString& Path, bool bMirrored);
	const FLRModelMesh* ModelOf(const FString& Key, const lr::Model* Source, bool bMirrored);
	// A power-up model's skeleton and parts.
	struct FFigure
	{
		std::vector<lr::SkeletonNode> Nodes;
		lr::Animation Animation;
		bool bAnimated = false;
	};
	const FFigure* Figure(const std::string& Model);
	// A menu animation file, decoded once (none: null).
	const lr::Animation* MenuAnimation(const std::string& Path);

	void AddReferencedObjects(FReferenceCollector& Collector);
	// The models, figures and animations dropped (the view's destruction).
	void Reset();

	TObjectPtr<UMaterialInterface> MaskedBase;
	TObjectPtr<UMaterialInterface> TranslucentBase;
	TObjectPtr<UMaterialInterface> AdditiveBase;
	// The materials made, by key (MaterialFor's, the pictures', the decals' and shadows').
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> Materials;

private:
	TMap<FString, TObjectPtr<UTexture2D>> Textures;
	std::unordered_map<std::string, std::optional<lr::MaterialLibrary>> LibraryCache;
	std::unordered_map<std::string, std::optional<lr::TextureList>> TextureCache;
	TMap<FString, TUniquePtr<FLRModelMesh>> Meshes;
	TMap<FString, TUniquePtr<FFigure>> Figures;
	TMap<FString, TUniquePtr<lr::Animation>> MenuAnimations;
};
