using UnrealBuildTool;

public class LegoRacersEditorTarget : TargetRules
{
	public LegoRacersEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("LegoRacers");
	}
}
