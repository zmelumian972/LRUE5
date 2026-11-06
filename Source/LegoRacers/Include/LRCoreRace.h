#pragma once

// The race in the Classic look: drawn from
// the core's world and the show only, never posed by UE itself. One scene capture per player viewport (split screen:
// two), each with its camera from the core; the track's worlds, the racers' members on their skeletons, the power-ups,
// the sky re-centred on each eye, the effects, decals and the cars' shadows, the time race's ghosts and the warp tube;
// materials and textures from the data cache (unlit: texture times the vertex colour decoded per pixel).
//
// The view owns its parts and runs them in order: the asset store, the models' actors and lights, the scenery, the
// racers, the power-ups, the sky, the effects, the captures and frame smoothing; and the menus' 3D views.

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LRCoreMenuViews.h"
#include "LRCoreRaceAssets.h"
#include "LRCoreRaceCaptures.h"
#include "LRCoreRaceEffects.h"
#include "LRCoreRaceModels.h"
#include "LRCoreRacePowerups.h"
#include "LRCoreRaceRacers.h"
#include "LRCoreRaceScenery.h"
#include "LRCoreRaceSky.h"

#include "LRCoreRace.generated.h"

class AActor;
class FReferenceCollector;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UWorld;

namespace lr
{
class Cache;
class Show;
struct World;
struct Game;
struct MenuData;
struct PlayerCamera;
} // namespace lr

UCLASS()
class ULRRaceView : public UObject
{
	GENERATED_BODY()

public:
	// The look's settings: set before Build.
	struct FLook
	{
		float Scale = 1; // the views' pixels per window pixel
	};
	FLook Settings;
	// Frame smoothing: while set, Pose leaves the captures to Smooth, which shows the views between the
	// last two poses at a fraction of the next frame.
	bool bSmoothing = false;
	void Smooth(float Fraction);
	// A race set up: its track, racers, power-ups and sky built (the loading screen's work).
	void Build(UWorld* World, const lr::Cache& Cache, const lr::World& W, const lr::Game& Game, const lr::MenuData& Data);
	void Clear();
	// After the frames of a tick: every view posed from the core and the show, and captured at the window's scale.
	void Pose(const lr::World& W, const lr::Show& Show, float WindowScale, float WindowAspect = 4.0f / 3);
	// The views to composite under the 2D (640x480 rectangles, their targets).
	int32 ViewCount() const { return Captures.Views.Num(); }
	UTextureRenderTarget2D* ViewTarget(int32 i) const { return Captures.Views.IsValidIndex(i) ? Captures.Views[i].Target.Get() : nullptr; }
	FIntRect ViewRect(int32 i) const { return Captures.Views.IsValidIndex(i) ? Captures.Views[i].Rect : FIntRect(); }
	bool bBuilt = false;

	virtual void BeginDestroy() override;
	// The parts' textures, materials and targets kept from the garbage collector.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// The menus' 3D (LRCoreMenuViews.h): a view's world and things posed from the core's scene, at the view's own place,
	// with the race view's assets; the camera it is seen from. Kept across races.
	FLRMenuCamera PoseMenuView(UWorld* World, const lr::Cache& Cache, const lr::MenuData& Data, int32 View, const FVector& At,
		const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game, AActor*& OutActor);

private:
	FLRRaceAssets Assets;
	FLRModels Models;
	FLRRaceScenery Scenery;
	FLRRaceRacers Racers;
	FLRRacePowerups Powerups;
	FLRRaceSky Sky;
	FLRRaceEffects Effects;
	FLRRaceCaptures Captures;
	FLRSmoothing Smoothing;
	FLRMenuViews MenuViews;

	// View v's target, what it hides and its camera.
	void PoseView(int32 v, const lr::World& W, float WindowScale, float WideAspect);
	void HideFromView(USceneCaptureComponent2D* Capture, int32 v, const lr::World& W);
};
