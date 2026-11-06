#pragma once

// The race view's scene captures (LRCoreRaceCaptures.cpp): one per player viewport, its target and
// rectangle; and frame smoothing: the components a pose moved, shown between their two poses.

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

class AActor;
class FReferenceCollector;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;
struct FLRModels;
struct FLRRaceSky;

struct FLRRaceCaptures
{
	struct FView
	{
		TWeakObjectPtr<USceneCaptureComponent2D> Capture;
		TWeakObjectPtr<UTextureRenderTarget2D> Target;
		FIntRect Rect;
	};
	TArray<FView> Views;
	TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;

	// A scene capture per camera, made as cameras appear.
	void AddViews(FLRModels& Models, int32 Count);
	static void ConfigureCapture(USceneCaptureComponent2D* Capture);
	void CaptureViews();
	void Clear();
	void AddReferencedObjects(FReferenceCollector& Collector);

private:
};

struct FLRSmoothing
{
	// Before a pose: everything back where the last pose put it; what the actors' components are before this one (none
	// when off).
	void Begin(bool bSmoothing, const TArray<TWeakObjectPtr<AActor>>& Actors);
	// After it: the components it moved, from and to.
	void End();
	// Each moved component at Fraction from its last pose to this one.
	void Apply(float Fraction);
	void Clear();

private:
	struct FMoved
	{
		TWeakObjectPtr<USceneComponent> Component;
		FTransform From, To;
	};
	TArray<FMoved> Moved;
	TMap<TWeakObjectPtr<USceneComponent>, FTransform> BeforePose;
};
