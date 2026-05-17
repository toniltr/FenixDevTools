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
			"UnrealEd",          // Editor utilities — UEditorEngine, actor iteration
			"LevelEditor",       // FLevelEditorModule — toolbar extension
			"ToolMenus",         // UToolMenus — register toolbar buttons
			"Json",              // FJsonObject, FJsonSerializer
			"JsonUtilities",     // FJsonObjectConverter
			"Projects",          // IPluginManager
			"DesktopPlatform",
			"EditorScriptingUtilities",
			"InputCore",
			"SceneOutliner",     // FActorFolders — gestión de carpetas del outliner
		});
	}
}
