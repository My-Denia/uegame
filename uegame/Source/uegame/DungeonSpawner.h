// DungeonSpawner.h - M2: renders the engine-agnostic M1 dungeon Layout as walkable geometry.
//
// Landed from the m2_ue/ scaffold and compile-verified under UE (full UBT rebuilds
// through M4/v1; exercised in PIE). Audit advisories applied at landing:
//   #1 "NavigationSystem" added to uegame.Build.cs dependencies
//   #2 NavMeshBoundsVolume is spawned in BeginPlay (game worlds only), never OnConstruction
//   #4 ConstructorHelpers::FObjectFinder at constructor scope (no static-in-lambda)
//   #5 ISM instances added with bWorldSpace=true (dungeon occupies absolute world coords)
//   #6 UEGAME_API export macro on the class

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonSpawner.generated.h"

class UInstancedStaticMeshComponent;
class ADungeonEnemy;
struct FCombatConfigRow;

enum class EUegameFinaleInitState : uint8
{
	NotRequired = 0,
	Pending,
	Succeeded,
	Failed
};

struct FFloorExitNeutralizationResult
{
	int32 ExpectedActive = 0;
	int32 Collected = 0;
	int32 Preflighted = 0;
	int32 Neutralized = 0;
	int32 DestroyQueued = 0;
	int32 RemainingActive = 0;
	int32 ResidualPrepared = 0;
	bool bCollectionPassed = false;
	bool bPreflightPassed = false;
	bool bSuccess = false;
	TArray<int64> CollectedIds;
};

struct FChallengeContractTransactionResult
{
	int32 Expected = 0;
	int32 ObservedActive = 0;
	int32 Eligible = 0;
	int32 Applied = 0;
	int32 RolledBack = 0;
	int32 ResidualModified = 0;
	bool bPreflightPassed = false;
	bool bCommitted = false;
	bool bRollbackComplete = true;
	bool bTargetCurrent = true;
	TArray<int64> ExpectedIds;
	TArray<int64> EligibleIds;
	TArray<int64> AppliedIds;
	TArray<int64> RolledBackIds;
};

UCLASS()
class UEGAME_API ADungeonSpawner : public AActor
{
	GENERATED_BODY()

public:
	ADungeonSpawner();

	/** M1 seed, passed straight through as (uint64)Seed to std::mt19937_64.
	 *  Do NOT replace with FRandomStream: the same seed would yield a different dungeon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon")
	int32 Seed = 7;

	/** World size of one grid cell in cm (tile size == grid step). Default 200: at 100 the
	 *  1-cell corridors erode below the nav AgentRadius(35) and get culled from the navmesh
	 *  (M2 finding) - the constraint is absorbed here in the adapter layer, M1 untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon", meta=(ClampMin="10.0"))
	float TileSize = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon", meta=(ClampMin="10.0"))
	float WallHeight = 200.0f;

	/** Try to spawn a NavMeshBoundsVolume covering the map at BeginPlay. Runtime-spawned
	 *  brush volumes carry no brush geometry, so the resulting bounds may be empty (the
	 *  outcome is logged). The robust alternative is a hand-placed volume; see
	 *  m2_ue/M2_UE_README.md. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon")
	bool bSpawnNavBounds = true;

	/** Move player pawn 0 to the start room center at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon")
	bool bTeleportPlayerToStart = true;

	/** M3: spawn enemies from the deterministic enemy plan at BeginPlay (game worlds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon")
	bool bSpawnEnemies = true;

	/** M4: one tick after BeginPlay, start a run (floor 1) seeded from this spawner if none
	 *  is active yet. This is the gameplay path into the floor loop - the Dungeon.StartRun
	 *  console verb is compiled out of Shipping builds, so it cannot be the only caller.
	 *  The FloorManager's own helper spawner has this disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dungeon")
	bool bAutoStartRun = true;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Rebuild ISM geometry from the M1 layout for the current Seed. Editor-safe. */
	UFUNCTION(BlueprintCallable, Category="Dungeon")
	void Build();

	// --- M4 floor transition (mechanism (a): in-place regeneration) ---

	/** Full 64-bit seed actually used by generate(). Floor seeds derived from
	 *  (runSeed, floorIndex) exceed int32; the editor-facing int32 Seed remains for
	 *  hand-testing and is used only when no 64-bit seed has been set. */
	uint64 GetEffectiveSeed64() const { return bHasSeed64 ? Seed64 : static_cast<uint64>(Seed); }
	void SetSeed64(uint64 InSeed) { Seed64 = InSeed; bHasSeed64 = true; }

	/** In-place floor transition: despawn all enemies, rebuild geometry from NewSeed,
	 *  re-dirty the navmesh over the whole map, respawn enemies (scaled), teleport the
	 *  player to the new start room. World and nav system stay alive (no OpenLevel -
	 *  that path loses the RecastNavMesh, M3 finding). */
	void RegenerateFloor(uint64 NewSeed, int32 InEnemiesPerRoomOverride = -1,
	                     float InEnemyHPOverride = -1.0f);

	/** Absolute world position of the start room center (valid after Build). */
	FVector GetStartWorldLocation() const { return StartWorld; }

	/** Absolute world center of the room farthest (2D) from the start room (valid after Build).
	 *  Used by the Dungeon.WalkFar evidence command. */
	FVector GetFarthestRoomCenterWorld() const { return FarthestRoomWorld; }

	// --- M3 room-clear tracking (evidence surface) ---

	/** Enemy death callback; logs [RoomClear] when a room's alive count reaches zero. */
	void NotifyEnemyDead(ADungeonEnemy* Enemy);

	/** Promote one frozen final-floor spawn to a fixed-profile Warden without changing identity. */
	bool InitializeFinale(const FCombatConfigRow& Row, int32 ResolveTokens);
	EUegameFinaleInitState GetFinaleInitState() const { return FinaleInitState; }
	bool HasFinaleInitialized() const { return FinaleInitState == EUegameFinaleInitState::Succeeded; }
	bool HasFinaleFailed() const { return FinaleInitState == EUegameFinaleInitState::Failed; }
	bool IsWardenDefeated() const { return bWardenDefeated; }
	ADungeonEnemy* GetWarden() const { return WardenEnemy.Get(); }
	int32 GetLivingOrdinaryEnemyCount() const;

	/** Atomically make every live enemy owned by this spawner harmless before progression. */
	FFloorExitNeutralizationResult DeactivateRemainingEnemiesForExit(int32 InFloorIndex);
	/** Preflight the full live room set, then apply all-or-rollback Challenge modifiers. */
	FChallengeContractTransactionResult ApplyChallengeContractTransactional(int32 InRoomIndex);
#if !UE_BUILD_SHIPPING
	/** Pre-mutation negative seam: 0=off, 1=fail after collection, 2=fail after preflight. */
	void SetExitWithdrawalFailureModeForTests(int32 Mode)
	{
		ExitWithdrawalFailureModeForTests = FMath::Clamp(Mode, 0, 2);
	}
	/** 0=none, 1=stale preflight, 2=partial apply followed by required rollback. */
	void SetChallengeContractFailureModeForTests(int32 Mode) { ChallengeContractFailureModeForTests = Mode; }
	/** One-shot finale init seam: 1=missing, 2=duplicate, 3=mismatch, 4=partial rollback, 5=invalid profile. */
	void SetFinaleInitFailureModeForTests(int32 Mode)
	{
		FinaleInitFailureModeForTests = FMath::Clamp(Mode, 0, 5);
	}
	/** Evidence-only cache staging for isolated callback validation; caller must restore it. */
	bool SetRoomAliveCountForTests(int32 RoomIndex, int32 Alive)
	{
		if (!RoomAliveCounts.IsValidIndex(RoomIndex))
		{
			return false;
		}
		RoomAliveCounts[RoomIndex] = FMath::Clamp(Alive, 0, RoomInitialCounts[RoomIndex]);
		return true;
	}
#endif

	int32 GetRoomCount() const { return RoomCentersWorld.Num(); }
	FVector GetRoomCenterWorld(int32 InRoomIndex) const
	{
		return RoomCentersWorld.IsValidIndex(InRoomIndex) ? RoomCentersWorld[InRoomIndex] : FVector::ZeroVector;
	}
	int32 GetAliveInRoom(int32 InRoomIndex) const
	{
		return RoomAliveCounts.IsValidIndex(InRoomIndex) ? RoomAliveCounts[InRoomIndex] : 0;
	}
	int32 GetInitialInRoom(int32 InRoomIndex) const
	{
		return RoomInitialCounts.IsValidIndex(InRoomIndex) ? RoomInitialCounts[InRoomIndex] : 0;
	}
	int32 GetActualEnemyRoomCount() const
	{
		int32 Count = 0;
		for (const int32 Initial : RoomInitialCounts)
		{
			Count += Initial > 0 ? 1 : 0;
		}
		return Count;
	}
	int32 GetTotalAliveEnemies() const
	{
		int32 Count = 0;
		for (const int32 Alive : RoomAliveCounts)
		{
			Count += FMath::Max(0, Alive);
		}
		return Count;
	}
	int32 GetStartRoomIndex() const { return StartRoomIndex; }

	// --- M6B encounter cache (evidence surface for Dungeon.RoomRoles / Dungeon.EnemyRoster;
	//     written by SpawnEnemies from what ACTUALLY spawned, never recomputed on read) ---

	/** True when the last SpawnEnemies ran the M6 assignment (active run + validated tables). */
	bool HasEncounterAssignment() const { return bEncounterAssigned; }
	uint64 GetEncounterRunSeed() const { return EncounterRunSeed; }
	int32 GetEncounterFloorIndex() const { return EncounterFloorIndex; }
	float GetEncounterHpMult() const { return EncounterHpMult; }
	uint64 GetCachedRoomRoleHash() const { return CachedRoomRoleHash; }
	uint64 GetCachedEnemyTypeHash() const { return CachedEnemyTypeHash; }
	/** m2 anchors for the same plan, cached at spawn time (baseline-unchanged evidence). */
	uint64 GetCachedSpawnPlanHash() const { return CachedSpawnPlanHash; }
	uint64 GetCachedEnemyPlanHash() const { return CachedEnemyPlanHash; }
	/** One m6::RoleId-as-int per room index. */
	const TArray<int32>& GetCachedRoomRoles() const { return CachedRoomRoles; }
	/** Per-room spawned archetype counts (X=Grunt, Y=Runner, Z=Brute). */
	const TArray<FIntVector>& GetCachedRoomTypeCounts() const { return CachedRoomTypeCounts; }
	/** Whole-floor spawned archetype tally (X=Grunt, Y=Runner, Z=Brute). */
	FIntVector GetCachedTypeTally() const { return CachedTypeTally; }

	/** M4: true when every room that started with enemies has zero alive. */
	bool AreAllRoomsCleared() const
	{
		for (int32 i = 0; i < RoomInitialCounts.Num(); ++i)
		{
			if (RoomInitialCounts[i] > 0 && RoomAliveCounts.IsValidIndex(i) && RoomAliveCounts[i] > 0)
			{
				return false;
			}
		}
		return true;
	}

private:
	void SpawnNavBounds();
	/** M4: (re)spawn the descend stairs in the farthest room (destroys the previous pad). */
	void SpawnStairs();
	/** Spawn the deterministic enemy plan. Overrides (<0 = use DataTable base values)
	 *  let RegenerateFloor apply the M4 per-floor scaling. */
	void SpawnEnemies(int32 InEnemiesPerRoomOverride = -1, float InEnemyHPOverride = -1.0f);
	/** Mark the whole map dirty so the dynamic navmesh rebuilds (M2 pattern, factored
	 *  out so floor transitions can reuse it). */
	void RefreshNavigation();
	void ResetFinaleState();

	/** M4: 64-bit runtime seed (floor seeds exceed int32). */
	uint64 Seed64 = 0;
	bool bHasSeed64 = false;

	UPROPERTY(VisibleAnywhere, Category="Dungeon")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category="Dungeon")
	TObjectPtr<UInstancedStaticMeshComponent> FloorISM;

	UPROPERTY(VisibleAnywhere, Category="Dungeon")
	TObjectPtr<UInstancedStaticMeshComponent> WallISM;

	UPROPERTY(VisibleAnywhere, Category="Dungeon")
	TObjectPtr<UInstancedStaticMeshComponent> CorridorISM;

	UPROPERTY(VisibleAnywhere, Category="Dungeon")
	TObjectPtr<UInstancedStaticMeshComponent> DoorISM;

	/** Absolute world position of the start room center; written by Build(). */
	FVector StartWorld = FVector::ZeroVector;

	/** Absolute world center of the room farthest from the start room; written by Build(). */
	FVector FarthestRoomWorld = FVector::ZeroVector;

	/** All room centers in world coords (index == M1 room index); written by Build(). */
	TArray<FVector> RoomCentersWorld;
	int32 StartRoomIndex = 0;
	int32 FarthestRoomIndex = 0;

	/** M3 per-room enemy bookkeeping; written by SpawnEnemies(). */
	TArray<int32> RoomAliveCounts;
	TArray<int32> RoomInitialCounts;
	TArray<TWeakObjectPtr<ADungeonEnemy>> SpawnedEnemyActors;
	TArray<int32> LastPlannedRooms;
	int32 LastPlannedEnemyCount = 0;
	EUegameFinaleInitState FinaleInitState = EUegameFinaleInitState::NotRequired;
	TWeakObjectPtr<ADungeonEnemy> WardenEnemy;
	bool bWardenDefeated = false;

	// --- M6B encounter cache backing fields (see public accessors above) ---
	bool bEncounterAssigned = false;
	uint64 EncounterRunSeed = 0;
	int32 EncounterFloorIndex = 0;
	float EncounterHpMult = 1.0f;
	uint64 CachedRoomRoleHash = 0;
	uint64 CachedEnemyTypeHash = 0;
	uint64 CachedSpawnPlanHash = 0;
	uint64 CachedEnemyPlanHash = 0;
	TArray<int32> CachedRoomRoles;
	TArray<FIntVector> CachedRoomTypeCounts;
	FIntVector CachedTypeTally = FIntVector::ZeroValue;
#if !UE_BUILD_SHIPPING
	int32 ExitWithdrawalFailureModeForTests = 0;
	int32 ChallengeContractFailureModeForTests = 0;
	int32 FinaleInitFailureModeForTests = 0;
#endif
};
