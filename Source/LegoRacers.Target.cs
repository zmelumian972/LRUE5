using UnrealBuildTool;

public class LegoRacersTarget : TargetRules
{
	public LegoRacersTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("LegoRacers");
	}
}
