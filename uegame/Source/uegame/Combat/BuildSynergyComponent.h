// BuildSynergyComponent.h - additive M8A runtime binding on the player.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "BuildSynergyComponent.generated.h"

struct FBuildSynergySwingPlan
{
	int32 ExecutionerDamage = 0;
	int32 TempoDamage = 0;
	int32 BulwarkDamage = 0;
	int32 HealRequest = 0;
	int32 AttackCooldownMs = 0;
	int32 ExecutionerRank = 0;
	int32 TempoChainAfter = 0;
	bool bExecutionerQualified = false;
	bool bTempoProc = false;
	bool bTempoResetByMiss = false;
	bool bCounterConsumed = false;
};

struct FBuildSynergySwingResult
{
	int32 ExecutionerDamage = 0;
	int32 TempoDamage = 0;
	int32 BulwarkDamage = 0;
	int32 ExecutionerAppliedDamage = 0;
	int32 TempoAppliedDamage = 0;
	int32 BulwarkAppliedDamage = 0;
	int32 Heal = 0;
	int32 CooldownRefundMs = 0;
	bool bExecutionerQualified = false;
	bool bExecutionerApplied = false;
	bool bTempoProc = false;
	bool bTempoApplied = false;
	bool bTempoResetByMiss = false;
	bool bCounterConsumed = false;
	bool bBulwarkApplied = false;
};

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class UEGAME_API UBuildSynergyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildSynergyComponent();

	/** Re-read the authoritative selected-affix families from ULoadoutComponent. */
	void RefreshFromLoadout();
	/** Emit raw ordered proc intents and consume Tempo/counter state; owns no target HP. */
	FBuildSynergySwingPlan PlanAcceptedSwing(
		int32 BaseDamage, int32 AttackCooldownMs, int32 Hits, double NowSeconds,
		bool bHasPrimary, int32 PrimaryPreHP, int32 PrimaryMaxHP, int32 PrimaryHPAfterBase);
	/** Reconcile the raw plan against authoritative E/T/B damage and final target death. */
	FBuildSynergySwingResult ReconcileAcceptedSwing(
		const FBuildSynergySwingPlan& Plan,
		int32 ExecutionerAppliedDamage, int32 TempoAppliedDamage,
		int32 BulwarkAppliedDamage, int32 PrimaryPoolAfter,
		bool bPrimaryDeadAfter, double NowSeconds);

	int32 GetExecutionerRank() const { return ExecutionerRank; }
	int32 GetTempoRank() const { return TempoRank; }
	int32 GetBulwarkRank() const { return BulwarkRank; }
	int32 GetTempoChain() const { return TempoChain; }
	int32 GetTempoThreshold() const { return TempoRank >= 2 ? 2 : 3; }
	bool IsCounterReady() const;
	float GetCounterRemainingSeconds() const;
	FString GetFeedbackText() const;

#if !UE_BUILD_SHIPPING
	/** Focused adapter seam; runtime attack/proc application still uses the production transaction. */
	void SetStateForTests(int32 InExecutioner, int32 InTempo, int32 InBulwark,
		int32 InTempoChain, int32 CounterRemainingMs);
#endif

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleOwnerDamaged(float Amount, AActor* DamageInstigator);
	UFUNCTION()
	void HandleOwnerDeath(AActor* DeadActor);

	void ClearRanksAndTransient();
	void SetFeedback(const FString& Text, double NowSeconds);
	int64 GetNowMs() const;

	int32 ExecutionerRank = 0;
	int32 TempoRank = 0;
	int32 BulwarkRank = 0;
	int32 TempoChain = 0;
	bool bCounterActive = false;
	int64 CounterExpiresAtMs = 0;
	uint64 LoadoutFingerprint = 0;
	FString FeedbackText;
	double FeedbackExpiresAt = -1.0;
#if !UE_BUILD_SHIPPING
	/** Preserve the focused injected state through exactly one production TryAttack refresh. */
	bool bSkipNextLoadoutRefreshForTests = false;
#endif
};
