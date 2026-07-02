// CombatTypes.h - M3 data-driven combat config (single DataTable row).
// All combat numbers live in Content/Data/CombatConfig.csv; nothing is hard-coded
// in gameplay logic. PerFloorScaling is RESERVED for M4 (defined, not used).

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

	/** RESERVED for M4 per-floor difficulty scaling. Defined per contract; NOT applied in M3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scaling")
	float PerFloorScaling = 1.0f;
};
