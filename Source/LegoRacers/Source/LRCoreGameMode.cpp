// The game on the core: booting it, its inputs, its frames, its 2D layers, its sounds and its saves, and the
// 3D: the menus' views (worlds, cars, pieces) and the race, plainly.

#include "LRCoreGameMode.h"
#include "LRCoreLog.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRun.h"
#include "LRCoreRace.h"
#include "LRCoreSettings.h"
#include "LRCoreSound.h"
#include "LRCore/Menus/VideoSettings.h"
#include "LRCore/Show/ShowState.h"
#include "Engine/GameViewportClient.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "LRData/Cars.h"
#include "LRData/Model.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"
#include "ProceduralMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "LRCore/Common/Program.h"
#include "LRCoreGameData.h"
#include "LRData/Look.h"
#include "LRData/Sound.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <algorithm>

using namespace LRUe;

namespace
{
// The race view's look from the settings.
ULRRaceView::FLook LookOf(const FLRSettings& S)
{
	ULRRaceView::FLook L;
	L.Scale = float(S.RenderScale) / 100;
	return L;
}

// A save slot's file in the save folder.
FString SavePath(const FString& Dir, int32 Slot) { return Dir / FString::Printf(TEXT("%d/racers.lrsave"), Slot); }

// A menu view's capture on its holder: only the view's actor, unlit and without effects, at a fixed exposure.
USceneCaptureComponent2D* NewViewCapture(AActor* Holder)
{
	USceneCaptureComponent2D* C = NewObject<USceneCaptureComponent2D>(Holder);
	C->bCaptureEveryFrame = false;
	C->bCaptureOnMovement = false;
	C->bAlwaysPersistRenderingState = true;
	C->CaptureSource = SCS_SceneColorHDR;
	C->CompositeMode = SCCM_Composite;
	C->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	C->ShowFlags.SetAtmosphere(false);
	C->ShowFlags.SetFog(false);
	C->ShowFlags.SetDynamicShadows(false);
	C->ShowFlags.SetAmbientOcclusion(false);
	C->ShowFlags.SetScreenSpaceReflections(false);
	C->ShowFlags.SetMotionBlur(false);
	C->ShowFlags.SetBloom(false);
	C->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	C->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	C->PostProcessSettings.bOverride_ReflectionMethod = true;
	C->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	C->PostProcessSettings.bOverride_AutoExposureMethod = true;
	C->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	C->PostProcessSettings.bOverride_AutoExposureBias = true;
	C->PostProcessSettings.AutoExposureBias = 0;
	C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	C->RegisterComponent();
	return C;
}

} // namespace

ALRCoreGameMode::ALRCoreGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	HUDClass = ALRCoreHUD::StaticClass();
	DefaultPawnClass = nullptr;
}

void ALRCoreGameMode::StartPlay()
{
	Super::StartPlay();
	Run = MakeUnique<FLRCoreRun>();
	if (!FindData()) {
		FPlatformMisc::RequestExit(false);
	}
}

// The install and its cache: opened if it is current, else built on a worker thread while a progress
// screen shows (the boot waits for it in Tick).
bool ALRCoreGameMode::FindData()
{
	FLRCoreRun& R = *Run;
	const bool bScripted = FApp::IsUnattended() || R.OwnsInput();
	FString Dir = FLRCoreData::FindInstall(bScripted);
	if (Dir.IsEmpty() && !bScripted) {
		Dir = FLRCoreData::AskForInstall();
	}
	if (Dir.IsEmpty()) {
		UE_LOG(LogLRCore, Error, TEXT("no LEGO Racers install (a folder with LEGO.JAM and LEGORacers.exe)"));
		return false;
	}
	if (R.Files.OpenCurrent(Dir)) {
		UE_LOG(LogLRCore, Display, TEXT("the data cache of %s"), *Dir);
		return BootOrQuit();
	}
	FString Why;
	if (!FLRCoreData::CheckInstall(Dir, Why)) {
		UE_LOG(LogLRCore, Error, TEXT("%s (%s)"), *Why, *Dir);
		Dir = bScripted ? FString() : FLRCoreData::AskForInstall(Why);
		if (Dir.IsEmpty()) {
			return false;
		}
		if (R.Files.OpenCurrent(Dir)) {
			return BootOrQuit();
		}
	}
	UE_LOG(LogLRCore, Display, TEXT("building the data cache from %s"), *Dir);
	R.Files.StartBuild(Dir);
	return true;
}

bool ALRCoreGameMode::BootOrQuit()
{
	if (!Boot()) {
		UE_LOG(LogLRCore, Error, TEXT("can't start the game (see above)"));
		return false;
	}
	return true;
}

void ALRCoreGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Run) {
		if (Run->Input && FSlateApplication::IsInitialized()) {
			FSlateApplication::Get().UnregisterInputPreProcessor(Run->Input);
		}
		Run.Reset();
	}
	Super::EndPlay(Reason);
}

bool ALRCoreGameMode::Boot()
{
	FLRCoreRun& R = *Run;
	if (!R.Data.load(R.Files.Cache)) {
		UE_LOG(LogLRCore, Error, TEXT("the menus' files don't load"));
		return false;
	}
	// The save files.
	R.SaveDir = FPaths::ProjectSavedDir() / TEXT("Save");
	if (R.Harness) {
		R.Harness->BootFolder(R.SaveDir);
	}
	std::array<std::vector<uint8_t>, 2> Saves;
	for (int32 Slot = 0; Slot < 2; Slot++) {
		TArray<uint8> Bytes;
		const FString File = SavePath(R.SaveDir, Slot);
		if (FFileHelper::LoadFileToArray(Bytes, *File, FILEREAD_Silent)) {
			Saves[Slot].assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
		}
	}
	uint8 PadCount = FSlateApplication::Get().IsGamepadAttached() ? 1 : 0;
	if (R.Harness) {
		R.Harness->BootSaves(Saves, PadCount);
	}
	R.Program.boot(R.Data, R.Files.Cache, PadCount, Saves);
#if PLATFORM_LINUXARM64
	// The AYN Thor has no keyboard: player 1 left on one (a save from a start that missed the pad) gets the pad.
	if (PadCount > 0 && R.Program.game.settings.layout[0] >= 2) {
		R.Program.game.pickLayout(0, 0);
	}
#endif
	R.Frames = 1; // the boot is frame 1
	if (R.Harness) {
		R.Harness->Booted(R);
	}
	// The presentation's settings.
	if (R.OwnsInput()) {
		R.Settings.ApplyUnattended(); // (the saved ones are ignored)
	}
	else {
		R.Settings.Load();
		R.Settings.ApplyVideo();
	}

	R.Input = MakeShared<FLRCoreRun::FInput>();
	R.Input->Run = Run.Get();
	FSlateApplication::Get().RegisterInputPreProcessor(R.Input);
	if (APlayerController* Player = GetWorld()->GetFirstPlayerController()) {
		// The core draws its own cursor: the system's is hidden but free (absolute positions).
		Player->bShowMouseCursor = true;
		Player->CurrentMouseCursor = EMouseCursor::None;
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		Player->SetInputMode(Mode);
	}
	SetUpLights();
	// Every 3D view is a scene capture composited with the 2D (the window's own view of the world isn't drawn).
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport()) {
		Viewport->bDisableWorldRendering = true;
	}
	R.bBooted = true;
	UE_LOG(LogLRCore, Display, TEXT("booted (%d images, %d pads)"), int32(R.Data.images.size()), PadCount);
	return true;
}

void ALRCoreGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Run && Run->Files.IsBuilding()) {
		if (Run->Files.Poll() && (!Run->Files.Error.IsEmpty() || !BootOrQuit())) {
			UE_LOG(LogLRCore, Error, TEXT("the data cache: %s"), *Run->Files.Error);
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	if (!Run || !Run->bBooted || Run->Program.mode == lr::Program::Mode::Quit) {
		return;
	}
	FLRCoreRun& R = *Run;
	// The mouse, in the 640x480 screen as the HUD last placed it.
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	float X = 0, Y = 0;
	if (Player && Player->GetMousePosition(X, Y) && !R.OwnsInput()) {
		R.MouseX = FMath::Clamp(int32((X - R.Origin.X) / R.Scale), 0, lr::ScreenWidth - 1);
		R.MouseY = FMath::Clamp(int32((Y - R.Origin.Y) / R.Scale), 0, lr::ScreenHeight - 1);
	}
	// The real time in 16 ms frames (four at most).
	R.ClockMs = FMath::Min(R.ClockMs + DeltaSeconds * 1000.0, 64.0 + 16.0);
	int32 Count = int32(R.ClockMs / 16.0);
	R.ClockMs -= Count * 16.0;
	if (R.OwnsInput()) {
		Count = 1; // (one 16 ms frame a tick)
	}
	for (int32 i = 0; i < Count && R.Program.mode != lr::Program::Mode::Quit; i++) {
		StepFrame(i);
	}
	// Frame smoothing: above 62.5 Hz a full-screen one-player race shows the view between its last two
	// frames by the next frame's fraction gone.
	const lr::World& W = R.Program.world;
	bool bSmooth = R.Settings.bSmoothing && R.RaceView && RaceView && RaceView->bBuilt && W.raceView.cameras.size() == 1 &&
		!W.raceView.cameras[0].hidden && DeltaSeconds < 0.016f;
	bSmooth = bSmooth && !R.OwnsInput();
	if (RaceView) {
		RaceView->bSmoothing = bSmooth;
	}
	if (Count == 0) {
		if (bSmooth) {
			RaceView->Smooth(float(R.ClockMs / 16.0));
		}
		return;
	}
	Sounds();
	PaintLayers();
	if (R.RaceView && RaceView) {
		RaceView->Pose(W, R.Show, R.Scale, R.WideRace ? R.WindowAspect : 4.0f / 3);
		if (bSmooth) {
			RaceView->Smooth(float(R.ClockMs / 16.0));
		}
	}
	PoseViews();
}

// The VIDEO SETTINGS screen's frame: its keys and the mouse, the values applied and saved at once,
// the menus' sounds; Esc or B closes it.
void ALRCoreGameMode::RunVideo()
{
	FLRCoreRun& R = *Run;
	lr::VideoSettings& V = R.Video;
	TArray<int32> Changed;
	for (const auto& [Key, bPad] : R.VideoKeys) {
		Changed.Add(V.key(Key, bPad));
	}
	R.VideoKeys.Reset();
	const uint32 Pressed = R.Buttons & ~R.VideoButtons;
	R.VideoButtons = R.Buttons;
	Changed.Add(V.mouse(R.MouseX, R.MouseY, Pressed));
	for (const int32 Sound : V.sounds) {
		R.Sounds.Add(Sound);
	}
	V.sounds.clear();
	bool bVideo = false, bAny = false;
	for (const int32 Row : Changed) {
		bAny |= Row >= 0;
		bVideo |= Row >= lr::VideoSettings::Resolution && Row <= lr::VideoSettings::Quality;
	}
	if (bAny) {
		R.Settings.FromScreen(V);
		if (bVideo) {
			R.Settings.ApplyVideo();
		}
		if (!R.OwnsInput()) {
			R.Settings.Save();
		}
	}
	if (V.closed) {
		R.bVideoOpen = false;
		if (APlayerController* Player = GetWorld()->GetFirstPlayerController()) {
			Player->CurrentMouseCursor = EMouseCursor::None;
		}
	}
}

// OPTIONS > VIDEO OPTIONS: the screen opens on its first row with the settings as they are.
void ALRCoreGameMode::OpenVideo()
{
	FLRCoreRun& R = *Run;
	R.bVideoOpen = true;
	R.Video = lr::VideoSettings();
	R.Settings.ToScreen(R.Video);
	R.VideoKeys.Reset();
	R.VideoButtons = R.Buttons;
	if (APlayerController* Player = GetWorld()->GetFirstPlayerController()) {
		Player->CurrentMouseCursor = EMouseCursor::Default; // (the core's cursor isn't drawn over it)
	}
	R.Log(FString::Printf(TEXT("%d,video_settings,1"), R.Frames));
}

void ALRCoreGameMode::StepFrame(int32 FrameIndex)
{
	FLRCoreRun& R = *Run;
	lr::Program& P = R.Program;
	R.Frames++;
	if (R.Harness) {
		R.Harness->BeforeFrame(R);
	}
	// The players' race keys from their controls.
	lr::Program::Input In;
	const std::vector<lr::MenuKey> Keys(R.Keys.GetData(), R.Keys.GetData() + R.Keys.Num());
	R.Keys.Reset();
	In.keys = Keys;
	In.mouseX = R.MouseX, In.mouseY = R.MouseY, In.buttons = R.Buttons;
	In.windowAspect = R.WindowAspect;
	for (size_t Player = 0; Player < 2; Player++) {
		const lr::KeyLayout& Layout = P.game.settings.layouts[P.game.settings.layout[Player] % 5];
		const TArray<uint32>& Down = Layout.kind == lr::KeyLayout::Joystick ? R.PadHeld[Layout.device & 1] : R.Held;
		In.players[Player] = lr::Program::raceKeys(Layout, {Down.GetData(), size_t(Down.Num())});
	}
	if (R.bVideoOpen) {
		// (The VIDEO SETTINGS screen has the input: the core gets none.)
		In.keys = {};
		In.buttons = 0;
		In.players = decltype(In.players){};
		RunVideo();
	}
	const bool bWasRace = P.mode == lr::Program::Mode::Race;
	P.frame(16, In);
	if (R.Harness) {
		R.Harness->AfterCoreFrame(R, bWasRace);
	}

	for (const int32 Sound : P.menus.sounds) {
		R.Sounds.Add(Sound);
	}
	// The tunes: the menus' as they start one or stop theirs; the race's cues (presentation draws the racing tune from
	// the candidates).
	if (P.mode == lr::Program::Mode::Menus && (P.menus.tuneStarted || (P.menus.tune < 0 && !R.Tune.IsEmpty()))) {
		const int32 T = P.menus.tune;
		R.SetTune(T >= 0 && size_t(T) < R.Data.music.names.size() ? FString(UTF8_TO_TCHAR(R.Data.music.names[size_t(T)].c_str()))
																  : FString(),
			P.menus.tuneLoops);
	}
	WriteSaves();
	if (P.menus.videoSettings) {
		P.menus.videoSettings = false;
		OpenVideo();
	}
	if (P.ended) {
		ClearRace();
	}
	if (P.loaded) {
		BuildRace();
		R.SetTune(FString(), false);
	}
	else if (P.mode == lr::Program::Mode::Race) {
		// The show runs on the race's ms (fast forward's x1.75; nothing while paused).
		const lr::World& W = P.world;
		R.Show.advance(W, W.session.paused() ? 0 : (W.session.brief.cheats & lr::CheatFastForward) ? int32(16 * 1.75f) : 16);
	}
	if (P.mode == lr::Program::Mode::Race && P.world.session.tune.started) {
		const lr::RaceTune& T = P.world.session.tune;
		const int32 Index = T.choices.empty() ? T.tune : T.choices[size_t(FMath::RandHelper(int32(T.choices.size())))];
		R.SetTune(Index >= 0 && Index < R.RaceMusic.Num() ? R.RaceMusic[Index] : FString(), T.loops);
	}
	R.RaceView = P.mode == lr::Program::Mode::Race && !P.loaded;
	if (R.Harness) {
		R.Harness->Trace(R);
	}
	if (P.mode == lr::Program::Mode::Quit) {
		UE_LOG(LogLRCore, Display, TEXT("QUIT chosen at frame %d"), R.Frames);
		R.Log(FString::Printf(TEXT("%d,quit,"), R.Frames));
		FPlatformMisc::RequestExit(false);
	}
	if (R.Harness) {
		R.Harness->AfterFrame(R, FrameIndex, RaceView);
	}
}

void ALRCoreGameMode::WriteSaves()
{
	FLRCoreRun& R = *Run;
	for (const lr::Menus::SaveFile& S : R.Program.menus.saves) {
		const FString File = SavePath(R.SaveDir, S.slot);
		if (!FFileHelper::SaveArrayToFile(TArrayView<const uint8>(S.bytes.data(), int32(S.bytes.size())), *File)) {
			UE_LOG(LogLRCore, Error, TEXT("can't write %s"), *File);
		}
	}
}

// The sounds the frames started and the tunes (the menus' and the race's), at the settings' volumes.
void ALRCoreGameMode::Sounds()
{
	FLRCoreRun& R = *Run;
	const lr::Program& P = R.Program;
	const float SoundVolume = P.game.settings.sound / 255.0f, MusicVolume = P.game.settings.music / 255.0f;
	// A sound or tune of the cache (a .PCM's path, or a tune's file name: matched case-insensitively), made once (a tune
	// once per looping or not).
	const auto Wave = [&](const FString& Path, bool Loops) -> USoundBase* {
		const FString Key = Path.ToUpper() + (Loops ? TEXT("#loop") : TEXT(""));
		TObjectPtr<UObject>* Found = Assets.Find(Key);
		if (!Found) {
			lr::Sound Sound;
			ULRCoreWave* Made = R.Files.Cache.get(TCHAR_TO_UTF8(*Path), Sound) && !Sound.samples.empty()
				? ULRCoreWave::Make(this, Sound, Loops)
				: nullptr;
			Found = &Assets.Add(Key, Made);
			if (!Made) {
				UE_LOG(LogLRCore, Warning, TEXT("no sound %s in the data cache"), *Path);
			}
		}
		return Cast<USoundBase>(Found->Get());
	};
	const auto PlayTune = [&](USoundBase* W) {
		if (Music) {
			Music->Stop();
			Music = nullptr;
		}
		if (W) {
			Music = UGameplayStatics::CreateSound2D(this, W, MusicVolume, 1.0f, 0.0f, nullptr, true);
			if (Music) {
				Music->Play();
			}
		}
	};
	for (const int32 Sound : R.Sounds) {
		if (Sound > 0 && size_t(Sound) <= R.Data.sounds.names.size()) {
			const FString Name = FPaths::GetBaseFilename(UTF8_TO_TCHAR(R.Data.sounds.names[size_t(Sound - 1)].c_str())).ToUpper();
			if (USoundBase* W = Wave(TEXT("MENUDATA/SOUNDS/") + Name + TEXT(".PCM"), false)) {
				UGameplayStatics::PlaySound2D(this, W, SoundVolume);
			}
		}
	}
	R.Sounds.Reset();
	// The race's voices as the show holds them after the tick's frames.
	if (RaceMixer && P.mode == lr::Program::Mode::Race) {
		RaceMixer->Update(R.Show, R.Files.Cache);
	}
	if (R.TuneChanged) {
		R.TuneChanged = false;
		PlayTune(R.Tune.IsEmpty() ? nullptr : Wave(R.Tune, R.TuneLoops));
	}
	if (Music) {
		Music->SetVolumeMultiplier(MusicVolume);
	}
}

// The draw list painted: a layer under the first 3D view, then one over each.
void ALRCoreGameMode::PaintLayers()
{
	FLRCoreRun& R = *Run;
	const lr::Menus& M = R.Program.menus;
	const std::vector<lr::MenuElement>& E = M.elements;
	R.Views.Reset();
	int32 Layer = 0;
	const auto PaintSpan = [&](std::span<const lr::MenuElement> Span, std::span<const lr::MenuGlyph> Glyphs) {
		if (R.LayerPixels.Num() <= Layer) {
			R.LayerPixels.SetNum(Layer + 1);
			R.LayerCrcs.Add(0);
		}
		TArray<uint32>& Pixels = R.LayerPixels[Layer];
		Pixels.Init(Layer == 0 && !R.RaceView ? 0xff000000u : 0u, lr::ScreenWidth * lr::ScreenHeight);
		lr::paint(R.Data, M.language, Span, Glyphs, Pixels.GetData());
		if (ScreenLayers.Num() <= Layer) {
			UTexture2D* T = UTexture2D::CreateTransient(lr::ScreenWidth, lr::ScreenHeight, PF_B8G8R8A8);
			T->Filter = TF_Bilinear; // the 640x480 screen scaled to the window filtered
			T->SRGB = true;
			T->UpdateResource();
			ScreenLayers.Add(T);
			R.LayerCrcs[Layer] = 0;
		}
		const uint32 Crc = FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * 4);
		if (Crc != R.LayerCrcs[Layer]) {
			R.LayerCrcs[Layer] = Crc;
			uint8* Copy = new uint8[lr::ScreenWidth * lr::ScreenHeight * 4];
			FMemory::Memcpy(Copy, Pixels.GetData(), lr::ScreenWidth * lr::ScreenHeight * 4);
			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, lr::ScreenWidth, lr::ScreenHeight);
			ScreenLayers[Layer]->UpdateTextureRegions(0, 1, Region, lr::ScreenWidth * 4, 4, Copy,
				[](uint8* Data, const FUpdateTextureRegion2D* Regions) {
					delete[] Data;
					delete Regions;
				});
		}
		Layer++;
	};
	// A one-player race in a window wider than 4:3: the 3D
	// fills the window and the HUD's elements keep to its edges: those on the screen's left third with the window's left,
	// the right third's with its right, the middle's centred. Layers 0..2: the HUD's left, middle and right; 3: the rest
	// (the demo's line, the fade, dialogs and standings, 4:3 centred).
	if (R.bVideoOpen) {
		// The VIDEO SETTINGS screen over everything (the menus' 3D views aren't drawn under it).
		R.Video.draw(R.Data, M.language, R.VideoElements, R.VideoGlyphs);
		PaintSpan(R.VideoElements, R.VideoGlyphs);
		return;
	}
	R.WideRace = R.RaceView && R.Program.world.raceView.cameras.size() == 1 && R.WindowAspect > 4.0f / 3 + 0.01f;
	if (R.WideRace) {
		const std::pair<uint32_t, uint32_t> Hud = M.hud.layers[0];
		std::vector<lr::MenuElement> Groups[3], Rest;
		for (size_t i = 0; i < E.size(); i++) {
			if (i < Hud.first || i >= Hud.second) {
				Rest.push_back(E[i]);
				continue;
			}
			const lr::MenuElement& El = E[i];
			const float X = El.kind == lr::MenuElement::Triangle ? (El.vertices[0][0] + El.vertices[1][0] + El.vertices[2][0]) / 3
				: El.kind == lr::MenuElement::Glyphs			 ? float(El.box[0]) + float(El.box[2]) / 2
																 : (El.dest[0] + El.dest[2]) / 2;
			Groups[X < lr::ScreenWidth / 3.0f ? 0 : X > 2 * lr::ScreenWidth / 3.0f ? 2 : 1].push_back(El);
		}
		for (const std::vector<lr::MenuElement>& G : Groups) {
			PaintSpan(G, M.glyphs);
		}
		PaintSpan(Rest, M.glyphs);
		return;
	}
	size_t First = 0;
	for (size_t i = 0; i < E.size(); i++) {
		if (E[i].kind != lr::MenuElement::View3D) {
			continue;
		}
		PaintSpan(std::span(E.data() + First, i - First), M.glyphs);
		First = i + 1;
		const FIntRect Rect(int32(E[i].dest[0]), int32(E[i].dest[1]), int32(E[i].dest[2]), int32(E[i].dest[3]));
		R.Views.Add({Rect, E[i].scene});
	}
	PaintSpan(std::span(E.data() + First, E.size() - First), M.glyphs);
}

void ALRCoreGameMode::DrawScreen(UCanvas* Canvas)
{
	if (!Run || !Canvas) {
		return;
	}
	FLRCoreRun& R = *Run;
	if (!R.bBooted) {
		// The data cache being built (first start, or the install changed).
		FCanvasTileItem Black(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor::Black);
		Canvas->DrawItem(Black);
		const FString Text = FString::Printf(TEXT("PREPARING THE GAME DATA FROM %s ... %d%%"), *R.Files.GameDir.ToUpper(),
			int32(R.Files.Progress() * 100));
		Canvas->SetDrawColor(FColor::White);
		float W = 0, H = 0;
		Canvas->StrLen(GEngine->GetMediumFont(), Text, W, H);
		Canvas->DrawText(GEngine->GetMediumFont(), Text, (Canvas->ClipX - W) * 0.5f, (Canvas->ClipY - H) * 0.5f);
		return;
	}
	// The 640x480 screen scaled to the window, 4:3 (pillarboxed or letterboxed).
	R.Scale = FMath::Min(Canvas->ClipX / lr::ScreenWidth, Canvas->ClipY / lr::ScreenHeight);
	R.WindowAspect = Canvas->ClipY > 0 ? float(Canvas->ClipX / Canvas->ClipY) : 4.0f / 3;
	R.Origin = FVector2D((Canvas->ClipX - lr::ScreenWidth * R.Scale) * 0.5f, (Canvas->ClipY - lr::ScreenHeight * R.Scale) * 0.5f);
	FCanvasTileItem Black(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor::Black);
	Black.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Black);
	// The race's views fill their viewports (a wide one-player race's the window's width); the 2D (the HUD, dialogs, the
	// fade) goes over them.
	if (R.RaceView && RaceView) {
		for (int32 v = 0; v < RaceView->ViewCount(); v++) {
			UTextureRenderTarget2D* T = RaceView->ViewTarget(v);
			if (T && T->GetResource()) {
				const FIntRect Rect = RaceView->ViewRect(v);
				FVector2D At = R.Origin + FVector2D(Rect.Min) * R.Scale, Size = FVector2D(Rect.Size()) * R.Scale;
				if (R.WideRace) {
					At.X = 0;
					Size.X = Canvas->ClipX;
				}
				FCanvasTileItem Item(At, T->GetResource(), Size, FLinearColor::White);
				Item.BlendMode = SE_BLEND_Opaque;
				Canvas->DrawItem(Item);
			}
		}
	}
	if (R.WideRace) {
		const float Right = Canvas->ClipX - lr::ScreenWidth * R.Scale;
		const float Xs[4] = {0, float(R.Origin.X), Right, float(R.Origin.X)};
		for (int32 Layer = 0; Layer < 4 && Layer < ScreenLayers.Num(); Layer++) {
			if (ScreenLayers[Layer] && ScreenLayers[Layer]->GetResource()) {
				FCanvasTileItem Item(FVector2D(Xs[Layer], R.Origin.Y), ScreenLayers[Layer]->GetResource(),
					FVector2D(lr::ScreenWidth, lr::ScreenHeight) * R.Scale, FLinearColor::White);
				Item.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Item);
			}
		}
		return;
	}
	for (int32 Layer = 0; Layer < ScreenLayers.Num() && Layer <= R.Views.Num(); Layer++) {
		if (ScreenLayers[Layer] && ScreenLayers[Layer]->GetResource()) {
			FCanvasTileItem Item(R.Origin, ScreenLayers[Layer]->GetResource(), FVector2D(lr::ScreenWidth, lr::ScreenHeight) * R.Scale,
				FLinearColor::White);
			Item.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Item);
		}
		if (Layer < R.Views.Num() && Layer < R.ViewShown.Num() && R.ViewShown[Layer] && Layer < ViewTargets.Num() && ViewTargets[Layer] &&
			ViewTargets[Layer]->GetResource()) {
			const FIntRect& V = R.Views[Layer].Rect;
			FCanvasTileItem Item(R.Origin + FVector2D(V.Min) * R.Scale, ViewTargets[Layer]->GetResource(), FVector2D(V.Size()) * R.Scale,
				FLinearColor::White);
			Item.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Item);
		}
	}
}

void ALRCoreHUD::DrawHUD()
{
	Super::DrawHUD();
	if (ALRCoreGameMode* Mode = Cast<ALRCoreGameMode>(GetWorld()->GetAuthGameMode())) {
		Mode->DrawScreen(Canvas);
	}
}

// ---- 3D ----

void ALRCoreGameMode::SetUpLights()
{
	VertexColour = GEngine->VertexColorMaterial;
	// (No lights: Classic is unlit.)
	Camera = GetWorld()->SpawnActor<ACameraActor>(FVector(0, 0, 100000), FRotator::ZeroRotator);
	if (APlayerController* Player = GetWorld()->GetFirstPlayerController(); Player && Camera) {
		Camera->GetCameraComponent()->SetFieldOfView(75);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		Player->SetViewTarget(Camera);
	}
}

// The menus' 3D views: each view's world and things built and posed by the race view's menu side
// (LRCoreMenus.cpp) from the core's scene, at its own place far below, captured into its target over the 2D under it.
void ALRCoreGameMode::PoseViews()
{
	FLRCoreRun& R = *Run;
	const lr::Program& P = R.Program;
	const lr::Menus& M = P.menus;
	if (!RaceView) {
		RaceView = NewObject<ULRRaceView>(this);
	}
	R.ViewShown.Init(false, R.Views.Num());
	for (int32 v = 0; v < R.Views.Num(); v++) {
		const FLRCoreRun::FView& View = R.Views[v];
		if (View.Scene < 0 || size_t(View.Scene) >= M.scenes.size() || View.Rect.Area() <= 0) {
			continue;
		}
		const lr::MenuScene& Scene = M.scenes[size_t(View.Scene)];
		const FVector Place(0, 0, -1.0e6 * (v + 1));
		AActor* Actor = nullptr;
		const FLRMenuCamera Cam = RaceView->PoseMenuView(GetWorld(), R.Files.Cache, R.Data, v, Place, Scene, M, P.game, Actor);
		if (!Cam.bValid || !Actor) {
			continue;
		}
		while (Captures.Num() <= v) {
			Captures.Add(NewViewCapture(LRUe::NewRootedActor(GetWorld(), FTransform::Identity)));
			ViewTargets.Add(nullptr);
		}
		// The target at the view's size in the window, the 2D under the view drawn into it first (a capture's alpha is
		// its inverse coverage).
		const FIntPoint Size(FMath::Max(1, int32(View.Rect.Width() * R.Scale)), FMath::Max(1, int32(View.Rect.Height() * R.Scale)));
		UTextureRenderTarget2D* Target = ViewTargets[v];
		if (!Target || Target->SizeX != Size.X || Target->SizeY != Size.Y) {
			Target = NewTarget(this, Size.X, Size.Y);
			ViewTargets[v] = Target;
		}
		UCanvas* Under = nullptr;
		FVector2D Extent;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Under, Extent, Context);
		for (int32 k = 0; Under && k <= v && k < ScreenLayers.Num(); k++) {
			Under->K2_DrawTexture(ScreenLayers[k], FVector2D::ZeroVector, Extent,
				FVector2D(View.Rect.Min) / FVector2D(lr::ScreenWidth, lr::ScreenHeight),
				FVector2D(View.Rect.Size()) / FVector2D(lr::ScreenWidth, lr::ScreenHeight), FLinearColor::White,
				k == 0 ? BLEND_Opaque : BLEND_Translucent);
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
		USceneCaptureComponent2D* C = Captures[v];
		C->TextureTarget = Target;
		C->ShowOnlyActors.Reset();
		C->ShowOnlyActors.Add(Actor);
		C->bOverride_CustomNearClippingPlane = true;
		C->CustomNearClippingPlane = FMath::Max(0.1f, Scene.nearPlane) * Cm;
		C->SetWorldLocationAndRotation(Cam.Eye, Cam.Rotation);
		// (The vertical field of view: UE's is horizontal.)
		const float Aspect = float(View.Rect.Width()) / float(View.Rect.Height());
		C->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Cam.Fov) * 0.5f) * Aspect));
		C->CaptureScene();
		R.ViewShown[v] = true;
	}
}

// The race, set up: its 3D built from the cache (LRCoreRace.h), its show loaded, its music list.
void ALRCoreGameMode::BuildRace()
{
	FLRCoreRun& R = *Run;
	const lr::Program& P = R.Program;
	const lr::World& W = P.world;
	ClearRace();
	const std::string Dir = "GAMEDATA/" + W.session.brief.track + "/";
	lr::NameList Tunes;
	R.RaceMusic.Reset();
	if (R.Files.Cache.get(Dir + "LEGOMSC", Tunes)) {
		for (const std::string& Name : Tunes.names) {
			R.RaceMusic.Add(UTF8_TO_TCHAR(Name.c_str()));
		}
	}
	R.Show.effectsVolume = P.game.settings.sound / 255.0f;
	if (!R.Show.load(R.Files.Cache, W)) {
		UE_LOG(LogLRCore, Error, TEXT("the race's sounds and effects don't load: the race runs silent"));
	}
	// The race's sounds: one mixed stream, playing from the race's setup on (silent until the show gives a voice).
	if (!RaceMixer) {
		RaceMixer = ULRRaceMixer::Make(this);
	}
	if (!RaceSound || !RaceSound->IsPlaying()) {
		RaceSound = UGameplayStatics::CreateSound2D(this, RaceMixer, 1.0f, 1.0f, 0.0f, nullptr, true);
		if (RaceSound) {
			RaceSound->Play();
		}
	}
	if (!RaceView) {
		RaceView = NewObject<ULRRaceView>(this);
	}
	RaceView->Settings = LookOf(R.Settings);
	RaceView->Build(GetWorld(), R.Files.Cache, W, P.game, R.Data);
	RaceView->Pose(W, R.Show, R.Scale);
}

void ALRCoreGameMode::ClearRace()
{
	if (RaceView) {
		RaceView->Clear();
	}
	// (Leaving the race unloads its banks and every voice)
	if (RaceMixer) {
		RaceMixer->Clear();
	}
}
