// UegameHUD.cpp - see header. Truthful, presentation-only readability overlay.

#include "UegameHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"      // GEngine->GetSmallFont
#include "Engine/Font.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"        // TActorIterator
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavigationSystemTypes.h"

#include "../Combat/BuildSynergyComponent.h"
#include "../Combat/DungeonEnemy.h"
#include "../Combat/DungeonStairs.h"
#include "../Combat/EncounterConfig.h"
#include "../Combat/FloorManager.h"
#include "../Combat/HealthComponent.h"
#include "../Combat/LoadoutComponent.h"
#include "../DungeonSpawner.h"
#include "../Presentation/PresentationFeedbackComponent.h"

#include "m8_objective_compass.hpp"

// --- Toggles (default ON: this is readability UI we want visible in normal play + capture) ---
static TAutoConsoleVariable<int32> CVarShowReadout(
	TEXT("ui.ShowReadout"), 1,
	TEXT("M7A.1 readability HUD overlay: 1 = draw player/loadout/room-role/encounter readout, 0 = draw nothing."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarReadoutScale(
	TEXT("ui.ShowReadoutScale"), 1.0f,
	TEXT("M7A.1 readability HUD text scale multiplier (clamped 0.5..4.0; default 1.0)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarShowObjectiveRoute(
	TEXT("ui.ShowObjectiveRoute"), 1,
	TEXT("Capsule-agent NavMesh objective route: 1 = query/draw, 0 = zero route queries."),
	ECVF_Default);

// --- M7A.2 per-enemy readout (archetype nameplates + live HP bars) ---
static TAutoConsoleVariable<int32> CVarShowEnemyReadout(
	TEXT("ui.ShowEnemyReadout"), 1,
	TEXT("M7A.2 per-enemy readability: 1 = draw archetype nameplates + HP bars over nearby live enemies, 0 = M7A.1 panels only. ui.ShowReadout=0 still hides everything."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarEnemyReadoutMaxDistance(
	TEXT("ui.EnemyReadoutMaxDistance"), 2000.0f,
	TEXT("M7A.2 enemy readout: max camera-to-enemy distance (uu) for a label (clamped 200..10000). Default 2000 exceeds LeashRange 1400, so anything that can chase you is labeled."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarEnemyReadoutMaxCount(
	TEXT("ui.EnemyReadoutMaxCount"), 8,
	TEXT("M7A.2 enemy readout: max labels drawn per frame (clamped 0..36). The occlusion trace budget is 2x this value (bounded refill)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarEnemyReadoutScale(
	TEXT("ui.EnemyReadoutScale"), 1.0f,
	TEXT("M7A.2 enemy readout text/bar scale multiplier (clamped 0.5..4.0; default 1.0)."),
	ECVF_Default);

namespace
{
	struct FHudLine
	{
		FString Text;
		FLinearColor Color = FLinearColor::White;
	};

	const FLinearColor kHeader(0.62f, 0.84f, 1.00f, 1.0f);   // light blue section header
	const FLinearColor kBody  (0.93f, 0.93f, 0.93f, 1.0f);   // near-white body text
	const FLinearColor kAccent(1.00f, 0.84f, 0.20f, 1.0f);   // yellow: reward / attention
	const FLinearColor kDim   (0.60f, 0.60f, 0.60f, 1.0f);   // dim: unavailable / stale
	const FLinearColor kPanelBg(0.0f, 0.0f, 0.0f, 0.55f);    // translucent black backing
	constexpr float kRouteThreatAcquireRangeCm = 450.0f;
	constexpr float kRouteThreatHoldRangeCm = 750.0f;

	// M7A.2 HP bar palette. Screen-space UI colors only - this is NOT the M7B world-material
	// archetype tint; BodyMID/kEnemyBaseColor/hit-flash are untouched by contract.
	const FLinearColor kHpGood (0.25f, 0.85f, 0.25f, 1.0f);  // fill > 50% HP
	const FLinearColor kHpBad  (0.90f, 0.15f, 0.15f, 1.0f);  // fill < 25% HP (25..50% reuses kAccent)
	const FLinearColor kBarBack(0.0f, 0.0f, 0.0f, 0.70f);    // HP bar backing strip

	const TCHAR* RunStateName(m8authority::RunState State)
	{
		switch (State)
		{
		case m8authority::RunState::Playing: return TEXT("PLAYING");
		case m8authority::RunState::Paused: return TEXT("PAUSED");
		case m8authority::RunState::Won: return TEXT("WON");
		case m8authority::RunState::Failed: return TEXT("FAILED");
		case m8authority::RunState::Error: return TEXT("ERROR");
		case m8authority::RunState::RestartPending: return TEXT("RESTARTING");
		case m8authority::RunState::QuitPending: return TEXT("QUITTING");
		}
		return TEXT("UNKNOWN");
	}

	// Located exactly as the forensic verbs locate it (DungeonEvidence.cpp FindSpawner): the first
	// ADungeonSpawner in the world. Guarantees the HUD reads the SAME instance Dungeon.RoomRoles /
	// Dungeon.EnemyRoster read, so their outputs cannot diverge by source.
	ADungeonSpawner* FindSpawner(UWorld* World)
	{
		if (World)
		{
			for (TActorIterator<ADungeonSpawner> It(World); It; ++It)
			{
				return *It;
			}
		}
		return nullptr;
	}

	const TCHAR* WardenPhaseName(EUegameWardenPhase Phase)
	{
		switch (Phase)
		{
		case EUegameWardenPhase::Guarded: return TEXT("GUARDED");
		case EUegameWardenPhase::Staggered: return TEXT("BROKEN - STRIKE NOW");
		case EUegameWardenPhase::Exposed: return TEXT("EXPOSED");
		case EUegameWardenPhase::Dead: return TEXT("DEFEATED");
		case EUegameWardenPhase::Inactive:
		default: return TEXT("INACTIVE");
		}
	}

	// Objective routing has stricter authority than the legacy readout: exactly one current
	// encounter snapshot must match the active run and floor. Ambiguity clears the arrow.
	ADungeonSpawner* FindFreshObjectiveSpawner(UWorld* World, const UUegameFloorManager* FM)
	{
		if (!World || !FM || !FM->IsRunActive())
		{
			return nullptr;
		}
		ADungeonSpawner* Match = nullptr;
		for (TActorIterator<ADungeonSpawner> It(World); It; ++It)
		{
			ADungeonSpawner* Candidate = *It;
			if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed()
				|| !Candidate->HasEncounterAssignment()
				|| Candidate->GetEncounterRunSeed() != FM->GetRunSeed()
				|| Candidate->GetEncounterFloorIndex() != FM->GetFloorIndex())
			{
				continue;
			}
			if (Match)
			{
				return nullptr;
			}
			Match = Candidate;
		}
		return Match;
	}

	FString ObjectiveDirection(const FVector& Target, const APawn* Pawn, const APlayerController* PC)
	{
		const FVector Delta = Pawn ? Target - Pawn->GetActorLocation() : FVector::ZeroVector;
		const FVector CameraForward = PC && PC->PlayerCameraManager
			? PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector::ZeroVector;
		const FVector PlayerForward = Pawn ? Pawn->GetActorForwardVector() : FVector::ForwardVector;
		return ANSI_TO_TCHAR(m8objective::direction_name(m8objective::map_direction(
			{ Delta.X, Delta.Y }, { CameraForward.X, CameraForward.Y },
			{ PlayerForward.X, PlayerForward.Y })));
	}

	bool TryMeasureReachablePath(UWorld* World, const APawn* Pawn, const FVector& Target, double& OutLength)
	{
		OutLength = 0.0;
		if (!World || !Pawn)
		{
			return false;
		}
		const FNavAgentProperties& AgentProps = Pawn->GetNavAgentPropertiesRef();
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const ANavigationData* NavData = NavSystem
			? NavSystem->GetNavDataForProps(AgentProps, Pawn->GetActorLocation()) : nullptr;
		if (!NavSystem || !NavData)
		{
			return false;
		}
		const float Radius = FMath::Max(AgentProps.AgentRadius, 35.0f);
		const float Height = FMath::Max(AgentProps.AgentHeight, 88.0f);
		const FVector ProjectionExtent(FMath::Max(Radius * 2.0f, 100.0f),
			FMath::Max(Radius * 2.0f, 100.0f), FMath::Max(Height, 200.0f));
		FNavLocation Start;
		FNavLocation End;
		if (!NavSystem->ProjectPointToNavigation(Pawn->GetActorLocation(), Start, ProjectionExtent, NavData)
			|| !NavSystem->ProjectPointToNavigation(Target, End, ProjectionExtent, NavData))
		{
			return false;
		}
		FPathFindingQuery Query(Pawn, *NavData, Start.Location, End.Location);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = NavSystem->FindPathSync(Query);
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.IsPartial()
			|| Result.Path->GetPathPoints().Num() < 2)
		{
			return false;
		}
		const TArray<FNavPathPoint>& Points = Result.Path->GetPathPoints();
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			OutLength += FVector::Dist2D(Points[Index - 1].Location, Points[Index].Location);
		}
		return FMath::IsFinite(OutLength);
	}

	struct FObjectiveSelection
	{
		FHudLine Line;
		AActor* Target = nullptr;
		FVector TargetLocation = FVector::ZeroVector;
		uint32 TargetKey = 0;
		uint64 RunSeed = 0;
		uint32 SpawnerKey = 0;
		int32 FloorIndex = INDEX_NONE;
	};

	FObjectiveSelection MakeObjective(
		FString Text, AActor* Target, const FVector& Location,
		const UUegameFloorManager* FM, const ADungeonSpawner* Spawner, uint32 SyntheticKey = 0)
	{
		FObjectiveSelection Selection;
		Selection.Line = { MoveTemp(Text), kAccent };
		Selection.Target = Target;
		Selection.TargetLocation = Location;
		Selection.TargetKey = SyntheticKey != 0 ? SyntheticKey : (Target ? Target->GetUniqueID() : 0);
		Selection.RunSeed = FM ? FM->GetRunSeed() : 0;
		Selection.SpawnerKey = Spawner ? Spawner->GetUniqueID() : 0;
		Selection.FloorIndex = FM ? FM->GetFloorIndex() : INDEX_NONE;
		return Selection;
	}

	FObjectiveSelection SelectObjective(
		UWorld* World, APawn* Pawn, const ULoadoutComponent* LC, const UUegameFloorManager* FM,
		AActor* LockedTarget, uint64 LockedRunSeed, int32 LockedFloorIndex, uint32 LockedSpawnerKey,
		int32 AuthorityFaultMode)
	{
		if (FM && FM->GetRunState() != m8authority::RunState::Playing)
		{
			return { { FString::Printf(TEXT("OBJECTIVE: %s"), RunStateName(FM->GetRunState())), kDim } };
		}
		if (FM && FM->IsRoomContractPending())
		{
			return { { TEXT("OBJECTIVE: CHOOSE ROOM CONTRACT [1/2]"), kAccent } };
		}
		if (LC && LC->IsRewardPending())
		{
			return { { TEXT("OBJECTIVE: CHOOSE REWARD [1/2/3]"), kAccent } };
		}
		ADungeonSpawner* Spawner = AuthorityFaultMode >= 5 ? nullptr : FindFreshObjectiveSpawner(World, FM);
		if (!Pawn || !Spawner)
		{
			return { { FM && FM->IsRunActive()
				? TEXT("OBJECTIVE: NO CURRENT TARGET") : TEXT("OBJECTIVE: STARTING RUN"), kDim } };
		}

		if (FM->IsFloorObjectiveComplete())
		{
			ADungeonStairs* BoundStairs = nullptr;
			int32 Matches = 0;
			const FVector Expected = Spawner->GetFarthestRoomCenterWorld();
			for (TActorIterator<ADungeonStairs> It(World); It; ++It)
			{
				ADungeonStairs* Candidate = *It;
				if (IsValid(Candidate) && !Candidate->IsActorBeingDestroyed()
					&& FVector::DistSquared2D(Candidate->GetActorLocation(), Expected) <= 1.0f)
				{
					BoundStairs = Candidate;
					++Matches;
				}
			}
			if (Matches == 1 && BoundStairs)
			{
				const float Metres = FVector::Dist2D(Pawn->GetActorLocation(), BoundStairs->GetActorLocation()) / 100.0f;
				return MakeObjective(FString::Printf(TEXT("OBJECTIVE: STAIRS %.1fm"), Metres),
					BoundStairs, BoundStairs->GetActorLocation(), FM, Spawner);
			}
			return { { TEXT("OBJECTIVE: EXIT READY - STAIRS UNAVAILABLE"), kDim } };
		}

		const bool bPrioritizeOrdinary = Spawner->HasFinaleInitialized()
			&& Spawner->GetLivingOrdinaryEnemyCount() > 0;
		if (Spawner->HasFinaleInitialized() && !bPrioritizeOrdinary)
		{
			ADungeonEnemy* Warden = Spawner->GetWarden();
			if (IsValid(Warden) && !Warden->IsActorBeingDestroyed() && Warden->IsActiveThreat()
				&& Warden->GetOwningSpawner() == Spawner)
			{
				const float Metres = FVector::Dist2D(
					Pawn->GetActorLocation(), Warden->GetActorLocation()) / 100.0f;
				return MakeObjective(FString::Printf(TEXT("OBJECTIVE: WARDEN %.1fm"), Metres),
					Warden, Warden->GetActorLocation(), FM, Spawner);
			}
		}
		TSet<int32> OrdinaryRooms;
		if (bPrioritizeOrdinary)
		{
			for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
			{
				ADungeonEnemy* Enemy = *It;
				if (IsValid(Enemy) && !Enemy->IsActorBeingDestroyed() && Enemy->IsActiveThreat()
					&& Enemy->GetOwningSpawner() == Spawner && !Enemy->IsWarden())
				{
					OrdinaryRooms.Add(Enemy->GetRoomIndex());
				}
			}
		}

		int32 ObjectiveRoom = INDEX_NONE;
		if (bPrioritizeOrdinary)
		{
			// Keep a live ordinary target stable once acquired. On acquisition, ignore room-risk
			// labels and choose the shortest complete capsule-agent path; unreachable rooms are
			// excluded instead of being selected and producing a wall-facing blocked arrow.
			ADungeonEnemy* LockedOrdinary = Cast<ADungeonEnemy>(LockedTarget);
			if (LockedOrdinary && IsValid(LockedOrdinary) && !LockedOrdinary->IsActorBeingDestroyed()
				&& LockedOrdinary->IsActiveThreat() && !LockedOrdinary->IsWarden()
				&& LockedOrdinary->GetOwningSpawner() == Spawner
				&& LockedRunSeed == FM->GetRunSeed() && LockedFloorIndex == FM->GetFloorIndex()
				&& LockedSpawnerKey == Spawner->GetUniqueID()
				&& OrdinaryRooms.Contains(LockedOrdinary->GetRoomIndex()))
			{
				ObjectiveRoom = LockedOrdinary->GetRoomIndex();
			}
			else
			{
				double BestPathLength = TNumericLimits<double>::Max();
				for (int32 Room : OrdinaryRooms)
				{
					double PathLength = 0.0;
					if (Spawner->GetAliveInRoom(Room) <= 0
						|| !TryMeasureReachablePath(World, Pawn, Spawner->GetRoomCenterWorld(Room), PathLength))
					{
						continue;
					}
					if (ObjectiveRoom == INDEX_NONE || PathLength < BestPathLength
						|| (FMath::IsNearlyEqual(PathLength, BestPathLength) && Room < ObjectiveRoom))
					{
						ObjectiveRoom = Room;
						BestPathLength = PathLength;
					}
				}
			}
		}
		else
		{
			// Before the finale, a chosen contract room remains the first tactical commitment;
			// otherwise preserve the established room-risk then distance ordering.
			ObjectiveRoom = FM->GetSelectedContractRoom();
			if (ObjectiveRoom < 0 || Spawner->GetAliveInRoom(ObjectiveRoom) <= 0)
			{
				ObjectiveRoom = INDEX_NONE;
				int32 BestRisk = TNumericLimits<int32>::Max();
				float BestDistance = TNumericLimits<float>::Max();
				const TArray<int32>& Roles = Spawner->GetCachedRoomRoles();
				for (int32 Room = 0; Room < Spawner->GetRoomCount(); ++Room)
				{
					if (Spawner->GetAliveInRoom(Room) <= 0 || !Roles.IsValidIndex(Room))
					{
						continue;
					}
					const int32 Risk = m8objective::room_risk_priority(Roles[Room]);
					const float Distance = FVector::DistSquared2D(
						Pawn->GetActorLocation(), Spawner->GetRoomCenterWorld(Room));
					if (ObjectiveRoom == INDEX_NONE || Risk < BestRisk
						|| (Risk == BestRisk && (Distance < BestDistance
							|| (FMath::IsNearlyEqual(Distance, BestDistance) && Room < ObjectiveRoom))))
					{
						ObjectiveRoom = Room;
						BestRisk = Risk;
						BestDistance = Distance;
					}
				}
			}
		}

		ADungeonEnemy* BestEnemy = nullptr;
		ADungeonEnemy* LockedEnemy = nullptr;
		m8objective::CandidateKey BestKey;
		for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
		{
			ADungeonEnemy* Enemy = *It;
			if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed() || !Enemy->IsActiveThreat()
				|| Enemy->GetOwningSpawner() != Spawner || Enemy->GetRoomIndex() != ObjectiveRoom
				|| (bPrioritizeOrdinary && Enemy->IsWarden()))
			{
				continue;
			}
			const int32 Ordinal = Enemy->GetSpawnOrdinal();
			const m8objective::CandidateKey Key{
				static_cast<double>(FVector::DistSquared2D(Pawn->GetActorLocation(), Enemy->GetActorLocation())),
				Ordinal >= 0 ? static_cast<uint32>(Ordinal) : 0u, Enemy->GetUniqueID(), Ordinal >= 0 };
			if (!BestEnemy || m8objective::candidate_less(Key, BestKey))
			{
				BestEnemy = Enemy;
				BestKey = Key;
			}
			if (Enemy == LockedTarget && LockedRunSeed == FM->GetRunSeed()
				&& LockedFloorIndex == FM->GetFloorIndex() && LockedSpawnerKey == Spawner->GetUniqueID())
			{
				LockedEnemy = Enemy;
			}
		}

		if (ObjectiveRoom != INDEX_NONE)
		{
			const FVector RoomCenter = Spawner->GetRoomCenterWorld(ObjectiveRoom);
			const float RoomMetres = FVector::Dist2D(Pawn->GetActorLocation(), RoomCenter) / 100.0f;
			ADungeonEnemy* SelectedEnemy = LockedEnemy ? LockedEnemy : BestEnemy;
			const float EnemyMetres = SelectedEnemy
				? FVector::Dist2D(Pawn->GetActorLocation(), SelectedEnemy->GetActorLocation()) / 100.0f : BIG_NUMBER;
			const bool bLockedNearby = SelectedEnemy == LockedEnemy && EnemyMetres <= kRouteThreatHoldRangeCm / 100.0f;
			const bool bAcquireNearby = SelectedEnemy && EnemyMetres <= kRouteThreatAcquireRangeCm / 100.0f;
			if ((bLockedNearby || bAcquireNearby || RoomMetres <= 4.5f) && SelectedEnemy)
			{
				return MakeObjective(FString::Printf(TEXT("OBJECTIVE: R%d %s %.1fm"), ObjectiveRoom,
					SelectedEnemy->GetArchetypeDisplayName(), EnemyMetres), SelectedEnemy,
					SelectedEnemy->GetActorLocation(), FM, Spawner);
			}
			uint32 RoomKey = (Spawner->GetUniqueID() * 16777619u) ^ static_cast<uint32>(ObjectiveRoom + 1);
			if (RoomKey == 0) { RoomKey = 1; }
			const bool bContractRoom = ObjectiveRoom == FM->GetSelectedContractRoom();
			const FString RoomText = bContractRoom
				? FString::Printf(TEXT("OBJECTIVE: CONTRACT ROOM R%d %.1fm"), ObjectiveRoom, RoomMetres)
				: FString::Printf(TEXT("OBJECTIVE: ROOM R%d %.1fm"), ObjectiveRoom, RoomMetres);
			AActor* RoomLockTarget = Spawner;
			if (bPrioritizeOrdinary && BestEnemy)
			{
				RoomLockTarget = BestEnemy;
			}
			return MakeObjective(RoomText, RoomLockTarget, RoomCenter, FM, Spawner, RoomKey);
		}

		return { { TEXT("OBJECTIVE: NO CURRENT TARGET"), kDim } };
	}

	// Nearest room by 2D distance from the pawn to each room center. Uses the spawner's own room
	// centers + count (the same indices GetCachedRoomRoles() is keyed by), so the resolved role is a
	// faithful read of the cache for the player's current room, not an independent recomputation.
	int32 NearestRoomIndex(ADungeonSpawner* Spawner, const APawn* Pawn)
	{
		if (!Spawner || !Pawn)
		{
			return INDEX_NONE;
		}
		const FVector Loc = Pawn->GetActorLocation();
		int32 Best = INDEX_NONE;
		float BestSq = TNumericLimits<float>::Max();
		const int32 Count = Spawner->GetRoomCount();
		for (int32 i = 0; i < Count; ++i)
		{
			const float D = FVector::DistSquared2D(Loc, Spawner->GetRoomCenterWorld(i));
			if (D < BestSq)
			{
				BestSq = D;
				Best = i;
			}
		}
		return Best;
	}

	// Measure the panel's content box for a set of lines (max line width, uniform line height).
	void MeasurePanel(AHUD* Hud, UFont* Font, const TArray<FHudLine>& Lines, float Scale,
	                  float& OutContentW, float& OutLineH)
	{
		OutContentW = 0.0f;
		OutLineH = 0.0f;
		for (const FHudLine& L : Lines)
		{
			float W = 0.0f, H = 0.0f;
			Hud->GetTextSize(L.Text, W, H, Font, Scale);
			OutContentW = FMath::Max(OutContentW, W);
			OutLineH = FMath::Max(OutLineH, H);
		}
	}

	// M7A.2: one enemy that survived the cheap (trace-free) filters, awaiting the
	// distance-ordered bounded-refill occlusion pass.
	struct FEnemyReadoutCandidate
	{
		ADungeonEnemy* Enemy = nullptr;
		float DistSq = 0.0f;
	};

	// Draw a translucent backing box then the colored lines. Returns the total panel width drawn.
	float DrawPanel(AHUD* Hud, UFont* Font, float OriginX, float OriginY,
	                const TArray<FHudLine>& Lines, float Scale)
	{
		if (!Hud || !Font || Lines.Num() == 0)
		{
			return 0.0f;
		}
		const float Pad = 8.0f * Scale;
		const float Gap = 3.0f * Scale;
		float ContentW = 0.0f, LineH = 0.0f;
		MeasurePanel(Hud, Font, Lines, Scale, ContentW, LineH);

		const float PanelW = ContentW + 2.0f * Pad;
		const float PanelH = Lines.Num() * LineH + (Lines.Num() - 1) * Gap + 2.0f * Pad;
		Hud->DrawRect(kPanelBg, OriginX, OriginY, PanelW, PanelH);

		float Y = OriginY + Pad;
		for (const FHudLine& L : Lines)
		{
			Hud->DrawText(L.Text, L.Color, OriginX + Pad, Y, Font, Scale, /*bScalePosition=*/false);
			Y += LineH + Gap;
		}
		return PanelW;
	}
}

void AUegameHUD::InvalidateObjectiveRoute()
{
	ObjectiveRouteWaypoint = FVector::ZeroVector;
	ObjectiveRouteRunSeed = 0;
	ObjectiveRouteTargetKey = 0;
	ObjectiveRoutePawnKey = 0;
	ObjectiveRouteFloorIndex = INDEX_NONE;
	ObjectiveRouteStatus = EObjectiveRouteStatus::None;
	ObjectiveRouteLastResult = 0;
}

void AUegameHUD::InvalidateObjectiveTarget()
{
	ObjectiveTargetLock.Reset();
	ObjectiveTargetLockRunSeed = 0;
	ObjectiveTargetLockSpawnerKey = 0;
	ObjectiveTargetLockFloorIndex = INDEX_NONE;
	bObjectiveRouteDeferQueryOnce = false;
	InvalidateObjectiveRoute();
}

FString AUegameHUD::ResolveObjectiveRouteCue(
	UWorld* World, APawn* Pawn, AActor* Target, uint32 TargetKey,
	uint64 RunSeed, int32 FloorIndex, const FVector& TargetLocation)
{
	if (!World || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()
		|| !IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		InvalidateObjectiveRoute();
		return TEXT("ROUTE BLOCKED");
	}

	const uint32 PawnKey = Pawn->GetUniqueID();
	const bool bIdentityChanged = ObjectiveRouteTargetKey != TargetKey
		|| ObjectiveRoutePawnKey != PawnKey || ObjectiveRouteRunSeed != RunSeed
		|| ObjectiveRouteFloorIndex != FloorIndex;
	if (bIdentityChanged)
	{
		const uint32 OldTargetKey = ObjectiveRouteTargetKey;
		const uint32 OldPawnKey = ObjectiveRoutePawnKey;
		const uint64 OldRunSeed = ObjectiveRouteRunSeed;
		const int32 OldFloorIndex = ObjectiveRouteFloorIndex;
		InvalidateObjectiveRoute();
		ObjectiveRouteTargetKey = TargetKey;
		ObjectiveRoutePawnKey = PawnKey;
		ObjectiveRouteRunSeed = RunSeed;
		ObjectiveRouteFloorIndex = FloorIndex;
		ObjectiveRouteStatus = EObjectiveRouteStatus::Updating;
#if !UE_BUILD_SHIPPING
		UE_LOG(LogTemp, Display,
			TEXT("[ObjectiveRouteInvalidated] reason=route-identity-change oldTargetKey=%u newTargetKey=%u oldPawnKey=%u newPawnKey=%u oldRunSeed=%llu newRunSeed=%llu oldFloor=%d newFloor=%d arrow=false stale=false"),
			OldTargetKey, TargetKey, OldPawnKey, PawnKey,
			static_cast<unsigned long long>(OldRunSeed), static_cast<unsigned long long>(RunSeed),
			OldFloorIndex, FloorIndex);
#endif
	}
	if (bObjectiveRouteDeferQueryOnce)
	{
		bObjectiveRouteDeferQueryOnce = false;
		ObjectiveRouteWaypoint = FVector::ZeroVector;
		ObjectiveRouteStatus = EObjectiveRouteStatus::Updating;
		return TEXT("ROUTE UPDATING");
	}

	// A combat objective is reached at attack range, not at the enemy capsule center. Continuing
	// to point through a body produces wall-like shoving and teaches the wrong player action.
	if (Cast<ADungeonEnemy>(Target)
		&& FVector::Dist2D(Pawn->GetActorLocation(), TargetLocation) <= 220.0f)
	{
		ObjectiveRouteWaypoint = FVector::ZeroVector;
		ObjectiveRouteStatus = EObjectiveRouteStatus::None;
		ObjectiveRouteLastResult = 0;
		return TEXT("IN RANGE - ATTACK [F]");
	}

	const double Now = FPlatformTime::Seconds();
	if (ObjectiveRouteQueryTimestampCount > 0 && Now < ObjectiveRouteLastQuerySeconds)
	{
		ObjectiveRouteQueryTimestampCount = 0;
		ObjectiveRouteLastQuerySeconds = -1.0;
	}
	std::vector<double> PriorTimestamps;
	PriorTimestamps.reserve(static_cast<std::size_t>(ObjectiveRouteQueryTimestampCount));
	for (int32 Index = 0; Index < ObjectiveRouteQueryTimestampCount; ++Index)
	{
		PriorTimestamps.push_back(ObjectiveRouteQueryTimestamps[Index]);
	}
	if (!m8objective::route_query_allowed(Now, PriorTimestamps))
	{
		if (ObjectiveRouteStatus == EObjectiveRouteStatus::Ready)
		{
			return FString::Printf(TEXT("ROUTE %s"),
				*ObjectiveDirection(ObjectiveRouteWaypoint, Pawn, GetOwningPlayerController()));
		}
		return ObjectiveRouteStatus == EObjectiveRouteStatus::Blocked
			? TEXT("ROUTE BLOCKED") : TEXT("ROUTE UPDATING");
	}
	if (ObjectiveRouteQueryTimestampCount == static_cast<int32>(m8objective::kRouteQueryMaximumPerSecond))
	{
		for (int32 Index = 1; Index < ObjectiveRouteQueryTimestampCount; ++Index)
		{
			ObjectiveRouteQueryTimestamps[Index - 1] = ObjectiveRouteQueryTimestamps[Index];
		}
		--ObjectiveRouteQueryTimestampCount;
	}
	ObjectiveRouteQueryTimestamps[ObjectiveRouteQueryTimestampCount++] = Now;
	ObjectiveRouteLastQuerySeconds = Now;
	++ObjectiveRouteQuerySerial;

	auto FailRoute = [this, Now, TargetKey, RunSeed, FloorIndex, Pawn](const TCHAR* Result, uint8 ResultCode,
		const ANavigationData* NavData, const FNavAgentProperties& AgentProps,
		const FVector& Start, const FVector& End, int32 PointCount, bool bPartial,
		double QueryStarted) -> FString
	{
		ObjectiveRouteLastQueryDurationMs = (FPlatformTime::Seconds() - QueryStarted) * 1000.0;
		ObjectiveRouteWaypoint = FVector::ZeroVector;
		ObjectiveRouteStatus = EObjectiveRouteStatus::Blocked;
		ObjectiveRouteLastResult = ResultCode;
#if !UE_BUILD_SHIPPING
		UE_LOG(LogTemp, Display,
			TEXT("[ObjectiveRouteQuery] serial=%llu timestamp=%.6f targetKey=%u runSeed=%llu floor=%d result=%s arrow=false stale=false navData=%s navClass=%s agentRadius=%.1f agentHeight=%.1f start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f) pawn=(%.1f,%.1f,%.1f) points=%d partial=%s durationMs=%.3f"),
			static_cast<unsigned long long>(ObjectiveRouteQuerySerial), Now, TargetKey,
			static_cast<unsigned long long>(RunSeed), FloorIndex, Result,
			NavData ? *NavData->GetName() : TEXT("NONE"),
			NavData ? *NavData->GetClass()->GetName() : TEXT("NONE"),
			AgentProps.AgentRadius, AgentProps.AgentHeight,
			Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z,
			Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Pawn->GetActorLocation().Z,
			PointCount, bPartial ? TEXT("true") : TEXT("false"), ObjectiveRouteLastQueryDurationMs);
#endif
		return TEXT("ROUTE BLOCKED");
	};

	const double QueryStarted = FPlatformTime::Seconds();
	const FNavAgentProperties& AgentProps = Pawn->GetNavAgentPropertiesRef();
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ANavigationData* NavData = NavSystem
		? NavSystem->GetNavDataForProps(AgentProps, Pawn->GetActorLocation()) : nullptr;
	if (!NavSystem || !NavData)
	{
		return FailRoute(TEXT("NO_MATCHING_NAVDATA"), 1, NavData, AgentProps,
			Pawn->GetActorLocation(), TargetLocation, 0, false, QueryStarted);
	}

	const float Radius = FMath::Max(AgentProps.AgentRadius, 35.0f);
	const float Height = FMath::Max(AgentProps.AgentHeight, 88.0f);
	const FVector ProjectionExtent(FMath::Max(Radius * 2.0f, 100.0f),
		FMath::Max(Radius * 2.0f, 100.0f), FMath::Max(Height, 200.0f));
	FNavLocation ProjectedStart;
	FNavLocation ProjectedEnd;
	bool bStartProjected = NavSystem->ProjectPointToNavigation(
		Pawn->GetActorLocation(), ProjectedStart, ProjectionExtent, NavData);
	bool bEndProjected = NavSystem->ProjectPointToNavigation(
		TargetLocation, ProjectedEnd, ProjectionExtent, NavData);
#if !UE_BUILD_SHIPPING
	if (ObjectiveRouteFaultModeForTests == 1) { bStartProjected = false; }
	if (ObjectiveRouteFaultModeForTests == 2) { bEndProjected = false; }
#endif
	if (!bStartProjected)
	{
		return FailRoute(TEXT("START_PROJECTION_FAILED"), 2, NavData, AgentProps,
			Pawn->GetActorLocation(), TargetLocation, 0, false, QueryStarted);
	}
	if (!bEndProjected)
	{
		return FailRoute(TEXT("END_PROJECTION_FAILED"), 3, NavData, AgentProps,
			ProjectedStart.Location, TargetLocation, 0, false, QueryStarted);
	}

	FPathFindingQuery Query(Pawn, *NavData, ProjectedStart.Location, ProjectedEnd.Location);
	Query.SetAllowPartialPaths(false);
	FPathFindingResult PathResult = NavSystem->FindPathSync(Query);
	bool bSuccessful = PathResult.IsSuccessful() && PathResult.Path.IsValid();
	bool bPartial = bSuccessful && PathResult.IsPartial();
	int32 PointCount = bSuccessful ? PathResult.Path->GetPathPoints().Num() : 0;
#if !UE_BUILD_SHIPPING
	if (ObjectiveRouteFaultModeForTests == 3) { bSuccessful = false; PointCount = 0; }
	if (ObjectiveRouteFaultModeForTests == 4) { bPartial = true; }
#endif
	if (!bSuccessful)
	{
		return FailRoute(TEXT("PATH_INVALID"), 4, NavData, AgentProps,
			ProjectedStart.Location, ProjectedEnd.Location, PointCount, bPartial, QueryStarted);
	}
	if (bPartial)
	{
		return FailRoute(TEXT("PATH_PARTIAL_REJECTED"), 5, NavData, AgentProps,
			ProjectedStart.Location, ProjectedEnd.Location, PointCount, true, QueryStarted);
	}
	if (PointCount < 2)
	{
		return FailRoute(TEXT("PATH_TOO_SHORT"), 6, NavData, AgentProps,
			ProjectedStart.Location, ProjectedEnd.Location, PointCount, false, QueryStarted);
	}

	const TArray<FNavPathPoint>& PathPoints = PathResult.Path->GetPathPoints();
	std::vector<m8objective::Vec2> OrderedPoints;
	OrderedPoints.reserve(static_cast<std::size_t>(PointCount));
	for (const FNavPathPoint& Point : PathPoints)
	{
		OrderedPoints.push_back({ Point.Location.X, Point.Location.Y });
	}
	const m8objective::WaypointChoice Choice = m8objective::select_route_waypoint(
		OrderedPoints, { Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y },
		{ ProjectedEnd.Location.X, ProjectedEnd.Location.Y }, 150.0);
	ObjectiveRouteWaypoint = Choice.used_target_fallback
		? ProjectedEnd.Location : PathPoints[static_cast<int32>(Choice.path_index)].Location;
	ObjectiveRouteStatus = EObjectiveRouteStatus::Ready;
	ObjectiveRouteLastResult = 7;
	ObjectiveRouteLastQueryDurationMs = (FPlatformTime::Seconds() - QueryStarted) * 1000.0;
	const FString Direction = ObjectiveDirection(ObjectiveRouteWaypoint, Pawn, GetOwningPlayerController());
	const float ControlYaw = GetOwningPlayerController()
		? GetOwningPlayerController()->GetControlRotation().Yaw : Pawn->GetActorRotation().Yaw;
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Display,
		TEXT("[ObjectiveRouteQuery] serial=%llu timestamp=%.6f targetKey=%u runSeed=%llu floor=%d result=READY arrow=true stale=false navData=%s navClass=%s agentRadius=%.1f agentHeight=%.1f start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f) pawn=(%.1f,%.1f,%.1f) waypoint=(%.1f,%.1f,%.1f) controlYaw=%.1f points=%d partial=false fallback=%s direction=%s durationMs=%.3f"),
		static_cast<unsigned long long>(ObjectiveRouteQuerySerial), Now, TargetKey,
		static_cast<unsigned long long>(RunSeed), FloorIndex,
		*NavData->GetName(), *NavData->GetClass()->GetName(), AgentProps.AgentRadius, AgentProps.AgentHeight,
		ProjectedStart.Location.X, ProjectedStart.Location.Y, ProjectedStart.Location.Z,
		ProjectedEnd.Location.X, ProjectedEnd.Location.Y, ProjectedEnd.Location.Z,
		Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Pawn->GetActorLocation().Z,
		ObjectiveRouteWaypoint.X, ObjectiveRouteWaypoint.Y, ObjectiveRouteWaypoint.Z,
		ControlYaw, PointCount, Choice.used_target_fallback ? TEXT("true") : TEXT("false"),
		*Direction, ObjectiveRouteLastQueryDurationMs);
#endif
	return FString::Printf(TEXT("ROUTE %s"), *Direction);
}

#if !UE_BUILD_SHIPPING
void AUegameHUD::SetObjectiveRouteFaultModeForTests(int32 Mode)
{
	ObjectiveRouteFaultModeForTests = FMath::Clamp(Mode, 0, 6);
	InvalidateObjectiveRoute();
	bObjectiveRouteDeferQueryOnce = true;
	UE_LOG(LogTemp, Display, TEXT("[ObjectiveRouteTest] faultMode=%d serial=%llu arrow=false"),
		ObjectiveRouteFaultModeForTests, static_cast<unsigned long long>(ObjectiveRouteQuerySerial));
}

void AUegameHUD::LogObjectiveRouteStatusForTests() const
{
	const TCHAR* Status = ObjectiveRouteStatus == EObjectiveRouteStatus::Ready ? TEXT("READY")
		: ObjectiveRouteStatus == EObjectiveRouteStatus::Blocked ? TEXT("BLOCKED")
		: ObjectiveRouteStatus == EObjectiveRouteStatus::Updating ? TEXT("UPDATING") : TEXT("NONE");
	const ADungeonEnemy* TargetEnemy = Cast<ADungeonEnemy>(ObjectiveTargetLock.Get());
	const ADungeonSpawner* TargetSpawner = TargetEnemy ? TargetEnemy->GetOwningSpawner() : nullptr;
	const TCHAR* TargetKind = TargetEnemy
		? (TargetEnemy->IsWarden() ? TEXT("warden") : TEXT("ordinary"))
		: (ObjectiveTargetLock.IsValid() ? TEXT("other") : TEXT("none"));
	UE_LOG(LogTemp, Display,
		TEXT("[ObjectiveRouteStatus] status=%s arrow=%s serial=%llu result=%u targetKey=%u pawnKey=%u runSeed=%llu floor=%d waypoint=(%.1f,%.1f,%.1f) faultMode=%d targetKind=%s targetRoom=%d ordinaryAlive=%d"),
		Status, ObjectiveRouteStatus == EObjectiveRouteStatus::Ready ? TEXT("true") : TEXT("false"),
		static_cast<unsigned long long>(ObjectiveRouteQuerySerial), ObjectiveRouteLastResult,
		ObjectiveRouteTargetKey, ObjectiveRoutePawnKey,
		static_cast<unsigned long long>(ObjectiveRouteRunSeed), ObjectiveRouteFloorIndex,
		ObjectiveRouteWaypoint.X, ObjectiveRouteWaypoint.Y, ObjectiveRouteWaypoint.Z,
		ObjectiveRouteFaultModeForTests, TargetKind,
		TargetEnemy ? TargetEnemy->GetRoomIndex() : INDEX_NONE,
		TargetSpawner ? TargetSpawner->GetLivingOrdinaryEnemyCount() : INDEX_NONE);
}

bool AUegameHUD::TryGetObjectiveRouteWaypointForTests(FVector& OutWaypoint) const
{
	if (ObjectiveRouteStatus != EObjectiveRouteStatus::Ready
		|| ObjectiveRouteWaypoint.IsNearlyZero())
	{
		OutWaypoint = FVector::ZeroVector;
		return false;
	}

	OutWaypoint = ObjectiveRouteWaypoint;
	return true;
}
#endif

void AUegameHUD::DrawHUD()
{
	Super::DrawHUD();

	if (CVarShowReadout.GetValueOnGameThread() == 0)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font || !Canvas)
	{
		return;
	}
	const float Scale = FMath::Clamp(CVarReadoutScale.GetValueOnGameThread(), 0.5f, 4.0f);

	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	ULoadoutComponent* LC = Pawn ? Pawn->FindComponentByClass<ULoadoutComponent>() : nullptr;
	UBuildSynergyComponent* Synergy = Pawn
		? Pawn->FindComponentByClass<UBuildSynergyComponent>() : nullptr;
	UPresentationFeedbackComponent* Presentation = Pawn
		? Pawn->FindComponentByClass<UPresentationFeedbackComponent>() : nullptr;
	UHealthComponent* HP = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	ADungeonSpawner* Spawner = FindSpawner(World);

	// Shipping-path combat feedback: Canvas primitives are normal HUD rendering, not debug draw.
	// The component retains only event-derived expiry and never supplies gameplay truth.
	if (Presentation)
	{
		const float CenterX = Canvas->SizeX * 0.5f;
		const float CenterY = Canvas->SizeY * 0.5f;
		const float SwingAlpha = Presentation->GetSwingAlpha();
		if (SwingAlpha > 0.0f)
		{
			const FLinearColor SwingColor = Presentation->WasLastSwingKill()
				? FLinearColor(1.0f, 0.2f, 0.12f, SwingAlpha)
				: FLinearColor(1.0f, 0.84f, 0.20f, SwingAlpha);
			const float Reach = 52.0f * Scale;
			DrawLine(CenterX - Reach, CenterY + Reach * 0.55f,
				CenterX + Reach, CenterY - Reach * 0.55f, SwingColor, 4.0f * Scale);
			DrawLine(CenterX - Reach * 0.72f, CenterY + Reach * 0.82f,
				CenterX + Reach * 0.72f, CenterY - Reach * 0.82f, SwingColor, 2.0f * Scale);
		}
		const float HitAlpha = Presentation->GetHitMarkerAlpha();
		if (HitAlpha > 0.0f)
		{
			const FLinearColor HitColor = Presentation->WasLastSwingKill()
				? FLinearColor(1.0f, 0.15f, 0.1f, HitAlpha)
				: FLinearColor(1.0f, 1.0f, 1.0f, HitAlpha);
			const float Inner = 7.0f * Scale;
			const float Outer = 18.0f * Scale;
			DrawLine(CenterX - Outer, CenterY - Outer, CenterX - Inner, CenterY - Inner,
				HitColor, 3.0f * Scale);
			DrawLine(CenterX + Inner, CenterY + Inner, CenterX + Outer, CenterY + Outer,
				HitColor, 3.0f * Scale);
			DrawLine(CenterX + Inner, CenterY - Inner, CenterX + Outer, CenterY - Outer,
				HitColor, 3.0f * Scale);
			DrawLine(CenterX - Outer, CenterY + Outer, CenterX - Inner, CenterY + Inner,
				HitColor, 3.0f * Scale);
		}
	}

	// ---------------- Left panel: player / build / reward offer ----------------
	TArray<FHudLine> Left;
	Left.Add({ TEXT("PLAYER / BUILD"), kHeader });

	if (HP)
	{
		Left.Add({ FString::Printf(TEXT("HP        %.0f / %.0f"), HP->GetHP(), HP->GetMaxHP()), kBody });
	}
	if (LC && LC->HasResolvedStats())
	{
		const int32 AtkMs = LC->GetResolvedAttackMs();
		const float Aps = (AtkMs > 0) ? (1000.0f / static_cast<float>(AtkMs)) : 0.0f;
		Left.Add({ FString::Printf(TEXT("Damage    %d"), LC->GetResolvedDamage()), kBody });
		Left.Add({ FString::Printf(TEXT("Attack    %d ms  (%.2f/s)"), AtkMs, Aps), kBody });

		const TArray<int32>& Picks = LC->GetChosenAffixIds();
		if (Picks.Num() == 0)
		{
			Left.Add({ TEXT("Picks:    none yet"), kDim });
		}
		else
		{
			FString P;
			for (int32 Id : Picks)
			{
				P += FString::Printf(TEXT("%d(%s) "), Id, *LC->DescribeAffixById(Id));
			}
			Left.Add({ FString::Printf(TEXT("Picks:    %s"), *P.TrimStartAndEnd()), kBody });
		}

		if (LC->IsRewardPending())
		{
			Left.Add({ FString::Printf(TEXT("REWARD PENDING (floor %d) - press 1/2/3"), LC->GetFloorForOffer()), kAccent });
			const TArray<int32>& Offer = LC->GetCurrentOfferIds();
			for (int32 i = 0; i < Offer.Num(); ++i)
			{
				Left.Add({ FString::Printf(TEXT("   [%d] %s"), i + 1, *LC->DescribeAffixById(Offer[i])), kAccent });
			}
		}
	}
	else
	{
		Left.Add({ TEXT("(loadout not initialized)"), kDim });
	}
	if (Synergy)
	{
		const int32 E = Synergy->GetExecutionerRank();
		const int32 T = Synergy->GetTempoRank();
		const int32 B = Synergy->GetBulwarkRank();
		Left.Add({ E > 0
			? FString::Printf(TEXT("Executioner R%d  finish <=40%% HP"), E)
			: TEXT("Executioner --"), E > 0 ? kBody : kDim });
		Left.Add({ T > 0
			? FString::Printf(TEXT("Tempo       R%d  chain %d/%d"),
				T, Synergy->GetTempoChain(), Synergy->GetTempoThreshold())
			: TEXT("Tempo       --"), T > 0 ? kBody : kDim });
		Left.Add({ B > 0
			? (Synergy->IsCounterReady()
				? FString::Printf(TEXT("Bulwark     R%d  COUNTER %.2fs"),
					B, Synergy->GetCounterRemainingSeconds())
				: FString::Printf(TEXT("Bulwark     R%d  take a hit, then counter"), B))
			: TEXT("Bulwark     --"),
			Synergy->IsCounterReady() ? kAccent : (B > 0 ? kBody : kDim) });
	}
	if (FM && FM->IsRunActive())
	{
		Left.Add({ FString::Printf(TEXT("Resolve     %d / 2  (-30 Warden Guard each)"),
			FM->GetResolveTokens()), FM->GetResolveTokens() > 0 ? kAccent : kDim });
	}
	DrawPanel(this, Font, 24.0f, 24.0f, Left, Scale);

	// ---------------- Right panel: run / floor / room role / encounter tally ----------------
	TArray<FHudLine> Right;
	Right.Add({ TEXT("RUN / ENCOUNTER"), kHeader });

	if (FM && FM->IsRunActive())
	{
		Right.Add({ FString::Printf(TEXT("Floor %d    seed 0x%llx"),
			FM->GetFloorIndex(), static_cast<unsigned long long>(FM->GetRunSeed())), kBody });
		Right.Add({ FString::Printf(TEXT("State      %s"), RunStateName(FM->GetRunState())), kBody });
		Right.Add({ FString::Printf(TEXT("Resolve    %d / 2"), FM->GetResolveTokens()),
			FM->GetResolveTokens() > 0 ? kAccent : kDim });
		if (FM->IsRoomContractPending())
		{
			Right.Add({ FString::Printf(TEXT("Contract   CHOOSE  Secure R%d / Challenge R%d"),
				FM->GetSecureContractRoom(), FM->GetChallengeContractRoom()), kAccent });
		}
		else if (FM->GetRoomContractChoice() == EUegameRoomContractChoice::Secure)
		{
			Right.Add({ FString::Printf(TEXT("Contract   SECURE R%d  +25%% HP now"), FM->GetSelectedContractRoom()), kBody });
		}
		else if (FM->GetRoomContractChoice() == EUegameRoomContractChoice::Challenge)
		{
			Right.Add({ FString::Printf(TEXT("Contract   CHALLENGE R%d  +50%% HP +1 Resolve on clear"), FM->GetSelectedContractRoom()), kAccent });
		}
		Right.Add({ FString::Printf(TEXT("Rooms      %d / %d required  (%d total)"),
			FM->GetClearedCombatRooms(), FM->GetRequiredCombatRooms(), FM->GetActualCombatRooms()), kBody });
		Right.Add({ FString::Printf(TEXT("Exit       objective %s  safe %s  skipped %d"),
			FM->IsFloorObjectiveComplete() ? TEXT("READY") : TEXT("OPEN"),
			FM->AreFloorExitThreatsWithdrawn() ? TEXT("YES") : TEXT("NO"),
			FM->GetAbandonedCombatRooms()),
			FM->IsProgressionBlockedByExitSafety() ? kAccent : kDim });
	}
	else
	{
		Right.Add({ TEXT("(no active run)"), kDim });
	}

	if (Spawner && Spawner->HasEncounterAssignment())
	{
		// Same snapshot-vs-live staleness guard the verbs use: never show a role/roster from a stale
		// spawner snapshot as if it were current.
		const bool bStale = !FM
			|| Spawner->GetEncounterRunSeed() != FM->GetRunSeed()
			|| Spawner->GetEncounterFloorIndex() != FM->GetFloorIndex();

		if (bStale)
		{
			// Truthfulness: a stale snapshot must not surface ANY encounter data as current - not the
			// role, and not the tally/typeHash either (they come from the same snapshot). Show one
			// honest "(stale cache)" line and nothing that could be mistaken for the live floor.
			Right.Add({ TEXT("Encounter: (stale cache)"), kDim });
		}
		else
		{
			const int32 Ri = NearestRoomIndex(Spawner, Pawn);
			const TArray<int32>& Roles = Spawner->GetCachedRoomRoles();
			if (Roles.IsValidIndex(Ri))
			{
				Right.Add({ FString::Printf(TEXT("Room %d     role: %s"),
					Ri, FUegameEncounterConfig::RoleName(Roles[Ri])), kBody });
			}
			else
			{
				Right.Add({ TEXT("Room role: (n/a)"), kDim });
			}

			const FIntVector T = Spawner->GetCachedTypeTally();
			Right.Add({ FString::Printf(TEXT("Enemies    Grunt %d  Runner %d  Brute %d"), T.X, T.Y, T.Z), kBody });
			Right.Add({ FString::Printf(TEXT("typeHash   0x%llx"),
				static_cast<unsigned long long>(Spawner->GetCachedEnemyTypeHash())), kDim });
			if (Spawner->HasFinaleInitialized())
			{
				if (const ADungeonEnemy* Warden = Spawner->GetWarden())
				{
					const bool bGuarded = Warden->GetWardenPhase() == EUegameWardenPhase::Guarded;
					Right.Add({ FString::Printf(TEXT("WARDEN    %s  %s %d/%d"),
						WardenPhaseName(Warden->GetWardenPhase()),
						bGuarded ? TEXT("Guard") : TEXT("HP"),
						Warden->GetCombatPoolCurrent(), Warden->GetCombatPoolMax()), kAccent });
					Right.Add({ FString::Printf(TEXT("Finale     ordinary threats %d"),
						Spawner->GetLivingOrdinaryEnemyCount()), kBody });
				}
				else if (Spawner->IsWardenDefeated())
				{
					Right.Add({ TEXT("WARDEN    DEFEATED"), kAccent });
				}
			}
		}
	}
	else
	{
		Right.Add({ TEXT("(no encounter assignment)"), kDim });
	}

	// Right-align the panel near the top-right edge.
	float RightContentW = 0.0f, RightLineH = 0.0f;
	MeasurePanel(this, Font, Right, Scale, RightContentW, RightLineH);
	const float RightPanelW = RightContentW + 2.0f * (8.0f * Scale);
	const float Rx = FMath::Max(24.0f, Canvas->SizeX - 24.0f - RightPanelW);
	DrawPanel(this, Font, Rx, 24.0f, Right, Scale);

	// ---------------- Objective: live target plus capsule-agent NavMesh waypoint ----------------
	// The target is a read-only view of current gameplay state. Route certification comes only
	// from the pawn's matching NavData and a non-partial path; any authority/query failure clears it.
	TArray<FHudLine> Objective;
	const bool bObjectivePawnValid = IsValid(Pawn) && !Pawn->IsActorBeingDestroyed()
		&& (!HP || !HP->IsDead());
	FObjectiveSelection ObjectiveSelection = SelectObjective(
		World, bObjectivePawnValid ? Pawn : nullptr, LC, FM,
		ObjectiveTargetLock.Get(), ObjectiveTargetLockRunSeed,
		ObjectiveTargetLockFloorIndex, ObjectiveTargetLockSpawnerKey,
#if !UE_BUILD_SHIPPING
		ObjectiveRouteFaultModeForTests
#else
		0
#endif
	);
	if (ObjectiveSelection.Target)
	{
		const bool bLockChanged = ObjectiveTargetLock.Get() != ObjectiveSelection.Target
			|| ObjectiveTargetLockRunSeed != ObjectiveSelection.RunSeed
			|| ObjectiveTargetLockFloorIndex != ObjectiveSelection.FloorIndex
			|| ObjectiveTargetLockSpawnerKey != ObjectiveSelection.SpawnerKey;
		if (bLockChanged)
		{
			const uint32 OldTargetKey = ObjectiveRouteTargetKey;
			ObjectiveTargetLock = ObjectiveSelection.Target;
			ObjectiveTargetLockRunSeed = ObjectiveSelection.RunSeed;
			ObjectiveTargetLockFloorIndex = ObjectiveSelection.FloorIndex;
			ObjectiveTargetLockSpawnerKey = ObjectiveSelection.SpawnerKey;
			InvalidateObjectiveRoute();
			bObjectiveRouteDeferQueryOnce = true;
#if !UE_BUILD_SHIPPING
			UE_LOG(LogTemp, Display,
				TEXT("[ObjectiveRouteInvalidated] reason=identity-change oldTargetKey=%u newTargetKey=%u runSeed=%llu floor=%d arrow=false stale=false"),
				OldTargetKey, ObjectiveSelection.TargetKey,
				static_cast<unsigned long long>(ObjectiveSelection.RunSeed), ObjectiveSelection.FloorIndex);
#endif
		}
		if (CVarShowObjectiveRoute.GetValueOnGameThread() != 0)
		{
			ObjectiveSelection.Line.Text += TEXT("  ");
			ObjectiveSelection.Line.Text += ResolveObjectiveRouteCue(
				World, Pawn, ObjectiveSelection.Target, ObjectiveSelection.TargetKey,
				ObjectiveSelection.RunSeed, ObjectiveSelection.FloorIndex,
				ObjectiveSelection.TargetLocation);
		}
		else
		{
			// A disabled route is a strict zero-query baseline, never a frozen arrow.
			InvalidateObjectiveRoute();
		}
	}
	else
	{
		const uint32 OldTargetKey = ObjectiveRouteTargetKey;
		InvalidateObjectiveTarget();
#if !UE_BUILD_SHIPPING
		if (OldTargetKey != 0)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[ObjectiveRouteInvalidated] reason=no-authoritative-target oldTargetKey=%u newTargetKey=0 arrow=false stale=false"),
				OldTargetKey);
		}
#endif
	}
	Objective.Add(ObjectiveSelection.Line);
	float ObjectiveW = 0.0f, ObjectiveLineH = 0.0f;
	MeasurePanel(this, Font, Objective, Scale, ObjectiveW, ObjectiveLineH);
	const float ObjectivePanelW = ObjectiveW + 16.0f * Scale;
	const float ObjectivePanelH = ObjectiveLineH + 16.0f * Scale;
	const float ObjectiveY = FMath::Max(24.0f, Canvas->SizeY - 24.0f - ObjectivePanelH);
	DrawPanel(this, Font, FMath::Max(24.0f, (Canvas->SizeX - ObjectivePanelW) * 0.5f),
		ObjectiveY, Objective, Scale);

	// ---------------- Center: onboarding plus authoritative pause/result flow ----------------
	TArray<FHudLine> Center;
	if (FM && FM->IsRunActive())
	{
		if (FM->IsRoomContractPending())
		{
			Center.Add({ TEXT("ROOM CONTRACT"), kAccent });
			Center.Add({ FString::Printf(TEXT("[1] SECURE R%d  +25%% HP NOW"),
				FM->GetSecureContractRoom()), kBody });
			Center.Add({ FString::Printf(TEXT("[2] CHALLENGE R%d  Stronger threats, +50%% HP +1 RESOLVE AFTER CLEAR"),
				FM->GetChallengeContractRoom()), kAccent });
			Center.Add({ TEXT("Choose 1 or 2 to begin this floor"), kDim });
		}
		else if (FM->IsProgressionBlockedByExitSafety())
		{
			Center.Add({ TEXT("EXIT BLOCKED - SAFETY CHECK FAILED"), FLinearColor(1.0f, 0.25f, 0.2f, 1.0f) });
			Center.Add({ TEXT("Try the stairs again"), kBody });
		}
		else if (FM->HasRoomContractFallbackWarning())
		{
			Center.Add({ TEXT("CHALLENGE UNAVAILABLE - SECURE AUTO-SELECTED"), kAccent });
		}
		else if (FM->HasRoomContractUnavailableWarning())
		{
			Center.Add({ TEXT("ROOM CONTRACT UNAVAILABLE - CONTINUING SAFELY"), kAccent });
		}
		else switch (FM->GetRunState())
		{
		case m8authority::RunState::Paused:
			Center.Add({ TEXT("PAUSED"), kAccent });
			Center.Add({ TEXT("Esc Resume    R Restart    Q Quit"), kBody });
			break;
		case m8authority::RunState::Won:
			Center.Add({ TEXT("RUN WON"), FLinearColor(0.35f, 1.0f, 0.45f, 1.0f) });
			Center.Add({ TEXT("R Play Again    Q Quit"), kBody });
			break;
		case m8authority::RunState::Failed:
			Center.Add({ TEXT("RUN FAILED"), FLinearColor(1.0f, 0.25f, 0.2f, 1.0f) });
			Center.Add({ TEXT("R Restart    Q Quit"), kBody });
			break;
		case m8authority::RunState::Error:
			Center.Add({ TEXT("FINAL CHALLENGE ERROR"), FLinearColor(1.0f, 0.25f, 0.2f, 1.0f) });
			Center.Add({ TEXT("R Restart    Q Quit"), kBody });
			break;
		case m8authority::RunState::Playing:
			Center.Add({ TEXT("WASD Move  Mouse Look  F Attack  Esc Pause  Q Quit"), kDim });
			break;
		default:
			break;
		}
	}
	if (Center.Num() > 0)
	{
		float CenterW = 0.0f, CenterLineH = 0.0f;
		MeasurePanel(this, Font, Center, Scale, CenterW, CenterLineH);
		const float CenterPanelW = CenterW + 2.0f * (8.0f * Scale);
		const float CenterPanelH = Center.Num() * CenterLineH
			+ FMath::Max(0, Center.Num() - 1) * 3.0f * Scale + 16.0f * Scale;
		DrawPanel(this, Font, (Canvas->SizeX - CenterPanelW) * 0.5f,
			FMath::Max(24.0f, ObjectiveY - 12.0f * Scale - CenterPanelH), Center, Scale);
	}

	// One transient cue lane. Presentation priority wins; the existing synergy component remains
	// the fallback live owner for proc text, so the HUD never caches or duplicates either state.
	FString TransientCue = Presentation ? Presentation->GetTransientText() : FString();
	if (TransientCue.IsEmpty() && Synergy)
	{
		TransientCue = Synergy->GetFeedbackText();
	}
	if (!TransientCue.IsEmpty())
	{
		TArray<FHudLine> CueLines;
		CueLines.Add({ TransientCue, kAccent });
		float CueW = 0.0f, CueH = 0.0f;
		MeasurePanel(this, Font, CueLines, Scale * 1.25f, CueW, CueH);
		DrawPanel(this, Font,
			FMath::Max(24.0f, (Canvas->SizeX - CueW - 16.0f * Scale) * 0.5f),
			Canvas->SizeY * 0.72f, CueLines, Scale * 1.25f);
	}

	// ---------------- M7A.2: per-enemy readout (nameplates + HP bars) ----------------
	// Same truthfulness contract as the panels: every value below is read live off the
	// actor (numeric archetype id) and its UHealthComponent - nothing is cached, inferred
	// from position/stats, or written back. Budgets (floor 3 spawns at most 36 enemies):
	// <=36 iterator steps, <=MaxCount*2 occlusion traces, <=MaxCount labels, zero UObject
	// creation, per frame.
	if (CVarShowEnemyReadout.GetValueOnGameThread() == 0 || !World || !PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const int32 MaxCount = FMath::Clamp(CVarEnemyReadoutMaxCount.GetValueOnGameThread(), 0, 36);
	if (MaxCount == 0)
	{
		return;
	}
	const float EScale  = FMath::Clamp(CVarEnemyReadoutScale.GetValueOnGameThread(), 0.5f, 4.0f);
	const float MaxDist = FMath::Clamp(CVarEnemyReadoutMaxDistance.GetValueOnGameThread(), 200.0f, 10000.0f);

	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector CamFwd = PC->PlayerCameraManager->GetCameraRotation().Vector();

	// Cheap trace-free filters: valid, alive, in range, in front of the camera.
	TArray<FEnemyReadoutCandidate> Candidates;
	Candidates.Reserve(36);
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		ADungeonEnemy* E = *It;
		if (!IsValid(E) || E->IsActorBeingDestroyed())
		{
			continue;
		}
		const UHealthComponent* HC = E->GetHealthComponent();
		if (!HC || HC->IsDead())
		{
			continue;
		}
		const FVector ToEnemy = E->GetActorLocation() - CamLoc;
		const float DistSq = ToEnemy.SizeSquared();
		if (DistSq > MaxDist * MaxDist || FVector::DotProduct(ToEnemy, CamFwd) <= 0.0f)
		{
			continue;
		}
		Candidates.Add({ E, DistSq });
	}

	// Nearest first; UniqueID (constant per actor lifetime) is a presentation-only
	// tiebreak so equidistant enemies never swap label slots between frames.
	Candidates.Sort([](const FEnemyReadoutCandidate& A, const FEnemyReadoutCandidate& B)
	{
		return (A.DistSq != B.DistSq) ? A.DistSq < B.DistSq
		                              : A.Enemy->GetUniqueID() < B.Enemy->GetUniqueID();
	});

	// Bounded refill (owner ruling): walk candidates nearest-first, skip occluded ones and
	// keep refilling from farther candidates, until MaxCount labels are drawn or the trace
	// budget (MaxCount*2) is spent. Never "nearest 8 are all behind walls so nothing draws",
	// yet still a hard per-frame trace ceiling.
	int32 TraceBudget = FMath::Min(Candidates.Num(), MaxCount * 2);
	int32 Drawn = 0;
	for (const FEnemyReadoutCandidate& C : Candidates)
	{
		if (Drawn >= MaxCount || TraceBudget <= 0)
		{
			break;
		}

		// Label anchor from the combined actor bounds (bOnlyCollidingComponents=false: the
		// BodyMesh is NoCollision and must count). Tracks every archetype's real top - the
		// x1.4 Brute mesh rises above the constant capsule; a hardcoded capsule-top Z would
		// clip it (mesh top = 88*(2S-1) vs capsule top = 88).
		FVector Origin, Extent;
		C.Enemy->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
		const FVector Anchor(Origin.X, Origin.Y, Origin.Z + Extent.Z);

		// Project through the HUD's own canvas scene view (same projection the frame renders
		// with, so resolution/aspect/DPI agree by construction). bClampToZeroPlane=false keeps
		// Z sign-meaningful: Z<=0 = behind the camera (second line of defense after the dot
		// product above). Off-screen anchors are culled without spending a trace.
		const FVector Proj = Project(Anchor, /*bClampToZeroPlane=*/false);
		if (Proj.Z <= 0.0f ||
			Proj.X < 0.0f || Proj.X > Canvas->SizeX ||
			Proj.Y < 0.0f || Proj.Y > Canvas->SizeY)
		{
			continue;
		}

		// Occlusion: camera->anchor against WorldStatic objects only - the same discipline as
		// ADungeonEnemy::ComputeLOSTo (dungeon walls occlude; pawns never block a label).
		--TraceBudget;
		FHitResult Hit;
		FCollisionQueryParams TraceParams(FName(TEXT("EnemyReadout")), /*bTraceComplex=*/false);
		if (World->LineTraceSingleByObjectType(
				Hit, CamLoc, Anchor, FCollisionObjectQueryParams(ECC_WorldStatic), TraceParams))
		{
			continue;   // behind a wall: refill from the next candidate
		}

		const UHealthComponent* HC = C.Enemy->GetHealthComponent();   // re-fetch: cheap, and no stale pointer risk
		if (!HC)
		{
			continue;
		}

		// Nameplate text reads the authoritative combat pool: Warden guard while guarded, HP after.
		// Unassigned enemies (no M6
		// assignment: static spawners, unavailable tables) read dim + neutral - never "Grunt".
		const int32 PoolCurrent = C.Enemy->GetCombatPoolCurrent();
		const int32 PoolMax = C.Enemy->GetCombatPoolMax();
		const bool bWardenGuard = C.Enemy->IsWarden()
			&& C.Enemy->GetWardenPhase() == EUegameWardenPhase::Guarded;
		const FString Label = C.Enemy->IsWarden()
			? FString::Printf(TEXT("WARDEN [%s] %s %d/%d"),
				WardenPhaseName(C.Enemy->GetWardenPhase()),
				bWardenGuard ? TEXT("Guard") : TEXT("HP"), PoolCurrent, PoolMax)
			: FString::Printf(TEXT("%s %d/%d"),
				C.Enemy->GetArchetypeDisplayName(), PoolCurrent, PoolMax);
		float TextW = 0.0f, TextH = 0.0f;
		GetTextSize(Label, TextW, TextH, Font, EScale);

		const float BarW  = 56.0f * EScale;
		const float BarH  = 6.0f  * EScale;
		const float PadX  = 3.0f  * EScale;
		const float PadY  = 2.0f  * EScale;
		const float Gap   = 2.0f  * EScale;    // text-to-bar gap
		const float LiftPx = 14.0f * EScale;   // screen-space gap above the head (constant on screen, not world-scaled)

		const float BlockW = FMath::Max(TextW, BarW) + 2.0f * PadX;
		const float BlockH = TextH + Gap + BarH + 2.0f * PadY;
		const float X = static_cast<float>(Proj.X) - 0.5f * BlockW;
		const float Y = static_cast<float>(Proj.Y) - LiftPx - BlockH;

		DrawRect(kPanelBg, X, Y, BlockW, BlockH);
		DrawText(Label, C.Enemy->HasArchetypeAssignment() ? kBody : kDim,
			X + 0.5f * (BlockW - TextW), Y + PadY, Font, EScale, /*bScalePosition=*/false);

		// HP bar: fixed screen-space size, live ratio, UI-space color by remaining fraction.
		const float Ratio = FMath::Clamp(
			static_cast<float>(PoolCurrent) / FMath::Max(static_cast<float>(PoolMax), 1.0f),
			0.0f, 1.0f);
		const FLinearColor Fill = (Ratio > 0.5f) ? kHpGood : (Ratio > 0.25f ? kAccent : kHpBad);
		const float BarX = static_cast<float>(Proj.X) - 0.5f * BarW;
		const float BarY = Y + PadY + TextH + Gap;
		DrawRect(kBarBack, BarX, BarY, BarW, BarH);
		if (Ratio > 0.0f)
		{
			DrawRect(Fill, BarX, BarY, BarW * Ratio, BarH);
		}
		++Drawn;
	}
}
