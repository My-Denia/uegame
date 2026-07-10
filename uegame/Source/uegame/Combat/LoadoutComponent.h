// LoadoutComponent.h - M5 build-diversity runtime state on the player pawn (PR #12B UE binding).
//
// Holds the run's affix picks and the resolved stats, and drives the reward-offer flow (earned on
// floor clear, not granted at run start). This is a REFLECTED UObject header, so by the repo's core-
// isolation discipline it exposes NO m5:: type - the engine-agnostic core (m5_loadout.hpp) never enters
// UHT, exactly like dungeon.hpp / m2_adapter.hpp. Every m5 call lives in LoadoutComponent.cpp; the state
// is mirrored here as plain reflectable ints/arrays for Blueprint and the Dungeon.LoadoutStatus verb.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "LoadoutComponent.generated.h"

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class UEGAME_API ULoadoutComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULoadoutComponent();

	/** Build the base stats (damage / attack_ms / max_hp from CombatConfig + the synthetic move base),
	 *  then resolve with zero picks. Called from the player's BeginPlay. Idempotent; does not touch HP. */
	void InitBaseFromConfig();

	/** Generate the 3-choose-1 reward offer for a just-cleared floor. offer_index = FloorIndex-1
	 *  (deterministic per floor, reproducible per run seed, no cross-run mutable counter). Sets
	 *  bRewardPending=true and logs/prints the offer. Returns false (no-op) if a reward is already
	 *  pending or the offer came back empty. */
	bool GenerateOfferForFloor(uint64 InRunSeed, int32 InFloorIndex);

	/** Apply the player's pick (0..N-1): append the affix id, re-resolve, top up MaxHP on the health
	 *  component, clear bRewardPending. Returns false on a bad index or when no reward is pending. */
	bool ChooseOffer(int32 Index);

	/** New-run reset (death/win/StartRun): clear picks/offer/pending, re-resolve to base, and push base
	 *  MaxHP back to the health component (no refund). A run is a fresh build. */
	void ResetForNewRun();

	bool IsRewardPending() const { return bRewardPending; }
	bool HasResolvedStats() const { return bInitialized; }

	// Resolved stats for the combat / HP seams (valid after InitBaseFromConfig()).
	int32 GetResolvedDamage() const { return ResolvedDamage; }
	int32 GetResolvedAttackMs() const { return ResolvedAttackMs; }
	int32 GetResolvedMaxHP() const { return ResolvedMaxHP; }
	int32 GetResolvedMoveSpeed() const { return ResolvedMoveSpeed; }

	// --- M7A.1 truthful-HUD read surface (additive, read-only) ---
	// The HUD overlay must show the SAME state Dungeon.LoadoutStatus logs, so it reads these exact
	// members rather than recomputing anything. No m5:: type crosses the reflected header (ints/arrays
	// only), matching this file's core-isolation discipline. These getters have no side effects and do
	// not change resolve/offer/reset behaviour.
	/** The run's accumulated affix picks (ids), in pick order. Same array LogStatus prints as chosen=[]. */
	const TArray<int32>& GetChosenAffixIds() const { return ChosenAffixIds; }
	/** The ids currently on offer (empty unless a reward is pending). Same array LogStatus prints as offer=[]. */
	const TArray<int32>& GetCurrentOfferIds() const { return CurrentOfferIds; }
	/** offer_index of the current/last offer (= FloorIndex-1). */
	int32 GetOfferIndex() const { return OfferIndex; }
	/** Floor the current/last offer was generated for. */
	int32 GetFloorForOffer() const { return FloorForOffer; }
	/** Human-readable label for an affix id using the SAME built-in pool + formatter the offer log uses
	 *  (e.g. "Damage +20%%", "MaxHP +60"). Returns "?" for an id not in the pool. Truthful single source:
	 *  it delegates to the same FindAffixById/DescribeAffix helpers that build the offer string. */
	FString DescribeAffixById(int32 Id) const;

	/** Forensic dump for Dungeon.LoadoutStatus (runSeed/floor/offerIndex/pending/offer/chosen/resolved/base). */
	void LogStatus() const;

private:
	/** Re-resolve ResolvedStats from ChosenAffixIds against the built-in pool + base. When bApplyMaxHpToHealth,
	 *  push the new MaxHP to the owner's UHealthComponent (bTopUpCurrent = immediate-reward top-up rule). */
	void ReResolve(bool bApplyMaxHpToHealth, bool bTopUpCurrent);

	/** The run's accumulated picks (affix ids). Feeds generate_offer's max_stacks accounting and, by id
	 *  lookup in the pool, the resolve() input. Only ids are persisted; the pool is authoritative by id. */
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	TArray<int32> ChosenAffixIds;

	/** The ids currently on offer (the 3-choose-1). Cleared once a pick is made. */
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	TArray<int32> CurrentOfferIds;

	/** True between GenerateOfferForFloor() and ChooseOffer(): descend is blocked while a reward is owed. */
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	bool bRewardPending = false;

	/** offer_index passed to generate_offer for the current offer (= FloorIndex-1). */
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	int32 OfferIndex = 0;

	/** Floor the current offer was generated for (forensics). */
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	int32 FloorForOffer = 0;

	/** Run seed the current offer was generated from (forensics; uint64, so not a UPROPERTY). */
	uint64 RunSeedForOffer = 0;

	// Base stats (mirrored from CombatConfig + synthetic move base).
	int32 BaseDamage = 0;
	int32 BaseAttackMs = 0;
	int32 BaseMaxHP = 0;
	int32 BaseMoveSpeed = 0;

	// Cached resolved stats (the values the combat / HP seams read).
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	int32 ResolvedDamage = 0;
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	int32 ResolvedAttackMs = 0;
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	int32 ResolvedMaxHP = 0;
	UPROPERTY(VisibleAnywhere, Category="Loadout")
	int32 ResolvedMoveSpeed = 0;

	bool bInitialized = false;
};
