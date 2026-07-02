// DungeonEnemy.h - M3's single enemy type.
// Pursuit: navmesh MoveToActor toward the player on a repath timer (depends on the
// M2 tileSize=200 navmesh fix). Contact damage on proximity with a per-target damage
// interval. Death notifies the spawner's room tracking. Placeholder visuals only.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatTypes.h"

#include "DungeonEnemy.generated.h"

class ADungeonSpawner;
class UHealthComponent;
class UStaticMeshComponent;

UCLASS()
class UEGAME_API ADungeonEnemy : public ACharacter
{
	GENERATED_BODY()

public:
	ADungeonEnemy();

	/** Apply the DataTable row + room bookkeeping. Call between deferred spawn and FinishSpawning. */
	void InitEnemy(const FCombatConfigRow& Row, int32 InRoomIndex, ADungeonSpawner* InSpawner);

	int32 GetRoomIndex() const { return RoomIndex; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void PursueTick();

	UFUNCTION()
	void HandleDeath(AActor* DeadActor);

	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	FTimerHandle PursueTimer;

	int32 RoomIndex = -1;
	TWeakObjectPtr<ADungeonSpawner> SpawnerRef;

	float ContactDamage = 10.0f;
	float DamageInterval = 1.0f;
	/** Contact reach: capsule radii sum + slack; set from capsule sizes at spawn. */
	float ContactRange = 130.0f;
	double LastContactDamageTime = -1000.0;

	bool bLoggedFirstMove = false;
};
