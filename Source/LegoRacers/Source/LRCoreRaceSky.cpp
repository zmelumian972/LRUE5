// The race view's sky (LRCoreRaceSky.h): the dome and the sky's world per view, posed from the show.

#include "LRCoreRaceSky.h"
#include "LRCoreRaceAssets.h"
#include "LRCoreLog.h"

#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Actor.h"
#include "LRCore/Race/World.h"
#include "LRCore/Show/ShowState.h"
#include "LRData/Cache.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"
#include "ProceduralMeshComponent.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

namespace
{
// The dome: radius 100, 11 segments, rings at the horizon, 65 degrees from the zenith and about 33 (no top cap).
constexpr int32 SkySegments = 11;
constexpr float SkyRings[3] = {0, 41.5f, 84};

void SkyGeometry(TArray<FVector>& Positions, TArray<int32>& Triangles)
{
	for (int32 r = 0; r < 3; r++) {
		const float Radius = FMath::Sqrt(100 * 100 - SkyRings[r] * SkyRings[r]);
		for (int32 s = 0; s < SkySegments; s++) {
			const float A = 2 * PI * float(s) / SkySegments;
			Positions.Add(FVector(FMath::Cos(A) * Radius, -FMath::Sin(A) * Radius, SkyRings[r]) * Cm);
		}
	}
	for (int32 r = 0; r < 2; r++) {
		for (int32 s = 0; s < SkySegments; s++) {
			const int32 A0 = r * SkySegments + s, A1 = r * SkySegments + (s + 1) % SkySegments;
			const int32 B0 = A0 + SkySegments, B1 = A1 + SkySegments;
			// Both faces (seen from inside).
			for (const int32 V : {A0, A1, B1, A0, B1, B0, A0, B1, A1, A0, B0, B1}) {
				Triangles.Add(V);
			}
		}
	}
}
} // namespace

void FLRRaceSky::Build(FLRRaceAssets& Assets, const lr::World& W)
{
	Track = UTF8_TO_TCHAR(W.session.brief.track.c_str());
	bMirrored = W.track.mirrored;
	// The sky's libraries: its world's (the sky material: RACERFX's skymat).
	lr::RaceFile Race;
	const std::string Dir = "GAMEDATA/" + std::string(TCHAR_TO_UTF8(*Track)) + "/";
	WarnUnless(Assets.Cache->get(Dir + std::string(TCHAR_TO_UTF8(*Track)) + ".RAB", Race), "the race's .RAB");
	lr::SceneFile SkyWorld;
	if (!Race.skyWorld.empty() && Assets.Cache->size(Dir + lr::binaryName(Race.skyWorld)) &&
		Assets.Cache->get(Dir + lr::binaryName(Race.skyWorld), SkyWorld)) {
		SkyLibraries = LibrariesOf(SkyWorld, Dir);
	}
	// (Made per view in Pose: each view's sky is re-centred on its eye.)
}

void FLRRaceSky::Pose(FLRRaceAssets& Assets, FLRModels& Models, int32 v, const lr::World& W, const lr::Show& Show)
{
	if (Views.Num() <= v) {
		Views.SetNum(v + 1);
	}
	FView& V = Views[v];
	const lr::PlayerCamera& C = W.raceView.cameras[size_t(v)];
	if (!V.Sky.IsValid()) {
		MakeSky(Assets, Models, V);
	}
	// Drawn behind everything: the whole sky scaled 1000 times about the eye; the dome 10 below it plus
	// the height offset.
	// Each view's dome and sky world around its own eye (split screen has no sky quirk).
	const FVector Eye = Ue(C.shownEye());
	V.Sky->SetActorTransform(FTransform(FQuat::Identity, Eye, FVector(1000)));
	V.Sky->SetActorHiddenInGame(!Show.sky.loaded);
	if (V.SkyDome.Num() && V.SkyDome[0].IsValid()) {
		UProceduralMeshComponent* Dome = V.SkyDome[0].Get();
		Dome->SetRelativeLocation(FVector(0, 0, (-10 + Show.sky.file.height) * Cm));
		Dome->SetVisibility(Show.sky.domeShown);
		ScratchColours.Reset();
		for (int32 r = 0; r < 3; r++) {
			const std::array<float, 3>& K = Show.sky.colours[size_t(r)];
			for (int32 s = 0; s < SkySegments; s++) {
				ScratchColours.Add(FColor(uint8(K[0] + 0.5f), uint8(K[1] + 0.5f), uint8(K[2] + 0.5f), 255));
			}
		}
		const FProcMeshSection* Sec = Dome->GetProcMeshSection(0);
		if (Sec) {
			ScratchPositions.Reset();
			for (const FProcMeshVertex& Pv : Sec->ProcVertexBuffer) {
				ScratchPositions.Add(Pv.Position);
			}
			Dome->UpdateMeshSection(0, ScratchPositions, {}, {}, ScratchColours, {});
		}
		for (int32 k = 1; k < V.SkyDome.Num(); k++) {
			if (V.SkyDome[k].IsValid()) {
				V.SkyDome[k]->SetVisibility(Show.sky.worldShown);
			}
		}
	}
}

// A view's sky: the dome, then the sky's world, its models at the eye, 30 up.
void FLRRaceSky::MakeSky(FLRRaceAssets& Assets, FLRModels& Models, FView& V)
{
	AActor* A = Models.NewActor(FTransform::Identity);
	V.Sky = A;
	UProceduralMeshComponent* Dome = NewMesh(A);
	TArray<FVector> Positions;
	TArray<int32> Triangles;
	SkyGeometry(Positions, Triangles);
	TArray<FColor> Colours;
	Colours.Init(FColor::Black, Positions.Num());
	Dome->CreateMeshSection(0, Positions, Triangles, {}, {}, Colours, {}, false);
	bool bFlip;
	uint8 D[4];
	Dome->SetMaterial(0, Assets.MaterialFor("skymat", Assets.CommonLibraries, 1, bFlip, D));
	V.SkyDome.Add(Dome);
	AddSkyWorld(Assets, Models, V, A);
}

void FLRRaceSky::AddSkyWorld(FLRRaceAssets& Assets, FLRModels& Models, FView& V, AActor* A)
{
	lr::RaceFile Race;
	const std::string Dir = "GAMEDATA/" + std::string(TCHAR_TO_UTF8(*Track)) + "/";
	lr::SceneFile SkyWorld;
	if (Assets.Cache->get(Dir + std::string(TCHAR_TO_UTF8(*Track)) + ".RAB", Race) && !Race.skyWorld.empty() &&
		Assets.Cache->size(Dir + lr::binaryName(Race.skyWorld)) && Assets.Cache->get(Dir + lr::binaryName(Race.skyWorld), SkyWorld)) {
		TArray<bool> Placed;
		Placed.Init(false, int32(SkyWorld.models.size()));
		const auto Add = [&](size_t m, const lr::Pose& Pose) {
			FLRPlaced P = Models.Place(Assets, A, ModelPath(*Assets.Cache, SkyWorld.models[m], Dir), SkyLibraries, false, bMirrored);
			if (!P.Mesh) {
				return;
			}
			lr::Pose At = Pose;
			At.position.z += 30;
			for (TWeakObjectPtr<UProceduralMeshComponent>& Part : P.Parts) {
				if (Part.IsValid()) {
					Part->SetRelativeTransform(FrameOf(At.rotation * P.Mesh->Scale, At.position));
					V.SkyDome.Add(Part);
				}
			}
		};
		for (const lr::SceneFile::Entity& E : SkyWorld.entities) {
			if (E.model >= 0 && size_t(E.model) < SkyWorld.models.size()) {
				Add(size_t(E.model), EntityPose(E, bMirrored));
				Placed[E.model] = true;
			}
		}
		for (size_t m = 0; m < SkyWorld.models.size(); m++) {
			if (!Placed[int32(m)]) {
				Add(m, {});
			}
		}
	}
}

void FLRRaceSky::HideView(USceneCaptureComponent2D* Capture, int32 v) const
{
	if (!Views.IsValidIndex(v)) {
		return;
	}
	for (const TWeakObjectPtr<UProceduralMeshComponent>& S : Views[v].SkyDome) {
		Capture->HiddenComponents.Add(S);
	}
}


void FLRRaceSky::Clear() { Views.Reset(); }
