using System.IO;
using UnrealBuildTool;

// The game's file formats (LEGO.JAM and what's in it, the .tun music), read into plain C++ data. No UE inside: the
// same sources build into the tests.
public class LRData : ModuleRules
{
	public LRData(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Include", "Private"));
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false; // (plain C++ files keep their own file-local names: no unity build)
		PublicDependencyModuleNames.Add("Core"); // UBT needs a module to link against; the sources don't include it
	}
}
