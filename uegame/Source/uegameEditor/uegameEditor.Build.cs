// uegameEditor - editor-only module hosting the project's MCP toolset (M2 evidence driving).
// ToolsetRegistry is an EditorOnly plugin, so the toolset class cannot live in the runtime module.

using UnrealBuildTool;

public class uegameEditor : ModuleRules
{
	public uegameEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",          // GEditor + FEditorFileUtils: PIE control, world resolution, SaveLevel
			"ToolsetRegistry",   // UToolsetDefinition / UToolsetRegistry (MCP tool surface)
			"uegame"             // ADungeonSpawner: the DungeonEditor.PlaceSpawnerInLevel placement tool
		});
	}
}
