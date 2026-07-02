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

	/** Begin a run at floor 1 (finds or spawns the dungeon spawner). */
	void StartRun(uint64 InRunSeed);

	/** Stairs overlap / Dungeon.Descend. bForce bypasses the clear-gate policy flag. */
	void RequestDescend(bool bForce);

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

	FDelegateHandle ActorsInitializedHandle;

	uint64 RunSeed = 0;
	int32 FloorIndex = 0;
	bool bRunActive = false;
	EPendingTransition PendingTransition = EPendingTransition::None;
};
