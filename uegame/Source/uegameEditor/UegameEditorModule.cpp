// uegameEditor module - registers the project MCP toolset with the ToolsetRegistry
// so the engine's Unreal MCP plugin exposes it to MCP clients.
//
// Registration is deferred to OnPostEngineInit: at module StartupModule time the
// editor subsystems (including UToolsetRegistrySubsystem) do not exist yet and
// UToolsetRegistry::RegisterToolsetClass fails with "AIToolsetRegistrySubsystem
// unavailable" (observed in uegame.log). The engine's own AgentSkillToolset is
// registered from the subsystem's Initialize for the same reason.

#include "UegameMcpToolset.h"

#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

class FUegameEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (GEditor)
		{
			// Hot-reload / late-load path: engine is already initialized.
			UToolsetRegistry::RegisterToolsetClass(UUegameMcpToolset::StaticClass());
		}
		else
		{
			PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
			{
				UToolsetRegistry::RegisterToolsetClass(UUegameMcpToolset::StaticClass());
			});
		}
	}

	virtual void ShutdownModule() override
	{
		if (PostEngineInitHandle.IsValid())
		{
			FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		}
		if (UObjectInitialized())
		{
			UToolsetRegistry::UnregisterToolsetClass(UUegameMcpToolset::StaticClass());
		}
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FUegameEditorModule, uegameEditor)
