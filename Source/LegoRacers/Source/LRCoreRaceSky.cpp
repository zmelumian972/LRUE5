// The race view's sky, the cars' shadows and split screen's sky mask.

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

namespace
{
// A car's shadow drawn into Size x Size BGRA: dark where any triangle covers a pixel's centre. Points are UE's local cm
// in the car's frame; the decal's u runs across (left), v along (forward): Show::project.
struct FShadowRaster
{
	static constexpr int32 Size = 64;
	float HalfW, HalfL;
	TArray<uint8> Pixels;

	FShadowRaster(float Width, float Length)
		: HalfW(Width * 1.15f * 0.5f)
		, HalfL(Length * 1.15f * 0.5f)
	{
		Pixels.Init(0, Size * Size * 4);
	}
	void Triangle(const FVector& A, const FVector& B, const FVector& C)
	{
		FVector2D P[3];
		const FVector* V[3] = {&A, &B, &C};
		for (int32 k = 0; k < 3; k++) {
			const float Fwd = float(V[k]->X) / Cm, Left = -float(V[k]->Y) / Cm; // (UE's y right = -left)
			P[k] = FVector2D((Left / (2 * HalfW) + 0.5f) * Size, (Fwd / (2 * HalfL) + 0.5f) * Size);
		}
		const float MinX = FMath::Max(0.0f, float(FMath::Min3(P[0].X, P[1].X, P[2].X))), MaxX = FMath::Min(float(Size - 1), float(FMath::Max3(P[0].X, P[1].X, P[2].X)));
		const float MinY = FMath::Max(0.0f, float(FMath::Min3(P[0].Y, P[1].Y, P[2].Y))), MaxY = FMath::Min(float(Size - 1), float(FMath::Max3(P[0].Y, P[1].Y, P[2].Y)));
		const float Area = float((P[1] - P[0]) ^ (P[2] - P[0]));
		if (FMath::Abs(Area) < 1e-6f) {
			return;
		}
		for (int32 Y = int32(MinY); Y <= int32(MaxY); Y++) {
			for (int32 X = int32(MinX); X <= int32(MaxX); X++) {
				const FVector2D Q(X + 0.5f, Y + 0.5f);
				const float W0 = float((P[1] - Q) ^ (P[2] - Q)) / Area, W1 = float((P[2] - Q) ^ (P[0] - Q)) / Area;
				if (W0 >= 0 && W1 >= 0 && W0 + W1 <= 1) {
					uint8* Px = &Pixels[(Y * Size + X) * 4];
					Px[0] = Px[1] = Px[2] = 8;
					Px[3] = 255;
				}
			}
		}
	}
};
} // namespace

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

// A racer's shadow's size: its chassis' (the champion's chassis for a champion), else 8 by 9 ft.
FVector2f ULRRaceView::ShadowSize(int32 Racer, const lr::World& W)
{
	lr::ChassisTable Table;
	lr::ChampionTable Champions;
	lr::DriverTable Drivers;
	WarnUnless(Cache->get(Common + "CHASSIS.CMB", Table), "CHASSIS.CMB");
	WarnUnless(Cache->get(Common + "CHAMPS.CCB", Champions), "CHAMPS.CCB");
	WarnUnless(Cache->get(Common + "DRIVERS.DDB", Drivers), "DRIVERS.DDB");
	const lr::RaceBrief::Slot& Slot = W.session.brief.slots[size_t(Racer)];
	const lr::DriverTable::Driver* Driver = Slot.driver.empty() ? nullptr : Drivers.find(Slot.driver);
	const lr::ChampionTable::Champion* Champion = Driver ? Champions.find(Driver->champion) : nullptr;
	FVector2f Size(8, 9);
	if (const lr::ChassisTable::Chassis* C = Table.find(Champion ? Champion->chassis : Slot.chassis)) {
		Size.X = C->shadowWidth > 0 ? C->shadowWidth : Size.X;
		Size.Y = C->shadowLength > 0 ? C->shadowLength : Size.Y;
	}
	return Size;
}

// A racer's shadow texture: every member's triangles in the car's frame at rest (the body at the car, the driver at its
// mount), point filtered (carshadN).
void ULRRaceView::MakeShadow(int32 Racer, const lr::World& W)
{
	FRacer& R = Racers[Racer];
	const FVector2f Size = ShadowSize(Racer, W);
	FShadowRaster Raster(Size.X, Size.Y);
	for (int32 m = 0; m < 4; m++) {
		const FPlaced& P = R.Members[m];
		if (!P.Mesh) {
			continue;
		}
		const float S = P.Mesh->Scale;
		const FVector Offset = m == 3 ? Ue(W.raceView.shapes[size_t(Racer)].mount) : FVector::ZeroVector;
		for (const FLRModelMesh::FSection& Sec : P.Mesh->Sections) {
			for (int32 t = 0; t + 2 < Sec.Triangles.Num(); t += 3) {
				Raster.Triangle(Sec.Positions[Sec.Triangles[t]] * S + Offset, Sec.Positions[Sec.Triangles[t + 1]] * S + Offset, Sec.Positions[Sec.Triangles[t + 2]] * S + Offset);
			}
		}
	}
	for (const TWeakObjectPtr<UProceduralMeshComponent>& C : R.Built) {
		for (int32 s = 0; C.IsValid() && s < C->GetNumSections(); s++) {
			const FProcMeshSection* Sec = C->GetProcMeshSection(s);
			for (int32 t = 0; Sec && t + 2 < Sec->ProcIndexBuffer.Num(); t += 3) {
				Raster.Triangle(Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t]].Position, Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t + 1]].Position,
					Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t + 2]].Position);
			}
		}
	}
	UTexture2D* T = NewTexture(FShadowRaster::Size, FShadowRaster::Size, TF_Nearest, TA_Clamp,
		[&](uint8* Mip) { FMemory::Memcpy(Mip, Raster.Pixels.GetData(), Raster.Pixels.Num()); });
	Shadows.Add(T);
	R.Shadow = T;
}

void ULRRaceView::BuildSky(const lr::World& W)
{
	(void)W;
	// The sky's libraries: its world's (the sky material: RACERFX's skymat).
	lr::RaceFile Race;
	const std::string Dir = "GAMEDATA/" + std::string(TCHAR_TO_UTF8(*Track)) + "/";
	WarnUnless(Cache->get(Dir + std::string(TCHAR_TO_UTF8(*Track)) + ".RAB", Race), "the race's .RAB");
	lr::SceneFile SkyWorld;
	if (!Race.skyWorld.empty() && Cache->size(Dir + lr::binaryName(Race.skyWorld)) && Cache->get(Dir + lr::binaryName(Race.skyWorld), SkyWorld)) {
		SkyLibraries = LibrariesOf(SkyWorld, Dir);
	}
	// (Made per view in Pose: each view's sky is re-centred on its eye.)
}

void ULRRaceView::PoseSky(int32 v, const lr::World& W, const lr::Show& Show)
{
	FView& V = Views[v];
	const lr::PlayerCamera& C = W.raceView.cameras[size_t(v)];
	if (!V.Sky.IsValid()) {
		MakeSky(V);
	}
	// Drawn behind everything: the whole sky scaled 1000 times about the eye; the dome 10 below it plus
	// the height offset.
	// Each view's dome and sky world around its own eye (split screen has no sky quirk).
	const FVector Eye = Ue(C.shownEye());
	V.Sky->SetActorTransform(FTransform(FQuat::Identity, Eye, FVector(1000)));
	V.Sky->SetActorHiddenInGame(!Show.hasSky);
	if (V.SkyDome.Num() && V.SkyDome[0].IsValid()) {
		UProceduralMeshComponent* Dome = V.SkyDome[0].Get();
		Dome->SetRelativeLocation(FVector(0, 0, (-10 + Show.sky.height) * Cm));
		Dome->SetVisibility(Show.domeShown);
		ScratchColours.Reset();
		for (int32 r = 0; r < 3; r++) {
			const std::array<float, 3>& K = Show.skyColours[size_t(r)];
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
				V.SkyDome[k]->SetVisibility(Show.worldShown);
			}
		}
	}
}

// A view's sky: the dome, then the sky's world, its models at the eye, 30 up.
void ULRRaceView::MakeSky(FView& V)
{
	AActor* A = NewActor(FTransform::Identity);
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
	Dome->SetMaterial(0, MaterialFor("skymat", CommonLibraries, 1, bFlip, D));
	V.SkyDome.Add(Dome);
	AddSkyWorld(V, A);
}

void ULRRaceView::AddSkyWorld(FView& V, AActor* A)
{
	lr::RaceFile Race;
	const std::string Dir = "GAMEDATA/" + std::string(TCHAR_TO_UTF8(*Track)) + "/";
	lr::SceneFile SkyWorld;
	if (Cache->get(Dir + std::string(TCHAR_TO_UTF8(*Track)) + ".RAB", Race) && !Race.skyWorld.empty() && Cache->size(Dir + lr::binaryName(Race.skyWorld)) &&
		Cache->get(Dir + lr::binaryName(Race.skyWorld), SkyWorld)) {
		TArray<bool> Placed;
		Placed.Init(false, int32(SkyWorld.models.size()));
		const auto Add = [&](size_t m, const lr::Pose& Pose) {
			const std::string Own = AssetPath(Dir, SkyWorld.models[m], ".GDB");
			FPlaced P = Place(A, UTF8_TO_TCHAR((Cache->has(Own) ? Own : AssetPath(Common, SkyWorld.models[m], ".GDB")).c_str()), SkyLibraries, false, bMirrored);
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

