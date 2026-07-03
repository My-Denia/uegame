// DungeonEditorPlacement.cpp - editor-only tooling (uegameEditor module).
//
// Places a persistent ADungeonSpawner into the open editor level as the shipping ENTRY
// (bAutoStartRun=true) and saves the map, so a fresh install boots straight into the floor
// loop through the placed spawner path (not the FloorManager bootstrap fallback).
//
// This is NOT a gameplay verb and NOT one of the shipping-gated Dungeon.* forensic verbs:
// it lives in the editor-only module (never compiled into a shipping game build), is namespaced
// DungeonEditor.* to keep it out of the runtime Dungeon.* set, and is driven via the project
// MCP toolset's ExecConsoleCommand. UE 5.8's ModelContextProtocol plugin ships no scene-editing
// tools, so this is the minimal editor surface needed to place + persist the actor. Idempotent:
// refuses if a spawner already exists (FindSpawner returns the first; duplicates would be silent).

#include "CoreMinimal.h"

#include "DungeonSpawner.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"            // FEditorFileUtils::SaveLevel (UnrealEd)
#include "HAL/IConsoleManager.h"

namespace
{
#if WITH_EDITOR

ADungeonSpawner* FindEditorSpawner(UWorld* World)
{
	for (TActorIterator<ADungeonSpawner> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void PlaceSpawnerInLevelCmd(const TArray<FString>& Args, UWorld* /*InWorld*/)
{
	if (!GEditor)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonPlace] GEditor unavailable"));
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonPlace] no editor world"));
		return;
	}
	// Idempotent: never stack spawners into the map.
	if (ADungeonSpawner* Existing = FindEditorSpawner(World))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DungeonPlace] a DungeonSpawner already exists in %s (%s); refusing to add another"),
			*World->GetName(), *Existing->GetActorLabel());
		return;
	}

	const int32 Seed = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 7;
	// Sane transform: origin. The dungeon occupies absolute world coords (bWorldSpace=true), so
	// the spawner's own transform never moves the geometry; origin keeps the outliner tidy.
	const FTransform Xf(FVector::ZeroVector);
	ADungeonSpawner* Spawner =
		World->SpawnActor<ADungeonSpawner>(ADungeonSpawner::StaticClass(), Xf);
	if (!Spawner)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonPlace] SpawnActor failed"));
		return;
	}
	// Editor int32 Seed drives only geometry preview; the RUN seed comes from the FloorManager
	// resolver (entropy by default). bAutoStartRun makes this instance the entry into the loop.
	Spawner->Seed = Seed;
	Spawner->bAutoStartRun = true;
	Spawner->SetActorLabel(TEXT("DungeonSpawner_Entry"));
	Spawner->MarkPackageDirty();

	// Persist the placement in the .umap.
	if (ULevel* Level = World->GetCurrentLevel())
	{
		const bool bSaved = FEditorFileUtils::SaveLevel(Level);
		UE_LOG(LogTemp, Display,
			TEXT("[DungeonPlace] placed ADungeonSpawner (seed=%d bAutoStartRun=true) into %s; saveLevel=%s"),
			Seed, *World->GetName(), bSaved ? TEXT("OK") : TEXT("FAILED"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonPlace] no current level to save"));
	}
}

FAutoConsoleCommandWithWorldAndArgs GPlaceSpawnerInLevelCmd(
	TEXT("DungeonEditor.PlaceSpawnerInLevel"),
	TEXT("Editor-only: place a persistent ADungeonSpawner (bAutoStartRun) into the open level and save the map"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaceSpawnerInLevelCmd));

#endif // WITH_EDITOR
} // namespace
