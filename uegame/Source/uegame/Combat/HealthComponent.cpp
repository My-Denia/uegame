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
