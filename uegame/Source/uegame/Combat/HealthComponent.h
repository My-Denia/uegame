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

	/** M4 forensic/managed set: clamps to [0, MaxHP], logs, fires OnDeath once at 0. */
	UFUNCTION(BlueprintCallable, Category="Combat")
	void SetHP(float NewHP);

	/** M4 run-restart path: clear the dead flag and restore HP (full refill if <0). */
	UFUNCTION(BlueprintCallable, Category="Combat")
	void Revive(float NewHP = -1.0f);

	/** Forensic-only (Dungeon.SetHP <v> [holdSec]): while set, TakeDamage is a no-op, so a
	 *  survival probe cannot be interrupted by a lethal hit - no HP drop, no OnDeath, no queued
	 *  run-fail. Never set in normal gameplay (default false). */
	void SetInvincible(bool bInInvincible) { bInvincible = bInInvincible; }
	bool IsInvincible() const { return bInvincible; }

	UPROPERTY(BlueprintAssignable, Category="Combat")
	FHealthDeathSignature OnDeath;

private:
	UPROPERTY(VisibleAnywhere, Category="Combat")
	float MaxHP = 100.0f;

	UPROPERTY(VisibleAnywhere, Category="Combat")
	float CurrentHP = 100.0f;

	bool bDead = false;

	/** Forensic invincibility (see SetInvincible). */
	bool bInvincible = false;
};
