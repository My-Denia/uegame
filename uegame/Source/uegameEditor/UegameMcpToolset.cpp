// UegameMcpToolset.cpp - see header. Editor-only (module uegameEditor).

#include "UegameMcpToolset.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	UWorld* ResolveTargetWorld()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		if (GEditor->PlayWorld)
		{
			return GEditor->PlayWorld;
		}
		return GEditor->GetEditorWorldContext().World();
	}
}

FString UUegameMcpToolset::ExecConsoleCommand(const FString& Command)
{
	UWorld* World = ResolveTargetWorld();
	if (!World || !GEngine)
	{
		return TEXT("error: no world available");
	}
	const bool bHandled = GEngine->Exec(World, *Command);
	return FString::Printf(TEXT("%s (world=%s, handled=%s)"),
		*Command, *World->GetName(), bHandled ? TEXT("yes") : TEXT("no"));
}

FString UUegameMcpToolset::StartPIE()
{
	if (!GEditor)
	{
		return TEXT("error: GEditor unavailable");
	}
	if (GEditor->PlayWorld)
	{
		return TEXT("already-running");
	}
	FRequestPlaySessionParams Params;
	GEditor->RequestPlaySession(Params);
	return TEXT("requested");
}

FString UUegameMcpToolset::StopPIE()
{
	if (!GEditor)
	{
		return TEXT("error: GEditor unavailable");
	}
	if (!GEditor->PlayWorld)
	{
		return TEXT("not-running");
	}
	GEditor->RequestEndPlayMap();
	return TEXT("stop-requested");
}

FString UUegameMcpToolset::GetPIEStatus()
{
	return (GEditor && GEditor->PlayWorld) ? TEXT("running") : TEXT("not-running");
}
