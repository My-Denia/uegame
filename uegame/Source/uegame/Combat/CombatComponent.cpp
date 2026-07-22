// CombatComponent.cpp - see header.

#include "CombatComponent.h"

#include "BuildSynergyComponent.h"
#include "CombatConfig.h"
#include "DungeonEnemy.h"
#include "HealthComponent.h"
#include "LoadoutComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "m8_build_synergy.hpp"

#include <vector>

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
	int32 AttackCooldownMs = FMath::RoundToInt(Cfg.PlayerAttackCooldown * 1000.0f);
	int32 AttackDamage = FMath::RoundToInt(Cfg.PlayerAttackDamage);
	bool  bFromLoadout   = false;
	if (const ULoadoutComponent* LC = Owner->FindComponentByClass<ULoadoutComponent>())
	{
		if (LC->HasResolvedStats())
		{
			AttackCooldownMs = LC->GetResolvedAttackMs();
			AttackDamage = LC->GetResolvedDamage();
			bFromLoadout   = true;
		}
	}

	const double AttackCooldown = static_cast<double>(AttackCooldownMs) / 1000.0;
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
	UBuildSynergyComponent* Synergy = Owner->FindComponentByClass<UBuildSynergyComponent>();
	if (Synergy)
	{
		Synergy->RefreshFromLoadout();
	}

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

	struct FCandidate
	{
		ADungeonEnemy* Enemy = nullptr;
		UHealthComponent* Health = nullptr;
		int32 PreHP = 0;
		int32 MaxHP = 1;
		double DistanceSquared = 0.0;
		int32 SpawnOrdinal = INDEX_NONE;
	};

	TArray<FCandidate> Candidates;
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		ADungeonEnemy* Enemy = *It;
		if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
		{
			continue;
		}
		if (FVector::Dist(Enemy->GetActorLocation(), Center) <= Radius)
		{
			if (UHealthComponent* HP = Enemy->FindComponentByClass<UHealthComponent>())
			{
				if (!HP->IsDead())
				{
					FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
					Candidate.Enemy = Enemy;
					Candidate.Health = HP;
					Candidate.PreHP = Enemy->GetCombatPoolCurrent();
					Candidate.MaxHP = Enemy->GetCombatPoolMax();
					Candidate.DistanceSquared = FVector::DistSquared(
						Owner->GetActorLocation(), Enemy->GetActorLocation());
					Candidate.SpawnOrdinal = Enemy->GetSpawnOrdinal();
				}
			}
		}
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		const int32 AO = A.SpawnOrdinal == INDEX_NONE ? MAX_int32 : A.SpawnOrdinal;
		const int32 BO = B.SpawnOrdinal == INDEX_NONE ? MAX_int32 : B.SpawnOrdinal;
		return AO < BO;
	});

	std::vector<m8::TargetSnapshot> Snapshots;
	Snapshots.reserve(static_cast<size_t>(Candidates.Num()));
	for (const FCandidate& Candidate : Candidates)
	{
		m8::TargetSnapshot Snapshot;
		Snapshot.current_hp = Candidate.PreHP;
		Snapshot.max_hp = Candidate.MaxHP;
		Snapshot.distance_squared = Candidate.DistanceSquared;
		Snapshot.spawn_ordinal = Candidate.SpawnOrdinal == INDEX_NONE
			? MAX_uint32 : static_cast<uint32>(FMath::Max(0, Candidate.SpawnOrdinal));
		Snapshots.push_back(Snapshot);
	}
	const size_t PrimaryIndex = m8::select_primary(Snapshots);
	FCandidate* Primary = PrimaryIndex < static_cast<size_t>(Candidates.Num())
		? &Candidates[static_cast<int32>(PrimaryIndex)] : nullptr;

	for (FCandidate& Candidate : Candidates)
	{
		Candidate.Enemy->ApplyPlayerDamage(AttackDamage, Owner);
	}
	const int32 Hits = Candidates.Num();

	if (Synergy)
	{
		const int32 HPAfterBase = Primary ? Primary->Enemy->GetCombatPoolCurrent() : 0;
		const FBuildSynergySwingResult Proc = Synergy->ResolveAcceptedSwing(
			AttackDamage, AttackCooldownMs, Hits, Now, Primary != nullptr,
			Primary ? Primary->PreHP : 0, Primary ? Primary->MaxHP : 1, HPAfterBase);

		if (Primary && !Primary->Health->IsDead() && Proc.ExecutionerDamage > 0)
		{
			Primary->Enemy->ApplyPlayerDamage(Proc.ExecutionerDamage, Owner);
		}
		if (Primary && !Primary->Health->IsDead() && Proc.TempoDamage > 0)
		{
			Primary->Enemy->ApplyPlayerDamage(Proc.TempoDamage, Owner);
		}
		if (Primary && !Primary->Health->IsDead() && Proc.BulwarkDamage > 0)
		{
			Primary->Enemy->ApplyPlayerDamage(Proc.BulwarkDamage, Owner);
		}
		if (Proc.CooldownRefundMs > 0)
		{
			LastAttackTime -= static_cast<double>(Proc.CooldownRefundMs) / 1000.0;
		}
		if (Proc.bExecutionerApplied || Proc.bTempoProc || Proc.bCounterConsumed
			|| Proc.CooldownRefundMs > 0)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[BuildSynergy] primaryOrdinal=%d E=%d T=%d B=%d heal=%d refundMs=%d"),
				Primary ? Primary->SpawnOrdinal : INDEX_NONE,
				Proc.ExecutionerDamage, Proc.TempoDamage, Proc.BulwarkDamage,
				Proc.Heal, Proc.CooldownRefundMs);
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
		UE_LOG(LogTemp, Display, TEXT("[Combat] attack hit %d enemies (dmg=%d each, %s)"),
			Hits, AttackDamage, bFromLoadout ? TEXT("resolved") : TEXT("csv"));
	}
}
