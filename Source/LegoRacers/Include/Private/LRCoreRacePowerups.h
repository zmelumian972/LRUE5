#pragma once

// The race view's power-ups (LRCoreRacePowerups.cpp): the bricks and their glows, the
// explosions, the actions' models, the warp tube and a warping player's view of it.

#include "LRCoreRaceModels.h"

class UMaterialInstanceDynamic;

namespace lr
{
struct World;
struct Brick;
struct Action;
} // namespace lr

struct FLRRacePowerups
{
	void Build(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W);
	void BuildWarpTube(FLRRaceAssets& Assets, FLRModels& Models);
	void Pose(FLRModels& Models, const lr::World& W);
	void PoseActions(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W);
	// Warping: only the tube, posed for the warp's time left, and the camera snapped behind it.
	void PoseWarpView(FLRRaceAssets& Assets, FLRModels& Models, USceneCaptureComponent2D* Capture, const lr::World& W,
		const lr::PlayerCamera& Cam, lr::Vec3& EyeAt, lr::Vec3& Look, lr::Vec3& Down, float& Fov);
	// What a camera doesn't draw: the tube (unless warping), the bricks out of its reach.
	void HideTube(USceneCaptureComponent2D* Capture) const;
	void HideBricks(USceneCaptureComponent2D* Capture, const lr::World& W, const lr::PlayerCamera& Cam) const;
	void Clear();

private:
	// The power-ups' models (POWERUP.WDB), by role, made once each and shown as needed.
	TArray<FLRPlaced> Bricks, BrickGlows, Explosions;
	// The colour bricks' materials by model (cube, streak) and colour, found once a race (kept alive by the assets').
	UMaterialInstanceDynamic* BrickColours[2][5] = {};
	AActor* PowerupActor = nullptr;
	// The actions' models (turbo burns and flames, shields, the dynamite, the grapple, missiles), by action and role
	// (a slot is reused by other kinds and levels, whose models differ), made when first wanted; the frame each was last
	// shown in.
	struct FActionModel
	{
		std::string Model;
		FLRPlaced Placed;
		uint32 ShownAt = 0;
	};
	TMap<uint32, TArray<FActionModel>> ActionModels;
	uint32 ActionFrame = 0;
	FLRPlaced Tube[2];
	std::string TubeModels[2];
	lr::Pose TubePoses[2];
	TArray<FTransform> ScratchNodes;
	std::vector<lr::NodeMatrix> ScratchMatrices;

	void PoseBrick(FLRModels& Models, const lr::World& W, const lr::Brick& B, FLRPlaced& Placed, int32 m);
	void PoseExplosions(const lr::World& W);
	FLRPlaced& ActionModel(FLRRaceAssets& Assets, FLRModels& Models, int32 Action, int32 Role, const std::string& Model);
	void ShowActionModel(FLRRaceAssets& Assets, FLRModels& Models, int32 Action, int32 Role, const std::string& Model, const lr::Pose& At,
		float Scale, int32 Part, float TimeMs, float Alpha, FVector2D Scroll);
	void PoseTurbo(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W, int32 i, const lr::Action& A, const lr::Pose& Car,
		float Seconds);
};
