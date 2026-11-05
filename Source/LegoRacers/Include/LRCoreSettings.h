#pragma once

// The presentation's own settings: the window and UE's rendering, saved in UE's user
// settings under [LegoRacers] (never in the core's save) and applied at once.

#include "CoreMinimal.h"

namespace lr
{
struct VideoSettings;
}

struct FLRSettings
{
	FIntPoint Resolution = FIntPoint(1600, 1200);
	int32 Window = 0;      // 0 windowed, 1 borderless, 2 fullscreen
	bool bVSync = false;
	int32 FrameCap = 0;    // frames per second, 0 none
	int32 Quality = 3;     // UE's scalability, 0 low .. 4 cinematic
	int32 RenderScale = 100; // percent of the race views' pixels
	bool bSmoothing = false;

	// The saved settings (missing ones: the defaults above, the window's from UE's own user settings).
	void Load();
	void Save() const;
	// The window, VSync, the frame cap and the quality, through UE's user settings.
	void ApplyVideo() const;

	void ToScreen(lr::VideoSettings& Screen) const;
	void FromScreen(const lr::VideoSettings& Screen);
};
