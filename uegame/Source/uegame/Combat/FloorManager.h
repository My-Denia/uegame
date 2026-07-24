// FloorManager.h - M4 run state machine (GameInstance-scoped).
// Owns {runSeed, floorIndex} and drives in-place floor transitions on the spawner
// (mechanism (a), GATE-1 decision: world + nav system stay alive across floors, so
// player HP carries over simply because the pawn is never destroyed).
// Win: descending past MaxFloors (DataTable) -> [RunWon]. Lose: player death during
// a run -> [RunFailed]. Both remain visible until the player explicitly restarts or quits.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "m8_runtime_authority.hpp"

#include "FloorManager.generated.h"

class ADungeonSpawner;
class ULoadoutComponent;
class UPresentationFeedbackComponent;
struct FActorsInitializedParams;

enum class EUegameRoomContractChoice : uint8
{
	Unavailable = 0,
	Pending,
	Secure,
	Challenge
};

enum class EUegameRoomContractWarning : uint8
{
	None = 0,
	SecureFallback,
	Unavailable
};

UCLASS()
class UEGAME_API UUegameFloorManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UUegameFloorManager* Get(UWorld* World);

	bool IsRunActive() const { return bRunActive; }
	uint64 GetRunSeed() const { return RunSeed; }
	int32 GetFloorIndex() const { return FloorIndex; }
	m8authority::RunState GetRunState() const { return RuntimeLifecycle.state; }
	int32 GetClearedCombatRooms() const { return ClearedCombatRooms; }
	int32 GetTotalClearedCombatRooms() const { return TotalClearedCombatRooms; }
	double GetRunElapsedSeconds() const;
	int32 GetRequiredCombatRooms() const { return RequiredCombatRooms; }
	int32 GetActualCombatRooms() const { return ActualCombatRooms; }
	bool IsFloorObjectiveComplete() const { return bFloorObjectiveComplete; }
	bool AreFloorExitThreatsWithdrawn() const { return bFloorExitThreatsWithdrawn; }
	bool IsProgressionBlockedByExitSafety() const { return bExitSafetyBlocked; }
	int32 GetAbandonedCombatRooms() const { return AbandonedCombatRooms; }
	int32 GetExitNeutralizedEnemies() const { return ExitNeutralizedEnemies; }
	int32 GetResolveTokens() const { return ResolveTokens; }
	EUegameRoomContractChoice GetRoomContractChoice() const { return RoomContractChoice; }
	bool IsRoomContractPending() const { return RoomContractChoice == EUegameRoomContractChoice::Pending; }
	bool IsRoomContractSelected() const
	{
		return RoomContractChoice == EUegameRoomContractChoice::Secure
			|| RoomContractChoice == EUegameRoomContractChoice::Challenge;
	}
	bool IsRoomContractChallenge() const { return RoomContractChoice == EUegameRoomContractChoice::Challenge; }
	bool IsRoomContractResolved() const { return bRoomContractSelectedRoomCleared; }
	bool ShouldPrioritizeContractRoom() const { return IsRoomContractSelected() && !bRoomContractSelectedRoomCleared; }
	int32 GetSecureContractRoom() const { return SecureContractRoom; }
	int32 GetChallengeContractRoom() const { return ChallengeContractRoom; }
	int32 GetSelectedContractRoom() const { return SelectedContractRoom; }
	bool IsChallengeContractDisabled() const { return bChallengeContractDisabled; }
	bool HasRoomContractFallbackWarning() const { return RoomContractWarning == EUegameRoomContractWarning::SecureFallback; }
	bool HasRoomContractUnavailableWarning() const { return RoomContractWarning == EUegameRoomContractWarning::Unavailable; }
	int32 GetRoomContractCommitCount() const { return RoomContractCommitCount; }
#if !UE_BUILD_SHIPPING
	/** Development-only authority seam: 0=normal, 1=missing, 2=ambiguous. */
	void SetFreshSpawnerFaultModeForTests(int32 Mode)
	{
		FreshSpawnerFaultModeForTests = FMath::Clamp(Mode, 0, 2);
	}
	/** One-shot fault used only by exit completion, after room-clear provenance succeeds. */
	void SetExitFreshSpawnerFaultModeForTests(int32 Mode)
	{
		ExitFreshSpawnerFaultModeForTests = FMath::Clamp(Mode, 0, 2);
	}
	/** Exercise the real HealthComponent-backed recovery exclusion path without duplicating it. */
	void ApplyRecoveryForTests(float Fraction, const TCHAR* Reason, int32 RoomIndex)
	{
		ApplyRecoveryFraction(Fraction, Reason, RoomIndex);
	}
	/** Jump through the real StartRun/StartFloor path for a focused final-floor matrix. */
	void StartFinaleForTests(uint64 InRunSeed, int32 InResolveTokens, int32 FailureMode);
#endif

	/** The single first-run seed authority (contract F, amended: seeded everywhere except this
	 *  one run-boundary point). Priority: explicit override (Dungeon.SetRunSeed) > bUseFixedFirstSeed
	 *  config (demo seed 7) > entropy. The ONLY function in gameplay that reads a clock for a seed;
	 *  everything downstream (deriveFloorSeed, nextRunSeed) stays deterministic. Logs [RunSeed]. */
	uint64 ResolveFirstRunSeed();

	/** Pin the next first-run seed (Dungeon.SetRunSeed forensic verb). Makes the entry path
	 *  reproduce a specific seed without touching the entropy call. */
	void SetExplicitFirstSeed(uint64 InSeed) { ExplicitFirstSeed = InSeed; }

	/** Begin a run at floor 1 (finds or spawns the dungeon spawner). Callers on the entry path
	 *  pass ResolveFirstRunSeed(); the forensic Dungeon.StartRun passes an explicit seed. */
	void StartRun(uint64 InRunSeed);

	/** Stairs overlap / Dungeon.Descend. bForce bypasses BOTH the clear-gate policy flag AND the M5
	 *  reward-pending gate (forensic force). */
	void RequestDescend(bool bForce);

	/** Idempotent objective completion. Exit withdrawal must succeed before reward/progression. */
	void NotifyFloorCleared();

	/** Accept one first true room clear only from the unique fresh source snapshot. */
	void NotifyRoomCleared(ADungeonSpawner* SourceSpawner, int32 RoomIndex, int32 CachedRole,
		int32 OldAlive, int32 NewAlive);

	/** M5: apply the player's reward pick (0..2) on the loadout component and, on success, re-poke the
	 *  stairs so a player already on the pad descends. Driven by Dungeon.ChooseLoadout and keys 1/2/3. */
	void TryChooseLoadout(int32 Index);

	/** Player death during a run: enter a visible [RunFailed] result state. */
	void NotifyRunFailed();

	/** Enter a visible, recoverable finale initialization error. Never auto-restarts. */
	void NotifyFinaleInitFailed();

	/** Ordinary player controls. Escape toggles pause; R restarts from pause/result/error; Q exits. */
	void TogglePause();
	void RequestManualRestart();
	void RequestQuit();

private:
	/** Descend is deferred to next tick because its trigger may sit on the call stack of an
	 *  actor the transition destroys. A death in that window clears the pending descend and
	 *  enters the stable Failed result instead. */
	enum class EPendingTransition : uint8 { None, Descend };

	void StartFloor(int32 NewFloorIndex);
	void RestartRun(const TCHAR* Reason);
	void ExecutePendingTransition();
	/** Shipping-safe bootstrap for maps without a pre-placed spawner (console verbs are
	 *  compiled out of Shipping, so gameplay code must be able to enter the loop alone). */
	void OnWorldActorsInitialized(const FActorsInitializedParams& Params);
	ADungeonSpawner* FindSpawner() const;
	ADungeonSpawner* FindUniqueFreshSpawnerForCurrentFloor() const;
	void HealPlayerFull() const;

	/** M5: the player pawn's loadout component (nullptr if no pawn / no component). */
	ULoadoutComponent* FindPlayerLoadout() const;
	UPresentationFeedbackComponent* FindPlayerPresentation() const;
	/** M5: reset the loadout to a fresh base build at run (re)start (picks cleared, MaxHP back to base). */
	void ResetLoadoutForNewRun() const;
	void ResetPresentationForNewRun() const;
	/** M5: re-attempt descend on every stairs pad after a reward pick (no-op unless the pawn is on a pad). */
	void RepokeStairsForDescend() const;
	void ApplyPostRewardRecovery() const;
	void ApplyWorldPause(bool bPaused) const;
	void RefreshWorldPause() const;
	void ClearRoomContractState();
	void EnterTerminalState(m8authority::RunEvent Event, const TCHAR* LogAnchor);
	void InitializeRoomContract(ADungeonSpawner* Spawner);
	bool TryCommitRoomContract(int32 Index);
	bool CommitSecureContract(ADungeonSpawner* Spawner, bool bFallback, const TCHAR* FailureReason);
	void ResolveRoomContractUnavailable(const TCHAR* Reason);
	void ApplyContractHeal(float Fraction, const TCHAR* Reason, int32 RoomIndex) const;
	void ApplyRecoveryFraction(float Fraction, const TCHAR* Reason, int32 RoomIndex) const;
	bool CanCompleteCurrentFloor() const;
	bool IsUniqueFreshSource(const ADungeonSpawner* SourceSpawner) const;

	FDelegateHandle ActorsInitializedHandle;

	uint64 RunSeed = 0;
	int32 FloorIndex = 0;
	bool bRunActive = false;
	EPendingTransition PendingTransition = EPendingTransition::None;
	m8authority::Lifecycle RuntimeLifecycle;
	int32 ClearedCombatRooms = 0;
	int32 TotalClearedCombatRooms = 0;
	double RunStartedAtGameSeconds = 0.0;
	int32 RequiredCombatRooms = 0;
	int32 ActualCombatRooms = 0;
	TSet<int32> ClearedRoomIndices;
	bool bFloorObjectiveComplete = false;
	bool bFloorExitThreatsWithdrawn = false;
	bool bExitSafetyBlocked = false;
	int32 AbandonedCombatRooms = 0;
	int32 ExitNeutralizedEnemies = 0;
	int32 ResolveTokens = 0;
	EUegameRoomContractChoice RoomContractChoice = EUegameRoomContractChoice::Unavailable;
	int32 SecureContractRoom = INDEX_NONE;
	int32 ChallengeContractRoom = INDEX_NONE;
	int32 SelectedContractRoom = INDEX_NONE;
	bool bChallengeContractDisabled = false;
	bool bRoomContractSelectedRoomCleared = false;
	bool bRoomContractAwarded = false;
	EUegameRoomContractWarning RoomContractWarning = EUegameRoomContractWarning::None;
	int32 RoomContractCommitCount = 0;
#if !UE_BUILD_SHIPPING
	int32 FreshSpawnerFaultModeForTests = 0;
	int32 ExitFreshSpawnerFaultModeForTests = 0;
#endif

	/** Set by Dungeon.SetRunSeed: overrides entropy for the next first-run seed. */
	TOptional<uint64> ExplicitFirstSeed;

	/** DefaultGame.ini [/Script/uegame.UegameFloorManager] bUseFixedFirstSeed (default false).
	 *  true => the first run uses the pinned demo seed 7 instead of entropy (portfolio/demo mode). */
	bool bUseFixedFirstSeed = false;
};
