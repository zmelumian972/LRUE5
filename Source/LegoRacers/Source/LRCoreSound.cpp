// The race's voices mixed into one stereo stream (LRCoreSound.h).

#include "LRCoreSound.h"

#include "LRCore/Show/ShowState.h"
#include "LRData/Cache.h"
#include "LRData/Sound.h"
#include "Sound/SoundGenerator.h"

#include <cmath>

namespace
{
// The voices as the mixer asks for them: stereo floats, each voice's sample stepped at its rate (the nearest frame),
// times its two gains.
class FLRMixerGenerator : public ISoundGenerator
{
public:
	explicit FLRMixerGenerator(TSharedPtr<ULRRaceMixer::FVoices, ESPMode::ThreadSafe> InShared) : Shared(MoveTemp(InShared)) {}
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
	{
		FMemory::Memzero(OutAudio, sizeof(float) * NumSamples);
		const int32 Frames = NumSamples / 2;
		FScopeLock Guard(&Shared->Lock);
		for (ULRRaceMixer::FVoice& V : Shared->Voices) {
			if (V.Serial == 0 || !V.Pcm.IsValid() || V.Step <= 0) {
				continue;
			}
			const TArray<int16>& Pcm = *V.Pcm;
			const int32 Length = Pcm.Num() / V.Channels;
			for (int32 f = 0; f < Frames && Length > 0; f++) {
				int32 At = int32(V.At);
				if (At >= Length) {
					if (!V.bLoops) {
						break;
					}
					V.At = std::fmod(V.At, double(Length));
					At = int32(V.At);
				}
				const float L = Pcm[At * V.Channels] / 32768.0f, R = Pcm[At * V.Channels + V.Channels - 1] / 32768.0f;
				OutAudio[2 * f] += L * V.Left;
				OutAudio[2 * f + 1] += R * V.Right;
				V.At += V.Step;
			}
		}
		return NumSamples;
	}

private:
	TSharedPtr<ULRRaceMixer::FVoices, ESPMode::ThreadSafe> Shared;
};
} // namespace

ULRRaceMixer* ULRRaceMixer::Make(UObject* Outer)
{
	ULRRaceMixer* Mixer = NewObject<ULRRaceMixer>(Outer);
	Mixer->Shared = MakeShared<FVoices, ESPMode::ThreadSafe>();
	Mixer->NumChannels = 2;
	Mixer->SetSampleRate(MixRate);
	Mixer->bLooping = true;
	Mixer->Duration = INDEFINITELY_LOOPING_DURATION;
	Mixer->SoundGroup = SOUNDGROUP_Default;
	return Mixer;
}

ISoundGeneratorPtr ULRRaceMixer::CreateSoundGenerator(const FSoundGeneratorInitParams&, TArray<FAudioParameter>&&)
{
	return MakeShared<FLRMixerGenerator, ESPMode::ThreadSafe>(Shared);
}

// A sample as a voice plays it: mono at 11025 Hz or less doubled by midpoints at twice the rate.
const ULRRaceMixer::FSample* ULRRaceMixer::SampleOf(const FString& Path, const lr::Cache& Cache)
{
	if (const FSample* Found = Samples.Find(Path)) {
		return Found->Pcm.IsValid() ? Found : nullptr;
	}
	FSample& Made = Samples.Add(Path);
	lr::Sound Sound;
	if (!Cache.get(TCHAR_TO_UTF8(*Path), Sound) || Sound.samples.empty() || Sound.channels == 0 || Sound.rate == 0) {
		UE_LOG(LogTemp, Warning, TEXT("LRCore: no race sound %s in the data cache"), *Path);
		return nullptr;
	}
	TSharedPtr<TArray<int16>, ESPMode::ThreadSafe> Pcm = MakeShared<TArray<int16>, ESPMode::ThreadSafe>();
	Made.Channels = int32(Sound.channels);
	Made.Rate = int32(Sound.rate);
	if (Sound.channels == 1 && Sound.rate <= 11025) {
		const int32 N = int32(Sound.samples.size());
		Pcm->SetNumUninitialized(2 * N);
		for (int32 i = 0; i < N; i++) {
			const int16 Next = i + 1 < N ? Sound.samples[size_t(i + 1)] : Sound.samples[size_t(i)];
			(*Pcm)[2 * i] = Sound.samples[size_t(i)];
			(*Pcm)[2 * i + 1] = int16((int32(Sound.samples[size_t(i)]) + int32(Next)) / 2);
		}
		Made.Rate *= 2;
	}
	else {
		Pcm->Append(Sound.samples.data(), int32(Sound.samples.size()));
	}
	Made.Pcm = Pcm;
	return &Made;
}

void ULRRaceMixer::Update(const lr::Show& Show, const lr::Cache& Cache)
{
	if (!Shared.IsValid()) {
		return;
	}
	FScopeLock Guard(&Shared->Lock);
	for (size_t v = 0; v < lr::Show::Voices; v++) {
		FVoice& V = Shared->Voices[v];
		const lr::ShowSound* S = Show.voices[v] ? Show.find(Show.voices[v]) : nullptr;
		const FSample* Sample = S ? SampleOf(UTF8_TO_TCHAR(Show.sample(S->id).c_str()), Cache) : nullptr;
		if (!Sample) {
			V = FVoice();
			continue;
		}
		if (V.Serial != S->serial) {
			V = FVoice();
			V.Serial = S->serial;
			V.Pcm = Sample->Pcm;
			V.Channels = Sample->Channels;
			V.bLoops = S->looping;
		}
		// The device's law: the gain from the heard volume, the far channel down by the pan (a
		// negative pan: the sound on the left), the rate the sample's times the heard pitch.
		const float Gain = lr::Show::gainOf(S->heard), Far = lr::Show::panGain(S->pan);
		V.Left = Gain * (S->pan > 0 ? Far : 1.0f);
		V.Right = Gain * (S->pan < 0 ? Far : 1.0f);
		V.Step = double(int32(float(Sample->Rate) * S->heardPitch)) / MixRate;
	}
}

void ULRRaceMixer::Clear()
{
	if (Shared.IsValid()) {
		FScopeLock Guard(&Shared->Lock);
		for (FVoice& V : Shared->Voices) {
			V = FVoice();
		}
	}
	Samples.Reset();
}
