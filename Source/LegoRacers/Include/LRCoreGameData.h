#pragma once

// The game's data on the UE side: finding the player's install (the saved game folder,
// <project or executable folder>/GameData, in developer builds <project>/build/gamedir; else the platform's file
// dialog), the data cache LRData builds from it on first start (and again when LEGO.JAM, LEGORacers.exe or the cache's
// format changes) under <Saved>/DataCache, and the sounds made from the cache's PCM at runtime. Nothing under Content/
// is made from game data.

#include "CoreMinimal.h"
#include "LRData/Cache.h"
#include "Sound/SoundWaveProcedural.h"

#include <array>
#include <atomic>
#include <memory>

#include "LRCoreGameData.generated.h"

namespace lr
{
struct Sound;
}

// A sound from the cache: its 16-bit PCM played as it is (at its own rate and channels), once or looping.
UCLASS()
class ULRCoreWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	static ULRCoreWave* Make(UObject* Outer, const lr::Sound& Sound, bool bLoops);
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params, TArray<FAudioParameter>&& Defaults) override;

private:
	TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> Pcm;
	bool bLoops = false;
};

// The install and its cache, opened.
struct FLRCoreData
{
	FString GameDir;
	std::array<TArray<uint8>, size_t(lr::CacheKind::Count)> Files;
	lr::Cache Cache;

	// The install's folder, or empty if none qualifies (holds LEGO.JAM and LEGORacers.exe). $LR_GAME_DIR first in
	// developer builds; the saved setting unless the run is scripted (scripted runs ignore saved settings).
	static FString FindInstall(bool bScripted);
	// Explains what the game needs (or, given one, what's wrong with the install it found), then the platform's file
	// dialog for LEGO.JAM, until the player picks a supported install (saved as the setting) or cancels.
	static FString AskForInstall(FString Why = FString());
	// Whether the folder holds a supported install; if not, why.
	static bool CheckInstall(const FString& Dir, FString& Why);

	// The cache for this install, if it is there and current: opened.
	bool OpenCurrent(const FString& InstallDir);
	// Builds the cache on a worker thread (Progress 0..1); Poll() is true once it is done, then Error tells.
	void StartBuild(const FString& InstallDir);
	bool Poll();
	bool IsBuilding() const { return Build.IsValid(); }
	float Progress() const { return BuildProgress.load(); }
	FString Error;

private:
	static FString CacheDir();
	struct FBuild;
	TSharedPtr<FBuild, ESPMode::ThreadSafe> Build;
	std::atomic<float> BuildProgress = 0;
};
