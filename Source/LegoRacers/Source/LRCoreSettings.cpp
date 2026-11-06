// The presentation's settings (LRCoreSettings.h).

#include "LRCoreSettings.h"

#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "LRCore/Menus/VideoSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Scalability.h"

namespace
{
const TCHAR* const Section = TEXT("LegoRacers");

// The screen's lists as values (VideoSettings' order).
const FIntPoint Resolutions[] = {{1280, 960}, {1600, 1200}, {1920, 1080}, {2048, 1536}, {2560, 1440}, {3840, 2160}};
const int32 Caps[] = {0, 60, 120, 144, 240};
const int32 Scales[] = {50, 67, 75, 100};

std::u16string Text(const FString& S)
{
	std::u16string Out;
	for (const TCHAR C : S) {
		Out.push_back(char16_t(C));
	}
	return Out;
}

EWindowMode::Type WindowModeOf(int32 Window)
{
	return Window == 2 ? EWindowMode::Fullscreen : Window == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
}
} // namespace

void FLRSettings::Load()
{
	if (const UGameUserSettings* U = GEngine ? GEngine->GetGameUserSettings() : nullptr) {
		Resolution = U->GetScreenResolution();
		const EWindowMode::Type Mode = U->GetFullscreenMode();
		Window = Mode == EWindowMode::Fullscreen ? 2 : Mode == EWindowMode::WindowedFullscreen ? 1 : 0;
		bVSync = U->IsVSyncEnabled();
		FrameCap = FMath::RoundToInt(U->GetFrameRateLimit());
		Quality = FMath::Clamp(U->GetOverallScalabilityLevel(), 0, 4);
	}
	if (!GConfig) {
		return;
	}
	GConfig->GetInt(Section, TEXT("ResolutionX"), Resolution.X, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("ResolutionY"), Resolution.Y, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("Window"), Window, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("VSync"), bVSync, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("FrameCap"), FrameCap, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("Quality"), Quality, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("RenderScale"), RenderScale, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("FrameSmoothing"), bSmoothing, GGameUserSettingsIni);
	Window = FMath::Clamp(Window, 0, 2);
	Quality = FMath::Clamp(Quality, 0, 4);
	RenderScale = FMath::Clamp(RenderScale, 25, 100);
}

void FLRSettings::Save() const
{
	if (!GConfig) {
		return;
	}
	GConfig->SetInt(Section, TEXT("ResolutionX"), Resolution.X, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("ResolutionY"), Resolution.Y, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("Window"), Window, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("VSync"), bVSync, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("FrameCap"), FrameCap, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("Quality"), Quality, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("RenderScale"), RenderScale, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("FrameSmoothing"), bSmoothing, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void FLRSettings::ApplyVideo() const
{
	UGameUserSettings* U = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!U) {
		return;
	}
	U->SetFullscreenMode(WindowModeOf(Window));
	U->SetScreenResolution(Resolution);
	U->SetVSyncEnabled(bVSync);
	U->SetFrameRateLimit(float(FrameCap));
	U->SetOverallScalabilityLevel(Quality);
	U->ApplySettings(false);
}

void FLRSettings::ApplyUnattended() const
{
	Scalability::FQualityLevels Levels;
	Levels.SetFromSingleQualityLevel(Quality);
	Scalability::SetQualityLevels(Levels);
	if (GEngine) {
		GEngine->SetMaxFPS(0);
	}
	if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))) {
		VSync->Set(0, ECVF_SetByCode);
	}
}

void FLRSettings::ToScreen(lr::VideoSettings& Screen) const
{
	const auto Index = [](auto& List, const auto& Value) {
		for (int32 i = 0; i < int32(UE_ARRAY_COUNT(List)); i++) {
			if (List[i] == Value) {
				return int8(i);
			}
		}
		return int8(-1);
	};
	using V = lr::VideoSettings;
	Screen.value[V::Resolution] = Index(Resolutions, Resolution);
	Screen.other[V::Resolution] = Text(FString::Printf(TEXT("%dX%d"), Resolution.X, Resolution.Y));
	Screen.value[V::Window] = int8(Window);
	Screen.value[V::VSync] = bVSync ? 0 : 1;
	Screen.value[V::FrameCap] = Index(Caps, FrameCap);
	Screen.other[V::FrameCap] = Text(FString::Printf(TEXT("%d FPS"), FrameCap));
	Screen.value[V::Quality] = int8(Quality);
	Screen.value[V::RenderScale] = Index(Scales, RenderScale);
	Screen.other[V::RenderScale] = Text(FString::Printf(TEXT("%d PERCENT"), RenderScale));
	Screen.value[V::Smoothing] = bSmoothing ? 0 : 1;
}

void FLRSettings::FromScreen(const lr::VideoSettings& Screen)
{
	using V = lr::VideoSettings;
	const auto At = [&](int32 Row) { return int32(Screen.value[size_t(Row)]); };
	if (At(V::Resolution) >= 0) {
		Resolution = Resolutions[At(V::Resolution)];
	}
	Window = FMath::Clamp(At(V::Window), 0, 2);
	bVSync = At(V::VSync) == 0;
	if (At(V::FrameCap) >= 0) {
		FrameCap = Caps[At(V::FrameCap)];
	}
	Quality = FMath::Clamp(At(V::Quality), 0, 4);
	if (At(V::RenderScale) >= 0) {
		RenderScale = Scales[At(V::RenderScale)];
	}
	bSmoothing = At(V::Smoothing) == 0;
}
