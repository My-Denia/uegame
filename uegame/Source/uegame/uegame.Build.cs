// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class uegame : ModuleRules
{
	public uegame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"uegame",
			"uegame/Variant_Platforming",
			"uegame/Variant_Platforming/Animation",
			"uegame/Variant_Combat",
			"uegame/Variant_Combat/AI",
			"uegame/Variant_Combat/Animation",
			"uegame/Variant_Combat/Gameplay",
			"uegame/Variant_Combat/Interfaces",
			"uegame/Variant_Combat/UI",
			"uegame/Variant_SideScrolling",
			"uegame/Variant_SideScrolling/AI",
			"uegame/Variant_SideScrolling/Gameplay",
			"uegame/Variant_SideScrolling/Interfaces",
			"uegame/Variant_SideScrolling/UI"
		});

		// M1/M2 engine-agnostic headers (dungeon.hpp, m2_adapter.hpp) live at the repo root
		// (one level above this .uproject) as the single source of truth, shared as-is with
		// the standalone g++ build. Exposed here so the future M2 spawner can #include them
		// without moving/duplicating the verified M1 core.
		// NOTE: not compile-verified - no UE engine is installed on this machine.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "..", ".."));

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
