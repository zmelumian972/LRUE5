#pragma once

// The race view's racers (LRCoreRaceRacers.cpp): each racer's members on their
// skeletons (body, variant, car, driver; a player's built car and driver), its shadow texture, a time race's ghosts.

#include "UObject/ObjectPtr.h"
#include "LRCoreRaceModels.h"

#include <cstdint>

class FReferenceCollector;
class UTexture2D;

namespace lr
{
struct World;
struct MenuData;
struct PieceLibrary;
struct CarPiece;
} // namespace lr

struct FLRRaceRacers
{
	void Build(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W, const lr::MenuData& Data);
	void Pose(FLRModels& Models, const lr::World& W);
	// Racer i's shadow texture (none: null).
	UTexture2D* ShadowOf(int32 i) const;
	void Clear();
	void AddReferencedObjects(FReferenceCollector& Collector);

private:
	// The racers: each member (body, variant, car, driver), its ghost copies.
	struct FRacer
	{
		FLRPlaced Members[4]; // (a built driver's: its head, on the face node)
		TArray<TWeakObjectPtr<UProceduralMeshComponent>> Built; // a built car's body (lit once)
		TArray<FLRSkin> Driver; // a built driver's body
		TWeakObjectPtr<UTexture2D> Shadow;
	};
	TArray<FRacer> Racers;
	FLRPlaced Ghosts[2][4];
	TArray<FLRSkin> GhostDrivers[2];
	TArray<TObjectPtr<UTexture2D>> Shadows;

	UProceduralMeshComponent* BuiltCar(FLRRaceAssets& Assets, AActor* Kart, const std::vector<lr::CarPiece>& Pieces,
		const lr::PieceLibrary& Low, const lr::MenuData& Data);
	// A player's built driver from its looks: the body skinned, the head as a placed model.
	void BuildRaceDriver(FLRRaceAssets& Assets, FLRModels& Models, AActor* Actor, const std::array<uint8_t, 4>& Looks,
		const lr::MenuData& Data, float Alpha, TArray<FLRSkin>& Body, FLRPlaced& Head);
	// Its body on the pelvis skeleton's nodes (placed: the core's figure nodes), its head on the head node.
	void PoseRaceDriver(FLRModels& Models, TArray<FLRSkin>& Body, FLRPlaced& Head, const std::vector<lr::NodeMatrix>& PelvisNodes);
	void PoseMember(FLRModels& Models, int32 i, int32 m, const lr::World& W);
	void PoseGhosts(FLRModels& Models, const lr::World& W);
	// Racer's shadow texture at Size (its chassis' shadow, ft).
	void MakeShadow(int32 Racer, FVector2f Size, const lr::World& W);
};
