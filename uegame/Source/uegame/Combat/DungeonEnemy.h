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
class UMaterialInstanceDynamic;
struct FEncounterArchetypeStats;

UCLASS()
class UEGAME_API ADungeonEnemy : public ACharacter
{
	GENERATED_BODY()

public:
	ADungeonEnemy();

	/** Apply the DataTable row + room bookkeeping. Call between deferred spawn and FinishSpawning. */
	void InitEnemy(const FCombatConfigRow& Row, int32 InRoomIndex, ADungeonSpawner* InSpawner);

	/** M6B: overlay one archetype's stats (stat-only variant - Grunt equals Default by the
	 *  loader's parity gate, so the Grunt path is behaviourally identical to pre-M6 spawns).
	 *  InHpMult re-applies the M4 per-floor HP multiplier to the archetype's base HP.
	 *  InTypeId (m6::TypeId order) is stored as this enemy's authoritative identity; the
	 *  log/display name is always DERIVED from it via FUegameEncounterConfig::TypeName.
	 *  Call AFTER InitEnemy and before FinishSpawning. Scales ONLY the visual BodyMesh;
	 *  the capsule, nav agent, ContactRange, AI state machine, and hit-flash material are
	 *  untouched by contract. */
	void ApplyArchetype(const FEncounterArchetypeStats& Stats, float InHpMult, int32 InTypeId);

	int32 GetRoomIndex() const { return RoomIndex; }

	// --- M7A.2 read-only identity/HP surface (consumed by the AUegameHUD enemy readout) ---
	/** True only after ApplyArchetype ran (M6 assignment). The static-spawner /
	 *  encounter-unavailable path never assigns and must read as a neutral "Enemy",
	 *  never as "Grunt". */
	bool HasArchetypeAssignment() const { return ArchetypeTypeId != INDEX_NONE; }
	/** Archetype id in m6::TypeId order; INDEX_NONE while unassigned. The numeric id is
	 *  the authoritative identity - display names are derived, never stored. */
	int32 GetArchetypeTypeId() const { return ArchetypeTypeId; }
	/** Derived, never stored: TypeName(id) when assigned, "Enemy" otherwise. */
	const TCHAR* GetArchetypeDisplayName() const;
	/** Read-only HP truth source for presentation. Const pointer: only the const getters
	 *  (GetHP/GetMaxHP/IsDead) are reachable, every mutator is non-const and blocked. */
	const UHealthComponent* GetHealthComponent() const { return Health; }

	// --- Run 2.5 perception (read by the Dungeon.AggroStatus forensic verb) ---
	bool IsChasing() const { return bChasing; }
	float GetAggroRange() const { return AggroRange; }
	float GetLeashRange() const { return LeashRange; }
	/** LOS to Target via a WorldStatic-only, strictly-horizontal trace: dungeon walls occlude,
	 *  dynamic pawns never do. Returns true when nothing blocks the sightline. */
	bool ComputeLOSTo(const AActor* Target) const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void PursueTick();

	UFUNCTION()
	void HandleDeath(AActor* DeadActor);

	/** Run 2.5 hit flash: pulse the body material white on taking damage, restore after ~0.12s. */
	UFUNCTION()
	void HandleDamaged(float Amount, AActor* DamageInstigator);
	void ClearFlash();

	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** Dynamic material instance for the hit flash (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;

	FTimerHandle PursueTimer;
	FTimerHandle FlashTimer;

	int32 RoomIndex = -1;
	/** M7A.2: archetype identity (m6::TypeId order); INDEX_NONE = no M6 assignment.
	 *  Reset by InitEnemy, set only by ApplyArchetype. Not reflected: presentation-facing
	 *  runtime state, never serialized and never exposed for mutation. */
	int32 ArchetypeTypeId = INDEX_NONE;
	TWeakObjectPtr<ADungeonSpawner> SpawnerRef;

	float ContactDamage = 10.0f;
	float DamageInterval = 1.0f;
	/** Contact reach: capsule radii sum + slack; set from capsule sizes at spawn. */
	float ContactRange = 130.0f;
	double LastContactDamageTime = -1000.0;

	// --- Run 2.5 perception state (from the DataTable row via InitEnemy) ---
	/** Idle (false, stands in place) vs Chasing (true). */
	bool bChasing = false;
	float AggroRange = 900.0f;
	float LeashRange = 1400.0f;

	bool bLoggedFirstMove = false;
};
