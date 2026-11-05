#pragma once

// A run's harness: what an unattended run does around the game (its own keys, saves and pads; a log of its frames;
// shots of the window). Normal play has none.

#include "CoreMinimal.h"

#include <array>
#include <vector>

struct FLRCoreRun;
class ULRRaceView;

struct FLRHarness
{
	virtual ~FLRHarness() = default;

	// The harness has the input: one 16 ms frame a tick, the player's devices and the saved settings ignored.
	virtual bool OwnsInput() const = 0;
	// Before the boot: the save folder, then the saves as read and the pads present.
	virtual void BootFolder(FString& SaveDir) = 0;
	virtual void BootSaves(std::array<std::vector<uint8_t>, 2>& Saves, uint8& Pads) = 0;
	// After the boot (frame 1), around each frame (the core's frame between BeforeFrame and AfterCoreFrame), and its log.
	virtual void Booted(FLRCoreRun& R) = 0;
	virtual void BeforeFrame(FLRCoreRun& R) = 0;
	virtual void AfterCoreFrame(FLRCoreRun& R, bool bWasRace) = 0;
	virtual void Trace(const FLRCoreRun& R) = 0;
	virtual void AfterFrame(FLRCoreRun& R, int32 FrameIndex, ULRRaceView* RaceView) = 0;
	virtual void Log(const FString& Line) = 0;
};
