// FloorManager.h - M4 run state machine (GameInstance-scoped).
// Owns {runSeed, floorIndex} and drives in-place floor transitions on the spawner
// (mechanism (a), GATE-1 decision: world + nav system stay alive across floors, so
// player HP carries over simply because the pawn is never destroyed).
// Win: descending past MaxFloors (DataTable) -> [RunWon]. Lose: player death during
// a run -> [RunFailed]. Either restarts from floor 1 with a fresh runSeed chained
// deterministically via m2::nextRunSeed (no unseeded RNG, contract F).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "FloorManager.generated.h"

class ADungeonSpawner;
class ULoadoutComponent;
struct FActorsInitializedParams;

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

	/** M5: a floor's last enemy room just cleared. If this floor owes a reward (FloorIndex < MaxFloors),
	 *  generate the 3-choose-1 offer on the player's loadout component and mark the reward pending, which
	 *  blocks descend until a pick is made. The final floor owes no reward (no useless pre-win offer). */
	void NotifyFloorCleared();

	/** M5: apply the player's reward pick (0..2) on the loadout component and, on success, re-poke the
	 *  stairs so a player already on the pad descends. Driven by Dungeon.ChooseLoadout and keys 1/2/3. */
	void TryChooseLoadout(int32 Index);

	/** Player death during a run: [RunFailed], chain a fresh seed, restart floor 1. */
	void NotifyRunFailed();

private:
	/** Transitions are deferred to next tick (the trigger may sit on the call stack of an
	 *  actor the transition destroys). The pending kind is re-read at fire time: a death
	 *  arriving inside the window upgrades a queued Descend to Fail - the run must never
	 *  continue onto the next floor with a dead pawn. The reverse never happens. */
	enum class EPendingTransition : uint8 { None, Descend, Fail };

	void StartFloor(int32 NewFloorIndex);
	void RestartRun(const TCHAR* Reason);
	void ExecutePendingTransition();
	/** Shipping-safe bootstrap for maps without a pre-placed spawner (console verbs are
	 *  compiled out of Shipping, so gameplay code must be able to enter the loop alone). */
	void OnWorldActorsInitialized(const FActorsInitializedParams& Params);
	ADungeonSpawner* FindSpawner() const;
	void HealPlayerFull() const;

	/** M5: the player pawn's loadout component (nullptr if no pawn / no component). */
	ULoadoutComponent* FindPlayerLoadout() const;
	/** M5: reset the loadout to a fresh base build at run (re)start (picks cleared, MaxHP back to base). */
	void ResetLoadoutForNewRun() const;
	/** M5: re-attempt descend on every stairs pad after a reward pick (no-op unless the pawn is on a pad). */
	void RepokeStairsForDescend() const;

	FDelegateHandle ActorsInitializedHandle;

	uint64 RunSeed = 0;
	int32 FloorIndex = 0;
	bool bRunActive = false;
	EPendingTransition PendingTransition = EPendingTransition::None;

	/** Set by Dungeon.SetRunSeed: overrides entropy for the next first-run seed. */
	TOptional<uint64> ExplicitFirstSeed;

	/** DefaultGame.ini [/Script/uegame.UegameFloorManager] bUseFixedFirstSeed (default false).
	 *  true => the first run uses the pinned demo seed 7 instead of entropy (portfolio/demo mode). */
	bool bUseFixedFirstSeed = false;
};
