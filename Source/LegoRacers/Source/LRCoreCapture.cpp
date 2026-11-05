// How the race's scene captures draw, and frame smoothing.

#include "LRCoreRace.h"

#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Actor.h"

void ULRRaceView::ConfigureCapture(USceneCaptureComponent2D* C) const
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

// ---- Frame smoothing ----

void ULRRaceView::SmoothBegin()
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

void ULRRaceView::SmoothEnd()
{
	Moved.Reset();
	for (const auto& [C, From] : BeforePose) {
		if (C.IsValid() && !From.Equals(C->GetComponentTransform(), 1e-3)) {
			Moved.Add({C, From, C->GetComponentTransform()});
		}
	}
	BeforePose.Reset();
}

void ULRRaceView::Smooth(float Fraction)
{
	if (!bBuilt) {
		return;
	}
	for (const FMoved& M : Moved) {
		if (M.Component.IsValid()) {
			FTransform T;
			T.Blend(M.From, M.To, FMath::Clamp(Fraction, 0.0f, 1.0f));
			M.Component->SetWorldTransform(T);
		}
	}
	CaptureViews();
}
