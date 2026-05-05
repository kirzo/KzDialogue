// Copyright 2026 kirzo

using UnrealBuildTool;

public class KzDialogueUncooked : ModuleRules
{
	public KzDialogueUncooked(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"KzDialogue",
			"BlueprintGraph",
			"GameplayTags",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"KismetCompiler",
			"UnrealEd",
			"Slate",
			"SlateCore",
		});
	}
}