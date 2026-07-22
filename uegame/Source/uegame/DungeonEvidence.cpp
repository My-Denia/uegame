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
#include "Combat/CombatConfig.h"
#include "Combat/DungeonEnemy.h"
#include "Combat/EncounterConfig.h"
#include "Combat/FloorManager.h"
#include "Combat/HealthComponent.h"
#include "Combat/LoadoutComponent.h"
#include "UI/UegameHUD.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

// Engine-agnostic scaling math (floorMultiplier/scaledEnemiesPerRoom), so BalanceReport echoes
// the SAME per-floor factors StartFloor() applies - zero duplicated constants. .cpp-only include
// (repo-root PrivateIncludePaths); this file has no UHT reflection, so no unity/std leakage.
#include "m2_adapter.hpp"

namespace
{

// Every console verb in this file is test/evidence scaffolding - the whole set is compiled
// out of Shipping (the M2-era Dungeon.Spawn/Dungeon.WalkFar included), so no forensic
// surface ships. Development/PIE builds keep the gate true, so evidence runs are unaffected.
#if !UE_BUILD_SHIPPING

uint64 GObjectiveRouteDriveGeneration = 0;
TWeakObjectPtr<APlayerController> GObjectiveRouteDriveController;

void ReleaseObjectiveRouteDriveInput(const TCHAR* Reason, float TravelCm, double ElapsedSeconds)
{
	if (APlayerController* PC = GObjectiveRouteDriveController.Get())
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
	}
	GObjectiveRouteDriveController.Reset();
	UE_LOG(LogTemp, Display,
		TEXT("[ObjectiveRouteDrive] stop reason=%s travelCm=%.1f elapsed=%.2f input=W_RELEASED"),
		Reason, TravelCm, ElapsedSeconds);
}

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
	// Run 2: the first-run seed resolver now owns the run seed (entropy by default), and the
	// spawner's default bAutoStartRun=true would route the auto-start through it - resolving a
	// RANDOM seed one tick later and regenerating over the requested one. Dungeon.Spawn is a
	// standalone single-floor forensic spawn (the documented "单层生成"), not a run entry: force
	// bAutoStartRun=false so the requested Seed is preserved (enemies still spawn via the
	// bSpawnEnemies && !bAutoStartRun path; no run => no stairs).
	Spawner->bAutoStartRun = false;
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

void DungeonFaceNearestCmd(const TArray<FString>& Args, UWorld* World)
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
	const bool bFilterType = Args.Num() > 0;
	const int32 RequestedType = bFilterType ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2) : -1;
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		if (!IsValid(*It) || !(*It)->IsActiveThreat()
			|| (bFilterType && (*It)->GetArchetypeTypeId() != RequestedType))
		{
			continue;
		}
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
	// The melee sweep uses the pawn's forward vector, while the third-person camera uses
	// controller rotation. Pin both so a deterministic presentation capture sees the target.
	const FVector Dir = Nearest->GetActorLocation() - Player->GetActorLocation();
	const FRotator Face(0.0f, Dir.Rotation().Yaw, 0.0f);
	Player->SetActorRotation(Face);
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->SetControlRotation(Face);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[DungeonEvidence] FaceNearest: yaw=%.0f dist=%.0f room=%d type=%s"),
		Face.Yaw, NearestDist, Nearest->GetRoomIndex(), Nearest->GetArchetypeDisplayName());
}

FAutoConsoleCommandWithWorldAndArgs GDungeonFaceNearestCmd(
	TEXT("Dungeon.FaceNearest"),
	TEXT("Rotate pawn/camera to the nearest active enemy, optionally filtered by typeId 0|1|2"),
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

void DungeonSetRunSeedCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || !World->IsGameWorld() || Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage (game worlds only): Dungeon.SetRunSeed <uint64 seed>"));
		return;
	}
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		const uint64 Seed = FCString::Strtoui64(*Args[0], nullptr, 10);
		// Pin the first-run seed and (re)start through the RESOLVER, so the reproducibility path
		// exercises the same entry as the shipping build: [RunSeed] source=explicit -> [RunStarted].
		// Unlike Dungeon.StartRun (which sets the seed directly), this proves the entry path itself
		// honors a pinned seed - the demonstration for acceptance C(b).
		FM->SetExplicitFirstSeed(Seed);
		FM->StartRun(FM->ResolveFirstRunSeed());
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
	int32 Alive = 0, Total = 0, ChallengeModified = 0;
	if (Spawner)
	{
		for (int32 i = 0; i < Spawner->GetRoomCount(); ++i)
		{
			Alive += Spawner->GetAliveInRoom(i);
			Total += Spawner->GetInitialInRoom(i);
			Rooms += FString::Printf(TEXT(" r%d=%d/%d"), i, Spawner->GetAliveInRoom(i), Spawner->GetInitialInRoom(i));
		}
	}
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		const ADungeonEnemy* Enemy = *It;
		if (IsValid(Enemy) && Enemy->GetOwningSpawner() == Spawner
			&& Enemy->IsRoomChallengeModified())
		{
			++ChallengeModified;
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[FloorStatus] runActive=%s state=%d floor=%d runSeed=%llu spawnerSeed=%llu playerHP=%.0f/%.0f alive=%d/%d objective=%s exitSafe=%s exitBlocked=%s contractChoice=%d contractPending=%s secureRoom=%d challengeRoom=%d selectedRoom=%d challengeDisabled=%s fallbackWarning=%s unavailableWarning=%s contractCommits=%d challengeModified=%d |%s"),
		(FM && FM->IsRunActive()) ? TEXT("yes") : TEXT("no"),
		FM ? static_cast<int32>(FM->GetRunState()) : -1,
		FM ? FM->GetFloorIndex() : -1,
		static_cast<unsigned long long>(FM ? FM->GetRunSeed() : 0),
		static_cast<unsigned long long>(Spawner ? Spawner->GetEffectiveSeed64() : 0),
		HP ? HP->GetHP() : -1.0f, HP ? HP->GetMaxHP() : -1.0f,
		Alive, Total,
		(FM && FM->IsFloorObjectiveComplete()) ? TEXT("true") : TEXT("false"),
		(FM && FM->AreFloorExitThreatsWithdrawn()) ? TEXT("true") : TEXT("false"),
		(FM && FM->IsProgressionBlockedByExitSafety()) ? TEXT("true") : TEXT("false"),
		FM ? static_cast<int32>(FM->GetRoomContractChoice()) : -1,
		(FM && FM->IsRoomContractPending()) ? TEXT("true") : TEXT("false"),
		FM ? FM->GetSecureContractRoom() : INDEX_NONE,
		FM ? FM->GetChallengeContractRoom() : INDEX_NONE,
		FM ? FM->GetSelectedContractRoom() : INDEX_NONE,
		(FM && FM->IsChallengeContractDisabled()) ? TEXT("true") : TEXT("false"),
		(FM && FM->HasRoomContractFallbackWarning()) ? TEXT("true") : TEXT("false"),
		(FM && FM->HasRoomContractUnavailableWarning()) ? TEXT("true") : TEXT("false"),
		FM ? FM->GetRoomContractCommitCount() : 0,
		ChallengeModified,
		*Rooms);
}

void DungeonSetHPCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.SetHP <n> [holdSeconds]"));
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	UHealthComponent* HP = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!HP)
	{
		return;
	}
	HP->SetHP(FCString::Atof(*Args[0]));   // test-only cheat, like KillNearest

	// Optional invincibility hold (acceptance F survival probe): an in-handler ticker re-tops
	// the pawn to full whenever it drops, firing far faster than the enemy 0.5s contact tick,
	// so a standing/walking pawn survives a multi-second WalkFar walk. In-handler composite
	// (repo rule: continuous forensic effects must not rely on external per-tick command pacing).
	const float HoldSec = (Args.Num() > 1) ? FCString::Atof(*Args[1]) : 0.0f;
	if (HoldSec > 0.0f)
	{
		// True invincibility: suppress TakeDamage at the source for holdSec. Re-topping HP from a
		// ticker raced the death chain (a lethal hit between ticks fires OnDeath -> HandlePlayerDeath
		// -> FloorManager queues a run-fail before the ticker can react, and neither SetHP nor Revive
		// cancels that queued restart). Suppressing damage means no HP drop, no OnDeath, no queued
		// fail - the survival/WalkFar probe runs uninterrupted regardless of incoming damage.
		HP->SetInvincible(true);
		// Generation guard: a later overlapping hold supersedes this one, so only the newest
		// ticker releases the shared flag (otherwise an earlier hold's expiry would drop
		// invincibility while a later hold is still meant to be active).
		static int32 GHoldGen = 0;
		const int32 MyGen = ++GHoldGen;
		TWeakObjectPtr<UHealthComponent> WeakHP = HP;
		const double StartTime = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Display,
			TEXT("[DungeonEvidence] SetHP invincibility hold armed=%.1fs (TakeDamage suppressed)"), HoldSec);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakHP, StartTime, HoldSec, MyGen](float) -> bool
			{
				if (!WeakHP.IsValid())
				{
					return false;   // pawn/PIE gone; the flag died with the component
				}
				if (FPlatformTime::Seconds() - StartTime >= HoldSec)
				{
					if (MyGen == GHoldGen)   // still the latest hold - safe to release
					{
						WeakHP->SetInvincible(false);
						UE_LOG(LogTemp, Display, TEXT("[DungeonEvidence] SetHP invincibility hold expired"));
					}
					return false;
				}
				return true;
			}), 0.1f);
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

FAutoConsoleCommandWithWorldAndArgs GDungeonSetRunSeedCmd(
	TEXT("Dungeon.SetRunSeed"),
	TEXT("Pin the first-run seed and restart via the entry resolver (reproducibility): Dungeon.SetRunSeed <seed>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonSetRunSeedCmd));

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

// --- Playability verb: objective-floor instrumentation ---

void DungeonBalanceReportCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	// Pure-DataTable math block: every number below traces to the combat row (echoed once by
	// FUegameCombatConfig). Objectives 2 (melee TTK) and 4 (floor-scaling K=2 drain ratio) are
	// fully decidable here; objectives 1 (first-contact) and 3 (standing survival) are empirical
	// and measured by the standing probe armed at the end (needs an active run).
	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	// Contact damage only lands on the enemy's 0.5s PursueTick (ADungeonEnemy sets a 0.5s timer),
	// and only when Now-lastHit >= DamageInterval - so the EFFECTIVE interval is the nominal one
	// rounded UP to the next tick (a 1.25s nominal actually fires at 1.5s). Compute DPS from the
	// effective interval so the report doesn't overstate incoming damage; use 0.5s multiples in
	// the DataTable to avoid the gap.
	constexpr float kEnemyTickSeconds = 0.5f;   // == ADungeonEnemy PursueTimer period
	const float EffInterval = (Cfg.EnemyDamageInterval > 0.0f)
		? FMath::CeilToFloat(Cfg.EnemyDamageInterval / kEnemyTickSeconds) * kEnemyTickSeconds
		: 0.0f;
	const float PerEnemyDPS = (EffInterval > 0.0f) ? (Cfg.EnemyContactDamage / EffInterval) : 0.0f;
	const int32 MaxFloors = FMath::Max(1, Cfg.MaxFloors);

	UE_LOG(LogTemp, Display, TEXT("[BalanceReport] === DataTable math (source=%s) ==="),
		FUegameCombatConfig::IsFromDataTable() ? TEXT("CSV") : TEXT("compiled-fallback"));
	UE_LOG(LogTemp, Display,
		TEXT("[BalanceReport] player{HP=%.0f atkDmg=%.0f range=%.0f cd=%.2f} enemy{HP=%.0f spd=%.0f dmg=%.0f interval=%.2f(eff %.2f @%.1fs tick)} perRoom=%d scaling=%.2f perEnemyDPS=%.2f"),
		Cfg.PlayerMaxHP, Cfg.PlayerAttackDamage, Cfg.PlayerAttackRange, Cfg.PlayerAttackCooldown,
		Cfg.EnemyMaxHP, Cfg.EnemyMoveSpeed, Cfg.EnemyContactDamage, Cfg.EnemyDamageInterval, EffInterval, kEnemyTickSeconds,
		Cfg.EnemiesPerRoom, Cfg.PerFloorScaling, PerEnemyDPS);

	double Floor1FullRoomRate = -1.0, FloorLastFullRoomRate = -1.0;
	int32 Floor1EffPerRoom = 0, FloorLastEffPerRoom = 0;
	for (int32 F = 1; F <= MaxFloors; ++F)
	{
		const double Mult = m2::floorMultiplier(F, Cfg.PerFloorScaling);
		const int32 EffPerRoom = m2::scaledEnemiesPerRoom(Cfg.EnemiesPerRoom, F, Cfg.PerFloorScaling);
		const float EffHP = static_cast<float>(Cfg.EnemyMaxHP * Mult);
		const int32 TTK = (Cfg.PlayerAttackDamage > 0.0f)
			? FMath::CeilToInt(EffHP / Cfg.PlayerAttackDamage) : -1;
		// Incoming drain RATE (HP/s) if K enemies converge = K * perEnemyDPS. Per-enemy DPS is
		// floor-invariant (contact dmg doesn't scale), so the K=1/2/4 columns are identical each
		// floor - they measure crowd size, not scaling. OBJ4 (does scaling stay fair?) instead
		// compares the FULL-ROOM convergence using the SCALED EffPerRoom, which is what actually
		// grows with PerFloorScaling (floor1 2 enemies -> floor3 4 at scaling 0.5).
		const double DrainTimeK1 = (PerEnemyDPS > 0.0) ? (Cfg.PlayerMaxHP / (1.0 * PerEnemyDPS)) : -1.0;
		const double DrainTimeK2 = (PerEnemyDPS > 0.0) ? (Cfg.PlayerMaxHP / (2.0 * PerEnemyDPS)) : -1.0;
		const double DrainTimeK4 = (PerEnemyDPS > 0.0) ? (Cfg.PlayerMaxHP / (4.0 * PerEnemyDPS)) : -1.0;
		const double FullRoomRate = static_cast<double>(EffPerRoom) * PerEnemyDPS;
		if (F == 1) { Floor1FullRoomRate = FullRoomRate; Floor1EffPerRoom = EffPerRoom; }
		if (F == MaxFloors) { FloorLastFullRoomRate = FullRoomRate; FloorLastEffPerRoom = EffPerRoom; }
		UE_LOG(LogTemp, Display,
			TEXT("[BalanceReport] floor=%d effPerRoom=%d effHP=%.0f meleeTTK=%d swings | timeToDrain K1=%.1fs K2=%.1fs K4=%.1fs | fullRoomRate=%.1f HP/s"),
			F, EffPerRoom, EffHP, TTK, DrainTimeK1, DrainTimeK2, DrainTimeK4, FullRoomRate);
	}

	const int32 TTK1 = (Cfg.PlayerAttackDamage > 0.0f)
		? FMath::CeilToInt(Cfg.EnemyMaxHP / Cfg.PlayerAttackDamage) : -1;
	const double RateRatio = (Floor1FullRoomRate > 0.0) ? (FloorLastFullRoomRate / Floor1FullRoomRate) : -1.0;
	UE_LOG(LogTemp, Display, TEXT("[BalanceReport] === objective floors ==="));
	UE_LOG(LogTemp, Display, TEXT("[BalanceReport] OBJ2 meleeTTK(floor1)=%d swings  (<=3 => %s)"),
		TTK1, (TTK1 >= 0 && TTK1 <= 3) ? TEXT("GREEN") : TEXT("RED"));
	UE_LOG(LogTemp, Display,
		TEXT("[BalanceReport] OBJ4 floorLast full-room drain / floor1 full-room drain = %.2fx  (scaled perRoom %d->%d; <=2x => %s)"),
		RateRatio, Floor1EffPerRoom, FloorLastEffPerRoom,
		(RateRatio > 0.0 && RateRatio <= 2.0) ? TEXT("GREEN") : TEXT("RED"));
	UE_LOG(LogTemp, Display,
		TEXT("[BalanceReport] OBJ1 first-contact(>=10s) & OBJ3 standing-survival(>=45s): see [BalanceProbe] below (empirical)"));

	// --- Standing probe: the ONE PIE actual per the contract (first-contact), plus survival.
	// The forensic pawn takes no input, so this measures approach time (spawn-room-free rule)
	// and how long a stationary player lasts. First-contact is a sustained state (reliable to
	// poll); survival is inferred from the run restarting (RunSeed changes on death - a standing
	// player cannot win), which is a persistent signal a coarse poll cannot miss.
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	APawn* Player = World && World->GetFirstPlayerController()
		? World->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!FM || !FM->IsRunActive() || !Player)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BalanceProbe] no active run/player - start a run (Dungeon.StartRun <seed>) then re-run BalanceReport for OBJ1/OBJ3"));
		return;
	}

	TWeakObjectPtr<UWorld> WeakWorld = World;
	TWeakObjectPtr<APawn> WeakPlayer = Player;
	const uint64 InitialRunSeed = FM->GetRunSeed();
	const double StartTime = FPlatformTime::Seconds();
	TSharedRef<bool> bContactLogged = MakeShared<bool>(false);
	UE_LOG(LogTemp, Display,
		TEXT("[BalanceProbe] armed on floor %d runSeed=%llu (standing, no input): timing first-contact + survival"),
		FM->GetFloorIndex(), static_cast<unsigned long long>(InitialRunSeed));

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakWorld, WeakPlayer, InitialRunSeed, StartTime, bContactLogged](float) -> bool
		{
			UWorld* W = WeakWorld.Get();
			APawn* P = WeakPlayer.Get();
			if (!W || !P)
			{
				return false;   // PIE ended
			}
			UUegameFloorManager* FM2 = UUegameFloorManager::Get(W);
			if (!FM2)
			{
				return false;
			}
			const double Elapsed = FPlatformTime::Seconds() - StartTime;

			if (!*bContactLogged)
			{
				float Nearest = TNumericLimits<float>::Max();
				for (TActorIterator<ADungeonEnemy> It(W); It; ++It)
				{
					Nearest = FMath::Min(Nearest,
						static_cast<float>(FVector::Dist2D((*It)->GetActorLocation(), P->GetActorLocation())));
				}
				if (Nearest <= 130.0f)   // ~enemy contact reach (capsule sum + slack)
				{
					*bContactLogged = true;
					UE_LOG(LogTemp, Display,
						TEXT("[BalanceProbe] firstContact elapsed=%.1fs nearestDist=%.0f"), Elapsed, Nearest);
				}
			}

			if (FM2->GetRunSeed() != InitialRunSeed)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[BalanceProbe] standingSurvival elapsed=%.1fs (run restarted => standing death)"), Elapsed);
				return false;
			}
			if (Elapsed > 180.0)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[BalanceProbe] standingSurvival >180s (still alive; stopping probe)"));
				return false;
			}
			return true;
		}), 0.25f);
}

// Run 2.5 perception forensics: per-enemy {state, dist, LOS, aggro/leash, pos} + a summary.
// P1 (beyond-range enemy Idle + pos delta 0 across two calls), P2 (wall-blocked in-range
// enemy Idle los=no), and P4 (chasing=0 at run start) all read off these lines.
void DungeonAggroStatusCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;

	int32 Total = 0;
	int32 Chasing = 0;
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		ADungeonEnemy* E = *It;
		if (!IsValid(E))
		{
			continue;
		}
		const bool bChase = E->IsChasing();
		const float Dist = Player ? FVector::Dist2D(E->GetActorLocation(), Player->GetActorLocation()) : -1.0f;
		const bool bLOS = Player ? E->ComputeLOSTo(Player) : false;
		const FVector P = E->GetActorLocation();
		if (bChase)
		{
			++Chasing;
		}
		UE_LOG(LogTemp, Display,
			TEXT("[AggroStatus] i=%d room=%d state=%s dist=%.0f los=%s aggro=%.0f leash=%.0f pos=(%.0f,%.0f,%.0f)"),
			Total, E->GetRoomIndex(), bChase ? TEXT("Chasing") : TEXT("Idle"),
			Dist, bLOS ? TEXT("yes") : TEXT("no"),
			E->GetAggroRange(), E->GetLeashRange(), P.X, P.Y, P.Z);
		++Total;
	}
	UE_LOG(LogTemp, Display, TEXT("[AggroStatus] summary total=%d chasing=%d idle=%d"),
		Total, Chasing, Total - Chasing);
}

FAutoConsoleCommandWithWorldAndArgs GDungeonBalanceReportCmd(
	TEXT("Dungeon.BalanceReport"),
	TEXT("Echo per-floor combat math (TTK, K=1/2/4 drain, scaling) + arm the standing first-contact/survival probe"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonBalanceReportCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonAggroStatusCmd(
	TEXT("Dungeon.AggroStatus"),
	TEXT("Log per-enemy perception: state (Idle/Chasing), distance, LOS, aggro/leash range, position"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonAggroStatusCmd));

// --- M5 build-diversity loadout forensics (PR #12B) ---

// Dump the player's loadout state (runSeed/floor/offerIndex/RewardPending/offer/chosen/resolved/base).
void DungeonLoadoutStatusCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	ULoadoutComponent* LC = Player ? Player->FindComponentByClass<ULoadoutComponent>() : nullptr;
	if (!LC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoadoutStatus] no loadout component on the player pawn"));
		return;
	}
	LC->LogStatus();
}

// Headless/forensic reward pick (no UI needed): Dungeon.ChooseLoadout <0|1|2>. Routed through the
// FloorManager so the pick + stairs re-poke share the exact code path the 1/2/3 keys use.
void DungeonChooseLoadoutCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.ChooseLoadout <0|1|2>"));
		return;
	}
	const int32 Index = FCString::Atoi(*Args[0]);
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		FM->TryChooseLoadout(Index);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] Dungeon.ChooseLoadout needs an active FloorManager"));
	}
}

FAutoConsoleCommandWithWorldAndArgs GDungeonLoadoutStatusCmd(
	TEXT("Dungeon.LoadoutStatus"),
	TEXT("Log the player's loadout: runSeed, floor, offerIndex, RewardPending, current offer, chosen ids, resolved + base stats"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonLoadoutStatusCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonChooseLoadoutCmd(
	TEXT("Dungeon.ChooseLoadout"),
	TEXT("Pick a loadout reward option (headless/forensic): Dungeon.ChooseLoadout <0|1|2>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonChooseLoadoutCmd));

// --- M6B encounter-diversity forensics (verbs 19-20) ---
// Both read the spawner's M6 cache (what ACTUALLY spawned this floor), never a recompute -
// so a hash mismatch here is real evidence, not a second copy of the same math.

// Dump the current floor's room-role assignment + the roomRoleHash anchor.
void DungeonRoomRolesCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	ADungeonSpawner* Spawner = FindSpawner(World);
	if (!FM || !FM->IsRunActive() || !Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomRoles] no active run/spawner (use Dungeon.StartRun <seed>)"));
		return;
	}
	if (!Spawner->HasEncounterAssignment())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomRoles] no encounter assignment cached (tables unavailable or floor spawned outside a run)"));
		return;
	}
	// Report the spawner's snapshot (what actually spawned); a drift vs the FloorManager's
	// live state would mean the cache is stale - surface it instead of mixing sources.
	const uint64 RunSeed = Spawner->GetEncounterRunSeed();
	const int32 Floor = Spawner->GetEncounterFloorIndex();
	if (RunSeed != FM->GetRunSeed() || Floor != FM->GetFloorIndex())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomRoles] STALE cache: spawner snapshot runSeed=%llu floor=%d vs FloorManager runSeed=%llu floor=%d"),
			static_cast<unsigned long long>(RunSeed), Floor,
			static_cast<unsigned long long>(FM->GetRunSeed()), FM->GetFloorIndex());
	}
	const uint64 FloorSeed = m2::deriveFloorSeed(RunSeed, Floor);   // no getter exists; same derivation StartFloor logs
	const TArray<int32>& Roles = Spawner->GetCachedRoomRoles();
	UE_LOG(LogTemp, Display,
		TEXT("[RoomRoles] runSeed=%llu floor=%d floorSeed=%llu rooms=%d roomRoleHash=0x%llx"),
		static_cast<unsigned long long>(RunSeed), Floor,
		static_cast<unsigned long long>(FloorSeed), Roles.Num(),
		static_cast<unsigned long long>(Spawner->GetCachedRoomRoleHash()));
	for (int32 RoomIdx = 0; RoomIdx < Roles.Num(); ++RoomIdx)
	{
		UE_LOG(LogTemp, Display, TEXT("[RoomRoles] room=%d role=%s"),
			RoomIdx, FUegameEncounterConfig::RoleName(Roles[RoomIdx]));
	}
}

// Dump the current floor's archetype roster (tally + per-room + resolved per-type stats) with
// the enemyTypeHash anchor AND the preserved m2 anchors (spawnPlanHash/enemyPlanHash).
void DungeonEnemyRosterCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World)
	{
		return;
	}
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	ADungeonSpawner* Spawner = FindSpawner(World);
	if (!FM || !FM->IsRunActive() || !Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnemyRoster] no active run/spawner (use Dungeon.StartRun <seed>)"));
		return;
	}
	if (!Spawner->HasEncounterAssignment())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnemyRoster] no encounter assignment cached (tables unavailable or floor spawned outside a run)"));
		return;
	}
	// Same snapshot-vs-live consistency guard as Dungeon.RoomRoles.
	if (Spawner->GetEncounterRunSeed() != FM->GetRunSeed() || Spawner->GetEncounterFloorIndex() != FM->GetFloorIndex())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EnemyRoster] STALE cache: spawner snapshot runSeed=%llu floor=%d vs FloorManager runSeed=%llu floor=%d"),
			static_cast<unsigned long long>(Spawner->GetEncounterRunSeed()), Spawner->GetEncounterFloorIndex(),
			static_cast<unsigned long long>(FM->GetRunSeed()), FM->GetFloorIndex());
	}
	const FIntVector Tally = Spawner->GetCachedTypeTally();
	UE_LOG(LogTemp, Display,
		TEXT("[EnemyRoster] runSeed=%llu floor=%d total=%d Grunt=%d Runner=%d Brute=%d enemyTypeHash=0x%llx spawnPlanHash=0x%llx enemyPlanHash=0x%llx"),
		static_cast<unsigned long long>(Spawner->GetEncounterRunSeed()), Spawner->GetEncounterFloorIndex(),
		Tally.X + Tally.Y + Tally.Z, Tally.X, Tally.Y, Tally.Z,
		static_cast<unsigned long long>(Spawner->GetCachedEnemyTypeHash()),
		static_cast<unsigned long long>(Spawner->GetCachedSpawnPlanHash()),
		static_cast<unsigned long long>(Spawner->GetCachedEnemyPlanHash()));
	const TArray<int32>& Roles = Spawner->GetCachedRoomRoles();
	const TArray<FIntVector>& RoomCounts = Spawner->GetCachedRoomTypeCounts();
	for (int32 RoomIdx = 0; RoomIdx < Roles.Num(); ++RoomIdx)
	{
		const FIntVector C = RoomCounts.IsValidIndex(RoomIdx) ? RoomCounts[RoomIdx] : FIntVector::ZeroValue;
		UE_LOG(LogTemp, Display,
			TEXT("[EnemyRoster] room=%d role=%s Grunt=%d Runner=%d Brute=%d"),
			RoomIdx, FUegameEncounterConfig::RoleName(Roles[RoomIdx]), C.X, C.Y, C.Z);
	}
	// Resolved per-type stats (loader table x current floor HP multiplier) - the same numbers
	// ApplyArchetype stamped on each pawn, so PIE can assert CSV -> pawn without a debugger.
	const float HpMult = Spawner->GetEncounterHpMult();
	for (int32 T = 0; T < FUegameEncounterConfig::NumTypes; ++T)
	{
		const FEncounterArchetypeStats& S = FUegameEncounterConfig::GetArchetype(T);
		UE_LOG(LogTemp, Display,
			TEXT("[EnemyRoster] type=%s hp=%.0f (arch=%.0f x mult=%.2f) speed=%.0f dmg=%.0f interval=%.2f aggro=%.0f leash=%.0f scale=%.2f"),
			FUegameEncounterConfig::TypeName(T), S.MaxHP * HpMult, S.MaxHP, HpMult,
			S.MoveSpeed, S.ContactDamage, S.DamageInterval, S.AggroRange, S.LeashRange, S.VisualScale);
	}
}

FAutoConsoleCommandWithWorldAndArgs GDungeonRoomRolesCmd(
	TEXT("Dungeon.RoomRoles"),
	TEXT("Log the current floor's room-role assignment: runSeed, floor, floorSeed, roomRoleHash, per-room role"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonRoomRolesCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonEnemyRosterCmd(
	TEXT("Dungeon.EnemyRoster"),
	TEXT("Log the current floor's archetype roster: tally, per-room roster, resolved stats, enemyTypeHash + preserved m2 anchors"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonEnemyRosterCmd));

// --- Phase 1A runtime-authority negative seams ---
// These setters/triggers exist only inside the file-wide !UE_BUILD_SHIPPING gate. They
// force otherwise rare failure branches; ordinary recovery is still exercised with R/Q.
void DungeonExitWithdrawalFailureCmd(const TArray<FString>& Args, UWorld* World)
{
	ADungeonSpawner* Spawner = FindSpawner(World);
	if (!Spawner || Args.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.ExitWithdrawalFailure <0|1>"));
		return;
	}
	const bool bForce = FCString::Atoi(*Args[0]) != 0;
	Spawner->SetForceExitWithdrawalFailureForTests(bForce);
	UE_LOG(LogTemp, Display, TEXT("[RuntimeAuthorityTest] exitWithdrawalFailure=%s"),
		bForce ? TEXT("true") : TEXT("false"));
}

void DungeonNotifyFloorClearedCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		FM->NotifyFloorCleared();
	}
}

void DungeonTryDescendCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		FM->RequestDescend(/*bForce=*/false);
	}
}

void DungeonFinaleInitFailCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
	{
		FM->NotifyFinaleInitFailed();
	}
}

void DungeonBehaviorProbeCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() != 2)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DungeonEvidence] usage: Dungeon.BehaviorProbe <typeId 0|1|2> <mode 0..8>"));
		return;
	}
	const int32 TypeId = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2);
	const int32 Mode = FMath::Clamp(FCString::Atoi(*Args[1]), 0, 8);
	APawn* Player = World->GetFirstPlayerController()
		? World->GetFirstPlayerController()->GetPawn() : nullptr;
	ADungeonEnemy* Target = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		ADungeonEnemy* Candidate = *It;
		if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed()
			|| !Candidate->IsActiveThreat() || Candidate->GetArchetypeTypeId() != TypeId)
		{
			continue;
		}
		const double Distance = Player
			? FVector::DistSquared2D(Player->GetActorLocation(), Candidate->GetActorLocation())
			: static_cast<double>(Candidate->GetSpawnOrdinal());
		if (!Target || Distance < BestDistance
			|| (Distance == BestDistance && Candidate->GetSpawnOrdinal() < Target->GetSpawnOrdinal()))
		{
			Target = Candidate;
			BestDistance = Distance;
		}
	}
	if (!Target)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[EnemyBehaviorTest] select=false type=%d mode=%d"), TypeId, Mode);
		return;
	}
	UE_LOG(LogTemp, Display,
		TEXT("[EnemyBehaviorTest] select=true type=%d mode=%d room=%d ordinal=%d"),
		TypeId, Mode, Target->GetRoomIndex(), Target->GetSpawnOrdinal());
	Target->RunBehaviorContractProbeForTests(Mode);
	if (Mode == 8)
	{
		Target->ApplyPlayerDamage(99999, Player);
	}
	else if (Mode == 7)
	{
		// Reset inside the same console handler. External commands cannot reliably
		// land inside the 0.05s behavior -> 0.5s damage window.
		if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
		{
			const uint64 Seed = FM->GetRunSeed();
			UE_LOG(LogTemp, Display,
				TEXT("[EnemyBehaviorTest] mode=7 restart=true seed=%llu"), Seed);
			FM->StartRun(Seed);
		}
	}
}

void DungeonContractFailureModeCmd(const TArray<FString>& Args, UWorld* World)
{
	ADungeonSpawner* Spawner = FindSpawner(World);
	if (!Spawner || Args.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.ContractFailureMode <0|1|2>"));
		return;
	}
	const int32 Mode = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2);
	Spawner->SetChallengeContractFailureModeForTests(Mode);
	UE_LOG(LogTemp, Display, TEXT("[RoomContractTest] failureMode=%d oneShot=true"), Mode);
}

void DungeonContractFreshSpawnerFailureCmd(const TArray<FString>& Args, UWorld* World)
{
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	if (!FM || Args.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.ContractFreshSpawnerFailure <0|1>"));
		return;
	}
	const bool bForce = FCString::Atoi(*Args[0]) != 0;
	FM->SetForceNoFreshSpawnerForTests(bForce);
	UE_LOG(LogTemp, Display, TEXT("[RoomContractTest] freshSpawnerFailure=%s"),
		bForce ? TEXT("true") : TEXT("false"));
}

void DungeonRouteFaultModeCmd(const TArray<FString>& Args, UWorld* World)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AUegameHUD* HUD = PC ? Cast<AUegameHUD>(PC->GetHUD()) : nullptr;
	if (!HUD || Args.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[DungeonEvidence] usage: Dungeon.RouteFaultMode <0..6>"));
		return;
	}
	HUD->SetObjectiveRouteFaultModeForTests(FMath::Clamp(FCString::Atoi(*Args[0]), 0, 6));
}

void DungeonRouteStatusCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AUegameHUD* HUD = PC ? Cast<AUegameHUD>(PC->GetHUD()) : nullptr;
	if (HUD)
	{
		HUD->LogObjectiveRouteStatusForTests();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ObjectiveRouteStatus] hud=false"));
	}
}

void DungeonRouteDriveCmd(const TArray<FString>& Args, UWorld* World)
{
	++GObjectiveRouteDriveGeneration;
	if (GObjectiveRouteDriveController.IsValid())
	{
		ReleaseObjectiveRouteDriveInput(TEXT("replaced"), 0.0f, 0.0);
	}

	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("[ObjectiveRouteDrive] start=false reason=no-game-world"));
		return;
	}
	if (Args.Num() > 0 && Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Display, TEXT("[ObjectiveRouteDrive] stop reason=requested input=already-released"));
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	AUegameHUD* HUD = PC ? Cast<AUegameHUD>(PC->GetHUD()) : nullptr;
	if (!PC || !Pawn || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("[ObjectiveRouteDrive] start=false reason=missing-player-or-hud"));
		return;
	}
	FVector InitialWaypoint;
	if (!HUD->TryGetObjectiveRouteWaypointForTests(InitialWaypoint))
	{
		UE_LOG(LogTemp, Error, TEXT("[ObjectiveRouteDrive] start=false reason=route-not-ready"));
		HUD->LogObjectiveRouteStatusForTests();
		return;
	}

	const float MaxSeconds = Args.Num() > 0
		? FMath::Clamp(FCString::Atof(*Args[0]), 1.0f, 60.0f)
		: 30.0f;
	const uint64 DriveGeneration = GObjectiveRouteDriveGeneration;
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const FVector StartLocation = Pawn->GetActorLocation();
	const double StartedAt = FPlatformTime::Seconds();
	GObjectiveRouteDriveController = PC;
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	UE_LOG(LogTemp, Display,
		TEXT("[ObjectiveRouteDrive] start=true maxSeconds=%.1f input=W_PRESSED steering=smooth"),
		MaxSeconds);

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[DriveGeneration, WeakWorld, StartLocation, StartedAt, MaxSeconds,
			LastWaypoint = InitialWaypoint, RouteUnavailableAt = -1.0](float DeltaSeconds) mutable
		{
			UWorld* CurrentWorld = WeakWorld.Get();
			APlayerController* CurrentPC = CurrentWorld ? CurrentWorld->GetFirstPlayerController() : nullptr;
			APawn* CurrentPawn = CurrentPC ? CurrentPC->GetPawn() : nullptr;
			AUegameHUD* CurrentHUD = CurrentPC ? Cast<AUegameHUD>(CurrentPC->GetHUD()) : nullptr;
			const double Elapsed = FPlatformTime::Seconds() - StartedAt;
			const float TravelCm = CurrentPawn
				? FVector::Dist2D(CurrentPawn->GetActorLocation(), StartLocation)
				: 0.0f;

			if (DriveGeneration != GObjectiveRouteDriveGeneration)
			{
				return false;
			}
			if (!CurrentWorld || !CurrentPC || !CurrentPawn || !CurrentHUD)
			{
				ReleaseObjectiveRouteDriveInput(TEXT("world-or-player-lost"), TravelCm, Elapsed);
				return false;
			}
			if (Elapsed >= MaxSeconds)
			{
				ReleaseObjectiveRouteDriveInput(TEXT("timeout"), TravelCm, Elapsed);
				CurrentHUD->LogObjectiveRouteStatusForTests();
				return false;
			}

			float NearestThreatCm = TNumericLimits<float>::Max();
			for (TActorIterator<ADungeonEnemy> It(CurrentWorld); It; ++It)
			{
				if (IsValid(*It) && (*It)->IsActiveThreat())
				{
					NearestThreatCm = FMath::Min(NearestThreatCm,
						FVector::Dist2D((*It)->GetActorLocation(), CurrentPawn->GetActorLocation()));
				}
			}
			if (NearestThreatCm <= 220.0f)
			{
				ReleaseObjectiveRouteDriveInput(TEXT("attack-range"), TravelCm, Elapsed);
				CurrentHUD->LogObjectiveRouteStatusForTests();
				return false;
			}

			FVector Waypoint = LastWaypoint;
			if (CurrentHUD->TryGetObjectiveRouteWaypointForTests(Waypoint))
			{
				LastWaypoint = Waypoint;
				RouteUnavailableAt = -1.0;
			}
			else
			{
				if (RouteUnavailableAt < 0.0)
				{
					RouteUnavailableAt = FPlatformTime::Seconds();
				}
				if (FPlatformTime::Seconds() - RouteUnavailableAt > 0.5)
				{
					ReleaseObjectiveRouteDriveInput(TEXT("route-unavailable"), TravelCm, Elapsed);
					CurrentHUD->LogObjectiveRouteStatusForTests();
					return false;
				}
			}

			const FVector ToWaypoint = Waypoint - CurrentPawn->GetActorLocation();
			if (ToWaypoint.SizeSquared2D() > FMath::Square(1.0f))
			{
				FRotator Rotation = CurrentPC->GetControlRotation();
				const float TargetYaw = ToWaypoint.Rotation().Yaw;
				Rotation.Yaw = FMath::FixedTurn(Rotation.Yaw, TargetYaw,
					FMath::Clamp(DeltaSeconds, 0.0f, 0.1f) * 150.0f);
				Rotation.Pitch = 0.0f;
				Rotation.Roll = 0.0f;
				CurrentPC->SetControlRotation(Rotation);
			}
			CurrentPC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
			return true;
		}), 0.0f);
}

void DungeonRouteRespawnPawnCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AGameModeBase* GameMode = World ? World->GetAuthGameMode<AGameModeBase>() : nullptr;
	APawn* OldPawn = PC ? PC->GetPawn() : nullptr;
	if (!World || !World->IsGameWorld() || !PC || !GameMode || !OldPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[ObjectiveRoutePawnChurn] respawn=false reason=missing-world-player-or-mode"));
		return;
	}

	const uint32 OldPawnKey = GetTypeHash(OldPawn);
	FTransform RespawnTransform = OldPawn->GetActorTransform();
	RespawnTransform.AddToTranslation(FVector(0.0f, 0.0f, 150.0f));
	PC->UnPossess();
	OldPawn->Destroy();
	GameMode->RestartPlayerAtTransform(PC, RespawnTransform);
	APawn* NewPawn = PC->GetPawn();
	const uint32 NewPawnKey = IsValid(NewPawn) ? GetTypeHash(NewPawn) : 0;
	UE_LOG(LogTemp, Display,
		TEXT("[ObjectiveRoutePawnChurn] respawn=%s oldPawnKey=%u newPawnKey=%u changed=%s"),
		IsValid(NewPawn) ? TEXT("true") : TEXT("false"), OldPawnKey, NewPawnKey,
		OldPawnKey != NewPawnKey && NewPawnKey != 0 ? TEXT("true") : TEXT("false"));
}

FAutoConsoleCommandWithWorldAndArgs GDungeonExitWithdrawalFailureCmd(
	TEXT("Dungeon.ExitWithdrawalFailure"),
	TEXT("Development-only negative seam: force floor-exit withdrawal failure (0|1)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonExitWithdrawalFailureCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonNotifyFloorClearedCmd(
	TEXT("Dungeon.NotifyFloorCleared"),
	TEXT("Development-only trigger for the ordinary atomic floor-completion path"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonNotifyFloorClearedCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonTryDescendCmd(
	TEXT("Dungeon.TryDescend"),
	TEXT("Development-only trigger for the ordinary non-forced stairs request path"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonTryDescendCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonFinaleInitFailCmd(
	TEXT("Dungeon.FinaleInitFail"),
	TEXT("Development-only trigger for the explicit non-restarting finale error state"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonFinaleInitFailCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonBehaviorProbeCmd(
	TEXT("Dungeon.BehaviorProbe"),
	TEXT("Development-only behavior probe: typeId 0|1|2; mode 0 positive, 1 range, 2 LOS, 3 nav, 4 leash, 5 stale, 6 dead, 7 arm-reset, 8 arm-death"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonBehaviorProbeCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonContractFailureModeCmd(
	TEXT("Dungeon.ContractFailureMode"),
	TEXT("Development-only one-shot room-contract seam: 0 normal, 1 stale preflight, 2 partial apply/rollback"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonContractFailureModeCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonContractFreshSpawnerFailureCmd(
	TEXT("Dungeon.ContractFreshSpawnerFailure"),
	TEXT("Development-only seam: make room-contract fresh-spawner lookup fail (0|1)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonContractFreshSpawnerFailureCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonRouteFaultModeCmd(
	TEXT("Dungeon.RouteFaultMode"),
	TEXT("Development-only objective route seam: 0 normal; 1/2 projection; 3 invalid; 4 partial; 5 no fresh authority; 6 ambiguous authority"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonRouteFaultModeCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonRouteStatusCmd(
	TEXT("Dungeon.RouteStatus"),
	TEXT("Development-only readback of the HUD route cache and query serial"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonRouteStatusCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonRouteDriveCmd(
	TEXT("Dungeon.RouteDrive"),
	TEXT("Development-only smooth held-W traversal of the visible objective route: [maxSeconds|stop]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonRouteDriveCmd));

FAutoConsoleCommandWithWorldAndArgs GDungeonRouteRespawnPawnCmd(
	TEXT("Dungeon.RouteRespawnPawn"),
	TEXT("Development-only pawn-identity churn probe for route-cache invalidation"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DungeonRouteRespawnPawnCmd));

#endif // !UE_BUILD_SHIPPING

} // namespace
