// BuildSynergyComponent.h - additive M8A runtime binding on the player.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "BuildSynergyComponent.generated.h"

struct FBuildSynergySwingResult
{
	int32 ExecutionerDamage = 0;
	int32 TempoDamage = 0;
	int32 BulwarkDamage = 0;
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
	/** Resolve one already-accepted ordinary melee swing. Cooldown rejection never calls this. */
	FBuildSynergySwingResult ResolveAcceptedSwing(
		int32 BaseDamage, int32 AttackCooldownMs, int32 Hits, double NowSeconds,
		bool bHasPrimary, int32 PrimaryPreHP, int32 PrimaryMaxHP, int32 PrimaryHPAfterBase);

	int32 GetExecutionerRank() const { return ExecutionerRank; }
	int32 GetTempoRank() const { return TempoRank; }
	int32 GetBulwarkRank() const { return BulwarkRank; }
	int32 GetTempoChain() const { return TempoChain; }
	int32 GetTempoThreshold() const { return TempoRank >= 2 ? 2 : 3; }
	bool IsCounterReady() const;
	float GetCounterRemainingSeconds() const;
	FString GetFeedbackText() const;

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
};
