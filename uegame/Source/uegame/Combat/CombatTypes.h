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

	// --- M8 room risk/reward and pacing recovery (fractions of resolved MaxHP) ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0.0", ClampMax="1.0"))
	float QuietRoomClearHealFraction = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StandardRoomClearHealFraction = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SkirmishRoomClearHealFraction = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StrongholdRoomClearHealFraction = 0.10f;

	/** After a successful non-final-floor reward pick, recover to at least this fraction of
	 *  the newly resolved MaxHP. This is a floor, not a fixed heal and never resurrects. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RewardRecoveryFloorFraction = 1.0f;

	/** Non-final floor objective quota: Base + (Floor-1)*PerFloor, capped to the number
	 *  of combat rooms that actually spawned. The final floor always requires full clear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0", ClampMax="1024"))
	int32 RequiredCombatRoomsBase = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0", ClampMax="1024"))
	int32 RequiredCombatRoomsPerFloor = 1;

	// --- M4 run loop ---

	/** Descend gate policy: false = descend-anytime; true = stairs refuse until the current
	 *  authoritative room objective is complete. Current CSV ships true. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run")
	bool bRequireFloorClearToDescend = false;

	/** Descending past this floor wins the run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run")
	int32 MaxFloors = 3;
};
