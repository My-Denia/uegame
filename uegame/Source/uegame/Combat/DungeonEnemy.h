// DungeonEnemy.h - M3 spawn identity with M8 bounded archetype behavior.
// Aggro/LOS/leash and 0.5s pursuit/damage quantization remain the outer gates. An additive
// elapsed-time core owns tells, commits, recovery and Runner circle/dash phases.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatTypes.h"

#include "DungeonEnemy.generated.h"

class ADungeonSpawner;
class UHealthComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UTextRenderComponent;
class AAIController;
struct FEncounterArchetypeStats;

enum class EUegameWardenPhase : uint8
{
	Inactive = 0,
	Guarded,
	Staggered,
	Exposed,
	Dead
};

UCLASS()
class UEGAME_API ADungeonEnemy : public ACharacter
{
	GENERATED_BODY()

public:
	ADungeonEnemy();

	/** Apply the DataTable row + room bookkeeping. Call between deferred spawn and FinishSpawning. */
	void InitEnemy(const FCombatConfigRow& Row, int32 InRoomIndex, ADungeonSpawner* InSpawner);

	/** M6B/M8: overlay one archetype's data-driven stats and select its bounded behavior.
	 *  Grunt stats equal Default by the loader parity gate; unassigned fallback also uses Grunt
	 *  behavior without claiming a false M6 identity.
	 *  InHpMult re-applies the M4 per-floor HP multiplier to the archetype's base HP.
	 *  InTypeId (m6::TypeId order) is stored as this enemy's authoritative identity; the
	 *  log/display name is always DERIVED from it via FUegameEncounterConfig::TypeName.
	 *  Call AFTER InitEnemy and before FinishSpawning. Scales ONLY the visual BodyMesh;
	 *  the capsule, nav agent, ContactRange, and hit-flash material remain unchanged. */
	void ApplyArchetype(const FEncounterArchetypeStats& Stats, float InHpMult, int32 InTypeId);

	int32 GetRoomIndex() const { return RoomIndex; }
	/** Deterministic combat ordering key assigned from the frozen spawn plan index. */
	int32 GetSpawnOrdinal() const { return SpawnOrdinal; }
	/** Read-only ownership used by the spawner's floor-exit safety transaction. */
	ADungeonSpawner* GetOwningSpawner() const { return SpawnerRef.Get(); }
	/** True only while this actor can still pursue or damage the player. */
	bool IsActiveThreat() const;
	/** Current pool for player targeting. Warden may substitute guard in S2.5. */
	int32 GetCombatPoolCurrent() const;
	int32 GetCombatPoolMax() const;
	/** Apply integer player damage and return the authoritative amount removed. */
	int32 ApplyPlayerDamage(int32 Amount, AActor* DamageInstigator);
	/** Atomically replace combat-facing stats with the fixed CSV Warden profile. */
	bool ConfigureAsWarden(const FCombatConfigRow& Row, int32 ResolveTokens);
	bool IsWarden() const { return bWarden; }
	EUegameWardenPhase GetWardenPhase() const;
	int32 GetWardenGuard() const { return bWarden ? WardenGuard : 0; }
	int32 GetWardenMaxGuard() const { return bWarden ? WardenMaxGuard : 0; }
	int32 GetPlayerDamageMultiplierPercent() const;
	float GetAssignedContactDamage() const { return AssignedContactDamage; }
	float GetCommittedContactDamage() const { return ContactDamage; }
	bool IsRoomChallengeModified() const { return bRoomChallengeModified; }

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

#if !UE_BUILD_SHIPPING
	/** Development-only composite probe for cancellation windows that external commands cannot
	 *  reach between the 0.05s behavior tick and 0.5s committed-damage tick. */
	void RunBehaviorContractProbeForTests(int32 Mode);
	/** One-shot atomic-config seam: 0=normal, 1=fail after stat mutation and require rollback. */
	void SetWardenConfigFaultModeForTests(int32 Mode)
	{
		WardenConfigFaultModeForTests = FMath::Clamp(Mode, 0, 1);
	}
#endif

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class ADungeonSpawner;
	void SetSpawnOrdinal(int32 InOrdinal) { SpawnOrdinal = InOrdinal; }
	/** Stop damage, movement, collision and visibility synchronously, then queue destroy. */
	bool NeutralizeForFloorExit(bool& bOutDestroyQueued);
	bool CanApplyRoomChallengeModifier() const;
	bool ApplyRoomChallengeModifier();
	bool RollbackRoomChallengeModifier();
	void PursueTick();
	/** One-shot same-room wake from a normal acquisition; alert transitions never recurse. */
	void AlertIdleRoomPeers(APawn* Player);
	void BehaviorTick();
	void ResetBehaviorState(bool bDead = false);
	void ApplyBehaviorPresentation();
	void ClearBehaviorPulse();
	void DriveBehaviorMovement(AAIController* AI, APawn* Player);
	int64 GetBehaviorNowMs() const;
	void ResetWardenState();

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

	UPROPERTY()
	TObjectPtr<UStaticMesh> GruntVisualMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RunnerVisualMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> BruteVisualMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> WardenVisualMesh;

	/** Persistent state-driven world label; it mirrors behavior truth and allocates no per-tick UObject. */
	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UTextRenderComponent> BehaviorText;

	/** Finale-only ground aura and light. Hidden on every ordinary enemy. */
	UPROPERTY(VisibleAnywhere, Category="Presentation")
	TObjectPtr<UStaticMeshComponent> WardenAuraMesh;

	UPROPERTY(VisibleAnywhere, Category="Presentation")
	TObjectPtr<UPointLightComponent> WardenLight;

	/** Dynamic material instance for the hit flash (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WardenAuraMID;

	FTimerHandle PursueTimer;
	FTimerHandle BehaviorTimer;
	FTimerHandle BehaviorPulseTimer;
	FTimerHandle FlashTimer;

	int32 RoomIndex = -1;
	int32 SpawnOrdinal = INDEX_NONE;
	/** M7A.2: archetype identity (m6::TypeId order); INDEX_NONE = no M6 assignment.
	 *  Reset by InitEnemy, set only by ApplyArchetype. Not reflected: presentation-facing
	 *  runtime state, never serialized and never exposed for mutation. */
	int32 ArchetypeTypeId = INDEX_NONE;
	TWeakObjectPtr<ADungeonSpawner> SpawnerRef;

	float ContactDamage = 10.0f;
	float AssignedContactDamage = 10.0f;
	float DamageInterval = 1.0f;
	/** Contact reach: capsule radii sum + slack; set from capsule sizes at spawn. */
	float ContactRange = 130.0f;
	double LastContactDamageTime = -1000.0;
	float BaseMoveSpeed = 240.0f;

	// Mirrored plain fields for m8enemy::State; engine-independent types stay out of reflection headers.
	int32 BehaviorPhase = 0;
	int64 BehaviorPhaseStartedMs = 0;
	int32 BehaviorCircleDirection = 1;
	FVector BehaviorDashTarget = FVector::ZeroVector;
	bool bBehaviorDashTargetValid = false;
	uint32 BehaviorCommitSerial = 0;
	bool bPendingCommittedHit = false;
	float PendingCommittedHitRange = 0.0f;
	int32 BehaviorDurationNumerator = 1;
	int32 BehaviorDurationDenominator = 1;

	// --- Run 2.5 perception state (from the DataTable row via InitEnemy) ---
	/** Idle (false, stands in place) vs Chasing (true). */
	bool bChasing = false;
	float AggroRange = 900.0f;
	float LeashRange = 1400.0f;

	bool bLoggedFirstMove = false;
	bool bNeutralizedForFloorExit = false;
	bool bRoomChallengeModified = false;
	float PreChallengeContactDamage = 0.0f;
	float PreChallengeBaseMoveSpeed = 0.0f;
	int32 PreChallengeDurationNumerator = 1;
	int32 PreChallengeDurationDenominator = 1;

	// Mirrored POD fields for m8finale::State; the standard-library core stays out of UHT headers.
	bool bWarden = false;
	int32 WardenMaxGuard = 0;
	int32 WardenGuard = 0;
	int64 WardenGuardBrokenAtMs = -1;
	int64 WardenStaggerMs = 0;
	int32 WardenStaggerDamagePct = 100;
	bool bWardenExposureInitialized = false;
#if !UE_BUILD_SHIPPING
	int32 BehaviorFaultModeForTests = 0;
	int32 WardenConfigFaultModeForTests = 0;
#endif
};
