// DungeonSpawner.h - M2: renders the engine-agnostic M1 dungeon Layout as walkable geometry.
//
// Landed from the m2_ue/ scaffold. NOT yet compile-verified under UE (pending the
// milestone-A UBT build). Audit advisories applied at landing:
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

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Rebuild ISM geometry from the M1 layout for the current Seed. Editor-safe. */
	UFUNCTION(BlueprintCallable, Category="Dungeon")
	void Build();

	/** Absolute world position of the start room center (valid after Build). */
	FVector GetStartWorldLocation() const { return StartWorld; }

	/** Absolute world center of the room farthest (2D) from the start room (valid after Build).
	 *  Used by the Dungeon.WalkFar evidence command. */
	FVector GetFarthestRoomCenterWorld() const { return FarthestRoomWorld; }

	// --- M3 room-clear tracking (evidence surface) ---

	/** Enemy death callback; logs [RoomClear] when a room's alive count reaches zero. */
	void NotifyEnemyDead(int32 InRoomIndex);

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
	int32 GetStartRoomIndex() const { return StartRoomIndex; }

private:
	void SpawnNavBounds();
	void SpawnEnemies();

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

	/** M3 per-room enemy bookkeeping; written by SpawnEnemies(). */
	TArray<int32> RoomAliveCounts;
	TArray<int32> RoomInitialCounts;
};
