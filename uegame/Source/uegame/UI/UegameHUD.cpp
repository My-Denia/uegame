// UegameHUD.cpp - see header. Truthful, presentation-only readability overlay.

#include "UegameHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"      // GEngine->GetSmallFont
#include "Engine/Font.h"
#include "EngineUtils.h"        // TActorIterator
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Math/UnrealMathUtility.h"

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
}
