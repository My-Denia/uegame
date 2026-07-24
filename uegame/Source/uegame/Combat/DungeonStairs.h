// DungeonStairs.h - M4 descend trigger, spawned in the farthest room of each floor.
// Overlap by the player pawn requests a descend through the FloorManager; the gate
// policy (descend-anytime vs require-floor-clear) lives in the DataTable row and is
// enforced by the FloorManager, not here.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "DungeonStairs.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class UEGAME_API ADungeonStairs : public AActor
{
	GENERATED_BODY()

public:
	ADungeonStairs();

	virtual void BeginPlay() override;

	/** Re-attempt the descend now that the floor's clear gate may have opened. Called by the
	 *  spawner when the last enemy dies: the overlap is edge-triggered, so a player standing on
	 *  the pad when the final enemy falls would otherwise stay stuck until stepping off and back
	 *  on. No-op unless the player pawn is currently inside the trigger. */
	void OnFloorCleared();

private:
	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEnd(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Ask the FloorManager to descend (gate enforced there). Shared by overlap + clear paths. */
	void RequestDescendNow();
	void SetExitReadyVisual(bool bReady);

	/** True while the player pawn is inside the trigger (Begin/End overlap). */
	bool bPawnInside = false;

	UPROPERTY(VisibleAnywhere, Category="Stairs")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category="Stairs")
	TObjectPtr<UStaticMeshComponent> PadMesh;

	UPROPERTY(VisibleAnywhere, Category="Stairs")
	TObjectPtr<UStaticMeshComponent> BeaconMesh;

	UPROPERTY(VisibleAnywhere, Category="Stairs")
	TObjectPtr<UPointLightComponent> BeaconLight;

	UPROPERTY(VisibleAnywhere, Category="Stairs")
	TObjectPtr<UTextRenderComponent> ExitLabel;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ExitMaterial;
};
