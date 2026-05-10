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
		});
	}
}
