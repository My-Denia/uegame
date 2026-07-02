// DungeonEvidence.cpp - console commands driving the M2 evidence run inside PIE/game.
//
//   Dungeon.Spawn [seed]   Spawn ADungeonSpawner with the given seed (default 7) into the
//                          current world. Deferred spawn so Seed applies before Build().
//   Dungeon.WalkFar        Path-check + physically walk the player character from where it
//                          stands to the farthest room center via the navmesh
//                          (UAIBlueprintHelperLibrary::SimpleMoveToLocation). Logs the nav
//                          path result (navmesh evidence), then arrival/timeout (collision +
//                          traversal evidence).
//
// Test/evidence scaffolding only - no gameplay. Lives in the runtime module so the same
// commands work in PIE and -game runs.

#include "DungeonSpawner.h"

#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace
{

ADungeonSpawner* FindSpawner(UWorld* World)
{
	for (TActorIterator<ADungeonSpawner> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void DungeonSpawnCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
	{
		return;
	}
	if (FindSpawner(World))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DungeonEvidence] a DungeonSpawner already exists in %s; not spawning another"), *World->GetName());
		return;
	}
	const int32 Seed = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 7;
	const FTransform Xf(FVector::ZeroVector);
	ADungeonSpawner* Spawner = World->SpawnActorDeferred<ADungeonSpawner>(ADungeonSpawner::StaticClass(), Xf);
	if (!Spawner)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] SpawnActorDeferred failed"));
		return;
	}
	Spawner->Seed = Seed;
	// Optional tile size (cm). 1-cell corridors at 100cm erode to ~30cm under the default
	// nav agent radius (35) and get culled from the navmesh; 200cm keeps them navigable.
	if (Args.Num() > 1)
	{
		const float TS = FCString::Atof(*Args[1]);
		if (TS >= 10.0f)
		{
			Spawner->TileSize = TS;
		}
	}
	Spawner->FinishSpawning(Xf);
	UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] spawned ADungeonSpawner seed=%d tileSize=%.0f in world=%s"),
		Seed, Spawner->TileSize, *World->GetName());
}

void DungeonWalkFarCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
	{
		return;
	}
	ADungeonSpawner* Spawner = FindSpawner(World);
	if (!Spawner)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] no DungeonSpawner in world; run Dungeon.Spawn first"));
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] no player pawn"));
		return;
	}

	const FVector From = Pawn->GetActorLocation();
	const FVector Dest = Spawner->GetFarthestRoomCenterWorld() + FVector(0.0f, 0.0f, 100.0f);

	// Navmesh evidence: synchronous path query start -> farthest room.
	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, Dest);
	if (Path && Path->IsValid())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[DungeonEvidence] NAVPATH valid=YES partial=%s points=%d length=%.0f from=(%.0f,%.0f) to=(%.0f,%.0f)"),
			Path->IsPartial() ? TEXT("YES") : TEXT("NO"),
			Path->PathPoints.Num(), Path->GetPathLength(),
			From.X, From.Y, Dest.X, Dest.Y);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] NAVPATH valid=NO - navmesh missing or destination unreachable"));
	}

	// Physically walk there (collision + navmesh path-following on the player character).
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(PC, Dest);
	UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] WALK started, dist2D=%.0f"), FVector::Dist2D(From, Dest));

	// Arrival watcher: logs ARRIVED (or TIMEOUT) with the final position.
	TWeakObjectPtr<APawn> WeakPawn = Pawn;
	const double StartTime = FPlatformTime::Seconds();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakPawn, Dest, StartTime](float) -> bool
		{
			if (!WeakPawn.IsValid())
			{
				return false;   // world/pawn gone (PIE ended)
			}
			const FVector Loc = WeakPawn->GetActorLocation();
			const float Dist2D = FVector::Dist2D(Loc, Dest);
			const double Elapsed = FPlatformTime::Seconds() - StartTime;
			if (Dist2D < 150.0f)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[DungeonEvidence] ARRIVED at farthest room: dist2D=%.0f pos=(%.0f,%.0f,%.0f) elapsed=%.1fs"),
					Dist2D, Loc.X, Loc.Y, Loc.Z, Elapsed);
				return false;
			}
			if (Elapsed > 90.0)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DungeonEvidence] WALK TIMEOUT: dist2D=%.0f pos=(%.0f,%.0f,%.0f)"),
					Dist2D, Loc.X, Loc.Y, Loc.Z);
				return false;
			}
			return true;
		}), 0.5f);
}

FAutoConsoleCommandWithWorldAndArgs GDungeonSpawnCmd(
	TEXT("Dungeon.Spawn"),
	TEXT("Spawn the M2 dungeon: Dungeon.Spawn [seed] (default 7)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonSpawnCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonWalkFarCmd(
	TEXT("Dungeon.WalkFar"),
	TEXT("Nav-path check + walk the player to the farthest room of the spawned dungeon"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonWalkFarCmd));

} // namespace
