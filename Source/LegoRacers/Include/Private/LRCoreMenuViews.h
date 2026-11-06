#pragma once

// The menus' 3D (LRCoreMenuViews.cpp): a view's world and things posed
// from the core's scene only, at the view's own place, with the race view's assets; the camera it is seen from (the
// world's own when it has one). Kept across races.

#include "LRCoreRaceModels.h"

namespace lr
{
struct CarMesh;
struct Game;
struct MenuData;
} // namespace lr

struct FLRMenuCamera
{
	FVector Eye = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float Fov = 45; // vertical, degrees
	bool bValid = false;
};

struct FLRMenuViews
{
	// Menu view v posed at At from the core's scene (its world rebuilt when it changes), its actor; the camera.
	FLRMenuCamera Pose(FLRRaceAssets& Assets, FLRModels& Models, UWorld* World, const lr::MenuData& Data, int32 v, const FVector& At,
		const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game, AActor*& OutActor);

private:
	struct FMenuThing
	{
		FString Key;
		TArray<FLRPlaced> Placed; // a minifig's head; a plain model's
		TArray<TWeakObjectPtr<UProceduralMeshComponent>> Built; // a built mesh (car, piece, marks)
		TArray<FLRSkin> Skins; // a minifig's body; an animated cinematic entity
		std::vector<lr::SkeletonNode> Nodes;
		const lr::Animation* Animation = nullptr;
		int32 FacePart = -1; // the body node the head rides on
		// A cinematic entity's: the libraries its materials resolve in (newest first) and the material
		// table swaps shown (slot, material).
		TArray<std::pair<std::string, std::string>> Libraries;
		std::vector<std::pair<int32_t, std::string>> Shown;
	};
	// A blended world's animated entity (racer select's turntable shadows), posed at the scene's frame.
	struct FBlended
	{
		FLRPlaced Placed;
		std::vector<lr::SkeletonNode> Nodes;
		const lr::Animation* Animation = nullptr;
		int32 Part = -1;
		lr::Pose Where;
		float Scale = 1;
	};
	struct FMenuView
	{
		TWeakObjectPtr<AActor> Actor;
		std::string World;
		bool bCinema = false;
		FLRMenuCamera Last; // (a cinematic with no camera keeps the last view)
		TArray<FMenuThing> Things;
		TArray<FBlended> Blended;
		TWeakObjectPtr<UProceduralMeshComponent> Sprites; // a cinematic's worlds'
	};
	TArray<FMenuView> MenuViews;

	FMenuView& MenuView(UWorld* World, int32 v, const FVector& At);
	void ResetMenuView(FMenuView& View, const std::string& WorldName, bool bCinema);
	void BuildMenuWorld(FLRRaceAssets& Assets, FLRModels& Models, FMenuView& View, const lr::MenuScene& Scene, const FVector& At);
	void AddBlendedWorld(FLRRaceAssets& Assets, FLRModels& Models, FMenuView& View, const std::string& Folder,
		const TArray<std::pair<std::string, std::string>>& Libraries);
	void PoseBlended(FLRModels& Models, FMenuView& View, const lr::MenuScene& Scene, const FVector& At);
	static FString ThingKey(const lr::MenuScene::Thing& Th, const lr::Menus& Menus, bool bRing);
	void PoseMenuThings(FLRRaceAssets& Assets, FLRModels& Models, FMenuView& View, const lr::MenuScene& Scene, const lr::Menus& Menus,
		const lr::Game& Game, const lr::MenuData& Data, const FVector& At);
	void PoseMenuSprites(FLRRaceAssets& Assets, FMenuView& V, const lr::MenuScene& Scene, const lr::MenuData& Data);
	void ClearThing(FMenuThing& T);
	// What building a thing works on: the assets, the models, the thing, its actor and the scene's thing.
	struct FBuildContext
	{
		FLRRaceAssets& Assets;
		FLRModels& Models;
		FMenuThing& T;
		AActor* Actor;
		const lr::MenuScene::Thing& Thing;
	};
	void BuildThing(const FBuildContext& Ctx, const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game,
		const lr::MenuData& Data);
	void BuildMinifig(const FBuildContext& Ctx, const lr::MenuData& Data);
	void BuildEntity(const FBuildContext& Ctx, const lr::MenuScene& Scene, const lr::MenuData& Data);
	void BuildPart(const FBuildContext& Ctx, const lr::MenuData& Data);
	void BuildMenuCar(const FBuildContext& Ctx, const lr::Menus& Menus, const lr::Game& Game, const lr::MenuData& Data);
	void BuildChassis(const FBuildContext& Ctx, const lr::MenuData& Data);
	void BuildMark(const FBuildContext& Ctx);
	void PoseThing(FLRRaceAssets& Assets, FLRModels& Models, FMenuThing& T, const lr::MenuScene& Scene, const lr::MenuScene::Thing& Thing,
		const lr::Menus& Menus, const FVector& At);
	// A cinematic entity's model table swaps and colour transform.
	void ShowTable(FLRRaceAssets& Assets, FMenuThing& T, const lr::MenuScene::Thing& Thing);
	void Recolour(FLRPlaced& P, const lr::MenuScene::Thing& Thing);
	// A built mesh as one component, a section per material, lit by the view's lights.
	UProceduralMeshComponent* BuiltMesh(FLRRaceAssets& Assets, FLRModels& Models, AActor* Actor, const lr::CarMesh& Mesh,
		const TArray<std::pair<std::string, std::string>>& Libraries, const std::vector<std::string>& Names, float Alpha, bool bBright);
};
