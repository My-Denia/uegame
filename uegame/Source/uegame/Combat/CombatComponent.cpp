// CombatComponent.cpp - see header.

#include "CombatComponent.h"

#include "CombatConfig.h"
#include "DungeonEnemy.h"
#include "HealthComponent.h"
#include "LoadoutComponent.h"
#include "DrawDebugHelpers.h"
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

	// M5: when the owner carries a loadout component, use its RESOLVED attack interval + damage (the build's
	// picks applied to the base); otherwise fall back to the raw CSV so bare test maps behave exactly as
	// before. attack_ms is milliseconds -> seconds for the cooldown. Range stays CSV-driven (not a loadout stat).
	float AttackCooldown = Cfg.PlayerAttackCooldown;
	float AttackDamage   = Cfg.PlayerAttackDamage;
	bool  bFromLoadout   = false;
	if (const ULoadoutComponent* LC = Owner->FindComponentByClass<ULoadoutComponent>())
	{
		if (LC->HasResolvedStats())
		{
			AttackCooldown = static_cast<float>(LC->GetResolvedAttackMs()) / 1000.0f;
			AttackDamage   = static_cast<float>(LC->GetResolvedDamage());
			bFromLoadout   = true;
		}
	}

	const double Now = World->GetTimeSeconds();
	const double Remaining = AttackCooldown - (Now - LastAttackTime);
	if (Remaining > 0.0)
	{
		// Evidence: cooldown gate. Logs the cooldown actually used + its source (resolved vs csv).
		UE_LOG(LogTemp, Display, TEXT("[Combat] attack REJECTED: on cooldown (%.2fs left, cd=%.2fs %s)"),
			Remaining, AttackCooldown, bFromLoadout ? TEXT("resolved") : TEXT("csv"));
		return;
	}
	LastAttackTime = Now;

	// Sphere in front of the owner: center = loc + forward * R/2, radius = R/2.
	const float R = Cfg.PlayerAttackRange;
	const FVector Center = Owner->GetActorLocation() + Owner->GetActorForwardVector() * (R * 0.5f);
	const float Radius = R * 0.5f;

#if ENABLE_DRAW_DEBUG
	// Run 2.5 attack legibility: a brief cone shows the swing (visible on misses too). Debug-draw
	// quality per the art level; ENABLE_DRAW_DEBUG-gated so it costs nothing in Shipping.
	DrawDebugCone(World, Owner->GetActorLocation(), Owner->GetActorForwardVector(),
		R, FMath::DegreesToRadians(35.0f), FMath::DegreesToRadians(35.0f),
		16, FColor::Yellow, /*bPersistentLines=*/false, /*LifeTime=*/0.3f, /*DepthPriority=*/0, /*Thickness=*/1.5f);
#endif

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
				HP->TakeDamage(AttackDamage, Owner);
				++Hits;
			}
		}
	}

	// Run 2.5 feedback anchor: fires on every swing, hit or miss (grep-testable the arc drew).
	UE_LOG(LogTemp, Display, TEXT("[Feedback] attackArc len=%.0f hits=%d"), R, Hits);

	if (Hits == 0)
	{
		// Evidence: range check (plan-audit delta #4) - swing landed on nothing.
		UE_LOG(LogTemp, Display, TEXT("[Combat] attack hit no targets in range (range=%.0f)"), R);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[Combat] attack hit %d enemies (dmg=%.0f each, %s)"),
			Hits, AttackDamage, bFromLoadout ? TEXT("resolved") : TEXT("csv"));
	}
}
