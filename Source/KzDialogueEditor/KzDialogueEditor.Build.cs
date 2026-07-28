// Copyright 2026 kirzo

using UnrealBuildTool;

public class KzDialogueEditor : ModuleRules
{
	public KzDialogueEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"KzDialogue",
				"KzLibEditor"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"KzDialogueUncooked",
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"AssetRegistry",
				"AssetTools",
				"ClassViewer",
				"PropertyEditor",
				"InputCore",
				"Projects",
				"ApplicationCore",
				"BlueprintGraph",
				"ToolWidgets",
				"EditorWidgets",
				"Sequencer",
				"MovieScene",
				"MovieSceneTools",
				"MovieSceneTracks",
				"GameplayTags",
				"GraphEditor",
				"ToolMenus",
				"ContentBrowser",
				"DesktopPlatform",
				"Localization",
			});
	}
}