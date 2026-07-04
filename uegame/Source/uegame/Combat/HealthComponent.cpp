// HealthComponent.cpp - see header.

#include "HealthComponent.h"

#include "GameFramework/Actor.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealthComponent::Init(float InMaxHP)
{
	MaxHP = FMath::Max(1.0f, InMaxHP);
	CurrentHP = MaxHP;
	bDead = false;
}

void UHealthComponent::TakeDamage(float Amount, AActor* DamageInstigator)
{
	if (bDead || bInvincible || Amount <= 0.0f)
	{
		// bInvincible is a forensic hold (Dungeon.SetHP <v> [holdSec]) only; suppressing damage
		// here means no HP drop => no OnDeath => no queued run-fail, so a survival/WalkFar probe
		// runs uninterrupted. Never set in normal play.
		return;
	}
	const float OldHP = CurrentHP;
	CurrentHP = FMath::Max(0.0f, CurrentHP - Amount);

	// Evidence line: every damage event traceable (amount + source + HP delta).
	UE_LOG(LogTemp, Display, TEXT("[Combat] %s took %.0f dmg from %s | HP %.0f -> %.0f"),
		*GetNameSafe(GetOwner()), Amount, *GetNameSafe(DamageInstigator), OldHP, CurrentHP);

	// Run 2.5 feedback hook: fire before the death check so a lethal hit still flashes.
	OnDamaged.Broadcast(Amount, DamageInstigator);

	if (CurrentHP <= 0.0f)
	{
		bDead = true;
		OnDeath.Broadcast(GetOwner());
	}
}

void UHealthComponent::SetHP(float NewHP)
{
	const float OldHP = CurrentHP;
	CurrentHP = FMath::Clamp(NewHP, 0.0f, MaxHP);
	UE_LOG(LogTemp, Display, TEXT("[Combat] %s SetHP %.0f -> %.0f (forensic/managed)"),
		*GetNameSafe(GetOwner()), OldHP, CurrentHP);
	if (CurrentHP <= 0.0f && !bDead)
	{
		bDead = true;
		OnDeath.Broadcast(GetOwner());
	}
}

void UHealthComponent::Revive(float NewHP)
{
	bDead = false;
	CurrentHP = (NewHP < 0.0f) ? MaxHP : FMath::Clamp(NewHP, 1.0f, MaxHP);
	UE_LOG(LogTemp, Display, TEXT("[Combat] %s revived, HP=%.0f/%.0f"),
		*GetNameSafe(GetOwner()), CurrentHP, MaxHP);
}

void UHealthComponent::SetMaxHP(float NewMaxHP, bool bTopUpCurrent)
{
	const float OldMax = MaxHP;
	const float NewMax = FMath::Max(1.0f, NewMaxHP);   // same floor Init uses
	const float Delta  = NewMax - OldMax;
	MaxHP = NewMax;

	if (bTopUpCurrent && Delta > 0.0f)
	{
		// A MaxHP increase adds the same delta to current HP (immediate-reward rule, contract #5).
		CurrentHP = FMath::Min(CurrentHP + Delta, NewMax);
	}
	else
	{
		// Decrease, or no top-up requested (run reset): clamp current down to the new ceiling, no refund.
		CurrentHP = FMath::Min(CurrentHP, NewMax);
	}

	// Deliberately no OnDeath/OnDamaged and no bDead change: this is a stat re-resolve, not damage/heal.
	// (A pick only happens on a cleared floor with the player alive; the flow never re-resolves a corpse.)
	UE_LOG(LogTemp, Display, TEXT("[Loadout] %s maxHP %.0f -> %.0f (topUp=%s) | HP %.0f/%.0f"),
		*GetNameSafe(GetOwner()), OldMax, NewMax,
		bTopUpCurrent ? TEXT("yes") : TEXT("no"), CurrentHP, MaxHP);
}
