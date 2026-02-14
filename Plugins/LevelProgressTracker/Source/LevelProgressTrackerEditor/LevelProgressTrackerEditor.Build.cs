// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LevelProgressTrackerEditor : ModuleRules
{
	public LevelProgressTrackerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"LevelProgressTracker",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"AssetRegistry",
				"Slate",
				"SlateCore",
				"PropertyEditor",
				"Projects",
				"ToolMenus",
			}
		);
	}
}
