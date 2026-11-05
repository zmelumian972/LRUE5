using System.IO;
using UnrealBuildTool;

// The game itself: the fixed-step simulation (karts, physics, the race, power-ups), the menus, the game's state and
// saves, in plain C++20. No UE inside: the same sources build into the tests.
public class LRCore : ModuleRules
{
	public LRCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Include", "Private"));
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false; // (plain C++ files keep their own file-local names: no unity build)
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "LRData" }); // Core only for UBT's linking
	}
}
