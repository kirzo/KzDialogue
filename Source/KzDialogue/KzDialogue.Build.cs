// Copyright 2026 kirzo

using UnrealBuildTool;

public class KzDialogue : ModuleRules
{
	public KzDialogue(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"GameplayTags",
				"KzLib",
				"UMG",
				"MovieScene",
				"MovieSceneTracks",
				"DeveloperSettings"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"AssetRegistry"
			}
			);
	}
}