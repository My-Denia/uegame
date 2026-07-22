// BuildSynergyComponent.cpp - see header.

#include "BuildSynergyComponent.h"

#include "DungeonEnemy.h"
#include "HealthComponent.h"
#include "LoadoutComponent.h"
#include "Engine/World.h"

// The engine-independent implementation is compiled once by M8BuildSynergyCore.cpp.
#include "m8_build_synergy.hpp"

namespace
{
	m8::Ranks MakeRanks(int32 Executioner, int32 Tempo, int32 Bulwark)
	{
		m8::Ranks Ranks;
		Ranks.executioner = Executioner;
		Ranks.tempo = Tempo;
		Ranks.bulwark = Bulwark;
		return Ranks;
	}

	m8::State MakeState(int32 TempoChain, bool bCounterActive, int64 CounterExpiresAtMs,
		uint64 Fingerprint)
	{
		m8::State State;
		State.tempo_chain = TempoChain;
		State.counter_active = bCounterActive;
		State.counter_expires_at_ms = CounterExpiresAtMs;
		State.loadout_fingerprint = Fingerprint;
		return State;
	}
}

UBuildSynergyComponent::UBuildSynergyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBuildSynergyComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UHealthComponent* Health = GetOwner()
		? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		Health->OnDamaged.AddDynamic(this, &UBuildSynergyComponent::HandleOwnerDamaged);
		Health->OnDeath.AddDynamic(this, &UBuildSynergyComponent::HandleOwnerDeath);
	}
	RefreshFromLoadout();
}

int64 UBuildSynergyComponent::GetNowMs() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::RoundToInt64(World->GetTimeSeconds() * 1000.0) : 0;
}

void UBuildSynergyComponent::SetFeedback(const FString& Text, double NowSeconds)
{
	FeedbackText = Text;
	FeedbackExpiresAt = NowSeconds + 1.25;
}

void UBuildSynergyComponent::ClearRanksAndTransient()
{
	ExecutionerRank = 0;
	TempoRank = 0;
	BulwarkRank = 0;
	TempoChain = 0;
	bCounterActive = false;
	CounterExpiresAtMs = 0;
	LoadoutFingerprint = 0;
	FeedbackText.Reset();
	FeedbackExpiresAt = -1.0;
}

void UBuildSynergyComponent::RefreshFromLoadout()
{
	const ULoadoutComponent* Loadout = GetOwner()
		? GetOwner()->FindComponentByClass<ULoadoutComponent>() : nullptr;
	if (!Loadout)
	{
		ClearRanksAndTransient();
		return;
	}

	int32 NewExecutioner = 0;
	int32 NewTempo = 0;
	int32 NewBulwark = 0;
	Loadout->GetSynergyFamilyRanks(NewExecutioner, NewTempo, NewBulwark);
	const uint64 NewFingerprint = Loadout->GetChosenAffixFingerprint();

	m8::State State = MakeState(TempoChain, bCounterActive, CounterExpiresAtMs, LoadoutFingerprint);
	const bool bReset = m8::sync_loadout(
		State, MakeRanks(NewExecutioner, NewTempo, NewBulwark), NewFingerprint);

	ExecutionerRank = m8::clamp_rank(NewExecutioner);
	TempoRank = m8::clamp_rank(NewTempo);
	BulwarkRank = m8::clamp_rank(NewBulwark);
	TempoChain = State.tempo_chain;
	bCounterActive = State.counter_active;
	CounterExpiresAtMs = State.counter_expires_at_ms;
	LoadoutFingerprint = State.loadout_fingerprint;

	if (bReset)
	{
		if (LoadoutFingerprint == 0)
		{
			FeedbackText.Reset();
			FeedbackExpiresAt = -1.0;
		}
		else if (const UWorld* World = GetWorld())
		{
			SetFeedback(FString::Printf(TEXT("BUILD ONLINE  E%d / T%d / B%d"),
				ExecutionerRank, TempoRank, BulwarkRank), World->GetTimeSeconds());
		}
		UE_LOG(LogTemp, Display,
			TEXT("[BuildIdentity] fingerprint=%llu E=%d T=%d B=%d transientReset=true"),
			static_cast<unsigned long long>(LoadoutFingerprint),
			ExecutionerRank, TempoRank, BulwarkRank);
	}
}

void UBuildSynergyComponent::HandleOwnerDamaged(float Amount, AActor* DamageInstigator)
{
	if (Amount <= 0.0f)
	{
		return;
	}
	RefreshFromLoadout();

	m8::State State = MakeState(TempoChain, bCounterActive, CounterExpiresAtMs, LoadoutFingerprint);
	const m8::DamageEventResult Result = m8::on_owner_damage(
		State, MakeRanks(ExecutionerRank, TempoRank, BulwarkRank),
		FMath::Max(1, FMath::RoundToInt(Amount)), Cast<ADungeonEnemy>(DamageInstigator) != nullptr,
		GetNowMs());
	TempoChain = State.tempo_chain;
	bCounterActive = State.counter_active;
	CounterExpiresAtMs = State.counter_expires_at_ms;

	const UWorld* World = GetWorld();
	if (World && Result.counter_opened && Result.tempo_reset)
	{
		SetFeedback(TEXT("TEMPO RESET | BULWARK COUNTER READY"), World->GetTimeSeconds());
	}
	else if (World && Result.counter_opened)
	{
		SetFeedback(TEXT("BULWARK COUNTER READY"), World->GetTimeSeconds());
	}
	else if (World && Result.tempo_reset)
	{
		SetFeedback(TEXT("TEMPO RESET - DAMAGED"), World->GetTimeSeconds());
	}
	UE_LOG(LogTemp, Display,
		TEXT("[BuildState] event=ownerDamage amount=%.0f enemy=%s tempoReset=%s counterOpened=%s tempo=%d counter=%s remainingMs=%lld"),
		Amount, Cast<ADungeonEnemy>(DamageInstigator) ? TEXT("true") : TEXT("false"),
		Result.tempo_reset ? TEXT("true") : TEXT("false"),
		Result.counter_opened ? TEXT("true") : TEXT("false"),
		TempoChain, bCounterActive ? TEXT("true") : TEXT("false"),
		static_cast<long long>(FMath::Max<int64>(0, CounterExpiresAtMs - GetNowMs())));
}

void UBuildSynergyComponent::HandleOwnerDeath(AActor* /*DeadActor*/)
{
	// The result screen represents the ended run, so authoritative M5 ranks stay visible.
	// Explicit new-run ResetForNewRun clears the picks/fingerprint and therefore the ranks.
	TempoChain = 0;
	bCounterActive = false;
	CounterExpiresAtMs = 0;
	FeedbackText.Reset();
	FeedbackExpiresAt = -1.0;
	UE_LOG(LogTemp, Display,
		TEXT("[BuildState] event=death E=%d T=%d B=%d ranksRetained=true transientReset=true"),
		ExecutionerRank, TempoRank, BulwarkRank);
}

FBuildSynergySwingResult UBuildSynergyComponent::ResolveAcceptedSwing(
	int32 BaseDamage, int32 AttackCooldownMs, int32 Hits, double NowSeconds,
	bool bHasPrimary, int32 PrimaryPreHP, int32 PrimaryMaxHP, int32 PrimaryHPAfterBase)
{
	m8::State State = MakeState(TempoChain, bCounterActive, CounterExpiresAtMs, LoadoutFingerprint);
	m8::SwingInput Input;
	Input.ranks = MakeRanks(ExecutionerRank, TempoRank, BulwarkRank);
	Input.base_damage = BaseDamage;
	Input.attack_cooldown_ms = AttackCooldownMs;
	Input.hits = Hits;
	Input.now_ms = FMath::RoundToInt64(NowSeconds * 1000.0);
	Input.has_primary = bHasPrimary;
	Input.primary_current_hp = PrimaryPreHP;
	Input.primary_max_hp = PrimaryMaxHP;
	Input.primary_hp_after_base = PrimaryHPAfterBase;
	const m8::SwingResult Out = m8::resolve_accepted_swing(State, Input);

	TempoChain = State.tempo_chain;
	bCounterActive = State.counter_active;
	CounterExpiresAtMs = State.counter_expires_at_ms;

	FBuildSynergySwingResult Result;
	Result.ExecutionerDamage = Out.executioner_damage;
	Result.TempoDamage = Out.tempo_damage;
	Result.BulwarkDamage = Out.bulwark_damage;
	if (Out.heal > 0)
	{
		if (UHealthComponent* Health = GetOwner()
			? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr)
		{
			Result.Heal = FMath::RoundToInt(Health->Heal(static_cast<float>(Out.heal)));
		}
	}
	Result.CooldownRefundMs = Out.cooldown_refund_ms;
	Result.bExecutionerQualified = Out.executioner_qualified;
	Result.bExecutionerApplied = Out.executioner_applied;
	Result.bTempoProc = Out.tempo_proc;
	Result.bTempoApplied = Out.tempo_applied;
	Result.bTempoResetByMiss = Out.tempo_reset_by_miss;
	Result.bCounterConsumed = Out.counter_consumed;
	Result.bBulwarkApplied = Out.bulwark_applied;

	TArray<FString> Cues;
	if (Out.executioner_applied) { Cues.Add(TEXT("EXECUTIONER!")); }
	if (Out.tempo_proc) { Cues.Add(Out.tempo_applied ? TEXT("TEMPO!") : TEXT("TEMPO SPENT")); }
	if (Out.counter_consumed) { Cues.Add(Out.bulwark_applied ? TEXT("BULWARK COUNTER!") : TEXT("COUNTER SPENT")); }
	if (Result.Heal > 0) { Cues.Add(FString::Printf(TEXT("+%d HP"), Result.Heal)); }
	if (Out.cooldown_refund_ms > 0) { Cues.Add(TEXT("COOLDOWN REFUND")); }
	if (Out.tempo_reset_by_miss) { Cues.Add(TEXT("TEMPO RESET - MISS")); }
	if (!Cues.IsEmpty())
	{
		SetFeedback(FString::Join(Cues, TEXT(" | ")), NowSeconds);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[BuildState] event=swing hits=%d E=%d T=%d B=%d heal=%d refundMs=%d tempo=%d missReset=%s counterConsumed=%s counter=%s"),
		Hits, Result.ExecutionerDamage, Result.TempoDamage, Result.BulwarkDamage,
		Result.Heal, Result.CooldownRefundMs, TempoChain,
		Result.bTempoResetByMiss ? TEXT("true") : TEXT("false"),
		Result.bCounterConsumed ? TEXT("true") : TEXT("false"),
		bCounterActive ? TEXT("true") : TEXT("false"));
	return Result;
}

bool UBuildSynergyComponent::IsCounterReady() const
{
	return BulwarkRank > 0 && bCounterActive && GetNowMs() <= CounterExpiresAtMs;
}

float UBuildSynergyComponent::GetCounterRemainingSeconds() const
{
	return IsCounterReady()
		? static_cast<float>(CounterExpiresAtMs - GetNowMs()) / 1000.0f : 0.0f;
}

FString UBuildSynergyComponent::GetFeedbackText() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() <= FeedbackExpiresAt ? FeedbackText : FString();
}
