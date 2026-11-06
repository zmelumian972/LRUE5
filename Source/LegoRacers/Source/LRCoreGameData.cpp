// Finding the install, building and opening the data cache, and the cache's sounds as UE waves (LRCoreGameData.h).

#include "LRCoreGameData.h"
#include "LRCoreLog.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "LRData/Archive.h"
#include "LRData/Digest.h"
#include "LRData/Executable.h"
#include "LRData/Sound.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Sound/SoundGenerator.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shobjidl.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
const TCHAR* const Section = TEXT("LegoRacers");
const TCHAR* const FolderKey = TEXT("GameFolder");

std::span<const uint8_t> View(const TArray<uint8>& Bytes) { return {Bytes.GetData(), size_t(Bytes.Num())}; }

// The sound's samples as the mixer asks for them (floats, interleaved), looping or once.
class FLRSoundGenerator : public ISoundGenerator
{
public:
	FLRSoundGenerator(TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> InPcm, bool bInLoops)
		: Pcm(MoveTemp(InPcm))
		, bLoops(bInLoops)
	{
	}
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
	{
		const int32 Total = Pcm->Num();
		for (int32 i = 0; i < NumSamples; i++) {
			if (At >= Total && bLoops && Total > 0) {
				At = 0;
			}
			OutAudio[i] = At < Total ? (*Pcm)[At++] / 32768.0f : 0.0f;
		}
		return NumSamples;
	}
	virtual bool IsFinished() const override { return !bLoops && At >= Pcm->Num(); }

private:
	TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> Pcm;
	bool bLoops;
	int32 At = 0;
};

bool ReadFile(const FString& Path, TArray<uint8>& Bytes) { return FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent); }

const TCHAR* const PickerTitle = TEXT("Select LEGO.JAM in your LEGO Racers folder");

// The picker asks for the file, not its folder: folder pickers hide the files, so the player couldn't see the right one.
#if PLATFORM_WINDOWS
FString PickJam()
{
	FString Picked;
	const HRESULT Com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); // (S_FALSE: already)
	IFileOpenDialog* Dialog = nullptr;
	if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog)))) {
		DWORD Options = 0;
		Dialog->GetOptions(&Options);
		Dialog->SetOptions(Options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
		const COMDLG_FILTERSPEC Types[] = {{L"LEGO.JAM", L"LEGO.JAM"}};
		Dialog->SetFileTypes(1, Types);
		Dialog->SetTitle(PickerTitle);
		IShellItem* Item = nullptr;
		if (SUCCEEDED(Dialog->Show(nullptr)) && SUCCEEDED(Dialog->GetResult(&Item))) {
			PWSTR Path = nullptr;
			if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Path))) {
				Picked = Path;
				::CoTaskMemFree(Path);
			}
			Item->Release();
		}
		Dialog->Release();
	}
	if (SUCCEEDED(Com)) {
		::CoUninitialize();
	}
	return Picked;
}
#else
// The desktop's own dialog: zenity, else kdialog.
FString PickJam()
{
	const TPair<const TCHAR*, FString> Tools[] = {
		{TEXT("/usr/bin/zenity"),
			FString::Printf(TEXT("--file-selection --title=\"%s\" --file-filter=\"LEGO.JAM | *.JAM *.jam\""), PickerTitle)},
		{TEXT("/usr/bin/kdialog"), FString::Printf(TEXT("--getopenfilename . \"*.JAM *.jam|LEGO.JAM\" --title \"%s\""), PickerTitle)}};
	for (const auto& [Tool, Params] : Tools) {
		if (!FPaths::FileExists(Tool)) {
			continue;
		}
		int32 Code = 0;
		FString Out, Err;
		if (FPlatformProcess::ExecProcess(Tool, *Params, &Code, &Out, &Err) && Code == 0) {
			return Out.TrimStartAndEnd();
		}
		return FString(); // (cancelled)
	}
	UE_LOG(LogLRCore, Error, TEXT("no file dialog (install zenity or kdialog), and no install found"));
	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("This computer has no file picker to choose LEGO.JAM with.\n\n"
											   "Install zenity or kdialog, or copy LEGO.JAM, LEGORacers.exe and the .TUN music files from "
											   "your LEGO Racers folder into:\n%s\n\nThen start the game again."),
			*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("GameData")))),
		FText::FromString(TEXT("LEGO Racers")));
	return FString();
}
#endif
} // namespace

ULRCoreWave* ULRCoreWave::Make(UObject* Outer, const lr::Sound& Sound, bool bInLoops)
{
	ULRCoreWave* Wave = NewObject<ULRCoreWave>(Outer);
	TSharedPtr<TArray<int16>, ESPMode::ThreadSafe> Samples = MakeShared<TArray<int16>, ESPMode::ThreadSafe>();
	Samples->Append(Sound.samples.data(), int32(Sound.samples.size()));
	Wave->Pcm = Samples;
	Wave->bLoops = bInLoops;
	Wave->NumChannels = int32(FMath::Max<uint32>(1, Sound.channels));
	Wave->SetSampleRate(int32(Sound.rate));
	Wave->bLooping = bInLoops;
	Wave->Duration = bInLoops ? INDEFINITELY_LOOPING_DURATION
							  : float(Samples->Num() / Wave->NumChannels) / float(FMath::Max<uint32>(1, Sound.rate));
	Wave->SoundGroup = SOUNDGROUP_Default;
	return Wave;
}

ISoundGeneratorPtr ULRCoreWave::CreateSoundGenerator(const FSoundGeneratorInitParams&, TArray<FAudioParameter>&&)
{
	return MakeShared<FLRSoundGenerator, ESPMode::ThreadSafe>(Pcm, bLoops);
}

bool FLRCoreData::CheckInstall(const FString& Dir, FString& Why)
{
	TArray<uint8> Exe, Jam;
	if (!ReadFile(Dir / TEXT("LEGO.JAM"), Jam)) {
		Why = FString::Printf(TEXT("LEGO.JAM isn't in this folder:\n%s"), *Dir);
		return false;
	}
	if (!ReadFile(Dir / TEXT("LEGORacers.exe"), Exe)) {
		Why = FString::Printf(TEXT("LEGORacers.exe has to be next to LEGO.JAM, but it isn't in this folder:\n%s"), *Dir);
		return false;
	}
	if (size_t(Exe.Num()) != lr::KnownExecutableSize || lr::hex(lr::sha256(View(Exe))) != lr::KnownExecutableDigest) {
		Why = FString::Printf(TEXT("The LEGORacers.exe in this folder is a version this game doesn't support, or the file is damaged:\n%s"),
			*Dir);
		return false;
	}
	lr::Archive Archive;
	if (!Archive.open(View(Jam))) {
		Why = FString::Printf(TEXT("The LEGO.JAM in this folder is damaged:\n%s"), *Dir);
		return false;
	}
	return true;
}

FString FLRCoreData::FindInstall(bool bScripted)
{
	const auto Qualifies = [](const FString& Dir) {
		return !Dir.IsEmpty() && FPaths::FileExists(Dir / TEXT("LEGO.JAM")) && FPaths::FileExists(Dir / TEXT("LEGORacers.exe"));
	};
	TArray<FString> Candidates;
#if !UE_BUILD_SHIPPING
	Candidates.Add(FPlatformMisc::GetEnvironmentVariable(TEXT("LR_GAME_DIR")));
#endif
	if (!bScripted) {
		FString Saved;
		if (GConfig && GConfig->GetString(Section, FolderKey, Saved, GGameUserSettingsIni)) {
			Candidates.Add(Saved);
		}
	}
	Candidates.Add(FPaths::ProjectDir() / TEXT("GameData"));
	Candidates.Add(FPaths::ConvertRelativePathToFull(FPlatformProcess::BaseDir()) / TEXT("GameData"));
#if !UE_BUILD_SHIPPING
	Candidates.Add(FPaths::ProjectDir() / TEXT("build/gamedir"));
#endif
	for (const FString& Dir : Candidates) {
		if (Qualifies(Dir)) {
			return FPaths::ConvertRelativePathToFull(Dir);
		}
	}
	return FString();
}

FString FLRCoreData::AskForInstall(FString Why)
{
	const FText Title = FText::FromString(TEXT("LEGO Racers"));
	for (;;) {
		const FString Text = Why.IsEmpty()
			? TEXT("Welcome to LEGO Racers!\n\n"
				   "This game runs on the files of the original LEGO Racers (1999),\n"
				   "so you need your own copy installed on this computer.\n"
				   "Nothing is downloaded, and your copy isn't changed.\n\n" // (line breaks by hand: Linux's message box doesn't wrap)
				   "Press OK, then find and select the file LEGO.JAM in the folder\n"
				   "where LEGO Racers is installed. LEGORacers.exe has to be in the same folder.\n\n"
				   "The first start then takes a while as the game prepares its data.\n\n"
				   "Press Cancel to quit.")
			: Why + TEXT("\n\nPress OK to choose another LEGO.JAM, or Cancel to quit.");
		if (FMessageDialog::Open(EAppMsgType::OkCancel, FText::FromString(Text), Title) != EAppReturnType::Ok) {
			return FString();
		}
		Why.Empty(); // (closing the picker goes back to the explanation)
		const FString Jam = PickJam();
		if (Jam.IsEmpty()) {
			continue;
		}
		const FString Dir = FPaths::GetPath(Jam);
		if (CheckInstall(Dir, Why)) {
			if (GConfig) {
				GConfig->SetString(Section, FolderKey, *Dir, GGameUserSettingsIni);
				GConfig->Flush(false, GGameUserSettingsIni);
			}
			return Dir;
		}
	}
}

FString FLRCoreData::CacheDir() { return FPaths::ProjectSavedDir() / TEXT("DataCache"); }

bool FLRCoreData::OpenCurrent(const FString& InstallDir)
{
	GameDir = InstallDir;
	TArray<uint8> Jam, Exe;
	if (!ReadFile(InstallDir / TEXT("LEGO.JAM"), Jam) || !ReadFile(InstallDir / TEXT("LEGORacers.exe"), Exe)) {
		return false;
	}
	const lr::Digest JamDigest = lr::sha256(View(Jam)), ExeDigest = lr::sha256(View(Exe));
	std::array<std::span<const uint8_t>, size_t(lr::CacheKind::Count)> Views;
	for (size_t k = 0; k < Files.size(); k++) {
		lr::CacheKind Kind;
		lr::Digest J, E;
		if (!ReadFile(CacheDir() / UTF8_TO_TCHAR(lr::CacheFileNames[k]), Files[k]) || !lr::cacheKey(View(Files[k]), Kind, J, E) ||
			J != JamDigest || E != ExeDigest) {
			return false;
		}
		Views[k] = View(Files[k]);
	}
	return Cache.open(Views);
}

struct FLRCoreData::FBuild
{
	TFuture<FString> Done; // the error, or empty
	std::array<std::vector<uint8_t>, size_t(lr::CacheKind::Count)> Files;
};

void FLRCoreData::StartBuild(const FString& InstallDir)
{
	GameDir = InstallDir;
	Error.Reset();
	BuildProgress = 0;
	Build = MakeShared<FBuild, ESPMode::ThreadSafe>();
	TSharedPtr<FBuild, ESPMode::ThreadSafe> B = Build;
	std::atomic<float>* ProgressOut = &BuildProgress;
	const FString OutDir = CacheDir();
	B->Done = Async(EAsyncExecution::Thread, [B, InstallDir, OutDir, ProgressOut]() -> FString {
		TArray<uint8> Jam, Exe;
		if (!ReadFile(InstallDir / TEXT("LEGO.JAM"), Jam) || !ReadFile(InstallDir / TEXT("LEGORacers.exe"), Exe)) {
			return TEXT("can't read LEGO.JAM and LEGORacers.exe");
		}
		// The tunes beside them, as the install spells them.
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(InstallDir / TEXT("*")), true, false);
		TArray<TArray<uint8>> Tunes;
		lr::CacheSource Source;
		Source.jam = View(Jam);
		Source.exe = View(Exe);
		Tunes.Reserve(Names.Num());
		for (const FString& Name : Names) {
			if (Name.EndsWith(TEXT(".tun"), ESearchCase::IgnoreCase) && ReadFile(InstallDir / Name, Tunes.AddDefaulted_GetRef())) {
				Source.tunes.emplace_back(TCHAR_TO_UTF8(*Name), View(Tunes.Last()));
			}
		}
		std::string Why;
		if (!lr::buildCache(Source, B->Files, Why, [ProgressOut](float F) { *ProgressOut = F * 0.95f; })) {
			return UTF8_TO_TCHAR(Why.c_str());
		}
		IFileManager::Get().MakeDirectory(*OutDir, true);
		for (size_t k = 0; k < B->Files.size(); k++) {
			const std::vector<uint8_t>& F = B->Files[k];
			if (!FFileHelper::SaveArrayToFile(TArrayView<const uint8>(F.data(), int32(F.size())),
					*(OutDir / UTF8_TO_TCHAR(lr::CacheFileNames[k])))) {
				return FString::Printf(TEXT("can't write %s"), *OutDir);
			}
		}
		*ProgressOut = 1;
		return FString();
	});
}

bool FLRCoreData::Poll()
{
	if (!Build.IsValid() || !Build->Done.IsReady()) {
		return false;
	}
	Error = Build->Done.Get();
	if (Error.IsEmpty()) {
		std::array<std::span<const uint8_t>, size_t(lr::CacheKind::Count)> Views;
		for (size_t k = 0; k < Files.size(); k++) {
			Files[k] = TArray<uint8>(Build->Files[k].data(), int32(Build->Files[k].size()));
			Views[k] = View(Files[k]);
		}
		if (!Cache.open(Views)) {
			Error = TEXT("the cache it built doesn't open");
		}
	}
	Build.Reset();
	return true;
}
