// DungeonStairs.h - M4 descend trigger, spawned in the farthest room of each floor.
// Overlap by the player pawn requests a descend through the FloorManager; the gate
// policy (descend-anytime vs require-floor-clear) lives in the DataTable row and is
// enforced by the FloorManager, not here.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "DungeonStairs.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class UEGAME_API ADungeonStairs : public AActor
{
	GENERATED_BODY()

public:
	ADungeonStairs();

	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category="Stairs")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category="Stairs")
	TObjectPtr<UStaticMeshComponent> PadMesh;
};
