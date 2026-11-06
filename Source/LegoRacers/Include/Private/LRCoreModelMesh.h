#pragma once

#include "CoreMinimal.h"
#include "LRCore/Common/Math.h"
#include "LRData/Archive.h"
#include "LRData/Scene.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Templates/Function.h"

#include <string>
#include <vector>

// A model's sections in UE's space: positions in the model's units (game y flipped) times 30.48, a section per part and
// material, with its material's name and the game's normals (for lighting). A model joining nodes (lr::joinsNodes) has
// one part: each vertex rides its own node, placed in the world as posed.
struct FLRModelMesh
{
	struct FSection
	{
		int32 Part = 0;
		std::string Material;
		TArray<FVector> Positions, Normals;
		TArray<FVector2D> Uvs;
		TArray<FColor> Colours;
		TArray<int32> Triangles;
		TArray<lr::Vec3> GameNormals;
		TArray<int32> VertexNodes; // joined models': each vertex's node
	};
	TArray<FSection> Sections;
	int32 Parts = 1;
	float Scale = 1;
	bool bLit = false;
	bool bJoined = false;
	lr::Sphere Bounds;
	std::vector<std::string> MaterialNames; // the model's material table (what material animation swaps by slot)
};

namespace LRUe
{
// The game's feet and axes in UE's: centimetres, y flipped.
inline constexpr float Cm = 30.48f;
inline FVector Ue(const lr::Vec3& V) { return FVector(V.x, -V.y, V.z) * Cm; }
inline FVector Along(const lr::Vec3& V) { return FVector(V.x, -V.y, V.z); }
// A frame of the game's rows (forward, left, up; scaled) and a place as UE's transform.
inline FTransform FrameOf(const lr::Basis& B, const lr::Vec3& At)
{
	return FTransform(FMatrix(Along(B.f), -Along(B.s), Along(B.u), Ue(At)));
}
// A game file's cache path: a folder, the name upper-cased, an extension (".GDB").
inline std::string AssetPath(const std::string& Folder, std::string_view Name, const char* Extension)
{
	return Folder + lr::upper(Name) + Extension;
}
inline std::string FolderOf(const std::string& Path) { return Path.substr(0, Path.rfind('/') + 1); }

// A plain actor spawned at At with a movable root (what its components attach to).
inline AActor* NewRootedActor(UWorld* World, const FTransform& At)
{
	AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), At);
	USceneComponent* Root = NewObject<USceneComponent>(A, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	A->SetRootComponent(Root);
	Root->RegisterComponent();
	A->SetActorTransform(At);
	return A;
}

// A movable procedural mesh on the actor's root: no shadow, no collision.
inline UProceduralMeshComponent* NewMesh(AActor* Actor)
{
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Actor);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCastShadow(false);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->bUseAsyncCooking = false;
	C->SetupAttachment(Actor->GetRootComponent());
	C->RegisterComponent();
	Actor->AddInstanceComponent(C);
	return C;
}

// A render target cleared to black.
inline UTextureRenderTarget2D* NewTarget(UObject* Outer, int32 Width, int32 Height, ETextureRenderTargetFormat Format = RTF_RGBA8_SRGB)
{
	UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>(Outer);
	T->RenderTargetFormat = Format;
	T->ClearColor = FLinearColor::Black;
	T->InitAutoFormat(Width, Height);
	T->UpdateResourceImmediate(true);
	return T;
}

// A transient BGRA texture whose top mip Fill writes (width x height x 4 bytes), uploaded.
inline UTexture2D* NewTexture(int32 Width, int32 Height, TextureFilter Filter, TextureAddress Address, TFunctionRef<void(uint8* Bgra)> Fill)
{
	UTexture2D* T = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	T->SRGB = true;
	T->Filter = Filter;
	T->AddressX = T->AddressY = Address;
	T->NeverStream = true;
	Fill(static_cast<uint8*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE)));
	T->GetPlatformData()->Mips[0].BulkData.Unlock();
	T->UpdateResource();
	return T;
}

// The material libraries a world names, with their texture lists, as cache paths.
inline TArray<std::pair<std::string, std::string>> LibrariesOf(const lr::SceneFile& Scene, const std::string& Folder)
{
	TArray<std::pair<std::string, std::string>> Out;
	for (size_t i = 0; i < Scene.materialLibraries.size(); i++) {
		const std::string Mdb = Folder + lr::upper(Scene.materialLibraries[i]) + ".MDB";
		const std::string Tdb = Folder + lr::upper(i < Scene.textureLists.size() ? Scene.textureLists[i] : Scene.materialLibraries[i]) +
			".TDB";
		Out.Add({Mdb, Tdb});
	}
	return Out;
}
} // namespace LRUe
