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
#include "Math/UnrealMathUtility.h"

#include "../Combat/DungeonEnemy.h"
#include "../Combat/EncounterConfig.h"
#include "../Combat/FloorManager.h"
#include "../Combat/HealthComponent.h"
#include "../Combat/LoadoutComponent.h"
#include "../DungeonSpawner.h"

// --- Toggles (default ON: this is readability UI we want visible in normal play + capture) ---
static TAutoConsoleVariable<int32> CVarShowReadout(
	TEXT("ui.ShowReadout"), 1,
	TEXT("M7A.1 readability HUD overlay: 1 = draw player/loadout/room-role/encounter readout, 0 = draw nothing."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarReadoutScale(
	TEXT("ui.ShowReadoutScale"), 1.0f,
	TEXT("M7A.1 readability HUD text scale multiplier (clamped 0.5..4.0; default 1.0)."),
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
	UHealthComponent* HP = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	ADungeonSpawner* Spawner = FindSpawner(World);

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
	DrawPanel(this, Font, 24.0f, 24.0f, Left, Scale);

	// ---------------- Right panel: run / floor / room role / encounter tally ----------------
	TArray<FHudLine> Right;
	Right.Add({ TEXT("RUN / ENCOUNTER"), kHeader });

	if (FM && FM->IsRunActive())
	{
		Right.Add({ FString::Printf(TEXT("Floor %d    seed 0x%llx"),
			FM->GetFloorIndex(), static_cast<unsigned long long>(FM->GetRunSeed())), kBody });
		Right.Add({ FString::Printf(TEXT("State      %s"), RunStateName(FM->GetRunState())), kBody });
		if (FM->IsRoomContractPending())
		{
			Right.Add({ FString::Printf(TEXT("Contract   CHOOSE  Secure R%d / Challenge R%d"),
				FM->GetSecureContractRoom(), FM->GetChallengeContractRoom()), kAccent });
		}
		else if (FM->GetRoomContractChoice() == EUegameRoomContractChoice::Secure)
		{
			Right.Add({ FString::Printf(TEXT("Contract   SECURE R%d"), FM->GetSelectedContractRoom()), kBody });
		}
		else if (FM->GetRoomContractChoice() == EUegameRoomContractChoice::Challenge)
		{
			Right.Add({ FString::Printf(TEXT("Contract   CHALLENGE R%d"), FM->GetSelectedContractRoom()), kAccent });
		}
		Right.Add({ FString::Printf(TEXT("Exit       objective %s  safe %s"),
			FM->IsFloorObjectiveComplete() ? TEXT("READY") : TEXT("OPEN"),
			FM->AreFloorExitThreatsWithdrawn() ? TEXT("YES") : TEXT("NO")),
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

	// ---------------- Center: onboarding plus authoritative pause/result flow ----------------
	TArray<FHudLine> Center;
	if (FM && FM->IsRunActive())
	{
		if (FM->IsRoomContractPending())
		{
			Center.Add({ TEXT("ROOM CONTRACT"), kAccent });
			Center.Add({ FString::Printf(TEXT("[1] SECURE R%d  Recover now / [2] CHALLENGE R%d  Greater reward, stronger threats"),
				FM->GetSecureContractRoom(), FM->GetChallengeContractRoom()), kBody });
			Center.Add({ TEXT("Choose 1 or 2 to begin this floor"), kDim });
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
		DrawPanel(this, Font, (Canvas->SizeX - CenterPanelW) * 0.5f,
			Canvas->SizeY - 72.0f * Scale, Center, Scale);
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

		// Nameplate text: "<Grunt|Runner|Brute|Enemy> cur/max". Unassigned enemies (no M6
		// assignment: static spawners, unavailable tables) read dim + neutral - never "Grunt".
		const FString Label = FString::Printf(TEXT("%s %.0f/%.0f"),
			C.Enemy->GetArchetypeDisplayName(), HC->GetHP(), HC->GetMaxHP());
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
		const float Ratio = FMath::Clamp(HC->GetHP() / FMath::Max(HC->GetMaxHP(), 1.0f), 0.0f, 1.0f);
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
