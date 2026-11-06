#pragma once

// The race view's sky (LRCoreRaceSky.cpp): per view, the dome and the sky's world
// re-centred on its eye, coloured and shown as the show has them.

#include "LRCoreRaceModels.h"

namespace lr
{
class Show;
struct World;
} // namespace lr

struct FLRRaceSky
{
	void Build(FLRRaceAssets& Assets, const lr::World& W);
	void Pose(FLRRaceAssets& Assets, FLRModels& Models, int32 v, const lr::World& W, const lr::Show& Show);
	// View v's sky left out of a capture.
	void HideView(USceneCaptureComponent2D* Capture, int32 v) const;
	void Clear();

private:
	// A view's sky, re-centred on its eye: its actor, the dome then the sky world's parts.
	struct FView
	{
		TWeakObjectPtr<AActor> Sky;
		TArray<TWeakObjectPtr<UProceduralMeshComponent>> SkyDome;
	};
	TArray<FView> Views;
	TArray<std::pair<std::string, std::string>> SkyLibraries;
	FString Track;
	bool bMirrored = false;
	TArray<FVector> ScratchPositions;
	TArray<FColor> ScratchColours;

	void MakeSky(FLRRaceAssets& Assets, FLRModels& Models, FView& V);
	void AddSkyWorld(FLRRaceAssets& Assets, FLRModels& Models, FView& V, AActor* A);
};
