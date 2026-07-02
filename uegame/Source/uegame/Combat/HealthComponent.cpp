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
	if (bDead || Amount <= 0.0f)
	{
		return;
	}
	const float OldHP = CurrentHP;
	CurrentHP = FMath::Max(0.0f, CurrentHP - Amount);

	// Evidence line: every damage event traceable (amount + source + HP delta).
	UE_LOG(LogTemp, Display, TEXT("[Combat] %s took %.0f dmg from %s | HP %.0f -> %.0f"),
		*GetNameSafe(GetOwner()), Amount, *GetNameSafe(DamageInstigator), OldHP, CurrentHP);

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
