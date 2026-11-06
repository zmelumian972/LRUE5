// The race in the Classic look (LRCoreRace.h): the view's parts built, cleared and posed in order; each view's camera.

#include "LRCoreRace.h"
#include "LRCoreLog.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "LRCore/Race/World.h"
#include "LRCore/Show/ShowState.h"
#include "LRData/Cache.h"
#include "LRData/Race.h"
#include "UObject/UObjectGlobals.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

void ULRRaceView::BeginDestroy()
{
	Assets.Reset();
	Super::BeginDestroy();
}

void ULRRaceView::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ULRRaceView* This = CastChecked<ULRRaceView>(InThis);
	This->Assets.AddReferencedObjects(Collector);
	This->Racers.AddReferencedObjects(Collector);
	This->Captures.AddReferencedObjects(Collector);
	Super::AddReferencedObjects(InThis, Collector);
}

void ULRRaceView::Build(UWorld* World, const lr::Cache& InCache, const lr::World& W, const lr::Game& Game, const lr::MenuData& Data)
{
	Clear();
	Effects.Prepare();
	Models.WorldRef = World;
	Assets.Cache = &InCache;
	Assets.Outer = this;
	Models.LightRace();
	if (!Assets.LoadBases()) {
		UE_LOG(LogLRCore, Error, TEXT("the Classic materials are missing (scripts/make_classic_materials.py)"));
		return;
	}
	const std::string Dir = "GAMEDATA/" + W.session.brief.track + "/";
	lr::RaceFile Race;
	if (!Assets.Cache->get(Dir + W.session.brief.track + ".RAB", Race)) {
		UE_LOG(LogLRCore, Error, TEXT("%hs doesn't decode: no race view"), (Dir + W.session.brief.track + ".RAB").c_str());
		return;
	}
	Assets.LoadRaceLibraries(Race, Dir);
	Scenery.Build(Assets, Models, Race, Dir, W);
	Racers.Build(Assets, Models, W, Data);
	Powerups.Build(Assets, Models, W);
	Sky.Build(Assets, W);
	Powerups.BuildWarpTube(Assets, Models);
	bBuilt = true;
}

void ULRRaceView::Clear()
{
	Models.DestroyActors();
	Captures.Clear();
	Sky.Clear();
	Effects.Clear();
	Scenery.Clear();
	Racers.Clear();
	Powerups.Clear();
	Assets.TrackLibraries.Reset();
	Assets.CommonLibraries.Reset();
	Smoothing.Clear();
	bBuilt = false;
}

void ULRRaceView::Pose(const lr::World& W, const lr::Show& Show, float WindowScale, float WindowAspect)
{
	if (!bBuilt || !Models.WorldRef.IsValid()) {
		return;
	}
	Smoothing.Begin(bSmoothing, Models.Actors); // (frame smoothing: what this pose moves, from where; nothing when off)
	Racers.Pose(Models, W);
	Scenery.Pose(Assets, Models, W);
	Powerups.Pose(Models, W);
	Powerups.PoseActions(Assets, Models, W);
	const int32 Count = int32(W.raceView.cameras.size());
	Captures.AddViews(Models, Count);
	for (int32 v = 0; v < Count; v++) {
		Sky.Pose(Assets, Models, v, W, Show);
		Effects.Build(Assets, Models, Racers, v, W, Show);
	}
	// (Split screen is split from race frame 1)
	for (int32 v = 0; v < Count; v++) {
		PoseView(v, W, WindowScale, Count == 1 && WindowAspect > 4.0f / 3 + 0.01f ? WindowAspect : 0);
	}
	if (bSmoothing) {
		Smoothing.End(); // (Smooth captures)
		return;
	}
	Captures.CaptureViews();
}

void ULRRaceView::Smooth(float Fraction)
{
	if (!bBuilt) {
		return;
	}
	Smoothing.Apply(Fraction);
	Captures.CaptureViews();
}

// View v's target, what it hides and its camera. WideAspect: a wide one-player race's window aspect (it fills the
// window's width), else 0.
void ULRRaceView::PoseView(int32 v, const lr::World& W, float WindowScale, float WideAspect)
{
	FLRRaceCaptures::FView& V = Captures.Views[v];
	const lr::PlayerCamera& Cam = W.raceView.cameras[size_t(v)];
	V.Rect = FIntRect(Cam.viewport[0], Cam.viewport[1], Cam.viewport[2], Cam.viewport[3]);
	const float Width = WideAspect > 0 ? V.Rect.Height() * WideAspect : float(V.Rect.Width());
	const float Scale = WindowScale * Settings.Scale; // (the render scale: the target drawn over the viewport, filtered)
	const FIntPoint Size(FMath::Max(1, FMath::RoundToInt(Width * Scale)), FMath::Max(1, FMath::RoundToInt(V.Rect.Height() * Scale)));
	UTextureRenderTarget2D* Target = V.Target.Get();
	if (!Target || Target->SizeX != Size.X || Target->SizeY != Size.Y) {
		Target = NewTarget(this, Size.X, Size.Y);
		Captures.Targets.Add(Target);
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
		Powerups.PoseWarpView(Assets, Models, Capture, W, Cam, EyeAt, Look, Down, Fov);
	}
	Capture->SetWorldLocationAndRotation(Ue(EyeAt), FRotationMatrix::MakeFromXZ(Along(Look), -Along(Down)).Rotator());
	// The horizontal field from the vertical by the viewport's aspect. A wide one-player race's spans
	// the window Hor+: tan(h/2) = tan(v/2) A, A the window's aspect (a 4:3 window gives 4/3).
	const float Aspect = WideAspect > 0 ? WideAspect : float(V.Rect.Width()) / float(FMath::Max(1, V.Rect.Height()));
	Capture->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov) * 0.5f) * Aspect));
}

// What view v's camera doesn't draw: the other views' skies and effects, the warp tube unless warping, the plain models
// and bricks out of its reach, the animated scenery it didn't draw.
void ULRRaceView::HideFromView(USceneCaptureComponent2D* Capture, int32 v, const lr::World& W)
{
	const lr::PlayerCamera& Cam = W.raceView.cameras[size_t(v)];
	Capture->HiddenComponents.Reset();
	for (int32 o = 0; o < Captures.Views.Num(); o++) {
		if (o == v) {
			continue;
		}
		Effects.HideView(Capture, o);
		Sky.HideView(Capture, o);
	}
	if (!Cam.hidden) {
		Powerups.HideTube(Capture);
	}
	Scenery.HidePlain(Capture, W, Cam);
	Powerups.HideBricks(Capture, W, Cam);
	Scenery.HideAnimated(Capture, W, v);
}

FLRMenuCamera ULRRaceView::PoseMenuView(UWorld* World, const lr::Cache& InCache, const lr::MenuData& Data, int32 v, const FVector& At,
	const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game, AActor*& OutActor)
{
	OutActor = nullptr;
	Models.WorldRef = World;
	Assets.Cache = &InCache;
	Assets.Outer = this;
	if (!Assets.LoadBases()) {
		return {};
	}
	return MenuViews.Pose(Assets, Models, World, Data, v, At, Scene, Menus, Game, OutActor);
}
