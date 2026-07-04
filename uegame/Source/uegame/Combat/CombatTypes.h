// CombatTypes.h - M3 data-driven combat config (single DataTable row).
// All combat numbers live in Content/Data/CombatConfig.csv; nothing is hard-coded
// in gameplay logic. PerFloorScaling drives the M4 per-floor difficulty scaling.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "CombatTypes.generated.h"

USTRUCT(BlueprintType)
struct FCombatConfigRow : public FTableRowBase
{
	GENERATED_BODY()

	// --- enemy ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy")
	float EnemyMaxHP = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy")
	float EnemyMoveSpeed = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy")
	float EnemyContactDamage = 10.0f;

	/** Seconds between contact-damage applications per target (no per-tick melting). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy")
	float EnemyDamageInterval = 1.0f;

	/** Run 2.5 perception: acquire the player as a chase target when within this distance AND
	 *  with line-of-sight. Below this OR no LOS => the enemy stays Idle (stands in place). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy")
	float AggroRange = 900.0f;

	/** Run 2.5 de-aggro: once chasing, keep chasing until the player is farther than this
	 *  (LeashRange > AggroRange = hysteresis; LOS is NOT re-checked while chasing, so rounding
	 *  a corner never flickers aggro). Beyond it => drop back to Idle and stop moving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy")
	float LeashRange = 1400.0f;

	// --- player ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player")
	float PlayerMaxHP = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player")
	float PlayerAttackDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player")
	float PlayerAttackRange = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player")
	float PlayerAttackCooldown = 0.6f;

	// --- spawning ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn")
	int32 EnemiesPerRoom = 2;

	/** M4 per-floor difficulty scaling (linear): multiplier(N) = 1 + (N-1) * PerFloorScaling,
	 *  applied to enemy count (rounded) and enemy MaxHP. Floor 1 is always base values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scaling")
	float PerFloorScaling = 1.0f;

	// --- M4 run loop ---

	/** Descend gate policy: false = descend-anytime (pacing and player agency; clearing rooms
	 *  stays an optional challenge, not a hard gate), true = stairs refuse until every enemy
	 *  room on the floor is cleared. Current CSV ships true (Run 2.5 stairs-cleared gate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run")
	bool bRequireFloorClearToDescend = false;

	/** Descending past this floor wins the run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run")
	int32 MaxFloors = 3;
};
