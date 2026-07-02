// CombatComponent.h - the player's single melee attack (M3).
// Cooldown-gated and range-checked; all numbers from the combat DataTable row.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "CombatComponent.generated.h"

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class UEGAME_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();

	/** One melee swing: rejected while on cooldown; hits enemies inside a sphere
	 *  placed AttackRange/2 in front of the owner (radius AttackRange/2). */
	UFUNCTION(BlueprintCallable, Category="Combat")
	void TryAttack();

private:
	double LastAttackTime = -1000.0;
};
