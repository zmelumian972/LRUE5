// How the race's scene captures draw, and frame smoothing.

#include "LRCoreRaceCaptures.h"
#include "LRCoreLog.h"
#include "LRCoreRaceModels.h"
#include "LRCoreRaceSky.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

#include "LRCoreModelMesh.h"

using namespace LRUe;

void FLRRaceCaptures::ConfigureCapture(USceneCaptureComponent2D* C)
{
	FEngineShowFlags& F = C->ShowFlags;
	FPostProcessSettings& P = C->PostProcessSettings;
	P.bOverride_DynamicGlobalIlluminationMethod = true;
	P.bOverride_ReflectionMethod = true;
	P.bOverride_AutoExposureMethod = true;
	P.AutoExposureMethod = AEM_Manual;
	P.bOverride_AutoExposureBias = true;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	P.AutoExposureApplyPhysicalCameraExposure = false;
	F.SetFog(false);
	F.SetMotionBlur(false);
	// The scene's colour as drawn, unlit; nothing on top.
	C->CaptureSource = SCS_SceneColorHDR;
	F.SetAtmosphere(false);
	F.SetDynamicShadows(false);
	F.SetAmbientOcclusion(false);
	F.SetScreenSpaceReflections(false);
	F.SetBloom(false);
	P.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	P.ReflectionMethod = EReflectionMethod::None;
	P.AutoExposureBias = 0;
}

// A scene capture per camera, made as cameras appear.
void FLRRaceCaptures::AddViews(FLRModels& Models, int32 Count)
{
	while (Views.Num() < Count) {
		FView& V = Views.AddDefaulted_GetRef();
		AActor* A = Models.NewActor(FTransform::Identity);
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

void FLRRaceCaptures::CaptureViews()
{
	for (FView& V : Views) {
		if (V.Capture.IsValid() && V.Capture->TextureTarget) {
			V.Capture->CaptureScene();
		}
	}
}


void FLRRaceCaptures::Clear() { Views.Reset(); }

void FLRRaceCaptures::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Targets);
}

// ---- Frame smoothing ----

void FLRSmoothing::Begin(bool bSmoothing, const TArray<TWeakObjectPtr<AActor>>& Actors)
{
	// Everything back where the last pose put it, then what it was before this one.
	for (const FMoved& M : Moved) {
		if (M.Component.IsValid()) {
			M.Component->SetWorldTransform(M.To);
		}
	}
	Moved.Reset();
	BeforePose.Reset();
	if (!bSmoothing) {
		return;
	}
	for (const TWeakObjectPtr<AActor>& A : Actors) {
		if (!A.IsValid()) {
			continue;
		}
		for (UActorComponent* C : A->GetComponents()) {
			if (USceneComponent* S = Cast<USceneComponent>(C)) {
				BeforePose.Add(S, S->GetComponentTransform());
			}
		}
	}
}

void FLRSmoothing::End()
{
	Moved.Reset();
	for (const auto& [C, From] : BeforePose) {
		if (C.IsValid() && !From.Equals(C->GetComponentTransform(), 1e-3)) {
			Moved.Add({C, From, C->GetComponentTransform()});
		}
	}
	BeforePose.Reset();
}

void FLRSmoothing::Apply(float Fraction)
{
	for (const FMoved& M : Moved) {
		if (M.Component.IsValid()) {
			FTransform T;
			T.Blend(M.From, M.To, FMath::Clamp(Fraction, 0.0f, 1.0f));
			M.Component->SetWorldTransform(T);
		}
	}
}

void FLRSmoothing::Clear()
{
	Moved.Reset();
	BeforePose.Reset();
}
