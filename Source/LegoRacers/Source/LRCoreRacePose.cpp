// The race view posed each frame from the core: racers, ghosts, scenery, power-ups, actions, the views' cameras.

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

// The car's shadow texture: its models seen from above, flattened, in (8, 8, 8) on transparent black,
// over the chassis' shadow size x 1.15. (No file gives its size: 64 x 64, decided.)
// ---- The sky ----

// ---- Posing ----

void ULRRaceView::PoseRacers(const lr::World& W)
{
	for (int32 i = 0; i < Racers.Num(); i++) {
		for (int32 m = 0; m < 4; m++) {
			PoseRacerMember(i, m, W);
		}
	}
	PoseGhosts(W);
}

namespace
{
void SetVisible(const TArray<TWeakObjectPtr<UProceduralMeshComponent>>& Parts, bool bVisible)
{
	for (const TWeakObjectPtr<UProceduralMeshComponent>& C : Parts) {
		if (C.IsValid()) {
			C->SetVisibility(bVisible);
		}
	}
}
} // namespace

// Racer i's member m (0 the body, 1 the chassis' second variant, 2 the chassis car, 3 the driver) at the core's pose.
void ULRRaceView::PoseRacerMember(int32 i, int32 m, const lr::World& W)
{
	FRacer& R = Racers[i];
	const bool bHidden = W.racers.race[size_t(i)].flags & lr::RacerGhost; // (warping: hidden)
	const lr::RacerShape& Shape = W.raceView.shapes[size_t(i)];
	const lr::RacerLook& Look = W.raceView.looks[size_t(i)];
	FPlaced& P = R.Members[m];
	lr::Pose Pose;
	float Scale = 1;
	const bool bPresent = W.member(size_t(i), lr::RacerShape::Member(m), Pose, Scale) && !bHidden;
	SetVisible(P.Parts, bPresent);
	if (m == 0) {
		SetVisible(R.Built, bPresent);
	}
	if (m == 3) {
		for (const FSkin& S : R.Driver) {
			SetVisible({S.Component}, bPresent);
		}
	}
	if (!bPresent) {
		return;
	}
	const int16 Figure = m == 1 ? Shape.variantAnimation : m == 2 ? Shape.carAnimation : m == 3 ? 0 : -1;
	const std::vector<lr::Pose>& Locals = m == 1 ? Look.variantNodes : m == 2 ? Look.carNodes : Look.figureNodes;
	TArray<FTransform> Nodes;
	TArray<FQuat> Rotations;
	if (Figure >= 0 && size_t(Figure) < W.raceView.skeletons.size() && !Locals.empty()) {
		std::vector<lr::NodeMatrix> Out;
		lr::placeNodes(W.raceView.skeletons[size_t(Figure)], Locals, Pose, Scale, Out);
		if (m == 3 && !R.Driver.IsEmpty()) {
			PoseRaceDriver(R.Driver, P, Out);
			return;
		}
		for (const lr::NodeMatrix& N : Out) {
			Nodes.Add(FrameOf(N.rows, N.position));
			Rotations.Add(Nodes.Last().GetRotation());
		}
	}
	// A body without a skeleton: the model's scale too (the built car is drawn at 1: the core's bounds).
	const float Whole = m == 0 && P.Mesh ? Scale * P.Mesh->Scale : Scale;
	const FTransform T = FrameOf(Pose.rotation * Whole, Pose.position);
	PoseNodes(P, Nodes, T);
	if (m == 0) {
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : R.Built) {
			if (C.IsValid()) {
				C->SetWorldTransform(FrameOf(Pose.rotation * Scale, Pose.position));
			}
		}
	}
	if (!Rotations.Num()) {
		Rotations.Add(T.GetRotation());
	}
	Relight(P, Rotations);
}

// A time race's ghosts at their runs' poses: the car, its body on it, the driver at its mount.
void ULRRaceView::PoseGhosts(const lr::World& W)
{
	for (int32 g = 0; g < 2; g++) {
		const lr::GhostRun& Run = g == 0 ? W.session.timeRace.record : W.session.timeRace.best;
		const bool bValid = g == 0 ? W.session.timeRace.recordValid : W.session.timeRace.bestValid;
		lr::Vec3 At;
		lr::Quat Q;
		const bool bShown = bValid && !Run.finished && W.ghostAt(Run, At, Q);
		for (int32 m = 0; m < 4; m++) {
			FPlaced& P = Ghosts[g][m];
			if (!P.Mesh) {
				continue;
			}
			SetVisible(P.Parts, bShown);
			if (m == 3) {
				for (const FSkin& S : GhostDrivers[g]) {
					SetVisible({S.Component}, bShown);
				}
			}
			if (P.Actor.IsValid()) {
				P.Actor->SetActorHiddenInGame(!bShown);
			}
			if (!bShown || Racers.IsEmpty()) {
				continue;
			}
			const lr::Basis B = lr::basisOf(Q);
			const lr::RacerShape& Shape = W.raceView.shapes[0];
			lr::Pose Pose{At, B};
			float Scale = m == 2 ? Shape.carScale : m == 3 ? Shape.driverScale : P.Mesh->Scale;
			if (m == 3) {
				Pose.position = lr::toWorld(B, Shape.mount) + At;
			}
			TArray<FTransform> Nodes;
			const int16 Figure = m == 2 ? Shape.carAnimation : m == 3 ? 0 : -1;
			if (Figure >= 0 && size_t(Figure) < W.raceView.skeletons.size()) {
				std::vector<lr::NodeMatrix> Out;
				lr::poseSkeleton(W.raceView.skeletons[size_t(Figure)], &W.raceView.figures[size_t(Figure)], {0, 0, true}, Pose, Scale, Out);
				if (m == 3 && !GhostDrivers[g].IsEmpty()) {
					PoseRaceDriver(GhostDrivers[g], P, Out);
					continue;
				}
				for (const lr::NodeMatrix& N : Out) {
					Nodes.Add(FrameOf(N.rows, N.position));
				}
			}
			PoseNodes(P, Nodes, FrameOf(B * Scale, At));
		}
	}
}

void ULRRaceView::PoseScenery(const lr::World& W)
{
	PoseAnimatedScenery(W);
	ApplyDrawDistances(W);
	AnimateMaterials(W);
}

// The animated scenery on its clocks (the core's).
void ULRRaceView::PoseAnimatedScenery(const lr::World& W)
{
	for (int32 k = 0; k < Animated.Num() && size_t(k) < W.track.animated.size(); k++) {
		FPlaced& P = Animated[k];
		if (!P.Mesh) {
			continue;
		}
		const lr::AnimatedModel& A = W.track.animated[size_t(k)];
		const lr::Clock& Clock = W.scenery.clocks[size_t(k)];
		ScratchNodes.Reset();
		if (!A.nodes.empty()) {
			const lr::Animation* Anim = A.animation >= 0 && size_t(A.animation) < W.track.animations.size() ? &W.track.animations[size_t(A.animation)] : nullptr;
			lr::poseSkeleton(A.nodes, Anim, {Clock.part, Clock.time, Clock.loop}, A.pose, A.scale, ScratchMatrices);
			for (const lr::NodeMatrix& N : ScratchMatrices) {
				ScratchNodes.Add(FrameOf(N.rows, N.position));
			}
		}
		PoseNodes(P, ScratchNodes, FrameOf(A.pose.rotation * A.scale, A.pose.position));
	}
}

void ULRRaceView::ApplyDrawDistances(const lr::World& W)
{
	// The plain models' draw distances as the core has them now: a model the core
	// hides (-1) is drawn by no camera (the rocket's two looks, C0R3's laser columns, C2R3's beamdumy); the others are
	// shown or hidden per camera by the core's rule as each view is captured (CaptureViews: World::plainInReach), never by
	// UE's cull distance, which the view distance scale shortens at lower qualities (the rocket from 400 ft at 0.8).
	if (PlainDistances.Num() != int32(W.scenery.modelDistances.size())) {
		PlainDistances.Init(-2, int32(W.scenery.modelDistances.size())); // (none applied yet)
	}
	for (int32 m = 0; m < PlainDistances.Num() && m < PlainOfEntity.Num(); m++) {
		const std::array<float, 3>& D = W.scenery.modelDistances[size_t(m)];
		const float Far = FMath::Max3(D[0], D[1], D[2]);
		if (PlainDistances[m] == Far || !Plain.IsValidIndex(PlainOfEntity[m])) {
			continue;
		}
		PlainDistances[m] = Far;
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : Plain[PlainOfEntity[m]].Parts) {
			if (C.IsValid()) {
				C->SetVisibility(Far > 0);
				C->SetCachedMaxDrawDistance(0);
				C->LDMaxDrawDistance = 0;
			}
		}
	}
}

// A scenery entity's placed model: animated (by Track::animated) or plain (by entity); none if it has none.
ULRRaceView::FPlaced* ULRRaceView::SceneryOf(bool bAnimated, int32 Entity)
{
	if (bAnimated) {
		return Animated.IsValidIndex(Entity) ? &Animated[Entity] : nullptr;
	}
	return PlainOfEntity.IsValidIndex(Entity) && Plain.IsValidIndex(PlainOfEntity[Entity]) ? &Plain[PlainOfEntity[Entity]] : nullptr;
}

void ULRRaceView::AnimateMaterials(const lr::World& W)
{
	// Material animation: a binding's shown frame swaps its slot's material (by texture, on the entity's own instances);
	// texture scroll: the entity's uv offset.
	if (ShownFrames.Num() != int32(W.track.materialBindings.size())) {
		ShownFrames.Init(0, int32(W.track.materialBindings.size()));
	}
	for (size_t b = 0; b < W.track.materialBindings.size(); b++) {
		const lr::Track::MaterialBinding& Bind = W.track.materialBindings[b];
		// (A binding shows its track's frame, the track's cursor; the code display's are re-rated and
		// stopped by the core)
		const int16 Frame = size_t(Bind.track) < W.raceView.materials.size() ? W.raceView.materials[size_t(Bind.track)].shown : int16(-1);
		if (ShownFrames[int32(b)] == Frame + 1) {
			continue;
		}
		ShownFrames[int32(b)] = Frame + 1;
		FPlaced* P = SceneryOf(Bind.animated, Bind.entity);
		const lr::Track::MaterialTrack& T = W.track.materialTracks[size_t(Bind.track)];
		if (!P || !P->Mesh || Bind.slot < 0 || size_t(Bind.slot) >= P->Mesh->MaterialNames.size() || Frame < 0 || size_t(Frame) >= T.frames.size()) {
			continue;
		}
		const std::string& Slot = P->Mesh->MaterialNames[size_t(Bind.slot)];
		bool bFlip;
		uint8 D[4];
		UMaterialInstanceDynamic* Shown = MaterialFor(T.frames[size_t(Frame)].first, TrackLibraries, 1, bFlip, D);
		ForSections(*P, &Slot, [&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) { C->SetMaterial(Index, Shown); });
	}
	for (size_t s = 0; s < W.raceView.scrollOffsets.size() && s < W.track.scrolls.size(); s++) {
		const lr::Track::Scroll& Sc = W.track.scrolls[s];
		FPlaced* P = SceneryOf(Sc.animated, Sc.entity);
		if (!P || !P->Mesh) {
			continue;
		}
		const FLinearColor Offset(float(W.raceView.scrollOffsets[s][0]) / 65536.0f, float(W.raceView.scrollOffsets[s][1]) / 65536.0f, 0, 0);
		ForSections(*P, nullptr, [&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) {
			UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C->GetMaterial(Index));
			if (!Mid || Mid->GetOuter() != C) {
				Mid = C->CreateDynamicMaterialInstance(Index, C->GetMaterial(Index));
			}
			if (Mid) {
				Mid->SetVectorParameterValue(TEXT("UVOffset"), Offset);
			}
		});
	}
}

void ULRRaceView::PosePowerups(const lr::World& W)
{
	// Bricks: the four shared entities (the colour brick's cube and streak, the white brick's cube and
	// streaks), each playing its own part 0 from the race's load on the core's clock, posed by the core at each brick's
	// place and size; the colour's materials in slot 0 (P, M, S, T: red, yellow, blue, green).
	for (int32 b = 0; b < Bricks.Num() && size_t(b) < W.powerUps.bricks.size(); b++) {
		const lr::Brick& B = W.powerUps.bricks[size_t(b)];
		PoseBrick(W, B, Bricks[b], 0);
		PoseBrick(W, B, BrickGlows[b], 1);
	}
	PoseExplosions(W);
}

// A brick's cube (m 0) or streak (1).
void ULRRaceView::PoseBrick(const lr::World& W, const lr::Brick& B, FPlaced& Placed, int32 m)
{
	FPlaced* P = &Placed;
	const float Size = B.size();
	if (!P->Mesh) {
		return;
	}
	const bool bShown = Size > 0;
	const lr::Track::BrickModel Which = B.colour ? (m == 0 ? lr::Track::BrickCube : lr::Track::BrickStreak) : (m == 0 ? lr::Track::WhiteCube : lr::Track::WhiteStreaks);
	ScratchNodes.Reset();
	if (bShown) {
		W.brickNodes(Which, B.position, Size, ScratchMatrices);
		for (const lr::NodeMatrix& N : ScratchMatrices) {
			ScratchNodes.Add(FrameOf(N.rows, N.position));
		}
	}
	for (TWeakObjectPtr<UProceduralMeshComponent>& C : P->Parts) {
		if (C.IsValid()) {
			C->SetVisibility(bShown);
		}
	}
	if (bShown) {
		PoseNodes(*P, ScratchNodes, FrameOf(lr::Basis{} * (Size * P->Mesh->Scale), B.position));
		TArray<FQuat> Rotations;
		for (const FTransform& N : ScratchNodes) {
			Rotations.Add(N.GetRotation());
		}
		if (Rotations.IsEmpty()) {
			Rotations.Add(FQuat::Identity);
		}
		Relight(*P, Rotations);
	}
	if (B.colour && bShown) {
		// The colour's material: the brick "pbrick" + P, M, S, T (red, yellow, blue, green), its streak "ptrail" + the same,
		// put in slot 0 of the shared models' own tables (both files name ptrailM there: the slot, not the name).
		static const char Letters[5] = {'P', 'P', 'S', 'T', 'M'};
		const std::string Wanted = std::string(m == 0 ? "pbrick" : "ptrail") + Letters[FMath::Min<int32>(B.colour, 4)];
		const std::string Slot0 = P->Mesh->MaterialNames.empty() ? std::string() : P->Mesh->MaterialNames[0];
		bool bFlip;
		uint8 D[4];
		UMaterialInstanceDynamic* Colour = MaterialFor(Wanted, PowerupLibraries, 1, bFlip, D);
		ForSections(*P, &Slot0, [&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) { C->SetMaterial(Index, Colour); });
	}
}

void ULRRaceView::PoseExplosions(const lr::World& W)
{
	// Explosions: scaled (1 - 0.05) g + 0.05 (at most 1), alpha (1 - g) 255.
	for (int32 e = 0; e < Explosions.Num() && size_t(e) < W.powerUps.explosions.size(); e++) {
		const lr::Explosion& X = W.powerUps.explosions[size_t(e)];
		FPlaced& P = Explosions[e];
		if (!P.Mesh) {
			continue;
		}
		const bool bShown = X.state == lr::Explosion::Exploding;
		const float S = FMath::Min(1.0f, (1 - 0.05f) * X.growth + 0.05f);
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
			if (C.IsValid()) {
				C->SetVisibility(bShown);
				C->SetWorldTransform(FrameOf(lr::Basis{} * (S * P.Mesh->Scale), X.centre));
				for (int32 m = 0; bShown && m < C->GetNumMaterials(); m++) {
					if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C->GetMaterial(m))) {
						if (Mid->GetOuter() != C) {
							Mid = C->CreateDynamicMaterialInstance(m, Mid);
						}
						Mid->SetScalarParameterValue(TEXT("Alpha"), 1 - X.growth);
					}
				}
			}
		}
	}
}

// ---- A tick's views ----

void ULRRaceView::Pose(const lr::World& W, const lr::Show& Show, float WindowScale, float WindowAspect)
{
	if (!bBuilt || !WorldRef.IsValid()) {
		return;
	}
	ShowClockMs = Show.clockMs;
	SmoothBegin(); // (frame smoothing: what this pose moves, from where; nothing when off)
	PoseRacers(W);
	PoseScenery(W);
	PosePowerups(W);
	PoseActions(W);
	const int32 Count = int32(W.raceView.cameras.size());
	AddViews(Count);
	for (int32 v = 0; v < Count; v++) {
		PoseSky(v, W, Show);
		BuildEffects(v, W, Show);
	}
	// (Split screen is split from race frame 1)
	for (int32 v = 0; v < Count; v++) {
		PoseView(v, W, WindowScale, Count == 1 && WindowAspect > 4.0f / 3 + 0.01f ? WindowAspect : 0);
	}
	if (bSmoothing) {
		SmoothEnd(); // (Smooth captures)
		return;
	}
	CaptureViews();
}

// A scene capture per camera, made as cameras appear.
void ULRRaceView::AddViews(int32 Count)
{
	while (Views.Num() < Count) {
		FView& V = Views.AddDefaulted_GetRef();
		AActor* A = NewActor(FTransform::Identity);
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(A);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bAlwaysPersistRenderingState = true;
		Capture->CompositeMode = SCCM_Overwrite;
		ConfigureCapture(Capture);
		Capture->bOverride_CustomNearClippingPlane = true;
		Capture->CustomNearClippingPlane = 5 * Cm;
		Capture->SetupAttachment(A->GetRootComponent());
		Capture->RegisterComponent();
		V.Capture = Capture;
	}
}

// View v's target, what it hides and its camera. WideAspect: a wide one-player race's window aspect (it fills the
// window's width), else 0.
void ULRRaceView::PoseView(int32 v, const lr::World& W, float WindowScale, float WideAspect)
{
	FView& V = Views[v];
	const lr::PlayerCamera& Cam = W.raceView.cameras[size_t(v)];
	V.Rect = FIntRect(Cam.viewport[0], Cam.viewport[1], Cam.viewport[2], Cam.viewport[3]);
	const float Width = WideAspect > 0 ? V.Rect.Height() * WideAspect : float(V.Rect.Width());
	const float Scale = WindowScale * Settings.Scale; // (the render scale: the target drawn over the viewport, filtered)
	const FIntPoint Size(FMath::Max(1, FMath::RoundToInt(Width * Scale)), FMath::Max(1, FMath::RoundToInt(V.Rect.Height() * Scale)));
	UTextureRenderTarget2D* Target = V.Target.Get();
	if (!Target || Target->SizeX != Size.X || Target->SizeY != Size.Y) {
		Target = NewTarget(this, Size.X, Size.Y);
		Targets.Add(Target);
		V.Target = Target;
	}
	USceneCaptureComponent2D* Capture = V.Capture.Get();
	Capture->TextureTarget = Target;
	HideFromView(Capture, v, W);
	lr::Vec3 Right, Down, Look;
	Cam.shownRows(Right, Down, Look);
	lr::Vec3 EyeAt = Cam.shownEye();
	float Fov = Cam.fov;
	Capture->ShowOnlyComponents.Reset();
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	if (Cam.hidden) {
		PoseWarpView(Capture, W, Cam, EyeAt, Look, Down, Fov);
	}
	Capture->SetWorldLocationAndRotation(Ue(EyeAt), FRotationMatrix::MakeFromXZ(Along(Look), -Along(Down)).Rotator());
	// The horizontal field from the vertical by the viewport's aspect. A wide one-player race's spans
	// the window Hor+: tan(h/2) = tan(v/2) A, A the window's aspect (a 4:3 window gives 4/3).
	const float Aspect = WideAspect > 0 ? WideAspect : float(V.Rect.Width()) / float(FMath::Max(1, V.Rect.Height()));
	Capture->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov) * 0.5f) * Aspect));
}

namespace
{
void HideParts(USceneCaptureComponent2D* Capture, const TArray<TWeakObjectPtr<UProceduralMeshComponent>>& Parts)
{
	for (const TWeakObjectPtr<UProceduralMeshComponent>& Part : Parts) {
		if (Part.IsValid()) {
			Capture->HiddenComponents.Add(Part);
		}
	}
}
} // namespace

// What view v's camera doesn't draw: the other views' skies and effects, the warp tube unless warping, the plain models
// and bricks out of its reach, the animated scenery it didn't draw.
void ULRRaceView::HideFromView(USceneCaptureComponent2D* Capture, int32 v, const lr::World& W)
{
	const lr::PlayerCamera& Cam = W.raceView.cameras[size_t(v)];
	Capture->HiddenComponents.Reset();
	for (int32 o = 0; o < Views.Num(); o++) {
		if (o == v) {
			continue;
		}
		if (Views[o].Effects.IsValid()) {
			Capture->HiddenComponents.Add(Views[o].Effects);
		}
		for (const TWeakObjectPtr<UProceduralMeshComponent>& S : Views[o].SkyDome) {
			Capture->HiddenComponents.Add(S);
		}
	}
	if (!Cam.hidden) {
		for (const FPlaced& P : Tube) {
			HideParts(Capture, P.Parts);
		}
	}
	// The plain models out of this camera's reach (the eye to the entity's position, squared, against
	// the core's stored levels; UE's frustum culling does the rest).
	for (int32 m = 0; m < PlainOfEntity.Num() && size_t(m) < W.scenery.modelDistances.size(); m++) {
		if (Plain.IsValidIndex(PlainOfEntity[m]) && !W.plainInReach(size_t(m), Cam.shownEye())) {
			HideParts(Capture, Plain[PlainOfEntity[m]].Parts);
		}
	}
	// The bricks' shared entities' draw distance (400 ft from this camera's eye).
	for (int32 b = 0; b < Bricks.Num() && size_t(b) < W.powerUps.bricks.size(); b++) {
		const lr::Brick& B = W.powerUps.bricks[size_t(b)];
		const lr::Track::BrickModel Which = B.colour ? lr::Track::BrickCube : lr::Track::WhiteCube;
		if (!lr::World::withinDistance(W.track.brickModels[Which].distances, lr::lengthSquared(B.position - Cam.shownEye()))) {
			HideParts(Capture, Bricks[b].Parts);
			HideParts(Capture, BrickGlows[b].Parts);
		}
	}
	for (int32 k = 0; k < Animated.Num() && size_t(k) < W.raceView.drawnBy.size(); k++) {
		if (!(W.raceView.drawnBy[size_t(k)] & (1u << v))) {
			HideParts(Capture, Animated[k].Parts);
		}
	}
}

// Warping: only the tube, its parts at 1500 - the ms left, the camera snapped behind a car at the origin facing
// (0, -1, 0) (the chase view's pose; FOV 45 cos(pi ms left / 1500) + base).
void ULRRaceView::PoseWarpView(USceneCaptureComponent2D* Capture, const lr::World& W, const lr::PlayerCamera& Cam, lr::Vec3& EyeAt, lr::Vec3& Look, lr::Vec3& Down, float& Fov)
{
	int32 Left = 0;
	for (const uint8 Index : W.powerUps.actionList) {
		const lr::Action& A = W.powerUps.actions[Index];
		if (A.kind == lr::Action::Warp && A.owner == Cam.racer) {
			Left = int32(A.ms);
		}
	}
	for (int32 e = 0; e < 2; e++) {
		FPlaced& P = Tube[e];
		if (!P.Mesh) {
			continue;
		}
		const FFigure* F = Figure(TubeModels[e]);
		TArray<FTransform> Nodes;
		if (F && !F->Nodes.empty()) {
			std::vector<lr::NodeMatrix> Out;
			const lr::Animation* An = F->bAnimated ? &F->Animation : nullptr;
			const float Frames = An ? An->parts[0].rate * float(1500 - Left) : 0;
			lr::poseSkeleton(F->Nodes, An, {An ? 0 : -1, Frames, true}, TubePoses[e], P.Mesh->Scale, Out);
			for (const lr::NodeMatrix& N : Out) {
				Nodes.Add(FrameOf(N.rows, N.position));
			}
		}
		PoseNodes(P, Nodes, FrameOf(TubePoses[e].rotation * P.Mesh->Scale, TubePoses[e].position));
		for (TWeakObjectPtr<UProceduralMeshComponent>& Part : P.Parts) {
			if (Part.IsValid()) {
				Capture->ShowOnlyComponents.Add(Part);
			}
		}
	}
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	const float SinP = FMath::Sin(FMath::DegreesToRadians(5.0f)), CosP = FMath::Cos(FMath::DegreesToRadians(5.0f));
	const float SinH = FMath::Sin(FMath::DegreesToRadians(35.0f));
	EyeAt = {0, CosP * 20, (SinP + SinH) * 20};
	Look = {0, -CosP, -SinP};
	Down = lr::normalize(lr::Vec3{0, 0, -1} - Look * lr::dot(lr::Vec3{0, 0, -1}, Look));
	Fov = 45 * FMath::Cos(PI * float(Left) / 1500) + Cam.fov;
}

void ULRRaceView::CaptureViews()
{
	for (FView& V : Views) {
		if (V.Capture.IsValid() && V.Capture->TextureTarget) {
			V.Capture->CaptureScene();
		}
	}
}

// Action's model in its role, shown this frame (made when first wanted).
ULRRaceView::FPlaced& ULRRaceView::ActionModel(int32 Action, int32 Role, const std::string& Model)
{
	TArray<FActionModel>& Slot = ActionModels.FindOrAdd(uint32(Action) << 8 | uint32(Role));
	for (FActionModel& Each : Slot) {
		if (Each.Model == Model) {
			Each.ShownAt = ActionFrame;
			return Each.Placed;
		}
	}
	FActionModel& Made = Slot.AddDefaulted_GetRef();
	Made.Model = Model;
	Made.ShownAt = ActionFrame;
	Made.Placed = Place(PowerupActor, UTF8_TO_TCHAR((AssetPath(Common, Model, ".GDB")).c_str()), PowerupLibraries, true);
	return Made.Placed;
}

// A model at a pose (a skeleton's part at a time, if it has one), with its parts' alpha; its uv offset. Shown collects the
// models shown this frame.
void ULRRaceView::ShowActionModel(int32 Action, int32 Role, const std::string& Model, const lr::Pose& At, float Scale, int32 Part, float TimeMs, float Alpha, FVector2D Scroll)
{
	FPlaced& P = ActionModel(Action, Role, Model);
	if (!P.Mesh) {
		return;
	}
	const FFigure* F = Figure(Model);
	TArray<FTransform> Nodes;
	if (F && !F->Nodes.empty()) {
		std::vector<lr::NodeMatrix> Out;
		const lr::Animation* A = F->bAnimated ? &F->Animation : nullptr;
		const float Frames = A && Part >= 0 && size_t(Part) < A->parts.size() ? A->parts[size_t(Part)].rate * TimeMs : 0;
		lr::poseSkeleton(F->Nodes, A, {A ? Part : -1, Frames, true}, At, Scale * P.Mesh->Scale, Out);
		for (const lr::NodeMatrix& N : Out) {
			Nodes.Add(FrameOf(N.rows, N.position));
		}
	}
	PoseNodes(P, Nodes, FrameOf(At.rotation * (Scale * P.Mesh->Scale), At.position));
	for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
		if (!C.IsValid()) {
			continue;
		}
		C->SetVisibility(true);
		for (int32 m = 0; m < C->GetNumMaterials(); m++) {
			UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C->GetMaterial(m));
			if ((Alpha < 1 || !Scroll.IsZero()) && Mid && Mid->GetOuter() != C) {
				// (Its own instance, translucent when it fades.)
				UMaterialInstanceDynamic* Shared = Mid;
				Mid = UMaterialInstanceDynamic::Create(Alpha < 1 ? TranslucentBase.Get() : Shared->Parent.Get(), C.Get());
				Mid->CopyParameterOverrides(Shared);
				C->SetMaterial(m, Mid);
			}
			if (Mid && Mid->GetOuter() == C) {
				Mid->SetScalarParameterValue(TEXT("Alpha"), Alpha);
				Mid->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(float(Scroll.X), float(Scroll.Y), 0, 0));
			}
		}
	}
}

// A turbo: the burn on the car (part 0 lighting, 1 boosting, 2 fading) and two flames behind it.
void ULRRaceView::PoseTurbo(const lr::World& W, int32 i, const lr::Action& A, const lr::Pose& Car, float Seconds)
{
	const int32 Level = FMath::Min<int32>(A.level, 2);
	static const char* Burns[3] = {"DT0BURN", "DT1BURN", "DT2CBURN"};
	static const char* Flames[3][2] = {{"DT0FLA1", "DT0FLA2"}, {"DT1FLA1", "DT1FLA2"}, {"DT2CFLA1", "DT2CFLA2"}};
	const int32 Part = A.state - 2;
	const float Alpha = A.state == 4 ? FMath::Max(0.0f, float(int32(A.ms)) - 350) / 350 * 191 / 255 : 1.0f;
	// The burn and both flames share one frame: the chassis car's (not
	// the leaned body's), turned a quarter about its up (rows left, back, up), 2 back and 3 up from it.
	lr::Pose Chassis;
	float ChassisScale = 1;
	const lr::Pose& C = W.member(size_t(A.owner), lr::RacerShape::Member(2), Chassis, ChassisScale) ? Chassis : Car;
	const lr::Basis& B = C.rotation;
	const lr::Pose At{C.position + B.f * -2 + B.u * 3, {B.s, B.f * -1, B.u}};
	// Burn and flames play one part (lit, boosting, fading) from the state's start.
	static const float Boosts[3] = {1000, 1500, 5000};
	const float Elapsed = (A.state == 2 ? 400 : A.state == 3 ? Boosts[Level] : 700) - float(A.ms);
	ShowActionModel(i, 0, Burns[Level], At, 1, Part, Elapsed, Alpha, FVector2D::ZeroVector);
	const bool bFlames = A.state != 2 || A.ms < 50;
	if (bFlames) {
		ShowActionModel(i, 1, Flames[Level][0], At, 1, Part, Elapsed, Alpha, FVector2D(0, 4 * Seconds));
		ShowActionModel(i, 2, Flames[Level][1], At, 1, Part, Elapsed, Alpha, FVector2D(0, 2 * Seconds));
	}
}

void ULRRaceView::PoseActions(const lr::World& W)
{
	ActionFrame++;
	const float Seconds = float(W.session.ms) / 1000;
	for (const uint8 Index : W.powerUps.actionList) {
		const lr::Action& A = W.powerUps.actions[Index];
		const int32 i = int32(Index);
		if (A.owner < 0 || size_t(A.owner) >= W.racers.visible.size()) {
			if (A.kind != lr::Action::Dynamite && A.kind != lr::Action::Missile && A.kind != lr::Action::Cannon) {
				continue;
			}
		}
		const lr::Pose* Car = A.owner >= 0 && size_t(A.owner) < W.racers.visible.size() ? &W.racers.visible[size_t(A.owner)] : nullptr;
		switch (A.kind) {
		case lr::Action::Turbo:
			if (Car && A.state >= 2 && A.state <= 4 && !(W.racers.race[size_t(A.owner)].flags & lr::RacerGhost)) {
				PoseTurbo(W, i, A, *Car, Seconds);
			}
			break;
		case lr::Action::Shield: {
			if (!Car || A.state < 3 || A.state > 4 || (W.racers.race[size_t(A.owner)].flags & lr::RacerGhost)) {
				break;
			}
			const int32 Level = FMath::Clamp<int32>(A.level, 0, 3);
			const float Alpha = A.state == 4 ? float(int32(A.ms)) / 1000 * 191 / 255 : 1.0f;
			lr::Pose At{Car->position + lr::Vec3{0, 0, 2}, lr::basisFromForwardUp(Car->rotation.f, {0, 0, 1})};
			ShowActionModel(i, 0, "SHEOUT" + std::to_string(Level), At, 1, 0, Seconds * 1000, Alpha, FVector2D(0, 4 * Seconds));
			ShowActionModel(i, 1, "SHEIN" + std::to_string(Level), At, 1, 0, Seconds * 1000, Alpha, FVector2D(0, 4 * Seconds));
			break;
		}
		case lr::Action::Dynamite: {
			if (A.shot.state != lr::Projectile::Flying && A.state != 2) {
				break;
			}
			const float Angle = A.phase;
			const float Cs = FMath::Cos(Angle), Sn = FMath::Sin(Angle);
			lr::Pose At{A.state == 2 && Car ? Car->position + lr::Vec3{0, 0, 5} : A.shot.at, {{Cs, 0, -Sn}, {0, 1, 0}, {Sn, 0, Cs}}};
			ShowActionModel(i, 0, "BARREL", At, 1, -1, 0, 1, FVector2D::ZeroVector);
			break;
		}
		case lr::Action::Hook:
			if (A.state >= 3 && A.state <= 5) {
				ShowActionModel(i, 0, "GRAPPLE", W.powerUps.grapple, 1, -1, 0, 1, FVector2D::ZeroVector);
			}
			break;
		case lr::Action::Missile:
			if (A.state == 2 && Car) {
				ShowActionModel(i, 0, "DMISSILE", A.ride, 1, 0, A.riseFrames / 0.03f, 1, FVector2D::ZeroVector);
			}
			else if (A.shot.state == lr::Projectile::Flying) {
				const lr::Vec3 F = lr::isZero(A.shot.velocity) ? lr::Vec3{1, 0, 0} : lr::normalize(A.shot.velocity);
				ShowActionModel(i, 0, "DMISSILE", {A.shot.at, lr::basisFromForwardUp(F, {0, 0, 1})}, 1, -1, 0, 1, FVector2D::ZeroVector);
			}
			break;
		default: break;
		}
	}
	for (auto& [Slot, Models] : ActionModels) {
		for (const FActionModel& Each : Models) {
			if (Each.ShownAt != ActionFrame) {
				SetVisible(Each.Placed.Parts, false);
			}
		}
	}
}
