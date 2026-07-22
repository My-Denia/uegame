// PresentationFeedbackComponent.cpp - see header.

#include "PresentationFeedbackComponent.h"

#include "Components/AudioComponent.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"

#include "m8_presentation.hpp"

namespace
{
	constexpr int32 kMaxRetainedAudio = 12;
	constexpr double kAudioSafetyMarginSeconds = 0.25;
	constexpr double kMinimumRetriggerSeconds = 0.04;
	constexpr double kSwingVisibleSeconds = 0.18;
	constexpr double kHitMarkerVisibleSeconds = 0.20;
}

UPresentationFeedbackComponent::UPresentationFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	LastCueTimes.Init(-1000.0, static_cast<int32>(m8presentation::Cue::Count));
}

double UPresentationFeedbackComponent::NowSeconds() const
{
	return FPlatformTime::Seconds();
}

void UPresentationFeedbackComponent::CleanupExpiredAudio(double Now)
{
	for (int32 Index = ActiveAudio.Num() - 1; Index >= 0; --Index)
	{
		UAudioComponent* Audio = ActiveAudio.IsValidIndex(Index) ? ActiveAudio[Index] : nullptr;
		const double ExpiresAt = ActiveAudioExpiresAt.IsValidIndex(Index)
			? ActiveAudioExpiresAt[Index] : -1.0;
		if (Now < ExpiresAt && Audio && Audio->IsPlaying())
		{
			continue;
		}
		if (Audio)
		{
			Audio->Stop();
			Audio->DestroyComponent();
		}
		ActiveAudio.RemoveAt(Index);
		ActiveWaves.RemoveAt(Index);
		ActiveAudioExpiresAt.RemoveAt(Index);
	}
}

void UPresentationFeedbackComponent::StopAndClearAudio()
{
	for (UAudioComponent* Audio : ActiveAudio)
	{
		if (Audio)
		{
			Audio->Stop();
			Audio->DestroyComponent();
		}
	}
	ActiveAudio.Reset();
	ActiveWaves.Reset();
	ActiveAudioExpiresAt.Reset();
}

void UPresentationFeedbackComponent::ResetForNewRun()
{
	StopAndClearAudio();
	TransientText.Reset();
	TransientTextExpiresAt = -1.0;
	TransientPriority = 0;
	SwingExpiresAt = -1.0;
	HitMarkerExpiresAt = -1.0;
	bLastSwingKill = false;
	LastCueTimes.Init(-1000.0, static_cast<int32>(m8presentation::Cue::Count));
	++PresentationGeneration;
	UE_LOG(LogTemp, Display, TEXT("[Presentation] reset generation=%u retained=0"),
		PresentationGeneration);
}

void UPresentationFeedbackComponent::SetTransientText(
	const FString& Text, int32 Priority, double DurationSeconds)
{
	const double Now = NowSeconds();
	if (Text.IsEmpty())
	{
		return;
	}
	if (Now >= TransientTextExpiresAt || Priority >= TransientPriority)
	{
		TransientText = Text;
		TransientPriority = Priority;
		TransientTextExpiresAt = Now + FMath::Max(0.05, DurationSeconds);
	}
}

void UPresentationFeedbackComponent::EmitCue(
	uint8 CueValue, const FString& Text, int32 Priority, double TextDuration)
{
	const m8presentation::Cue Cue = static_cast<m8presentation::Cue>(CueValue);
	const int32 CueIndex = static_cast<int32>(Cue);
	if (!LastCueTimes.IsValidIndex(CueIndex))
	{
		return;
	}

	const double Now = NowSeconds();
	SetTransientText(Text, Priority, TextDuration);
	if (Now - LastCueTimes[CueIndex] < kMinimumRetriggerSeconds)
	{
		UE_LOG(LogTemp, Display, TEXT("[Presentation] cue=%hs audio=suppressed-retrigger generation=%u"),
			m8presentation::cue_name(Cue), PresentationGeneration);
		return;
	}
	LastCueTimes[CueIndex] = Now;
	CleanupExpiredAudio(Now);
	if (ActiveAudio.Num() >= kMaxRetainedAudio)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Presentation] cue=%hs audio=suppressed-cap retained=%d generation=%u"),
			m8presentation::cue_name(Cue), ActiveAudio.Num(), PresentationGeneration);
		return;
	}

	const m8presentation::PcmBuffer Pcm = m8presentation::generate_pcm(Cue);
	if (Pcm.samples.empty())
	{
		return;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	if (!Wave)
	{
		return;
	}
	Wave->SetSampleRate(static_cast<uint32>(Pcm.sample_rate));
	Wave->SetNumFrames(static_cast<uint32>(Pcm.samples.size()));
	Wave->NumChannels = 1;
	Wave->SampleByteSize = sizeof(int16);
	Wave->Duration = static_cast<float>(Pcm.samples.size()) / static_cast<float>(Pcm.sample_rate);
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.samples.data()),
		static_cast<int32>(Pcm.samples.size() * sizeof(int16)));

	UAudioComponent* Audio = UGameplayStatics::CreateSound2D(
		this, Wave, 0.72f, 1.0f, 0.0f, nullptr,
		/*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/false);
	if (!Audio)
	{
		return;
	}
	Audio->bIsUISound = true;
	ActiveWaves.Add(Wave);
	ActiveAudio.Add(Audio);
	ActiveAudioExpiresAt.Add(Now + Wave->Duration + kAudioSafetyMarginSeconds);
	Audio->Play();
	UE_LOG(LogTemp, Display,
		TEXT("[Presentation] cue=%hs samples=%d signature=%llu retained=%d generation=%u"),
		m8presentation::cue_name(Cue), Pcm.samples.size(),
		static_cast<unsigned long long>(Pcm.signature), ActiveAudio.Num(), PresentationGeneration);
}

void UPresentationFeedbackComponent::EmitAttackResolved(int32 HitCount, int32 KillCount)
{
	const double Now = NowSeconds();
	SwingExpiresAt = Now + kSwingVisibleSeconds;
	bLastSwingKill = KillCount > 0;
	if (KillCount > 0)
	{
		HitMarkerExpiresAt = Now + kHitMarkerVisibleSeconds;
		EmitCue(static_cast<uint8>(m8presentation::Cue::EnemyDeath), TEXT("ENEMY DOWN"), 70, 0.65);
	}
	else if (HitCount > 0)
	{
		HitMarkerExpiresAt = Now + kHitMarkerVisibleSeconds;
		EmitCue(static_cast<uint8>(m8presentation::Cue::Hit), TEXT("HIT"), 30, 0.25);
	}
	else
	{
		EmitCue(static_cast<uint8>(m8presentation::Cue::AttackMiss), TEXT("MISS"), 10, 0.20);
	}
}

void UPresentationFeedbackComponent::EmitPlayerDamaged(float Amount)
{
	EmitCue(static_cast<uint8>(m8presentation::Cue::PlayerDamage),
		FString::Printf(TEXT("-%d HP"), FMath::Max(1, FMath::RoundToInt(Amount))), 50, 0.45);
}

void UPresentationFeedbackComponent::EmitRewardOffered()
{
	EmitCue(static_cast<uint8>(m8presentation::Cue::RewardOffered), TEXT("REWARD READY"), 80, 0.75);
}

void UPresentationFeedbackComponent::EmitRewardChosen(const FString& AffixName)
{
	EmitCue(static_cast<uint8>(m8presentation::Cue::RewardChosen),
		FString::Printf(TEXT("BUILD + %s"), *AffixName), 85, 1.0);
}

void UPresentationFeedbackComponent::EmitBuildProc(const FString& ProcText)
{
	// The existing synergy component owns the visible proc text. This component adds audio and
	// only uses the same reconciled text as a fallback transient if no stronger cue is active.
	EmitCue(static_cast<uint8>(m8presentation::Cue::BuildProc), ProcText, 65, 0.65);
}

void UPresentationFeedbackComponent::EmitRunWon()
{
	EmitCue(static_cast<uint8>(m8presentation::Cue::Victory), TEXT("RUN WON"), 100, 2.0);
}

void UPresentationFeedbackComponent::EmitRunFailed()
{
	EmitCue(static_cast<uint8>(m8presentation::Cue::Defeat), TEXT("RUN FAILED"), 100, 2.0);
}

FString UPresentationFeedbackComponent::GetTransientText() const
{
	return NowSeconds() <= TransientTextExpiresAt ? TransientText : FString();
}

float UPresentationFeedbackComponent::GetSwingAlpha() const
{
	return FMath::Clamp(static_cast<float>((SwingExpiresAt - NowSeconds()) / kSwingVisibleSeconds), 0.0f, 1.0f);
}

float UPresentationFeedbackComponent::GetHitMarkerAlpha() const
{
	return FMath::Clamp(static_cast<float>((HitMarkerExpiresAt - NowSeconds()) / kHitMarkerVisibleSeconds), 0.0f, 1.0f);
}

void UPresentationFeedbackComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	CleanupExpiredAudio(NowSeconds());
}

void UPresentationFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAndClearAudio();
	Super::EndPlay(EndPlayReason);
}
