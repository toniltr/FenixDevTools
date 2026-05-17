using UnrealBuildTool;

public class FenixDevTools : ModuleRules
{
	public FenixDevTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FenixCore",   // FFenixConditionGroup, FFenixEvent, FFenixItem
			"FenixPlay",   // AFenixActor — leer conditions/events en el exporter
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"EditorStyle",
			"UnrealEd",
			"LevelEditor",
			"ToolMenus",
			"Json",
			"JsonUtilities",
			"Projects",
			"DesktopPlatform",
			"EditorScriptingUtilities",
			"InputCore",
		});
	}
}
