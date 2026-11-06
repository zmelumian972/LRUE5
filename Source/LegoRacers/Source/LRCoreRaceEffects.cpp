// The race view's effects (LRCoreRaceEffects.h): particles, billboards, sprites and decals, batched by material per view
//.

#include "LRCoreRaceEffects.h"
#include "LRCoreRaceAssets.h"
#include "LRCoreRaceRacers.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "LRCore/Race/World.h"
#include "LRCore/Show/ShowState.h"
#include "LRData/Cache.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

// ---- A view's effects: particles facing its camera, decals (its shadows' alpha) ----

// The effects' triangles of one view, batched by material.
struct FLRRaceEffects::FBatches
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

void FLRRaceEffects::Build(FLRRaceAssets& Assets, FLRModels& Models, const FLRRaceRacers& Racers, int32 v, const lr::World& W,
	const lr::Show& Show)
{
	if (Meshes.Num() <= v) {
		Meshes.SetNum(v + 1);
	}
	if (!Meshes[v].IsValid()) {
		AActor* A = Models.NewActor(FTransform::Identity);
		UProceduralMeshComponent* C = NewMesh(A);
		C->SetReceivesDecals(false);
		Meshes[v] = C;
	}
	FBatches E;
	const lr::PlayerCamera& Cam = W.raceView.cameras[size_t(v)];
	Cam.shownRows(E.Right, E.Down, E.Look);
	E.Eye = Cam.shownEye();
	AddParticles(Assets, E, Show);
	AddBillboards(Assets, E, W);
	AddSprites(Assets, E, W);
	AddDecals(Assets, Racers, E, v, Show);
	UProceduralMeshComponent* C = Meshes[v].Get();
	for (int32 b = 0; b < E.Batches.Num(); b++) {
		const FBatches::FBatch& B = E.Batches[b];
		// A batch with last frame's triangles over as many vertices only moves its vertices; any other is made again.
		const FProcMeshSection* Had = C->GetProcMeshSection(b);
		if (Had && B.P.Num() && Had->ProcVertexBuffer.Num() == B.P.Num() && Had->ProcIndexBuffer.Num() == B.T.Num() &&
			FMemory::Memcmp(Had->ProcIndexBuffer.GetData(), B.T.GetData(), B.T.Num() * sizeof(int32)) == 0) {
			C->UpdateMeshSection(b, B.P, {}, B.Uv, B.Colour, {});
		}
		else {
			C->CreateMeshSection(b, B.P, B.T, {}, B.Uv, B.Colour, {}, false);
		}
		C->SetMaterial(b, B.Material);
	}
	// The sections no batch fills now are emptied (an empty section draws nothing).
	for (int32 b = E.Batches.Num(); b < C->GetNumSections(); b++) {
		if (C->GetProcMeshSection(b)->ProcVertexBuffer.Num()) {
			C->ClearMeshSection(b);
		}
	}
}

// Particles: a quad each, along the camera's rows, coloured by the material's diffuse.
void FLRRaceEffects::AddParticles(FLRRaceAssets& Assets, FBatches& E, const lr::Show& Show)
{
	for (size_t s = 0; s < 2; s++) {
		const lr::EffectSet& Set = Show.effects.sets[s];
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
				if (!Show.effects.particleLook(s, Slot, Pt, Width, Height, Material)) {
					continue;
				}
				const FMaterialLook& L = LookOf(Assets, Material, s == 0 ? Assets.CommonLibraries : Assets.EmitterLibraries, true);
				E.Quad(L.Material, Pt.position, Width, Height, L.bFlip, FColor(L.Diffuse[0], L.Diffuse[1], L.Diffuse[2], L.Diffuse[3]));
			}
		}
	}
}

// Billboards: the normal explosions' flashes, the cannonballs.
void FLRRaceEffects::AddBillboards(FLRRaceAssets& Assets, FBatches& E, const lr::World& W)
{
	const auto Billboard = [&](const lr::Vec3& At, float Width, float Height, const std::string& Material, uint8 Alpha) {
		const FMaterialLook& L = LookOf(Assets, Material, Assets.SharedLibraries, true);
		E.Quad(L.Material, At, Width, Height, L.bFlip,
			FColor(L.Diffuse[0], L.Diffuse[1], L.Diffuse[2], uint8((int32(L.Diffuse[3]) * Alpha) / 255)));
	};
	for (size_t e = 0; e < 6 && e < W.powerUps.explosions.size(); e++) {
		const lr::Explosion& X = W.powerUps.explosions[e];
		if (X.state == lr::Explosion::Exploding) {
			const float G = X.growth;
			Billboard(X.centre, (15 - 0.1f) * G + 0.1f, (15 - 0.2f) * G + 0.2f, "exflash",
				uint8(FMath::Clamp((2 - 2 * G) * 255, 0.0f, 255.0f)));
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
void FLRRaceEffects::AddSprites(FLRRaceAssets& Assets, FBatches& E, const lr::World& W)
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
		const FMaterialLook& L = LookOf(Assets, *Name, Assets.TrackLibraries, false);
		FBatches::FBatch& B = E.Of(L.Material);
		AddSprite(S.corners(E.Right, E.Down, E.Look), L.bFlip, B.P, B.Uv, B.Colour, B.T);
	}
}

// A decal's material: a racer's shadow on its own texture, else the named material pulled toward the eye; found once a
// race.
UMaterialInterface* FLRRaceEffects::DecalMaterial(FLRRaceAssets& Assets, const FLRRaceRacers& Racers, const lr::Decal& D, bool& bFlip)
{
	bFlip = false;
	if (D.kind == lr::Decal::Shadow) {
		if (ShadowLooks.Num() <= D.owner) {
			ShadowLooks.SetNumZeroed(D.owner + 1);
		}
		UMaterialInterface*& Shadow = ShadowLooks[D.owner];
		if (Shadow) {
			return Shadow;
		}
		TObjectPtr<UMaterialInstanceDynamic>& Mid = Assets.Materials.FindOrAdd(FString::Printf(TEXT("shadow%d"), D.owner));
		if (!Mid) {
			Mid = UMaterialInstanceDynamic::Create(Assets.TranslucentBase, Assets.Outer);
			Mid->SetScalarParameterValue(TEXT("Alpha"), 180.0f / 255);
			Mid->SetScalarParameterValue(TEXT("Pull"), 0.005f);
		}
		// (The material outlives the race; its texture is this race's racer's, set once a race.)
		Mid->SetTextureParameterValue(TEXT("Tex"), Racers.ShadowOf(D.owner));
		Shadow = Mid;
		return Shadow;
	}
	FMaterialLook& L = LookOf(Assets, D.material, Assets.SharedLibraries, true);
	bFlip = L.bFlip;
	if (L.Decal) {
		return L.Decal;
	}
	UMaterialInstanceDynamic* Mid = L.Material;
	TObjectPtr<UMaterialInstanceDynamic>& Pulled = Assets.Materials.FindOrAdd(FString::Printf(TEXT("decal:%hs"), D.material.c_str()));
	if (!Pulled) {
		// (A copy of its instance, pulled toward the eye.)
		Pulled = UMaterialInstanceDynamic::Create(Mid->Parent, Assets.Outer);
		UTexture* Tex = nullptr;
		if (Mid->GetTextureParameterValue(TEXT("Tex"), Tex) && Tex) {
			Pulled->SetTextureParameterValue(TEXT("Tex"), Tex);
		}
		float A = 1;
		Mid->GetScalarParameterValue(TEXT("Alpha"), A);
		Pulled->SetScalarParameterValue(TEXT("Alpha"), A);
		Pulled->SetScalarParameterValue(TEXT("Pull"), 0.005f);
	}
	L.Decal = Pulled;
	return L.Decal;
}

// Decals: projected triangles, white times their alpha (a shadow's by this camera's distance).
void FLRRaceEffects::AddDecals(FLRRaceAssets& Assets, const FLRRaceRacers& Racers, FBatches& E, int32 v, const lr::Show& Show)
{
	for (const lr::Decal& D : Show.decals.shown) {
		uint8 Alpha = D.alpha;
		if (D.kind == lr::Decal::Shadow) {
			if (!Racers.ShadowOf(D.owner)) {
				continue;
			}
			Alpha = Show.decals.shadowAlpha[size_t(D.owner)][size_t(v)];
		}
		bool bFlip;
		UMaterialInterface* M = DecalMaterial(Assets, Racers, D, bFlip);
		if (Alpha == 0 || D.points.empty()) {
			continue;
		}
		FBatches::FBatch& B = E.Of(M);
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

void FLRRaceEffects::Prepare()
{
	Looks.Reset();
	LastLook = 0;
	ShadowLooks.Reset();
}

// Name's look in Libraries, the last one asked for tried first (a slot's particles share theirs): no key built, no hash.
FLRRaceEffects::FMaterialLook& FLRRaceEffects::LookOf(FLRRaceAssets& Assets, const std::string& Name,
	const TArray<std::pair<std::string, std::string>>& Libraries, bool bTranslucent)
{
	const auto Is = [&](const FMaterialLook& L) { return L.Libraries == &Libraries && L.bTranslucent == bTranslucent && L.Name == Name; };
	if (Looks.IsValidIndex(LastLook) && Is(Looks[LastLook])) {
		return Looks[LastLook];
	}
	LastLook = Looks.IndexOfByPredicate(Is);
	if (LastLook == INDEX_NONE) {
		LastLook = Looks.AddDefaulted();
		FMaterialLook& L = Looks[LastLook];
		L.Name = Name;
		L.Libraries = &Libraries;
		L.bTranslucent = bTranslucent;
		L.Material = Assets.MaterialFor(Name, Libraries, 1, L.bFlip, L.Diffuse, bTranslucent);
	}
	return Looks[LastLook];
}

void FLRRaceEffects::HideView(USceneCaptureComponent2D* Capture, int32 v) const
{
	if (Meshes.IsValidIndex(v) && Meshes[v].IsValid()) {
		Capture->HiddenComponents.Add(Meshes[v]);
	}
}

void FLRRaceEffects::Clear() { Meshes.Reset(); }
