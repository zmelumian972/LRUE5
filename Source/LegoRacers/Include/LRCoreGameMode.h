#pragma once

// The game on LRCore's Program: UE boots it at the legal screen with the save files it
// finds, feeds it the keyboard, mouse and pads, paints its 640x480 draw list on the CPU as the original's device drew it
// (lr::paint) into textures between its 3D views and scales them to the window (4:3, pillarboxed), poses
// the menus' 3D views and draws the race in the Classic look from World and its show (LRCoreRace.h), plays
// its sound and tune cues and writes the save files it hands out.
//
// Its files: the data cache of the player's install (LRCoreGameData.h); meshes and sounds are made from it at runtime.
// The saves:
// <Saved>/Save, as <slot>/racers.lrsave.
// An unattended run's harness (LRCoreHarness.h) can drive it: its own keys, saves and pads, a log, shots.

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "LRCoreRun.h"
#include "LRCoreGameMode.generated.h"

class ACameraActor;
class UAudioComponent;
class UCanvas;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneCaptureComponent2D;
class UTexture2D;
class UTextureRenderTarget2D;
class ULRRaceMixer;
class ULRRaceView;

UCLASS()
class ALRCoreGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALRCoreGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// The HUD's: the 2D layers and the 3D views, in the draw list's order.
	void DrawScreen(UCanvas* Canvas);

private:
	TUniquePtr<FLRCoreRun> Run; // the program, its files and the input state (plain C++)

	// The 2D layers: under the first 3D view, then over each; the 3D views' targets and captures, one per view.
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> ScreenLayers;
	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> ViewTargets;
	UPROPERTY()
	TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;
	UPROPERTY()
	TObjectPtr<ULRRaceView> RaceView; // the race's 3D (LRCoreRace.h)
	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;
	UPROPERTY()
	TObjectPtr<UAudioComponent> Music;
	UPROPERTY()
	TObjectPtr<ULRRaceMixer> RaceMixer; // the race's voices mixed (LRCoreSound.h)
	UPROPERTY()
	TObjectPtr<UAudioComponent> RaceSound; // playing it
	UPROPERTY()
	TObjectPtr<UMaterialInterface> VertexColour;
	UPROPERTY()
	TMap<FString, TObjectPtr<UObject>> Assets; // the sounds made from the cache, by path (null: none)

	bool FindData();
	bool BootOrQuit();
	bool Boot();
	void StepFrame(int32 FrameIndex);
	void RunVideo();
	void OpenVideo();
	void Sounds();
	void PaintLayers();
	void WriteSaves();
	// 3D: the menus' views, the race.
	void SetUpLights();
	void PoseViews();
	void BuildRace();
	void ClearRace();
	AActor* NewActor(const FTransform& At);
};

UCLASS()
class ALRCoreHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
