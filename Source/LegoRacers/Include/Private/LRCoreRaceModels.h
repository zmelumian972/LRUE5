#pragma once

// The race view's models as components (LRCoreRaceModels.cpp): the race's actors, the
// lights lit models are lit with, models placed, posed per part or per vertex and relit.

#include "CoreMinimal.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Show/View.h"
#include "LRCoreModelMesh.h"
#include "LRData/Model.h"
#include "Templates/Function.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

class AActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneCaptureComponent2D;
class UWorld;
struct FLRRaceAssets;

// A model drawn somewhere: its components (one per part) and how they're posed.
struct FLRPlaced
{
	TWeakObjectPtr<AActor> Actor;
	TArray<TWeakObjectPtr<UProceduralMeshComponent>> Parts; // by part (node); null where the model has none
	const FLRModelMesh* Mesh = nullptr;
	bool bLit = false;
	TArray<FColor> Diffuse, Ambient; // each section's material's (lighting)
};

// A model posed per vertex (each vertex on the node current at its load), skinned on the CPU: one
// component, a section per material, its vertices placed in the world every frame.
struct FLRSkin
{
	TWeakObjectPtr<UProceduralMeshComponent> Component;
	lr::Model Model;
	struct FSection
	{
		TArray<int32> Vertices; // the model's, per section vertex
		FColor Diffuse, Ambient;
		std::string Material; // the model's material it draws (what a material track's slot swaps)
	};
	TArray<FSection> Sections;
	// A cinematic colour effect's transform of its prelit colours.
	bool bColoured = false;
	int32 Shift[3] = {}, Offset[3] = {};
	// A race's built driver: each section's colours, lit once at build; none: lit as posed.
	TArray<TArray<FColor>> Prelit;
};

struct FLRModels
{
	// The world the race's actors are spawned in, and those actors (destroyed by DestroyActors).
	TWeakObjectPtr<UWorld> WorldRef;
	TArray<TWeakObjectPtr<AActor>> Actors;
	AActor* NewActor(const FTransform& At);
	void DestroyActors();

	// The light lit models are lit with: the race's, or a menu scene's.
	int32 LitAmbient[3] = {60, 60, 60}, LitColour[3] = {255, 255, 255};
	lr::Vec3 LitDirection = lr::normalize({-0.825175881f, -0.278099984f, -0.491675943f});
	// More lights (a menu world's or a cinematic's, up to 7 in all; 0 for the race), and a vertex lit by them all.
	int32 LitMore = 0;
	lr::Vec3 LitMoreDirections[6];
	int32 LitMoreColours[6][3] = {};
	FColor LitVertex(const lr::Vec3& N, const uint8* Diffuse, const uint8* Ambient, int32 Boost = 0) const;
	void LightRace();
	void LightMenu(const lr::MenuLighting& L);

	FLRPlaced Place(FLRRaceAssets& Assets, AActor* Actor, const FString& Path, const TArray<std::pair<std::string, std::string>>& Libraries,
		bool bLit, bool bMirrored = false, float Alpha = 1);
	FLRPlaced PlaceMesh(FLRRaceAssets& Assets, AActor* Actor, const FLRModelMesh* Mesh,
		const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit, float Alpha,
		const TMap<FString, UMaterialInterface*>* Overrides);
	void PoseNodes(FLRPlaced& P, const TArray<FTransform>& Nodes, const FTransform& Whole);
	void Relight(FLRPlaced& P, const TArray<FQuat>& Rotations);
	// Posed (PoseNodes) and relit by its nodes' rotations (none: Whole's); all of it on one transform (a head on its node).
	void PoseLit(FLRPlaced& P, const TArray<FTransform>& Nodes, const FTransform& Whole);
	void PoseOn(FLRPlaced& P, const FTransform& At);
	static void ForSections(FLRPlaced& P, const std::string* Only,
		TFunctionRef<void(UProceduralMeshComponent*, int32, const FLRModelMesh::FSection&)> Do);
	FLRSkin BuildSkin(FLRRaceAssets& Assets, AActor* Actor, const lr::Model& Model,
		const TArray<std::pair<std::string, std::string>>& Libraries, const TMap<FString, UMaterialInterface*>* Overrides, bool bFaceless,
		float Alpha = 1);
	void PoseSkin(FLRSkin& S, const std::vector<lr::NodeMatrix>& Nodes, const FVector& At);

private:
	// Per-frame scratch (posed vertices, relit colours), kept so a frame allocates nothing for them.
	TArray<FVector> ScratchPositions;
	TArray<FColor> ScratchColours;
	TArray<FQuat> ScratchRotations;
	TArray<FTransform> ScratchOn;
};

namespace LRUe
{
// Skeleton nodes as UE transforms (then moved by Offset).
void FramesOf(const std::vector<lr::NodeMatrix>& Nodes, TArray<FTransform>& Out, const FTransform& Offset = FTransform::Identity);
// Each valid component shown or hidden.
void SetVisible(const TArray<TWeakObjectPtr<UProceduralMeshComponent>>& Parts, bool bVisible);
// Each valid component left out of a capture.
void HideParts(USceneCaptureComponent2D* Capture, const TArray<TWeakObjectPtr<UProceduralMeshComponent>>& Parts);
// A sprite's quad facing the camera at its corners, both sides; uvs inside the wrap's bleed.
void AddSprite(const std::array<lr::Vec3, 4>& Corners, bool bFlip, TArray<FVector>& P, TArray<FVector2D>& Uv, TArray<FColor>& Colour,
	TArray<int32>& T);
// A colour effect's transform of a prelit colour.
FColor Transformed(const FColor& C, const int32 Shift[3], const int32 Offset[3]);
} // namespace LRUe
