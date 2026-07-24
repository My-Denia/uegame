// HealthComponent.h - shared HP for player and enemy (components over inheritance).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHealthDeathSignature, AActor*, DeadActor);
/** Fired on every application of real damage (not while dead/invincible, and not for 0). Drives
 *  Run 2.5 feedback: the enemy hit flash and the player screen pulse bind to this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHealthDamagedSignature, float, Amount, AActor*, DamageInstigator);

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

	/** Restore HP on a living actor without changing MaxHP or death/event state. */
	float Heal(float Amount);

	/** M5 loadout: change MaxHP without touching dead/event state - a THIRD path, distinct from Init
	 *  (refills), SetHP (fires OnDeath at 0), and Revive (clears bDead). bTopUpCurrent raises CurrentHP by
	 *  the same positive delta as MaxHP, so a +MaxHP build pick is an immediate reward, not "go heal to use
	 *  it"; a decrease (or bTopUpCurrent=false) clamps CurrentHP down to the new max with no refund. NEVER
	 *  broadcasts OnDeath/OnDamaged and NEVER clears/sets bDead, so a mid-run re-resolve cannot revive a
	 *  corpse or re-fire death. */
	UFUNCTION(BlueprintCallable, Category="Combat")
	void SetMaxHP(float NewMaxHP, bool bTopUpCurrent);

	/** Forensic-only (Dungeon.SetHP <v> [holdSec]): while set, TakeDamage is a no-op, so a
	 *  survival probe cannot be interrupted by a lethal hit - no HP drop, no OnDeath, no queued
	 *  run-fail. Never set in normal gameplay (default false). */
	void SetInvincible(bool bInInvincible) { bInvincible = bInInvincible; }
	bool IsInvincible() const { return bInvincible; }

	UPROPERTY(BlueprintAssignable, Category="Combat")
	FHealthDeathSignature OnDeath;

	/** Fires on each real damage application (see FHealthDamagedSignature). */
	UPROPERTY(BlueprintAssignable, Category="Combat")
	FHealthDamagedSignature OnDamaged;

private:
	UPROPERTY(VisibleAnywhere, Category="Combat")
	float MaxHP = 100.0f;

	UPROPERTY(VisibleAnywhere, Category="Combat")
	float CurrentHP = 100.0f;

	bool bDead = false;

	/** Forensic invincibility (see SetInvincible). */
	bool bInvincible = false;
};
