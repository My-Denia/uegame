// FloorManager.cpp - see header.

#include "FloorManager.h"

#include "CombatConfig.h"
#include "DungeonStairs.h"
#include "HealthComponent.h"
#include "LoadoutComponent.h"
#include "../Presentation/PresentationFeedbackComponent.h"
#include "../DungeonSpawner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"

// Engine-agnostic seed pipeline (repo root include path; .cpp-only include).
#include "m2_adapter.hpp"
#include "m8_room_contract.hpp"
#include "m8_room_flow.hpp"

#include <vector>

namespace
{
	// Demo/portfolio seed: the cross-compiler pinned forensic seed. Used only when
	// bUseFixedFirstSeed is set (or as the explicit reproducibility target); the DEFAULT
	// first-run seed is now entropy (ResolveFirstRunSeed). Restarts chain deterministically
	// via m2::nextRunSeed regardless of how the first seed was picked.
	constexpr uint64 kDemoRunSeed = 7;

	m8room::Config GetRoomFlowConfig(const FCombatConfigRow& Cfg)
	{
		return {{
			Cfg.QuietRoomClearHealFraction,
			Cfg.StandardRoomClearHealFraction,
			Cfg.SkirmishRoomClearHealFraction,
			Cfg.StrongholdRoomClearHealFraction
		}, Cfg.RewardRecoveryFloorFraction,
			Cfg.RequiredCombatRoomsBase, Cfg.RequiredCombatRoomsPerFloor};
	}

	m8contract::Choice ToCoreContractChoice(EUegameRoomContractChoice Choice)
	{
		switch (Choice)
		{
		case EUegameRoomContractChoice::Pending: return m8contract::Choice::Pending;
		case EUegameRoomContractChoice::Secure: return m8contract::Choice::Secure;
		case EUegameRoomContractChoice::Challenge: return m8contract::Choice::Challenge;
		case EUegameRoomContractChoice::Unavailable:
		default: return m8contract::Choice::Unavailable;
		}
	}

	const TCHAR* InputOwnerName(m8contract::InputOwner Owner)
	{
		switch (Owner)
		{
		case m8contract::InputOwner::PauseMenu: return TEXT("PauseMenu");
		case m8contract::InputOwner::Contract: return TEXT("Contract");
		case m8contract::InputOwner::Reward: return TEXT("Reward");
		case m8contract::InputOwner::None:
		default: return TEXT("None");
		}
	}

	std::vector<std::int64_t> ToContractIds(const TArray<int64>& Ids)
	{
		std::vector<std::int64_t> Out;
		Out.reserve(static_cast<size_t>(Ids.Num()));
		for (const int64 Id : Ids)
		{
			Out.push_back(static_cast<std::int64_t>(Id));
		}
		return Out;
	}
}

void UUegameFloorManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Portfolio/demo mode: pin the first run to seed 7 without any console command.
	// Default false => the first run is entropy-seeded (a fresh install gets a random dungeon).
	if (GConfig)
	{
		GConfig->GetBool(TEXT("/Script/uegame.UegameFloorManager"),
			TEXT("bUseFixedFirstSeed"), bUseFixedFirstSeed, GGameIni);
	}
	ActorsInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(
		this, &UUegameFloorManager::OnWorldActorsInitialized);
}

uint64 UUegameFloorManager::ResolveFirstRunSeed()
{
	if (ExplicitFirstSeed.IsSet())
	{
		const uint64 S = ExplicitFirstSeed.GetValue();
		// One-shot: consume the override so a LATER fresh entry in the same GameInstance (e.g. a
		// map reload/travel during PIE) falls back to bUseFixedFirstSeed/entropy instead of staying
		// silently pinned to this seed - which would invalidate a subsequent randomness check.
		ExplicitFirstSeed.Reset();
		UE_LOG(LogTemp, Display, TEXT("[RunSeed] source=explicit value=%llu"),
			static_cast<unsigned long long>(S));
		return S;
	}
	if (bUseFixedFirstSeed)
	{
		UE_LOG(LogTemp, Display, TEXT("[RunSeed] source=fixed-demo value=%llu"),
			static_cast<unsigned long long>(kDemoRunSeed));
		return kDemoRunSeed;
	}

	// === THE single entropy point (contract F, amended). This is the only place in gameplay
	// that reads a clock to pick a seed. Two independent clock sources XOR'd then splitmix64-
	// mixed so low-entropy wall-clock bits spread across all 64 bits. Everything downstream
	// (deriveFloorSeed per floor, nextRunSeed per restart) is a pure integer function of this
	// value, so the whole run stays reproducible once the seed is known/logged. ===
	const uint64 Entropy =
		static_cast<uint64>(FDateTime::Now().GetTicks()) ^
		(static_cast<uint64>(FPlatformTime::Cycles64()) * 0x9E3779B97F4A7C15ULL);
	const uint64 Seed = m2::mix64(Entropy);
	UE_LOG(LogTemp, Display, TEXT("[RunSeed] source=entropy value=%llu"),
		static_cast<unsigned long long>(Seed));
	return Seed;
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
	// Bootstrap is the shipping-safe fallback for spawner-less maps; its seed comes from the
	// single resolver (entropy by default), NOT a hard-coded 7.
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bRunActive && !FindSpawner())
			{
				const uint64 Seed = ResolveFirstRunSeed();
				UE_LOG(LogTemp, Display,
					TEXT("[Dungeon] AutoStartRun: runSeed=%llu (bootstrap; no spawner in level)"),
					static_cast<unsigned long long>(Seed));
				StartRun(Seed);
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

double UUegameFloorManager::GetRunElapsedSeconds() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return World && bRunActive
		? FMath::Max(0.0, static_cast<double>(World->GetTimeSeconds()) - RunStartedAtGameSeconds)
		: 0.0;
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

ADungeonSpawner* UUegameFloorManager::FindUniqueFreshSpawnerForCurrentFloor() const
{
#if !UE_BUILD_SHIPPING
	if (FreshSpawnerFaultModeForTests != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FreshSpawnerAuthorityTest] forced=%s"),
			FreshSpawnerFaultModeForTests == 1 ? TEXT("missing") : TEXT("ambiguous"));
		return nullptr;
	}
#endif
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	ADungeonSpawner* Match = nullptr;
	for (TActorIterator<ADungeonSpawner> It(World); It; ++It)
	{
		ADungeonSpawner* Candidate = *It;
		if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed()
			|| Candidate->GetWorld() != World || !Candidate->HasEncounterAssignment()
			|| Candidate->GetEncounterRunSeed() != RunSeed
			|| Candidate->GetEncounterFloorIndex() != FloorIndex)
		{
			continue;
		}
		if (Match)
		{
			return nullptr;
		}
		Match = Candidate;
	}
	return Match;
}

bool UUegameFloorManager::IsUniqueFreshSource(const ADungeonSpawner* SourceSpawner) const
{
	return SourceSpawner && FindUniqueFreshSpawnerForCurrentFloor() == SourceSpawner;
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
	ResolveTokens = 0;
	TotalClearedCombatRooms = 0;
	RunStartedAtGameSeconds = static_cast<double>(World->GetTimeSeconds());
	ClearRoomContractState();
	RuntimeLifecycle.state = m8authority::RunState::Playing;
	RefreshWorldPause();
	PendingTransition = EPendingTransition::None;   // a manual (re)start cancels queued intent
	UE_LOG(LogTemp, Display, TEXT("[RunStarted] runSeed=%llu maxFloors=%d resolve=0"),
		static_cast<unsigned long long>(RunSeed), FUegameCombatConfig::Get().MaxFloors);
	ResetLoadoutForNewRun();   // M5: a run is a fresh build (clears any picks from a prior run this session)
	ResetPresentationForNewRun();
	StartFloor(1);
}

#if !UE_BUILD_SHIPPING
void UUegameFloorManager::StartFinaleForTests(
	uint64 InRunSeed, int32 InResolveTokens, int32 FailureMode)
{
	StartRun(InRunSeed);
	if (!bRunActive || RuntimeLifecycle.state != m8authority::RunState::Playing)
	{
		return;
	}
	ResolveTokens = FMath::Clamp(InResolveTokens, 0, 2);
	if (ADungeonSpawner* Spawner = FindUniqueFreshSpawnerForCurrentFloor())
	{
		Spawner->SetFinaleInitFailureModeForTests(FailureMode);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[FinaleTestStart] runSeed=%llu resolve=%d failureMode=%d"),
		static_cast<unsigned long long>(InRunSeed), ResolveTokens, FailureMode);
	StartFloor(FUegameCombatConfig::Get().MaxFloors);
}
#endif

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
	ClearedCombatRooms = 0;
	RequiredCombatRooms = 0;
	ActualCombatRooms = 0;
	ClearedRoomIndices.Reset();
	bFloorObjectiveComplete = false;
	bFloorExitThreatsWithdrawn = false;
	bExitSafetyBlocked = false;
	AbandonedCombatRooms = 0;
	ExitNeutralizedEnemies = 0;

	const uint64 FloorSeed = m2::deriveFloorSeed(RunSeed, FloorIndex);
	const double Mult = m2::floorMultiplier(FloorIndex, Cfg.PerFloorScaling);
	const int32 EffPerRoom = m2::scaledEnemiesPerRoom(Cfg.EnemiesPerRoom, FloorIndex, Cfg.PerFloorScaling);
	const float EffHP = static_cast<float>(Cfg.EnemyMaxHP * Mult);

	// Evidence (acceptance D): effective scaled values traced to the DataTable factors.
	UE_LOG(LogTemp, Display,
		TEXT("[FloorConfig] floor=%d mult=%.2f effPerRoom=%d effHP=%.0f (base perRoom=%d HP=%.0f scaling=%.2f from DataTable)"),
		FloorIndex, Mult, EffPerRoom, EffHP,
		Cfg.EnemiesPerRoom, Cfg.EnemyMaxHP, Cfg.PerFloorScaling);

	const FCombatConfigRow* FinaleConfig = FloorIndex >= Cfg.MaxFloors ? &Cfg : nullptr;
	if (!Spawner->RegenerateFloor(FloorSeed, EffPerRoom, EffHP, FinaleConfig, ResolveTokens))
	{
		NotifyFinaleInitFailed();
		return;
	}
	ActualCombatRooms = FMath::Max(0, Spawner->GetActualEnemyRoomCount());
	RequiredCombatRooms = m8room::required_combat_rooms(
		FloorIndex, Cfg.MaxFloors, ActualCombatRooms, GetRoomFlowConfig(Cfg));
	UE_LOG(LogTemp, Display,
		TEXT("[FloorObjective] floor=%d cleared=0 required=%d actual=%d completed=false exitSafe=false"),
		FloorIndex, RequiredCombatRooms, ActualCombatRooms);
	InitializeRoomContract(Spawner);

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
		TEXT("[FloorStarted] floor=%d runSeed=%llu floorSeed=%llu playerHP=%.0f/%.0f resolve=%d finale=%d"),
		FloorIndex,
		static_cast<unsigned long long>(RunSeed),
		static_cast<unsigned long long>(FloorSeed),
		PawnHP, PawnMax, ResolveTokens, static_cast<int32>(Spawner->GetFinaleInitState()));
	if (FloorIndex > 1)
	{
		if (UPresentationFeedbackComponent* Presentation = FindPlayerPresentation())
		{
			Presentation->EmitFloorStarted(FloorIndex, FinaleConfig != nullptr);
		}
	}

	if (RequiredCombatRooms == 0)
	{
		NotifyFloorCleared();
	}
}

void UUegameFloorManager::RequestDescend(bool bForce)
{
	if (!bRunActive)
	{
		UE_LOG(LogTemp, Error, TEXT("[FloorManager] no active run (use Dungeon.StartRun <seed>)"));
		return;
	}
	if (RuntimeLifecycle.state != m8authority::RunState::Playing)
	{
		UE_LOG(LogTemp, Display, TEXT("[Stairs] descend BLOCKED by run-state=%d"),
			static_cast<int32>(RuntimeLifecycle.state));
		return;
	}
	if (PendingTransition != EPendingTransition::None)
	{
		return;   // duplicate trigger, or the player already died this tick
	}
	// A floor cannot be replaced while its hard-paused room contract is unresolved. This
	// applies even to Development-only forced descent so test verbs cannot orphan Pending.
	if (IsRoomContractPending())
	{
		UE_LOG(LogTemp, Display, TEXT("[Stairs] descend BLOCKED by room-contract-pending"));
		return;
	}

	// A prior withdrawal failure is recoverable. Once the ordinary all-clear condition is
	// true, retry the same atomic completion path before deciding whether the exit is safe.
	if (!bForce && !bFloorObjectiveComplete
		&& (bExitSafetyBlocked || CanCompleteCurrentFloor()))
	{
		NotifyFloorCleared();
	}

	// The pure authority sees the exact runtime facts. Reward-pending remains part of the same
	// decision, so no caller can accidentally gate objective safety and reward in different orders.
	if (!bForce)
	{
		const ULoadoutComponent* LC = FindPlayerLoadout();
		m8authority::ExitInput Exit;
		Exit.objective_progress = bFloorObjectiveComplete ? 1 : 0;
		Exit.objective_threshold = 1;
		Exit.withdrawal = bFloorExitThreatsWithdrawn
			? m8authority::WithdrawalState::Succeeded
			: (bExitSafetyBlocked ? m8authority::WithdrawalState::Failed
				: m8authority::WithdrawalState::Pending);
		Exit.reward = LC && LC->IsRewardPending()
			? m8authority::RewardState::Pending
			: m8authority::RewardState::Resolved;
		const m8authority::ExitDecision Decision = m8authority::decide_exit(Exit);
		if (!Decision.descend_allowed)
		{
			const TCHAR* Reason = !Decision.objective_complete
				? TEXT("floor objective/exit safety") : TEXT("reward-pending (press 1/2/3)");
			UE_LOG(LogTemp, Display, TEXT("[Stairs] descend BLOCKED by %s"), Reason);
			return;
		}
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
	if (!bRunActive
		|| RuntimeLifecycle.state == m8authority::RunState::Failed
		|| RuntimeLifecycle.state == m8authority::RunState::Won
		|| RuntimeLifecycle.state == m8authority::RunState::Error)
	{
		return;
	}

	// A death inside a queued descend's one-tick window upgrades that transition: the run
	// must fail, never continue onto the next floor with a dead pawn. StartFloor() does not
	// revive, so letting the descend win would strand the run at HP=0 with OnDeath spent.
	const bool bUpgradedPendingDescend = (PendingTransition == EPendingTransition::Descend);
	PendingTransition = EPendingTransition::None;

	// Evidence (acceptance E): lose path.
	UE_LOG(LogTemp, Display, TEXT("[RunFailed] floor=%d runSeed=%llu%s"),
		FloorIndex, static_cast<unsigned long long>(RunSeed),
		bUpgradedPendingDescend ? TEXT(" (death overrides pending descend)") : TEXT(""));
	EnterTerminalState(m8authority::RunEvent::Fail, TEXT("RunFailed"));
}

void UUegameFloorManager::ExecutePendingTransition()
{
	const EPendingTransition Kind = PendingTransition;
	PendingTransition = EPendingTransition::None;
	if (!bRunActive || Kind == EPendingTransition::None)
	{
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
		EnterTerminalState(m8authority::RunEvent::Win, TEXT("RunWon"));
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
	// M5: reset the build to base BEFORE the heal, so MaxHP is back to base(140) when Revive refills current
	// HP (never leaves the new run at a prior run's boosted max, never refills-then-tops).
	ResetLoadoutForNewRun();
	ResolveTokens = 0;
	HealPlayerFull();
	m8authority::apply_event(RuntimeLifecycle, m8authority::RunEvent::AckRestart);
	ResetPresentationForNewRun();
	StartFloor(1);
}

void UUegameFloorManager::ApplyWorldPause(bool bPaused) const
{
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		UGameplayStatics::SetGamePaused(World, bPaused);
	}
}

void UUegameFloorManager::RefreshWorldPause() const
{
	const bool bLifecyclePaused = RuntimeLifecycle.state == m8authority::RunState::Paused
		|| RuntimeLifecycle.state == m8authority::RunState::Won
		|| RuntimeLifecycle.state == m8authority::RunState::Failed
		|| RuntimeLifecycle.state == m8authority::RunState::Error;
	ApplyWorldPause(IsRoomContractPending() || bLifecyclePaused);
}

void UUegameFloorManager::ClearRoomContractState()
{
	RoomContractChoice = EUegameRoomContractChoice::Unavailable;
	SecureContractRoom = INDEX_NONE;
	ChallengeContractRoom = INDEX_NONE;
	SelectedContractRoom = INDEX_NONE;
	bChallengeContractDisabled = false;
	RoomContractWarning = EUegameRoomContractWarning::None;
	RoomContractCommitCount = 0;
	bRoomContractSelectedRoomCleared = false;
	bRoomContractAwarded = false;
}

void UUegameFloorManager::EnterTerminalState(m8authority::RunEvent Event, const TCHAR* LogAnchor)
{
	const m8authority::LifecycleDecision Decision = m8authority::apply_event(RuntimeLifecycle, Event);
	if (!Decision.state_changed)
	{
		return;
	}
	ClearRoomContractState();
	RefreshWorldPause();
	UE_LOG(LogTemp, Display, TEXT("[RunState] source=%s state=%d manualActionRequired=true"),
		LogAnchor, static_cast<int32>(RuntimeLifecycle.state));
	if (UPresentationFeedbackComponent* Presentation = FindPlayerPresentation())
	{
		if (Event == m8authority::RunEvent::Win)
		{
			Presentation->EmitRunWon();
		}
		else if (Event == m8authority::RunEvent::Fail)
		{
			Presentation->EmitRunFailed();
		}
	}
}

void UUegameFloorManager::NotifyFinaleInitFailed()
{
	if (!bRunActive || RuntimeLifecycle.state == m8authority::RunState::Error)
	{
		return;
	}
	PendingTransition = EPendingTransition::None;
	UE_LOG(LogTemp, Error,
		TEXT("[FinaleInitFailed] floor=%d runSeed=%llu action=manual-restart-or-quit"),
		FloorIndex, static_cast<unsigned long long>(RunSeed));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Red,
			TEXT("FINAL CHALLENGE COULD NOT START. Press R to restart or Q to quit."));
	}
	EnterTerminalState(m8authority::RunEvent::FinaleInitFail, TEXT("FinaleInitFailed"));
}

void UUegameFloorManager::TogglePause()
{
	if (!bRunActive)
	{
		return;
	}
	if (IsRoomContractPending())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RunPause] ignored=room-contract-pending choose=1-or-2"));
		return;
	}
	const m8authority::LifecycleDecision Decision =
		m8authority::apply_event(RuntimeLifecycle, m8authority::RunEvent::TogglePause);
	if (!Decision.state_changed)
	{
		return;
	}
	const bool bPaused = RuntimeLifecycle.state == m8authority::RunState::Paused;
	RefreshWorldPause();
	UE_LOG(LogTemp, Display, TEXT("[RunPause] paused=%s"), bPaused ? TEXT("true") : TEXT("false"));
}

void UUegameFloorManager::RequestManualRestart()
{
	if (!bRunActive)
	{
		return;
	}
	const m8authority::LifecycleDecision Decision =
		m8authority::apply_event(RuntimeLifecycle, m8authority::RunEvent::Restart);
	if (!Decision.restart_requested)
	{
		return;
	}
	PendingTransition = EPendingTransition::None;
	ClearRoomContractState();
	RefreshWorldPause();
	RestartRun(TEXT("manual"));
}

void UUegameFloorManager::RequestQuit()
{
	const m8authority::LifecycleDecision Decision =
		m8authority::apply_event(RuntimeLifecycle, m8authority::RunEvent::Quit);
	if (!Decision.quit_requested)
	{
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[RunQuitRequested] floor=%d runSeed=%llu"),
		FloorIndex, static_cast<unsigned long long>(RunSeed));
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	UKismetSystemLibrary::QuitGame(World, PC, EQuitPreference::Quit, false);
}

ULoadoutComponent* UUegameFloorManager::FindPlayerLoadout() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	return Pawn ? Pawn->FindComponentByClass<ULoadoutComponent>() : nullptr;
}

UPresentationFeedbackComponent* UUegameFloorManager::FindPlayerPresentation() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	return Pawn ? Pawn->FindComponentByClass<UPresentationFeedbackComponent>() : nullptr;
}

void UUegameFloorManager::ResetLoadoutForNewRun() const
{
	if (ULoadoutComponent* LC = FindPlayerLoadout())
	{
		LC->ResetForNewRun();
	}
}

void UUegameFloorManager::ResetPresentationForNewRun() const
{
	if (UPresentationFeedbackComponent* Presentation = FindPlayerPresentation())
	{
		Presentation->ResetForNewRun();
	}
}

void UUegameFloorManager::RepokeStairsForDescend() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	// Mirror the clear-time re-poke (DungeonSpawner::NotifyEnemyDead / 7efdff0): OnFloorCleared() is a no-op
	// unless the player pawn is currently inside the stairs trigger, so this only descends a player who is
	// already waiting on the pad; otherwise the normal overlap fires when they walk on.
	for (TActorIterator<ADungeonStairs> It(World); It; ++It)
	{
		It->OnFloorCleared();
	}
}

void UUegameFloorManager::InitializeRoomContract(ADungeonSpawner* Spawner)
{
	ClearRoomContractState();
	RefreshWorldPause();

	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	ADungeonSpawner* FreshSpawner = FindUniqueFreshSpawnerForCurrentFloor();
	if (!Spawner || FreshSpawner != Spawner || FloorIndex >= Cfg.MaxFloors)
	{
		UE_LOG(LogTemp, Display, TEXT("[RoomContract] floor=%d state=Unavailable reason=%s paused=false"),
			FloorIndex, FloorIndex >= Cfg.MaxFloors ? TEXT("final-floor") : TEXT("no-unique-fresh-spawner"));
		return;
	}

	std::vector<m8contract::RoomCandidate> Candidates;
	Candidates.reserve(static_cast<size_t>(Spawner->GetRoomCount()));
	const FVector Start = Spawner->GetRoomCenterWorld(Spawner->GetStartRoomIndex());
	const TArray<int32>& Roles = Spawner->GetCachedRoomRoles();
	for (int32 RoomIndex = 0; RoomIndex < Spawner->GetRoomCount(); ++RoomIndex)
	{
		if (!Roles.IsValidIndex(RoomIndex))
		{
			continue;
		}
		Candidates.push_back({
			RoomIndex,
			static_cast<m8contract::Role>(Roles[RoomIndex]),
			static_cast<double>(FVector::DistSquared2D(Start, Spawner->GetRoomCenterWorld(RoomIndex))),
			Spawner->GetInitialInRoom(RoomIndex),
			RoomIndex == Spawner->GetStartRoomIndex()
		});
	}
	const m8contract::Selection Selection = m8contract::select_rooms(Candidates);
	if (!Selection.available)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomContract] floor=%d state=Unavailable reason=zero-candidates paused=false"), FloorIndex);
		return;
	}

	SecureContractRoom = Selection.secure_room;
	ChallengeContractRoom = Selection.challenge_room;
	RoomContractChoice = EUegameRoomContractChoice::Pending;
	RefreshWorldPause();
	UE_LOG(LogTemp, Display,
		TEXT("[RoomContract] floor=%d state=Pending secureRoom=%d challengeRoom=%d paused=true"),
		FloorIndex, SecureContractRoom, ChallengeContractRoom);
}

void UUegameFloorManager::ApplyContractHeal(float Fraction, const TCHAR* Reason, int32 RoomIndex) const
{
	ApplyRecoveryFraction(Fraction, Reason, RoomIndex);
}

void UUegameFloorManager::ApplyRecoveryFraction(float Fraction, const TCHAR* Reason, int32 RoomIndex) const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UHealthComponent* HP = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!HP || HP->IsDead())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RecoveryTxn] floor=%d room=%d reason=%s applied=false deadOrMissing=true"),
			FloorIndex, RoomIndex, Reason);
		return;
	}
	const float Before = HP->GetHP();
	const float MaxHP = HP->GetMaxHP();
	const float Nominal = MaxHP * FMath::Clamp(Fraction, 0.0f, 1.0f);
	const float Applied = HP->Heal(Nominal);
	const float After = HP->GetHP();
	const bool bClamped = Applied + KINDA_SMALL_NUMBER < Nominal;
	UE_LOG(LogTemp, Display,
		TEXT("[RecoveryTxn] floor=%d room=%d reason=%s before=%.1f max=%.1f nominal=%.1f applied=%.1f after=%.1f clamped=%s"),
		FloorIndex, RoomIndex, Reason, Before, MaxHP, Nominal, Applied, After,
		bClamped ? TEXT("true") : TEXT("false"));
}

void UUegameFloorManager::ApplyPostRewardRecovery() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UHealthComponent* HP = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!HP || HP->IsDead())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RecoveryTxn] floor=%d room=-1 reason=reward-floor applied=false deadOrMissing=true"),
			FloorIndex);
		return;
	}
	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	const m8room::Recovery Planned = m8room::apply_reward_floor(
		HP->GetHP(), HP->GetMaxHP(), GetRoomFlowConfig(Cfg));
	const float Before = HP->GetHP();
	const float MaxHP = HP->GetMaxHP();
	const float Applied = HP->Heal(static_cast<float>(Planned.restored));
	const float After = HP->GetHP();
	const bool bClamped = Applied + KINDA_SMALL_NUMBER < static_cast<float>(Planned.restored);
	UE_LOG(LogTemp, Display,
		TEXT("[RecoveryTxn] floor=%d room=-1 reason=reward-floor before=%.1f max=%.1f nominal=%.1f applied=%.1f after=%.1f clamped=%s"),
		FloorIndex, Before, MaxHP, static_cast<float>(Planned.restored), Applied, After,
		bClamped ? TEXT("true") : TEXT("false"));
}

void UUegameFloorManager::ResolveRoomContractUnavailable(const TCHAR* Reason)
{
	RoomContractChoice = EUegameRoomContractChoice::Unavailable;
	SelectedContractRoom = INDEX_NONE;
	bRoomContractSelectedRoomCleared = false;
	bRoomContractAwarded = false;
	RoomContractWarning = EUegameRoomContractWarning::Unavailable;
	RefreshWorldPause();
	UE_LOG(LogTemp, Error,
		TEXT("[RoomContract] floor=%d state=Unavailable reason=%s paused=false pending=false commitCount=%d"),
		FloorIndex, Reason, RoomContractCommitCount);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Red,
			TEXT("ROOM CONTRACT UNAVAILABLE - continuing without a contract."));
	}
}

bool UUegameFloorManager::CommitSecureContract(
	ADungeonSpawner* Spawner, bool bFallback, const TCHAR* FailureReason)
{
	if (!IsRoomContractPending() || RoomContractCommitCount != 0 || !Spawner)
	{
		return false;
	}
	const m8contract::TransactionTransition Policy =
		m8contract::choose_secure(m8contract::begin_transaction(true));
	if (Policy.action != m8contract::TransactionAction::CommitSecure
		|| Policy.state.commit_count != 1 || Policy.state.world_paused)
	{
		ResolveRoomContractUnavailable(TEXT("secure-policy-rejected"));
		return false;
	}
	int32 Room = SecureContractRoom;
	if (Room < 0 || Spawner->GetAliveInRoom(Room) <= 0)
	{
		Room = ChallengeContractRoom;
	}
	if (Room < 0 || Spawner->GetAliveInRoom(Room) <= 0)
	{
		Room = INDEX_NONE;
		for (int32 Candidate = 0; Candidate < Spawner->GetRoomCount(); ++Candidate)
		{
			if (Spawner->GetAliveInRoom(Candidate) > 0)
			{
				Room = Candidate;
				break;
			}
		}
	}
	if (Room < 0)
	{
		ResolveRoomContractUnavailable(TEXT("secure-fallback-has-no-live-room"));
		return false;
	}

	SelectedContractRoom = Room;
	RoomContractChoice = EUegameRoomContractChoice::Secure;
	bRoomContractSelectedRoomCleared = false;
	bRoomContractAwarded = false;
	bChallengeContractDisabled = bFallback;
	RoomContractWarning = bFallback
		? EUegameRoomContractWarning::SecureFallback
		: EUegameRoomContractWarning::None;
	++RoomContractCommitCount;
	ApplyContractHeal(0.25f, bFallback ? TEXT("challenge-fallback") : TEXT("secure-commit"), Room);
	RefreshWorldPause();
	UE_LOG(LogTemp, Display,
		TEXT("[RoomContract] floor=%d state=Secure selectedRoom=%d fallback=%s failure=%s paused=false pending=false commitCount=%d"),
		FloorIndex, Room, bFallback ? TEXT("true") : TEXT("false"), FailureReason,
		RoomContractCommitCount);
	if (bFallback && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Yellow,
			TEXT("CHALLENGE UNAVAILABLE - SECURE selected automatically."));
	}
	return true;
}

bool UUegameFloorManager::TryCommitRoomContract(int32 Index)
{
	if (!IsRoomContractPending() || (Index != 0 && Index != 1))
	{
		return false;
	}
	ADungeonSpawner* Spawner = FindUniqueFreshSpawnerForCurrentFloor();
	if (Index == 0)
	{
		if (!Spawner)
		{
			ResolveRoomContractUnavailable(TEXT("secure-no-unique-fresh-spawner"));
			return false;
		}
		return CommitSecureContract(Spawner, false, TEXT("none"));
	}

	bChallengeContractDisabled = false;
	if (!Spawner)
	{
		bChallengeContractDisabled = true;
		ResolveRoomContractUnavailable(TEXT("challenge-no-unique-fresh-spawner"));
		return false;
	}
	if (ChallengeContractRoom < 0 || Spawner->GetAliveInRoom(ChallengeContractRoom) <= 0)
	{
		bChallengeContractDisabled = true;
		return CommitSecureContract(Spawner, true, TEXT("stale-challenge-target"));
	}

	const FChallengeContractTransactionResult Txn =
		Spawner->ApplyChallengeContractTransactional(ChallengeContractRoom);
	const m8contract::ContractTransaction Pending = m8contract::begin_transaction(true);
	const m8contract::TransactionTransition Preflight = m8contract::preflight_challenge(
		Pending, ToContractIds(Txn.ExpectedIds), ToContractIds(Txn.EligibleIds),
		Txn.bTargetCurrent);
	if (Preflight.action == m8contract::TransactionAction::CommitSecure)
	{
		bChallengeContractDisabled = true;
		return CommitSecureContract(Spawner, true, TEXT("challenge-preflight-failed"));
	}
	if (Preflight.action != m8contract::TransactionAction::ApplyChallenge)
	{
		ResolveRoomContractUnavailable(TEXT("challenge-policy-preflight-error"));
		return false;
	}

	const m8contract::TransactionTransition Final = m8contract::finalize_challenge(
		Preflight.state, ToContractIds(Txn.AppliedIds), ToContractIds(Txn.RolledBackIds));
	if (Txn.bCommitted && Final.action == m8contract::TransactionAction::CommitChallenge)
	{
		SelectedContractRoom = ChallengeContractRoom;
		RoomContractChoice = EUegameRoomContractChoice::Challenge;
		bRoomContractSelectedRoomCleared = false;
		bRoomContractAwarded = false;
		++RoomContractCommitCount;
		RefreshWorldPause();
		UE_LOG(LogTemp, Display,
			TEXT("[RoomContract] floor=%d state=Challenge selectedRoom=%d paused=false pending=false commitCount=%d"),
			FloorIndex, SelectedContractRoom, RoomContractCommitCount);
		return true;
	}

	bChallengeContractDisabled = true;
	if (!Txn.bRollbackComplete || Final.action == m8contract::TransactionAction::SignalError)
	{
		ResolveRoomContractUnavailable(TEXT("challenge-rollback-incomplete"));
		return false;
	}
	if (Final.action != m8contract::TransactionAction::CommitSecure)
	{
		ResolveRoomContractUnavailable(TEXT("challenge-policy-finalize-error"));
		return false;
	}
	return CommitSecureContract(Spawner, true, TEXT("partial-challenge-apply"));
}

bool UUegameFloorManager::CanCompleteCurrentFloor() const
{
	const bool bContractComplete = m8contract::can_complete_floor(
		bFloorObjectiveComplete, ClearedCombatRooms, RequiredCombatRooms,
		ToCoreContractChoice(RoomContractChoice), bRoomContractSelectedRoomCleared);
	if (!bContractComplete)
	{
		return false;
	}
	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	if (FloorIndex < Cfg.MaxFloors)
	{
		return true;
	}
	const ADungeonSpawner* Spawner = FindUniqueFreshSpawnerForCurrentFloor();
	return Spawner && Spawner->HasFinaleInitialized() && Spawner->IsWardenDefeated();
}

void UUegameFloorManager::NotifyRoomCleared(
	ADungeonSpawner* SourceSpawner, int32 RoomIndex, int32 CachedRole,
	int32 OldAlive, int32 NewAlive)
{
	const bool bRoomValid = SourceSpawner
		&& RoomIndex >= 0 && RoomIndex < SourceSpawner->GetRoomCount()
		&& SourceSpawner->GetInitialInRoom(RoomIndex) > 0
		&& SourceSpawner->GetAliveInRoom(RoomIndex) == NewAlive;
	const TArray<int32>* Roles = SourceSpawner ? &SourceSpawner->GetCachedRoomRoles() : nullptr;
	const bool bRoleValid = Roles && Roles->IsValidIndex(RoomIndex)
		&& (*Roles)[RoomIndex] == CachedRole && CachedRole >= 0 && CachedRole <= 3;
	const bool bFirstTrueClear = OldAlive > 0 && NewAlive == 0
		&& !ClearedRoomIndices.Contains(RoomIndex);
	if (!bRunActive || !IsUniqueFreshSource(SourceSpawner)
		|| !bRoomValid || !bRoleValid || !bFirstTrueClear)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomClearRejected] floor=%d room=%d role=%d old=%d new=%d sourceFresh=%s roomValid=%s roleValid=%s firstTrue=%s"),
			FloorIndex, RoomIndex, CachedRole, OldAlive, NewAlive,
			IsUniqueFreshSource(SourceSpawner) ? TEXT("true") : TEXT("false"),
			bRoomValid ? TEXT("true") : TEXT("false"),
			bRoleValid ? TEXT("true") : TEXT("false"),
			bFirstTrueClear ? TEXT("true") : TEXT("false"));
		return;
	}

	ClearedRoomIndices.Add(RoomIndex);
	ClearedCombatRooms = FMath::Min(ActualCombatRooms, ClearedCombatRooms + 1);
	++TotalClearedCombatRooms;
	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	const float RoleFraction = static_cast<float>(m8room::room_fraction(
		static_cast<m8room::Role>(CachedRole), GetRoomFlowConfig(Cfg)));
	ApplyRecoveryFraction(RoleFraction, TEXT("role-clear"), RoomIndex);

	if (IsRoomContractSelected() && RoomIndex == SelectedContractRoom
		&& !bRoomContractSelectedRoomCleared)
	{
		bRoomContractSelectedRoomCleared = true;
		if (IsRoomContractChallenge() && !bRoomContractAwarded)
		{
			ApplyRecoveryFraction(0.50f, TEXT("challenge-clear"), RoomIndex);
			const int32 OldResolve = ResolveTokens;
			ResolveTokens = FMath::Clamp(ResolveTokens + 1, 0, 2);
			bRoomContractAwarded = true;
			UE_LOG(LogTemp, Display,
				TEXT("[RoomContractAward] floor=%d room=%d choice=Challenge healFraction=0.50 awarded=true resolve=%d->%d"),
				FloorIndex, RoomIndex, OldResolve, ResolveTokens);
		}
		else
		{
			UE_LOG(LogTemp, Display,
				TEXT("[RoomContractResolved] floor=%d room=%d choice=Secure awarded=false"),
				FloorIndex, RoomIndex);
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[FloorObjective] floor=%d cleared=%d required=%d actual=%d contractRoom=%d contractResolved=%s completed=false"),
		FloorIndex, ClearedCombatRooms, RequiredCombatRooms, ActualCombatRooms,
		SelectedContractRoom, bRoomContractSelectedRoomCleared ? TEXT("true") : TEXT("false"));
	if (CanCompleteCurrentFloor())
	{
		NotifyFloorCleared();
	}
}

void UUegameFloorManager::NotifyFloorCleared()
{
	if (!bRunActive || RuntimeLifecycle.state != m8authority::RunState::Playing
		|| bFloorObjectiveComplete)
	{
		return;
	}
	if (!CanCompleteCurrentFloor())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[FloorObjective] floor=%d cleared=%d required=%d actual=%d contractRoom=%d contractResolved=%s transition=blocked-or-repeat"),
			FloorIndex, ClearedCombatRooms, RequiredCombatRooms, ActualCombatRooms,
			SelectedContractRoom, bRoomContractSelectedRoomCleared ? TEXT("true") : TEXT("false"));
		return;
	}

	ADungeonSpawner* Spawner = nullptr;
#if !UE_BUILD_SHIPPING
	const int32 ExitAuthorityFault = ExitFreshSpawnerFaultModeForTests;
	ExitFreshSpawnerFaultModeForTests = 0;
	if (ExitAuthorityFault != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ExitFreshSpawnerAuthorityTest] forced=%s oneShot=true"),
			ExitAuthorityFault == 1 ? TEXT("missing") : TEXT("ambiguous"));
	}
	else
#endif
	{
		Spawner = FindUniqueFreshSpawnerForCurrentFloor();
	}
	const uint64 AttemptRunSeed = RunSeed;
	const int32 AttemptFloor = FloorIndex;
	FFloorExitNeutralizationResult Withdrawal;
	if (Spawner)
	{
		Withdrawal = Spawner->DeactivateRemainingEnemiesForExit(FloorIndex);
	}
	const bool bProvenanceStillCurrent = bRunActive
		&& RunSeed == AttemptRunSeed && FloorIndex == AttemptFloor
		&& IsUniqueFreshSource(Spawner);

	m8authority::ExitInput Exit;
	Exit.objective_progress = 1;
	Exit.objective_threshold = 1;
	Exit.withdrawal = Withdrawal.bSuccess && bProvenanceStillCurrent
		? m8authority::WithdrawalState::Succeeded
		: m8authority::WithdrawalState::Failed;
	Exit.reward = m8authority::RewardState::Unavailable;
	const m8authority::ExitDecision Decision = m8authority::decide_exit(Exit);
	bExitSafetyBlocked = Decision.blocked_visible;
	if (!Decision.objective_complete)
	{
		bFloorObjectiveComplete = false;
		bFloorExitThreatsWithdrawn = false;
		UE_LOG(LogTemp, Error,
			TEXT("[FloorObjective] floor=%d cleared=%d required=%d complete=false exitSafe=false exitBlocked=true reward=false provenanceCurrent=%s collected=%d preflighted=%d neutralized=%d destroyQueued=%d remainingActive=%d retry=stairs"),
			FloorIndex, ClearedCombatRooms, RequiredCombatRooms,
			bProvenanceStillCurrent ? TEXT("true") : TEXT("false"),
			Withdrawal.Collected, Withdrawal.Preflighted, Withdrawal.Neutralized,
			Withdrawal.DestroyQueued, Withdrawal.RemainingActive);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Red,
				TEXT("EXIT BLOCKED - safety check failed. Try the stairs again."));
		}
		return;
	}

	bFloorExitThreatsWithdrawn = true;
	bFloorObjectiveComplete = true;
	bExitSafetyBlocked = false;
	AbandonedCombatRooms = FMath::Max(0, ActualCombatRooms - ClearedCombatRooms);
	ExitNeutralizedEnemies = Withdrawal.Neutralized;
	UE_LOG(LogTemp, Display,
		TEXT("[FloorObjective] floor=%d cleared=%d required=%d actual=%d complete=true exitSafe=true abandonedRooms=%d neutralized=%d rewardEligible=true"),
		FloorIndex, ClearedCombatRooms, RequiredCombatRooms, ActualCombatRooms,
		AbandonedCombatRooms, ExitNeutralizedEnemies);
	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();

	if (FloorIndex >= Cfg.MaxFloors)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[Loadout] floor=%d is the final floor (maxFloors=%d); no reward offer, win path proceeds"),
			FloorIndex, Cfg.MaxFloors);
		RepokeStairsForDescend();
		return;
	}

	if (ULoadoutComponent* LC = FindPlayerLoadout())
	{
		LC->GenerateOfferForFloor(RunSeed, FloorIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Loadout] floor %d cleared but the player has no loadout component; no reward offered"), FloorIndex);
	}
	RepokeStairsForDescend();
}

void UUegameFloorManager::TryChooseLoadout(int32 Index)
{
	if (!bRunActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Loadout] no active run; ChooseLoadout ignored"));
		return;
	}
	ULoadoutComponent* LC = FindPlayerLoadout();
	const m8contract::Dispatch Dispatch = m8contract::dispatch_slot(
		RuntimeLifecycle.state == m8authority::RunState::Paused,
		IsRoomContractPending(), LC && LC->IsRewardPending(), Index);
	UE_LOG(LogTemp, Display,
		TEXT("[InputDispatch] floor=%d state=%d index=%d owner=%s accepted=%s contractPending=%s rewardPending=%s"),
		FloorIndex, static_cast<int32>(RuntimeLifecycle.state), Index,
		InputOwnerName(Dispatch.owner), Dispatch.accepted ? TEXT("true") : TEXT("false"),
		IsRoomContractPending() ? TEXT("true") : TEXT("false"),
		LC && LC->IsRewardPending() ? TEXT("true") : TEXT("false"));
	if (Dispatch.owner == m8contract::InputOwner::PauseMenu)
	{
		return;
	}
	if (Dispatch.owner == m8contract::InputOwner::Contract)
	{
		if (Dispatch.accepted)
		{
			TryCommitRoomContract(Index);
		}
		else
		{
			UE_LOG(LogTemp, Display,
				TEXT("[RoomContract] floor=%d state=Pending input=%d accepted=false"), FloorIndex, Index + 1);
		}
		return;
	}
	if (!LC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Loadout] no loadout component on player; ChooseLoadout ignored"));
		return;
	}
	if (!LC->ChooseOffer(Index))
	{
		return;   // invalid index or nothing pending - ChooseOffer already logged the reason
	}
	// The pick resolves MaxHP first; recovery then uses that authoritative new ceiling.
	ApplyPostRewardRecovery();
	// Reward taken -> RewardPending is now false. Re-poke the stairs so a player already standing on the pad
	// descends immediately (otherwise they descend on the next overlap when they step onto the pad).
	RepokeStairsForDescend();
}
