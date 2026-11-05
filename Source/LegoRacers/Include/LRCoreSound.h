#pragma once

// The race's sounds: the show's 32 voices mixed here into one stereo stream
// that UE plays as a plain 2D sound (none of UE's attenuation or spatialisation). Each voice plays its sample from the
// data cache (a mono sample at 11025 Hz or less upsampled twice by midpoints) at the rate, gain and
// pan the show gives it each frame: the gain by the device's law, the far channel attenuated by the pan, the rate the
// sample's times the heard pitch. (A UE audio component can't pan a 2D sound, so the voices are mixed in software.)

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"

#include "LRCoreSound.generated.h"

namespace lr
{
class Cache;
class Show;
} // namespace lr

UCLASS()
class ULRRaceMixer : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	static constexpr int32 MixRate = 48000;
	static ULRRaceMixer* Make(UObject* Outer);
	// After a tick's frames: each voice as the show holds it now (a new sound starts from its beginning; a voice the
	// show let go falls silent).
	void Update(const lr::Show& Show, const lr::Cache& Cache);
	// The race is over: every voice silent, the samples let go.
	void Clear();
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params, TArray<FAudioParameter>&& Defaults) override;

	using FPcm = TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe>;
	struct FVoice
	{
		uint32 Serial = 0; // the show's sound (0: silent)
		FPcm Pcm;
		int32 Channels = 1;
		bool bLoops = false;
		double At = 0; // in the sample's frames
		double Step = 0; // its frames per output frame
		float Left = 0, Right = 0;
	};
	struct FVoices
	{
		FCriticalSection Lock;
		FVoice Voices[32];
	};

private:
	struct FSample
	{
		FPcm Pcm;
		int32 Channels = 1, Rate = 0;
	};
	const FSample* SampleOf(const FString& Path, const lr::Cache& Cache);
	TSharedPtr<FVoices, ESPMode::ThreadSafe> Shared;
	TMap<FString, FSample> Samples; // by the cache's path (no PCM: none)
};
