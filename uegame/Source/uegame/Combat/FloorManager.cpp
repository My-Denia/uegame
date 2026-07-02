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
		Deferred->FinishSpawning(FTransform(FVector::ZeroVector));
		Deferred->bSpawnEnemies = true;
		Deferred->bTeleportPlayerToStart = true;
		Spawner = Deferred;
	}

	RunSeed = InRunSeed;
	bRunActive = true;
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

	bTransitionPending = false;
}

void UUegameFloorManager::RequestDescend(bool bForce)
{
	if (!bRunActive)
	{
		UE_LOG(LogTemp, Error, TEXT("[FloorManager] no active run (use Dungeon.StartRun <seed>)"));
		return;
	}
	if (bTransitionPending)
	{
		return;
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
	bTransitionPending = true;

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
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]() { RestartRun(TEXT("won")); }));
	}
	else
	{
		const int32 Next = FloorIndex + 1;
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this, Next]() { StartFloor(Next); }));
	}
}

void UUegameFloorManager::NotifyRunFailed()
{
	if (!bRunActive || bTransitionPending)
	{
		return;
	}
	// Evidence (acceptance E): lose path.
	UE_LOG(LogTemp, Display, TEXT("[RunFailed] floor=%d runSeed=%llu"),
		FloorIndex, static_cast<unsigned long long>(RunSeed));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Red,
			FString::Printf(TEXT("RUN FAILED on floor %d. Restarting with a fresh seed..."), FloorIndex));
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	bTransitionPending = true;
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this]() { RestartRun(TEXT("failed")); }));
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
