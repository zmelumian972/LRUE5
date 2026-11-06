#pragma once

// The race view's scenery (LRCoreRaceScenery.cpp): the track's worlds, their plain entities where
// they stand and their animated ones posed from the core, the materials animated and scrolled, the models each camera
// draws.

#include "LRCoreRaceModels.h"

namespace lr
{
struct World;
struct RaceFile;
} // namespace lr

struct FLRRaceScenery
{
	void Build(FLRRaceAssets& Assets, FLRModels& Models, const lr::RaceFile& Race, const std::string& Dir, const lr::World& W);
	void Pose(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W);
	// What view v's camera doesn't draw: the plain models out of its reach, the animated ones it didn't draw.
	void HidePlain(USceneCaptureComponent2D* Capture, const lr::World& W, const lr::PlayerCamera& Cam) const;
	void HideAnimated(USceneCaptureComponent2D* Capture, const lr::World& W, int32 v) const;
	void Clear();

private:
	// The track: its plain entities, its animated ones (by World::track.animated).
	TArray<FLRPlaced> Animated;
	TArray<FLRPlaced> Plain;
	TArray<int32> PlainOfEntity; // Track::models' index -> Plain's (-1 none)
	TArray<int16> ShownFrames; // each material binding's frame last shown
	TArray<float> PlainDistances; // each plain model entity's farthest squared draw distance last applied (the core's)
	bool bMirrored = false;
	TArray<FTransform> ScratchNodes;
	std::vector<lr::NodeMatrix> ScratchMatrices;

	void PoseAnimated(FLRModels& Models, const lr::World& W);
	void ApplyDrawDistances(const lr::World& W);
	FLRPlaced* SceneryOf(bool bAnimated, int32 Entity);
	void AnimateMaterials(FLRRaceAssets& Assets, const lr::World& W);
};
