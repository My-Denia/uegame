// uegameEditor module - registers the project MCP toolset with the ToolsetRegistry
// so the engine's Unreal MCP plugin exposes it to MCP clients.

#include "UegameMcpToolset.h"

#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

class FUegameEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolsetRegistry::RegisterToolsetClass(UUegameMcpToolset::StaticClass());
	}

	virtual void ShutdownModule() override
	{
		if (UObjectInitialized())
		{
			UToolsetRegistry::UnregisterToolsetClass(UUegameMcpToolset::StaticClass());
		}
	}
};

IMPLEMENT_MODULE(FUegameEditorModule, uegameEditor)
