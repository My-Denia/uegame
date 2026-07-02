// CombatComponent.cpp - see header.

#include "CombatComponent.h"

#include "CombatConfig.h"
#include "DungeonEnemy.h"
#include "HealthComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCombatComponent::TryAttack()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	const double Now = World->GetTimeSeconds();
	const double Remaining = Cfg.PlayerAttackCooldown - (Now - LastAttackTime);
	if (Remaining > 0.0)
	{
		// Evidence: cooldown gate (plan-audit delta #4).
		UE_LOG(LogTemp, Display, TEXT("[Combat] attack REJECTED: on cooldown (%.2fs left)"), Remaining);
		return;
	}
	LastAttackTime = Now;

	// Sphere in front of the owner: center = loc + forward * R/2, radius = R/2.
	const float R = Cfg.PlayerAttackRange;
	const FVector Center = Owner->GetActorLocation() + Owner->GetActorForwardVector() * (R * 0.5f);
	const float Radius = R * 0.5f;

	int32 Hits = 0;
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		ADungeonEnemy* Enemy = *It;
		if (!IsValid(Enemy))
		{
			continue;
		}
		if (FVector::Dist(Enemy->GetActorLocation(), Center) <= Radius)
		{
			if (UHealthComponent* HP = Enemy->FindComponentByClass<UHealthComponent>())
			{
				HP->TakeDamage(Cfg.PlayerAttackDamage, Owner);
				++Hits;
			}
		}
	}

	if (Hits == 0)
	{
		// Evidence: range check (plan-audit delta #4) - swing landed on nothing.
		UE_LOG(LogTemp, Display, TEXT("[Combat] attack hit no targets in range (range=%.0f)"), R);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[Combat] attack hit %d enemies (dmg=%.0f each)"), Hits, Cfg.PlayerAttackDamage);
	}
}
