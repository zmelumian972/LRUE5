// The race view's effects: particles, billboards, sprites and decals, batched by material per view.

#include "LRCoreRace.h"
#include "LRCoreLog.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Show/ShowState.h"
#include "LRCore/Race/World.h"
#include "LRData/Archive.h"
#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Look.h"
#include "LRData/Model.h"
#include "LRData/Pieces.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

#include <algorithm>
#include <cmath>

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

// ---- A view's effects: particles facing its camera, decals (its shadows' alpha) ----

// The effects' triangles of one view, batched by material.
struct ULRRaceView::FEffects
{
	struct FBatch
	{
		UMaterialInterface* Material;
		TArray<FVector> P;
		TArray<FVector2D> Uv;
		TArray<FColor> Colour;
		TArray<int32> T;
	};
	TArray<FBatch> Batches;
	lr::Vec3 Right, Down, Look, Eye; // the camera's rows and eye, as shown
	TArray<std::pair<std::string, std::string>> Shared; // the effects' and the power-ups' libraries

	FBatch& Of(UMaterialInterface* M)
	{
		for (FBatch& B : Batches) {
			if (B.Material == M) {
				return B;
			}
		}
		FBatch& B = Batches.AddDefaulted_GetRef();
		B.Material = M;
		return B;
	}
	// A quad facing the camera, centred at At, both sides; uvs 0.01..0.98.
	void Quad(UMaterialInterface* M, const lr::Vec3& At, float Width, float Height, bool bFlip, FColor Colour)
	{
		FBatch& B = Of(M);
		const int32 First = B.P.Num();
		const lr::Vec3 X = Right * (Width * 0.5f), Y = Down * (Height * 0.5f);
		const lr::Vec3 Corners[4] = {At - X - Y, At + X - Y, At + X + Y, At - X + Y};
		const FVector2D Uvs[4] = {{0.01, 0.01}, {0.98, 0.01}, {0.98, 0.98}, {0.01, 0.98}};
		for (int32 k = 0; k < 4; k++) {
			B.P.Add(Ue(Corners[k]));
			B.Uv.Add(bFlip ? FVector2D(Uvs[k].X, 1 - Uvs[k].Y) : Uvs[k]);
			B.Colour.Add(Colour);
		}
		for (const int32 k : {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2}) {
			B.T.Add(First + k);
		}
	}
};

void ULRRaceView::BuildEffects(int32 v, const lr::World& W, const lr::Show& Show)
{
	FView& V = Views[v];
	if (!V.Effects.IsValid()) {
		AActor* A = NewActor(FTransform::Identity);
		UProceduralMeshComponent* C = NewMesh(A);
		C->SetReceivesDecals(false);
		V.Effects = C;
	}
	FEffects E;
	const lr::PlayerCamera& Cam = W.raceView.cameras[size_t(v)];
	Cam.shownRows(E.Right, E.Down, E.Look);
	E.Eye = Cam.shownEye();
	E.Shared = CommonLibraries;
	E.Shared.Append(PowerupLibraries);
	AddParticles(E, Show);
	AddBillboards(E, W);
	AddSprites(E, W);
	AddDecals(E, v, Show);
	UProceduralMeshComponent* C = V.Effects.Get();
	C->ClearAllMeshSections();
	for (int32 b = 0; b < E.Batches.Num(); b++) {
		C->CreateMeshSection(b, E.Batches[b].P, E.Batches[b].T, {}, E.Batches[b].Uv, E.Batches[b].Colour, {}, false);
		C->SetMaterial(b, E.Batches[b].Material);
	}
}

// Particles: a quad each, along the camera's rows, coloured by the material's diffuse.
void ULRRaceView::AddParticles(FEffects& E, const lr::Show& Show)
{
	TArray<std::pair<std::string, std::string>> TrackSet;
	for (const std::string& Mdb : EmitterLibraries[1]) {
		TrackSet.Add({Mdb, Mdb.substr(0, Mdb.size() - 3) + "TDB"});
	}
	for (size_t s = 0; s < 2; s++) {
		const lr::EffectSet& Set = Show.sets[s];
		for (const lr::EffectSlot& Slot : Set.slots) {
			if (!Slot.active || Slot.look < 0) {
				continue;
			}
			const float Radius = Set.file.emitters[size_t(Slot.look)].radius;
			if (Radius > 0 && Slot.anchored && lr::lengthSquared(E.Eye - Slot.anchor) > Radius * Radius) {
				continue;
			}
			for (const lr::ShowParticle& Pt : Slot.particles) {
				float Width, Height;
				std::string Material;
				if (!Show.particleLook(s, Slot, Pt, Width, Height, Material)) {
					continue;
				}
				bool bFlip;
				uint8 D[4];
				UMaterialInstanceDynamic* Mid = MaterialFor(Material, s == 0 ? CommonLibraries : TrackSet, 1, bFlip, D, true);
				E.Quad(Mid, Pt.position, Width, Height, bFlip, FColor(D[0], D[1], D[2], D[3]));
			}
		}
	}
}

// Billboards: the normal explosions' flashes, the cannonballs.
void ULRRaceView::AddBillboards(FEffects& E, const lr::World& W)
{
	const auto Billboard = [&](const lr::Vec3& At, float Width, float Height, const std::string& Material, uint8 Alpha) {
		bool bFlip;
		uint8 D[4];
		UMaterialInstanceDynamic* Mid = MaterialFor(Material, E.Shared, 1, bFlip, D, true);
		E.Quad(Mid, At, Width, Height, bFlip, FColor(D[0], D[1], D[2], uint8((int32(D[3]) * Alpha) / 255)));
	};
	for (size_t e = 0; e < 6 && e < W.powerUps.explosions.size(); e++) {
		const lr::Explosion& X = W.powerUps.explosions[e];
		if (X.state == lr::Explosion::Exploding) {
			const float G = X.growth;
			Billboard(X.centre, (15 - 0.1f) * G + 0.1f, (15 - 0.2f) * G + 0.2f, "exflash", uint8(FMath::Clamp((2 - 2 * G) * 255, 0.0f, 255.0f)));
		}
	}
	for (const uint8 Index : W.powerUps.actionList) {
		const lr::Action& A = W.powerUps.actions[Index];
		if (A.kind == lr::Action::Cannon && A.shot.state == lr::Projectile::Flying) {
			Billboard(A.shot.at, 5, 5, "cannon", 255);
		}
	}
}

// The world's sprites: in reach of this eye, turned to this camera, masked and depth-tested like the
// scenery.
void ULRRaceView::AddSprites(FEffects& E, const lr::World& W)
{
	for (const lr::Sprite& S : W.track.sprites) {
		if (!S.inReach(E.Eye)) {
			continue;
		}
		const std::string* Name = &S.material;
		if (S.track >= 0) {
			const lr::Track::MaterialTrack& Frames = W.track.materialTracks[size_t(S.track)];
			const int16 Shown = W.raceView.materials[size_t(S.track)].shown;
			if (Shown < 0 || size_t(Shown) >= Frames.frames.size()) {
				continue;
			}
			Name = &Frames.frames[size_t(Shown)].first;
		}
		FSpriteLook& Look_ = SpriteLooks.FindOrAdd(UTF8_TO_TCHAR(Name->c_str()));
		if (!Look_.Material) {
			uint8 D[4];
			Look_.Material = MaterialFor(*Name, TrackLibraries, 1, Look_.bFlip, D);
		}
		FEffects::FBatch& B = E.Of(Look_.Material);
		AddSprite(S.corners(E.Right, E.Down, E.Look), Look_.bFlip, B.P, B.Uv, B.Colour, B.T);
	}
}

// A decal's material: a racer's shadow on its own texture, else the named material pulled toward the eye.
UMaterialInterface* ULRRaceView::DecalMaterial(const lr::Decal& D, const TArray<std::pair<std::string, std::string>>& Shared, bool& bFlip)
{
	bFlip = false;
	if (D.kind == lr::Decal::Shadow) {
		const FString Key = FString::Printf(TEXT("shadow%d"), D.owner);
		TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key);
		if (!Found) {
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(TranslucentBase, this);
			Mid->SetTextureParameterValue(TEXT("Tex"), Racers[D.owner].Shadow.Get());
			Mid->SetScalarParameterValue(TEXT("Alpha"), 180.0f / 255);
			Mid->SetScalarParameterValue(TEXT("Pull"), 0.005f);
			Found = &Materials.Add(Key, Mid);
		}
		return *Found;
	}
	uint8 Dif[4];
	UMaterialInstanceDynamic* Mid = MaterialFor(D.material, Shared, 1, bFlip, Dif, true);
	const FString Key = FString::Printf(TEXT("decal:%hs"), D.material.c_str());
	TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key);
	if (!Found) {
		// (A copy of its instance, pulled toward the eye.)
		UMaterialInstanceDynamic* Pulled = UMaterialInstanceDynamic::Create(Mid->Parent, this);
		UTexture* Tex = nullptr;
		if (Mid->GetTextureParameterValue(TEXT("Tex"), Tex) && Tex) {
			Pulled->SetTextureParameterValue(TEXT("Tex"), Tex);
		}
		float A = 1;
		Mid->GetScalarParameterValue(TEXT("Alpha"), A);
		Pulled->SetScalarParameterValue(TEXT("Alpha"), A);
		Pulled->SetScalarParameterValue(TEXT("Pull"), 0.005f);
		Found = &Materials.Add(Key, Pulled);
	}
	return *Found;
}

// Decals: projected triangles, white times their alpha (a shadow's by this camera's distance).
void ULRRaceView::AddDecals(FEffects& E, int32 v, const lr::Show& Show)
{
	for (const lr::Decal& D : Show.decals) {
		uint8 Alpha = D.alpha;
		if (D.kind == lr::Decal::Shadow) {
			if (!Racers.IsValidIndex(D.owner) || !Racers[D.owner].Shadow.IsValid()) {
				continue;
			}
			Alpha = Show.shadowAlpha[size_t(D.owner)][size_t(v)];
		}
		bool bFlip;
		UMaterialInterface* M = DecalMaterial(D, E.Shared, bFlip);
		if (Alpha == 0 || D.points.empty()) {
			continue;
		}
		FEffects::FBatch& B = E.Of(M);
		for (size_t k = 0; k + 2 < D.points.size(); k += 3) {
			const int32 First = B.P.Num();
			for (size_t c = 0; c < 3; c++) {
				B.P.Add(Ue(D.points[k + c]));
				B.Uv.Add(FVector2D(D.uvs[k + c][0], bFlip ? 1 - D.uvs[k + c][1] : D.uvs[k + c][1]));
				B.Colour.Add(FColor(255, 255, 255, Alpha));
			}
			for (const int32 i : {0, 1, 2, 0, 2, 1}) {
				B.T.Add(First + i);
			}
		}
	}
}

void ULRRaceView::AddSprite(const std::array<lr::Vec3, 4>& Corners, bool bFlip, TArray<FVector>& P, TArray<FVector2D>& Uv, TArray<FColor>& Colour, TArray<int32>& T)
{
	const int32 First = P.Num();
	const FVector2D Uvs[4] = {{0.01, 0.01}, {0.99, 0.01}, {0.99, 0.99}, {0.01, 0.99}}; // (inside the wrap's bleed)
	for (int32 k = 0; k < 4; k++) {
		P.Add(Ue(Corners[size_t(k)]));
		Uv.Add(bFlip ? FVector2D(Uvs[k].X, 1 - Uvs[k].Y) : Uvs[k]);
		Colour.Add(FColor::White);
	}
	for (const int32 k : {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2}) {
		T.Add(First + k);
	}
}
