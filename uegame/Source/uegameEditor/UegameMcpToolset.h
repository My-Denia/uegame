// UegameMcpToolset.h - project MCP toolset exposed through the engine's Unreal MCP plugin.
//
// UE 5.8's experimental ModelContextProtocol plugin only ships an AgentSkill toolset;
// it has no tools for driving the editor. This toolset adds the minimal surface the
// M2 evidence run needs: exec a console command, start/stop PIE, query PIE state.
// Everything else (logs, screenshots) is read from disk by the driving agent.

#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "UegameMcpToolset.generated.h"

/** Editor-driving tools for the uegame project: console commands and PIE control. */
UCLASS()
class UUegameMcpToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/** Executes a console command. Targets the active PIE world when one exists,
	 *  otherwise the editor world. Returns a status string. */
	UFUNCTION(meta = (AICallable))
	static FString ExecConsoleCommand(const FString& Command);

	/** Requests a Play-In-Editor session in the active viewport. Returns a status string;
	 *  poll GetPIEStatus until it reports running. */
	UFUNCTION(meta = (AICallable))
	static FString StartPIE();

	/** Ends the current Play-In-Editor session, if any. */
	UFUNCTION(meta = (AICallable))
	static FString StopPIE();

	/** Returns "running" when a PIE world is active, otherwise "not-running". */
	UFUNCTION(meta = (AICallable))
	static FString GetPIEStatus();
};
