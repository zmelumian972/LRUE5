#pragma once

// The race view's effects (LRCoreRaceEffects.cpp): per view, the particles,
// billboards and sprites facing its camera and the decals, batched by material in one mesh.

#include "LRCoreRaceModels.h"

class UMaterialInstanceDynamic;
struct FLRRaceRacers;

namespace lr
{
class Show;
struct World;
struct Decal;
} // namespace lr

struct FLRRaceEffects
{
	// At a race's build: the materials found for the last race dropped.
	void Prepare();
	void Build(FLRRaceAssets& Assets, FLRModels& Models, const FLRRaceRacers& Racers, int32 v, const lr::World& W, const lr::Show& Show);
	// View v's effects left out of a capture.
	void HideView(USceneCaptureComponent2D* Capture, int32 v) const;
	void Clear();

private:
	TArray<TWeakObjectPtr<UProceduralMeshComponent>> Meshes; // each view's particles, decals, beams (facing its camera)
	// A material's look, found once a race by name, library list (one of the assets' race libraries) and translucency:
	// what MaterialFor gives, and the decals' copy of it once wanted. Kept alive by the assets' materials.
	struct FMaterialLook
	{
		std::string Name;
		const TArray<std::pair<std::string, std::string>>* Libraries = nullptr;
		bool bTranslucent = false;
		UMaterialInstanceDynamic* Material = nullptr;
		UMaterialInterface* Decal = nullptr;
		bool bFlip = false;
		uint8 Diffuse[4] = {};
	};
	TArray<FMaterialLook> Looks;
	int32 LastLook = 0;
	TArray<UMaterialInterface*> ShadowLooks; // each racer's shadow material, by racer
	FMaterialLook& LookOf(FLRRaceAssets& Assets, const std::string& Name, const TArray<std::pair<std::string, std::string>>& Libraries,
		bool bTranslucent);

	struct FBatches;
	void AddParticles(FLRRaceAssets& Assets, FBatches& E, const lr::Show& Show);
	void AddBillboards(FLRRaceAssets& Assets, FBatches& E, const lr::World& W);
	void AddSprites(FLRRaceAssets& Assets, FBatches& E, const lr::World& W);
	void AddDecals(FLRRaceAssets& Assets, const FLRRaceRacers& Racers, FBatches& E, int32 v, const lr::Show& Show);
	UMaterialInterface* DecalMaterial(FLRRaceAssets& Assets, const FLRRaceRacers& Racers, const lr::Decal& D, bool& bFlip);
};
