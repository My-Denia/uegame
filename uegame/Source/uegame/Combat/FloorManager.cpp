// FloorManager.cpp - see header.

#include "FloorManager.h"

#include "CombatConfig.h"
#include "HealthComponent.h"
#include "../DungeonSpawner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

// Engine-agnostic seed pipeline (repo root include path; .cpp-only include).
#include "m2_adapter.hpp"

namespace
{
	// Run seed for maps with no pre-placed spawner: the cross-compiler pinned forensic
	// seed, so a fresh install's first run IS the pre-registered reference run. Restarts
	// chain away from it deterministically via m2::nextRunSeed.
	constexpr uint64 kDefaultRunSeed = 7;
}

void UUegameFloorManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ActorsInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(
		this, &UUegameFloorManager::OnWorldActorsInitialized);
}

void UUegameFloorManager::Deinitialize()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(ActorsInitializedHandle);
	Super::Deinitialize();
}

void UUegameFloorManager::OnWorldActorsInitialized(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	// One tick later the player pawn exists. A level-placed spawner takes precedence:
	// this side yields whenever FindSpawner() sees one (that spawner's own bAutoStartRun
	// decides), and both sides re-check bRunActive, so double-starts are impossible.
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bRunActive && !FindSpawner())
			{
				UE_LOG(LogTemp, Display,
					TEXT("[Dungeon] AutoStartRun: runSeed=%llu (default; no spawner in level)"),
					static_cast<unsigned long long>(kDefaultRunSeed));
				StartRun(kDefaultRunSeed);
			}
		}));
}

UUegameFloorManager* UUegameFloorManager::Get(UWorld* World)
{
	if (!World || !World->GetGameInstance())
	{
		return nullptr;
	}
	return World->GetGameInstance()->GetSubsystem<UUegameFloorManager>();
}

ADungeonSpawner* UUegameFloorManager::FindSpawner() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ADungeonSpawner> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void UUegameFloorManager::HealPlayerFull() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (UHealthComponent* HP = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		HP->Revive();
	}
}

void UUegameFloorManager::StartRun(uint64 InRunSeed)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("[FloorManager] StartRun is game-world only"));
		return;
	}

	ADungeonSpawner* Spawner = FindSpawner();
	if (!Spawner)
	{
		// Fresh spawner with the run's floor-1 state driven entirely through
		// RegenerateFloor (single code path for every floor). Suppress the
		// BeginPlay-side enemy spawn/teleport, then re-enable for the regen.
		ADungeonSpawner* Deferred = World->SpawnActorDeferred<ADungeonSpawner>(
			ADungeonSpawner::StaticClass(), FTransform(FVector::ZeroVector), nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Deferred)
		{
			UE_LOG(LogTemp, Error, TEXT("[FloorManager] spawner spawn failed"));
			return;
		}
		Deferred->bSpawnEnemies = false;
		Deferred->bTeleportPlayerToStart = false;
		Deferred->bAutoStartRun = false;   // this spawner IS manager-driven; never self-start
		Deferred->FinishSpawning(FTransform(FVector::ZeroVector));
		Deferred->bSpawnEnemies = true;
		Deferred->bTeleportPlayerToStart = true;
		Spawner = Deferred;
	}

	RunSeed = InRunSeed;
	bRunActive = true;
	PendingTransition = EPendingTransition::None;   // a manual (re)start cancels queued intent
	UE_LOG(LogTemp, Display, TEXT("[RunStarted] runSeed=%llu maxFloors=%d"),
		static_cast<unsigned long long>(RunSeed), FUegameCombatConfig::Get().MaxFloors);
	StartFloor(1);
}

void UUegameFloorManager::StartFloor(int32 NewFloorIndex)
{
	ADungeonSpawner* Spawner = FindSpawner();
	if (!bRunActive || !Spawner)
	{
		UE_LOG(LogTemp, Error, TEXT("[FloorManager] StartFloor without active run/spawner"));
		return;
	}

	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	FloorIndex = NewFloorIndex;

	const uint64 FloorSeed = m2::deriveFloorSeed(RunSeed, FloorIndex);
	const double Mult = m2::floorMultiplier(FloorIndex, Cfg.PerFloorScaling);
	const int32 EffPerRoom = m2::scaledEnemiesPerRoom(Cfg.EnemiesPerRoom, FloorIndex, Cfg.PerFloorScaling);
	const float EffHP = static_cast<float>(Cfg.EnemyMaxHP * Mult);

	// Evidence (acceptance D): effective scaled values traced to the DataTable factors.
	UE_LOG(LogTemp, Display,
		TEXT("[FloorConfig] floor=%d mult=%.2f effPerRoom=%d effHP=%.0f (base perRoom=%d HP=%.0f scaling=%.2f from DataTable)"),
		FloorIndex, Mult, EffPerRoom, EffHP,
		Cfg.EnemiesPerRoom, Cfg.EnemyMaxHP, Cfg.PerFloorScaling);

	Spawner->RegenerateFloor(FloorSeed, EffPerRoom, EffHP);

	// Evidence (acceptance E): HP persistence is visible here - the pawn survives the
	// in-place transition, so playerHP logged at floor start carries damage from the
	// previous floor.
	float PawnHP = -1.0f, PawnMax = -1.0f;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (const UHealthComponent* HP = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		PawnHP = HP->GetHP();
		PawnMax = HP->GetMaxHP();
	}
	UE_LOG(LogTemp, Display,
		TEXT("[FloorStarted] floor=%d runSeed=%llu floorSeed=%llu playerHP=%.0f/%.0f"),
		FloorIndex,
		static_cast<unsigned long long>(RunSeed),
		static_cast<unsigned long long>(FloorSeed),
		PawnHP, PawnMax);
}

void UUegameFloorManager::RequestDescend(bool bForce)
{
	if (!bRunActive)
	{
		UE_LOG(LogTemp, Error, TEXT("[FloorManager] no active run (use Dungeon.StartRun <seed>)"));
		return;
	}
	if (PendingTransition != EPendingTransition::None)
	{
		return;   // duplicate trigger, or the player already died this tick
	}

	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	ADungeonSpawner* Spawner = FindSpawner();

	if (!bForce && Cfg.bRequireFloorClearToDescend && Spawner && !Spawner->AreAllRoomsCleared())
	{
		UE_LOG(LogTemp, Display, TEXT("[Stairs] descend BLOCKED by clear-gate policy (enemies alive)"));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[FloorCompleted] floor=%d"), FloorIndex);

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	PendingTransition = EPendingTransition::Descend;
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this]() { ExecutePendingTransition(); }));
}

void UUegameFloorManager::NotifyRunFailed()
{
	if (!bRunActive || PendingTransition == EPendingTransition::Fail)
	{
		return;   // no run, or this death is already being handled
	}

	// A death inside a queued descend's one-tick window upgrades that transition: the run
	// must fail, never continue onto the next floor with a dead pawn. StartFloor() does not
	// revive, so letting the descend win would strand the run at HP=0 with OnDeath spent.
	const bool bUpgradedPendingDescend = (PendingTransition == EPendingTransition::Descend);

	// Evidence (acceptance E): lose path.
	UE_LOG(LogTemp, Display, TEXT("[RunFailed] floor=%d runSeed=%llu%s"),
		FloorIndex, static_cast<unsigned long long>(RunSeed),
		bUpgradedPendingDescend ? TEXT(" (death overrides pending descend)") : TEXT(""));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Red,
			FString::Printf(TEXT("RUN FAILED on floor %d. Restarting with a fresh seed..."), FloorIndex));
	}

	if (bUpgradedPendingDescend)
	{
		PendingTransition = EPendingTransition::Fail;   // reuse the tick RequestDescend queued
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	PendingTransition = EPendingTransition::Fail;
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this]() { ExecutePendingTransition(); }));
}

void UUegameFloorManager::ExecutePendingTransition()
{
	const EPendingTransition Kind = PendingTransition;
	PendingTransition = EPendingTransition::None;
	if (!bRunActive || Kind == EPendingTransition::None)
	{
		return;
	}

	if (Kind == EPendingTransition::Fail)
	{
		RestartRun(TEXT("failed"));
		return;
	}

	// Descend. The win check runs at fire time, not request time, so a death that upgraded
	// the pending transition can never leave a stray [RunWon] behind it.
	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	if (FloorIndex >= Cfg.MaxFloors)
	{
		// Evidence (acceptance E): win path.
		UE_LOG(LogTemp, Display, TEXT("[RunWon] runSeed=%llu floorsCleared=%d"),
			static_cast<unsigned long long>(RunSeed), FloorIndex);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Green,
				FString::Printf(TEXT("RUN WON - %d floors! Restarting with a fresh seed..."), FloorIndex));
		}
		RestartRun(TEXT("won"));
	}
	else
	{
		StartFloor(FloorIndex + 1);
	}
}

void UUegameFloorManager::RestartRun(const TCHAR* Reason)
{
	const uint64 OldSeed = RunSeed;
	RunSeed = m2::nextRunSeed(RunSeed);   // deterministic fresh-seed chain (contract F)
	UE_LOG(LogTemp, Display, TEXT("[RunRestart] reason=%s oldRunSeed=%llu newRunSeed=%llu"),
		Reason,
		static_cast<unsigned long long>(OldSeed),
		static_cast<unsigned long long>(RunSeed));
	HealPlayerFull();
	StartFloor(1);
}
