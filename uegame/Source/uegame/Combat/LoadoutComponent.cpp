// LoadoutComponent.cpp - see header. Binds the engine-agnostic M5 core (m5_loadout.hpp) to the pawn.

#include "LoadoutComponent.h"

#include "CombatConfig.h"
#include "HealthComponent.h"
#include "Engine/Engine.h"          // GEngine (on-screen debug offer)
#include "GameFramework/Actor.h"

// Engine-agnostic loadout core - .cpp-only include (repo-root PrivateIncludePaths, uegame.Build.cs).
// Declarations only here; the definitions are compiled by Combat/M5LoadoutCore.cpp (the sole TU that
// includes m5_loadout.cpp). No m5 type appears in the reflected header, so nothing reaches UHT.
#include "m5_loadout.hpp"

namespace
{
	// --- Built-in affix pool (Option A, contract #12B sec.6): DamagePct / AttackIntervalPct / MaxHpFlat only.
	// MoveSpeedPct is deliberately EXCLUDED from the live pool - player move speed is still the hardcoded
	// MaxWalkSpeed=500 (not datafied), so move_speed resolves to base and is never written back.
	//
	// LoadOnce + validate, mirroring FUegameCombatConfig's missing-table policy: a pool that fails
	// validate_affix_pool() is treated like a missing table - Fatal in Shipping/Test (never ship wrong
	// affix data), warn + empty fallback in Editor/dev. A future affix DataTable/CSV would slot in here
	// with the same policy; for the first version the built-in pool IS the source and is authored to pass.
	const std::vector<m5::Affix>& GetAffixPool()
	{
		static std::vector<m5::Affix> GPool;
		static bool GLoaded = false;
		if (GLoaded)
		{
			return GPool;
		}
		GLoaded = true;

		GPool = {
			// id, kind,                        magnitude, weight, max_stacks (0 = unlimited)
			{ 1, m5::AffixKind::DamagePct,          20, 100, 0 },
			{ 2, m5::AffixKind::DamagePct,          35,  60, 0 },
			{ 3, m5::AffixKind::AttackIntervalPct, -15, 100, 0 },
			{ 4, m5::AffixKind::AttackIntervalPct, -25,  60, 0 },
			{ 5, m5::AffixKind::MaxHpFlat,          30, 100, 0 },
			{ 6, m5::AffixKind::MaxHpFlat,          60,  60, 0 },
		};

		const m5::ValidationResult VR = m5::validate_affix_pool(GPool);
		if (!VR.ok)
		{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
			UE_LOG(LogTemp, Fatal,
				TEXT("[AffixPool] FATAL: built-in affix pool failed validation (%s); refusing to run"),
				*FString(VR.reason.c_str()));
#else
			UE_LOG(LogTemp, Warning,
				TEXT("[AffixPool] built-in affix pool failed validation (%s); falling back to EMPTY pool (no offers)"),
				*FString(VR.reason.c_str()));
			GPool.clear();
#endif
		}
		else
		{
			UE_LOG(LogTemp, Display,
				TEXT("[AffixPool] validated ok: %d affixes (built-in; MoveSpeedPct excluded from live pool)"),
				static_cast<int32>(GPool.size()));
		}
		return GPool;
	}

	const m5::Affix* FindAffixById(uint32 Id)
	{
		for (const m5::Affix& A : GetAffixPool())
		{
			if (A.id == Id)
			{
				return &A;
			}
		}
		return nullptr;
	}

	FString DescribeAffix(const m5::Affix& A)
	{
		switch (A.kind)
		{
		case m5::AffixKind::DamagePct:         return FString::Printf(TEXT("Damage %+lld%%"),      static_cast<long long>(A.magnitude));
		case m5::AffixKind::AttackIntervalPct: return FString::Printf(TEXT("AtkInterval %+lld%%"), static_cast<long long>(A.magnitude));
		case m5::AffixKind::MaxHpFlat:         return FString::Printf(TEXT("MaxHP %+lld"),         static_cast<long long>(A.magnitude));
		case m5::AffixKind::MoveSpeedPct:      return FString::Printf(TEXT("MoveSpeed %+lld%%"),   static_cast<long long>(A.magnitude));
		}
		return TEXT("?");
	}
}

ULoadoutComponent::ULoadoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULoadoutComponent::InitBaseFromConfig()
{
	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	BaseDamage    = FMath::RoundToInt(Cfg.PlayerAttackDamage);            // CSV 15
	BaseAttackMs  = FMath::RoundToInt(Cfg.PlayerAttackCooldown * 1000.0f); // CSV 0.6s -> 600 ms
	BaseMaxHP     = FMath::RoundToInt(Cfg.PlayerMaxHP);                   // CSV 140
	// Synthetic: the player's move speed is the hardcoded MaxWalkSpeed=500 (uegameCharacter.cpp), not in
	// the CSV. Option A keeps MoveSpeedPct out of the live pool, so this base is never modified/written back.
	BaseMoveSpeed = 500;

	ChosenAffixIds.Reset();
	CurrentOfferIds.Reset();
	bRewardPending = false;
	OfferIndex = 0;
	FloorForOffer = 0;
	RunSeedForOffer = 0;
	bInitialized = true;

	// Resolve base (zero picks). Do NOT touch HP here - the pawn's BeginPlay already Init()'d the health
	// component to PlayerMaxHP; re-applying max here would be redundant and could fight that init order.
	ReResolve(/*bApplyMaxHpToHealth=*/false, /*bTopUpCurrent=*/false);

	UE_LOG(LogTemp, Display,
		TEXT("[Loadout] base initialized: dmg=%d attack_ms=%d max_hp=%d move=%d (pool: see [AffixPool])"),
		BaseDamage, BaseAttackMs, BaseMaxHP, BaseMoveSpeed);
}

void ULoadoutComponent::ReResolve(bool bApplyMaxHpToHealth, bool bTopUpCurrent)
{
	m5::BaseStats Base;
	Base.base_damage = BaseDamage;
	Base.attack_ms   = BaseAttackMs;
	Base.max_hp      = BaseMaxHP;
	Base.move_speed  = BaseMoveSpeed;

	std::vector<m5::Affix> Chosen;
	Chosen.reserve(static_cast<size_t>(ChosenAffixIds.Num()));
	for (int32 Id : ChosenAffixIds)
	{
		if (const m5::Affix* A = FindAffixById(static_cast<uint32>(Id)))
		{
			Chosen.push_back(*A);
		}
	}

	const m5::ResolvedStats RS = m5::resolve(Base, Chosen);
	ResolvedDamage    = RS.damage;
	ResolvedAttackMs  = RS.attack_ms;
	ResolvedMaxHP     = RS.max_hp;
	ResolvedMoveSpeed = RS.move_speed;

	if (bApplyMaxHpToHealth)
	{
		if (UHealthComponent* HP = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr)
		{
			HP->SetMaxHP(static_cast<float>(ResolvedMaxHP), bTopUpCurrent);
		}
	}
}

bool ULoadoutComponent::GenerateOfferForFloor(uint64 InRunSeed, int32 InFloorIndex)
{
	if (!bInitialized)
	{
		InitBaseFromConfig();
	}
	if (bRewardPending)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Loadout] reward already pending (floor %d); not regenerating the offer"), FloorForOffer);
		return false;
	}

	RunSeedForOffer = InRunSeed;
	FloorForOffer   = InFloorIndex;
	OfferIndex      = FMath::Max(0, InFloorIndex - 1);   // deterministic per floor; no cross-run counter

	std::vector<std::uint32_t> ChosenIds;
	ChosenIds.reserve(static_cast<size_t>(ChosenAffixIds.Num()));
	for (int32 Id : ChosenAffixIds)
	{
		ChosenIds.push_back(static_cast<std::uint32_t>(Id));
	}

	const std::vector<m5::Affix> Offer = m5::generate_offer(
		GetAffixPool(), ChosenIds, RunSeedForOffer,
		static_cast<std::uint64_t>(InFloorIndex), static_cast<std::uint64_t>(OfferIndex), 3);

	CurrentOfferIds.Reset();
	if (Offer.empty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Loadout] empty offer on floor %d (pool exhausted or empty); no reward"), InFloorIndex);
		return false;
	}

	FString Ids;
	FString Pretty;
	for (const m5::Affix& A : Offer)
	{
		CurrentOfferIds.Add(static_cast<int32>(A.id));
		Ids    += FString::Printf(TEXT("%u "), A.id);
		Pretty += FString::Printf(TEXT("[%d] %s   "), CurrentOfferIds.Num() - 1, *DescribeAffix(A));
	}
	bRewardPending = true;

	UE_LOG(LogTemp, Display,
		TEXT("[Loadout] offer floor=%d offerIndex=%d runSeed=%llu ids=[%s] -> %s"),
		InFloorIndex, OfferIndex, static_cast<unsigned long long>(RunSeedForOffer), *Ids, *Pretty);
	UE_LOG(LogTemp, Display,
		TEXT("[Loadout] RewardPending=true on floor %d (descend blocked until a pick: Dungeon.ChooseLoadout <0|1|2> or keys 1/2/3)"),
		InFloorIndex);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Yellow,
			FString::Printf(TEXT("REWARD - pick an affix (1/2/3):  %s"), *Pretty));
	}
	return true;
}

bool ULoadoutComponent::ChooseOffer(int32 Index)
{
	if (!bRewardPending)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Loadout] no reward pending; ChooseLoadout ignored"));
		return false;
	}
	if (!CurrentOfferIds.IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Loadout] invalid choice index %d (offer has %d options)"), Index, CurrentOfferIds.Num());
		return false;
	}

	const int32 ChosenId = CurrentOfferIds[Index];
	ChosenAffixIds.Add(ChosenId);
	bRewardPending = false;
	CurrentOfferIds.Reset();

	// Re-resolve and push the (possibly higher) MaxHP to the health component, topping up current HP by the
	// same delta (immediate-reward rule). Damage / attack_ms are picked up live by CombatComponent::TryAttack.
	ReResolve(/*bApplyMaxHpToHealth=*/true, /*bTopUpCurrent=*/true);

	const m5::Affix* A = FindAffixById(static_cast<uint32>(ChosenId));
	UE_LOG(LogTemp, Display,
		TEXT("[Loadout] chose index=%d id=%d (%s) | resolved dmg=%d attack_ms=%d max_hp=%d move=%d | picks=%d"),
		Index, ChosenId, A ? *DescribeAffix(*A) : TEXT("?"),
		ResolvedDamage, ResolvedAttackMs, ResolvedMaxHP, ResolvedMoveSpeed, ChosenAffixIds.Num());
	return true;
}

void ULoadoutComponent::ResetForNewRun()
{
	ChosenAffixIds.Reset();
	CurrentOfferIds.Reset();
	bRewardPending = false;
	OfferIndex = 0;
	FloorForOffer = 0;
	RunSeedForOffer = 0;
	if (!bInitialized)
	{
		InitBaseFromConfig();
		return;   // InitBaseFromConfig already resolved base; HP is owned by the caller's heal path
	}

	// Push base MaxHP back to the health component (no top-up: a new run starts at the base build; the
	// caller's HealPlayerFull()/Revive then refills current HP to that base max).
	ReResolve(/*bApplyMaxHpToHealth=*/true, /*bTopUpCurrent=*/false);
	UE_LOG(LogTemp, Display,
		TEXT("[Loadout] reset for new run (picks cleared; resolved back to base dmg=%d max_hp=%d)"),
		ResolvedDamage, ResolvedMaxHP);
}

void ULoadoutComponent::LogStatus() const
{
	FString OfferStr;
	for (int32 Id : CurrentOfferIds)
	{
		const m5::Affix* A = FindAffixById(static_cast<uint32>(Id));
		OfferStr += A ? FString::Printf(TEXT("%d(%s) "), Id, *DescribeAffix(*A))
		              : FString::Printf(TEXT("%d(?) "), Id);
	}
	FString ChosenStr;
	for (int32 Id : ChosenAffixIds)
	{
		ChosenStr += FString::Printf(TEXT("%d "), Id);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[LoadoutStatus] runSeed=%llu floor=%d offerIndex=%d rewardPending=%s offer=[%s] chosen=[%s] resolved{dmg=%d attack_ms=%d max_hp=%d move=%d} base{dmg=%d attack_ms=%d max_hp=%d move=%d}"),
		static_cast<unsigned long long>(RunSeedForOffer), FloorForOffer, OfferIndex,
		bRewardPending ? TEXT("true") : TEXT("false"),
		OfferStr.IsEmpty() ? TEXT("-") : *OfferStr,
		ChosenStr.IsEmpty() ? TEXT("-") : *ChosenStr,
		ResolvedDamage, ResolvedAttackMs, ResolvedMaxHP, ResolvedMoveSpeed,
		BaseDamage, BaseAttackMs, BaseMaxHP, BaseMoveSpeed);
}
