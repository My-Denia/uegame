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

#include "Combat/CombatComponent.h"
#include "Combat/DungeonEnemy.h"
#include "Combat/FloorManager.h"
#include "Combat/HealthComponent.h"
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

// Every console verb in this file is test/evidence scaffolding - the whole set is compiled
// out of Shipping (the M2-era Dungeon.Spawn/Dungeon.WalkFar included), so no forensic
// surface ships. Development/PIE builds keep the gate true, so evidence runs are unaffected.
#if !UE_BUILD_SHIPPING

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
	if (!World->IsGameWorld())
	{
		// Without this guard the MCP bridge (which falls back to the editor world when
		// PIE is not active) would spawn the dungeon INTO the editor level and dirty it.
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] Dungeon.Spawn is PIE/game-world only; start PIE first"));
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

// ---------------------------------------------------------------------------
// M3 forensic verbs - test/evidence only (whole file gated at the top).
// ---------------------------------------------------------------------------

void DungeonCombatStatusCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	const UHealthComponent* PlayerHP = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;

	float NearestDist = -1.0f;
	int32 EnemyCount = 0;
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		++EnemyCount;
		if (Player)
		{
			const float D = FVector::Dist2D((*It)->GetActorLocation(), Player->GetActorLocation());
			if (NearestDist < 0.0f || D < NearestDist)
			{
				NearestDist = D;
			}
		}
	}

	FString Rooms;
	if (ADungeonSpawner* Spawner = FindSpawner(World))
	{
		for (int32 i = 0; i < Spawner->GetRoomCount(); ++i)
		{
			Rooms += FString::Printf(TEXT(" r%d=%d/%d"), i,
				Spawner->GetAliveInRoom(i), Spawner->GetInitialInRoom(i));
		}
	}

	// Evidence snapshot: player HP, nearest-enemy distance, per-room alive/initial.
	UE_LOG(LogTemp, Display,
		TEXT("[CombatStatus] playerHP=%.0f/%.0f enemies=%d nearestDist=%.0f | alive/initial:%s"),
		PlayerHP ? PlayerHP->GetHP() : -1.0f, PlayerHP ? PlayerHP->GetMaxHP() : -1.0f,
		EnemyCount, NearestDist, *Rooms);
}

void DungeonAttackCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	UCombatComponent* Combat = Player ? Player->FindComponentByClass<UCombatComponent>() : nullptr;
	if (!Combat)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] no CombatComponent on player pawn"));
		return;
	}
	Combat->TryAttack();
}

void DungeonKillNearestCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Player)
	{
		return;
	}
	ADungeonEnemy* Nearest = nullptr;
	float NearestDist = TNumericLimits<float>::Max();
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		const float D = FVector::Dist2D((*It)->GetActorLocation(), Player->GetActorLocation());
		if (D < NearestDist)
		{
			NearestDist = D;
			Nearest = *It;
		}
	}
	if (!Nearest)
	{
		UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] KillNearest: no enemies left"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] KillNearest: room=%d dist=%.0f (test-only cheat)"),
		Nearest->GetRoomIndex(), NearestDist);
	if (UHealthComponent* HP = Nearest->FindComponentByClass<UHealthComponent>())
	{
		HP->TakeDamage(99999.0f, Player);
	}
}

void DungeonTeleportToRoomCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.TeleportToRoom <roomIndex>"));
		return;
	}
	ADungeonSpawner* Spawner = FindSpawner(World);
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Spawner || !Player)
	{
		return;
	}
	const int32 Idx = FCString::Atoi(*Args[0]);
	if (Idx < 0 || Idx >= Spawner->GetRoomCount())
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] room index %d out of range (rooms=%d)"),
			Idx, Spawner->GetRoomCount());
		return;
	}
	Player->SetActorLocation(Spawner->GetRoomCenterWorld(Idx) + FVector(0.0f, 0.0f, 100.0f));
	// Evidence: teleport target logged so RoomCleared's index can be asserted against it.
	UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] teleported player to room=%d"), Idx);
}

FAutoConsoleCommandWithWorldAndArgs GDungeonCombatStatusCmd(
	TEXT("Dungeon.CombatStatus"),
	TEXT("Log player HP, enemy count, nearest-enemy distance, per-room alive/initial counts"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonCombatStatusCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonAttackCmd(
	TEXT("Dungeon.Attack"),
	TEXT("Trigger the player's melee attack (same path as the F key)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonAttackCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonKillNearestCmd(
	TEXT("Dungeon.KillNearest"),
	TEXT("Test-only cheat: apply lethal damage to the enemy nearest the player"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonKillNearestCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonTeleportToRoomCmd(
	TEXT("Dungeon.TeleportToRoom"),
	TEXT("Teleport the player to a room center: Dungeon.TeleportToRoom <roomIndex>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonTeleportToRoomCmd));

void DungeonFaceNearestCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Player)
	{
		return;
	}
	ADungeonEnemy* Nearest = nullptr;
	float NearestDist = TNumericLimits<float>::Max();
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		const float D = FVector::Dist2D((*It)->GetActorLocation(), Player->GetActorLocation());
		if (D < NearestDist)
		{
			NearestDist = D;
			Nearest = *It;
		}
	}
	if (!Nearest)
	{
		UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] FaceNearest: no enemies"));
		return;
	}
	// The melee sweep is front-offset; forensic runs cannot steer the pawn, so rotate it
	// (attack uses the PAWN's forward vector).
	const FVector Dir = Nearest->GetActorLocation() - Player->GetActorLocation();
	const FRotator Face(0.0f, Dir.Rotation().Yaw, 0.0f);
	Player->SetActorRotation(Face);
	UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] FaceNearest: yaw=%.0f dist=%.0f room=%d"),
		Face.Yaw, NearestDist, Nearest->GetRoomIndex());
}

FAutoConsoleCommandWithWorldAndArgs GDungeonFaceNearestCmd(
	TEXT("Dungeon.FaceNearest"),
	TEXT("Rotate the player pawn to face the nearest enemy (forensic aid for the melee sweep)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonFaceNearestCmd));

// --- M4 forensic verbs ---

void DungeonRegenCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || !World->IsGameWorld() || Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage (game worlds only): Dungeon.Regen <uint64 seed>"));
		return;
	}
	ADungeonSpawner* Spawner = FindSpawner(World);
	if (!Spawner)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] no DungeonSpawner; run Dungeon.Spawn first"));
		return;
	}
	const uint64 NewSeed = FCString::Strtoui64(*Args[0], nullptr, 10);
	Spawner->RegenerateFloor(NewSeed);
}

FAutoConsoleCommandWithWorldAndArgs GDungeonRegenCmd(
	TEXT("Dungeon.Regen"),
	TEXT("In-place regenerate the dungeon from a 64-bit seed (M4 transition spike): Dungeon.Regen <seed>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonRegenCmd));

void DungeonStartRunCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || !World->IsGameWorld() || Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage (game worlds only): Dungeon.StartRun <uint64 runSeed>"));
		return;
	}
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		FM->StartRun(FCString::Strtoui64(*Args[0], nullptr, 10));
	}
}

void DungeonDescendCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		FM->RequestDescend(/*bForce=*/true);   // forensic: bypass the clear-gate policy
	}
}

void DungeonFloorStatusCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	ADungeonSpawner* Spawner = FindSpawner(World);
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	const UHealthComponent* HP = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;

	FString Rooms;
	int32 Alive = 0, Total = 0;
	if (Spawner)
	{
		for (int32 i = 0; i < Spawner->GetRoomCount(); ++i)
		{
			Alive += Spawner->GetAliveInRoom(i);
			Total += Spawner->GetInitialInRoom(i);
			Rooms += FString::Printf(TEXT(" r%d=%d/%d"), i, Spawner->GetAliveInRoom(i), Spawner->GetInitialInRoom(i));
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[FloorStatus] runActive=%s floor=%d runSeed=%llu spawnerSeed=%llu playerHP=%.0f/%.0f alive=%d/%d |%s"),
		(FM && FM->IsRunActive()) ? TEXT("yes") : TEXT("no"),
		FM ? FM->GetFloorIndex() : -1,
		static_cast<unsigned long long>(FM ? FM->GetRunSeed() : 0),
		static_cast<unsigned long long>(Spawner ? Spawner->GetEffectiveSeed64() : 0),
		HP ? HP->GetHP() : -1.0f, HP ? HP->GetMaxHP() : -1.0f,
		Alive, Total, *Rooms);
}

void DungeonSetHPCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.SetHP <n>"));
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	if (UHealthComponent* HP = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		HP->SetHP(FCString::Atof(*Args[0]));   // test-only cheat, like KillNearest
	}
}

void DungeonDescendThenDieCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	// Deterministic probe for the pending-descend death window. The window is exactly one
	// tick and the MCP bridge pumps one command per tick, so the race is not reachable from
	// two console calls - both actions must run inside a single handler. Expected result:
	// [FloorCompleted] -> [RunFailed] (death overrides pending descend) -> restart at floor 1,
	// and never a floor advance with a dead pawn.
	if (!World)
	{
		return;
	}
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	UHealthComponent* HP = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!FM || !HP)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] DescendThenDie needs an active run and a player"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[DescendThenDie] same-tick descend + lethal SetHP (window probe)"));
	FM->RequestDescend(/*bForce=*/true);
	HP->SetHP(0.0f);
}

FAutoConsoleCommandWithWorldAndArgs GDungeonStartRunCmd(
	TEXT("Dungeon.StartRun"),
	TEXT("Begin an M4 run at floor 1: Dungeon.StartRun <uint64 runSeed>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonStartRunCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonDescendCmd(
	TEXT("Dungeon.Descend"),
	TEXT("Force a floor transition (bypasses the clear-gate policy; forensic)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonDescendCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonFloorStatusCmd(
	TEXT("Dungeon.FloorStatus"),
	TEXT("Log run/floor state: floorIndex, runSeed, spawner seed, player HP, alive/total per room"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonFloorStatusCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonSetHPCmd(
	TEXT("Dungeon.SetHP"),
	TEXT("Test-only cheat: set the player's HP (0 triggers the death path)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonSetHPCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonDescendThenDieCmd(
	TEXT("Dungeon.DescendThenDie"),
	TEXT("Forensic: force-descend and kill the player in the SAME tick (pending-window probe)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonDescendThenDieCmd));

#endif // !UE_BUILD_SHIPPING

} // namespace
