// HealthComponent.h - shared HP for player and enemy (components over inheritance).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHealthDeathSignature, AActor*, DeadActor);

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class UEGAME_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	/** Set max HP (from the combat DataTable row) and refill. */
	UFUNCTION(BlueprintCallable, Category="Combat")
	void Init(float InMaxHP);

	/** Apply damage; logs the delta; fires OnDeath exactly once when HP hits 0. */
	UFUNCTION(BlueprintCallable, Category="Combat")
	void TakeDamage(float Amount, AActor* DamageInstigator);

	UFUNCTION(BlueprintPure, Category="Combat")
	float GetHP() const { return CurrentHP; }

	UFUNCTION(BlueprintPure, Category="Combat")
	float GetMaxHP() const { return MaxHP; }

	UFUNCTION(BlueprintPure, Category="Combat")
	bool IsDead() const { return bDead; }

	UPROPERTY(BlueprintAssignable, Category="Combat")
	FHealthDeathSignature OnDeath;

private:
	UPROPERTY(VisibleAnywhere, Category="Combat")
	float MaxHP = 100.0f;

	UPROPERTY(VisibleAnywhere, Category="Combat")
	float CurrentHP = 100.0f;

	bool bDead = false;
};
