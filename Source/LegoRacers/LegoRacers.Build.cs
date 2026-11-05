using System.IO;
using UnrealBuildTool;

public class LegoRacers : ModuleRules
{
	public LegoRacers(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Include", "Private"));
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent", "RenderCore", "AudioMixer", "Slate", "SlateCore", "ApplicationCore" });
		PrivateDependencyModuleNames.Add("AudioExtensions"); // the cache's sounds as procedural waves (LRCoreGameData.h)
		PrivateDependencyModuleNames.AddRange(new string[] { "LRData", "LRCore" }); // the cache (LRCoreGameData) and the game (LRCoreGameMode)
	}
}
