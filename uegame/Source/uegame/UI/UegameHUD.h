// UegameHUD.h - M7A.1 truthful readability overlay.
//
// A code-only AHUD (no UMG/Slate widget, no imported asset) that renders CURRENT M5/M6 runtime state
// on the normal game camera so a viewer of ordinary play (or a showcase capture) can read what the
// systems are doing without the log. It is strictly PRESENTATION: DrawHUD() reads live from the same
// truth-sources the forensic verbs read - ULoadoutComponent (Dungeon.LoadoutStatus), the ADungeonSpawner
// encounter cache (Dungeon.RoomRoles / Dungeon.EnemyRoster), UHealthComponent, and UUegameFloorManager -
// and never copies, fakes, recomputes, or writes any gameplay/determinism state. It consumes no RNG and
// adds no gameplay tick, so it cannot perturb any M5/M6 hash.
//
// Toggle with the console vars ui.ShowReadout (1=draw, 0=nothing) and ui.ShowReadoutScale (text scale).
// Wired as the game HUD via AuegameGameMode::HUDClass, inherited by the BP game mode. Present in all
// build configs (this is player-facing UI, not a forensic verb); it references no !UE_BUILD_SHIPPING code.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "UegameHUD.generated.h"

class ADungeonSpawner;

UCLASS()
class UEGAME_API AUegameHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

#if !UE_BUILD_SHIPPING
	/** Development-only failure injection and readback for the fail-closed route gate. */
	void SetObjectiveRouteFaultModeForTests(int32 Mode);
	void LogObjectiveRouteStatusForTests() const;
	bool TryGetObjectiveRouteWaypointForTests(FVector& OutWaypoint) const;
#endif

private:
	enum class EObjectiveRouteStatus : uint8
	{
		None,
		Updating,
		Ready,
		Blocked
	};

	void InvalidateObjectiveRoute();
	void InvalidateObjectiveTarget();
	FString ResolveObjectiveRouteCue(
		UWorld* World,
		APawn* Pawn,
		AActor* Target,
		uint32 TargetKey,
		uint64 RunSeed,
		int32 FloorIndex,
		const FVector& TargetLocation);

	// Presentation-only weak selection lock plus POD route cache. Query timestamps survive
	// invalidation so target churn cannot bypass the global five-query-per-second ceiling.
	TWeakObjectPtr<AActor> ObjectiveTargetLock;
	uint64 ObjectiveTargetLockRunSeed = 0;
	uint32 ObjectiveTargetLockSpawnerKey = 0;
	int32 ObjectiveTargetLockFloorIndex = INDEX_NONE;
	FVector ObjectiveRouteWaypoint = FVector::ZeroVector;
	uint64 ObjectiveRouteRunSeed = 0;
	uint32 ObjectiveRouteTargetKey = 0;
	uint32 ObjectiveRoutePawnKey = 0;
	int32 ObjectiveRouteFloorIndex = INDEX_NONE;
	EObjectiveRouteStatus ObjectiveRouteStatus = EObjectiveRouteStatus::None;
	double ObjectiveRouteQueryTimestamps[5] = {};
	int32 ObjectiveRouteQueryTimestampCount = 0;
	double ObjectiveRouteLastQuerySeconds = -1.0;
	double ObjectiveRouteLastQueryDurationMs = 0.0;
	uint64 ObjectiveRouteQuerySerial = 0;
	uint8 ObjectiveRouteLastResult = 0;
	uint8 ObjectiveRouteSource = 0; // 0=none, 1=Recast, 2=verified deterministic grid
	bool bObjectiveRouteDeferQueryOnce = false;
	uint32 OnboardingPresentationGeneration = MAX_uint32;
	double OnboardingStartedAtGameSeconds = 0.0;
#if !UE_BUILD_SHIPPING
	int32 ObjectiveRouteFaultModeForTests = 0;
#endif
};
