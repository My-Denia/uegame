// PresentationFeedbackComponent.h - bounded, event-derived player feedback.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "PresentationFeedbackComponent.generated.h"

class UAudioComponent;
class USoundWaveProcedural;

UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class UEGAME_API UPresentationFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPresentationFeedbackComponent();

	/** One post-transaction outcome for every cooldown-accepted swing. */
	void EmitAttackResolved(int32 HitCount, int32 KillCount);
	void EmitPlayerDamaged(float Amount);
	void EmitRewardOffered();
	void EmitRewardChosen(const FString& AffixName);
	void EmitBuildProc(const FString& ProcText);
	void EmitFloorStarted(int32 FloorIndex, bool bFinale);
	void EmitWardenBroken();
	void EmitRunWon();
	void EmitRunFailed();

	/** Run-generation boundary: stop audio and clear every transient presentation field. */
	void ResetForNewRun();

	FString GetTransientText() const;
	float GetSwingAlpha() const;
	float GetHitMarkerAlpha() const;
	bool WasLastSwingKill() const { return bLastSwingKill; }
	int32 GetRetainedAudioCount() const { return ActiveAudio.Num(); }
	uint32 GetPresentationGeneration() const { return PresentationGeneration; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void EmitCue(uint8 CueValue, const FString& Text, int32 Priority, double TextDuration);
	void SetTransientText(const FString& Text, int32 Priority, double DurationSeconds);
	void StopAndClearAudio();
	void CleanupExpiredAudio(double NowSeconds);
	double NowSeconds() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundWaveProcedural>> ActiveWaves;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> ActiveAudio;
	TArray<double> ActiveAudioExpiresAt;

	FString TransientText;
	double TransientTextExpiresAt = -1.0;
	int32 TransientPriority = 0;
	double SwingExpiresAt = -1.0;
	double HitMarkerExpiresAt = -1.0;
	bool bLastSwingKill = false;
	TArray<double> LastCueTimes;
	uint32 PresentationGeneration = 0;
};
