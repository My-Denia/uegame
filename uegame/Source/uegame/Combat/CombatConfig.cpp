// CombatConfig.cpp - see header.

#include "CombatConfig.h"

#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "m8_room_flow.hpp"

namespace
{
	FCombatConfigRow GCachedRow;
	bool GLoaded = false;
	bool GFromTable = false;

	m8room::Config RoomFlowConfig(const FCombatConfigRow& Row)
	{
		return {{
			Row.QuietRoomClearHealFraction,
			Row.StandardRoomClearHealFraction,
			Row.SkirmishRoomClearHealFraction,
			Row.StrongholdRoomClearHealFraction
		}, Row.RewardRecoveryFloorFraction,
			Row.RequiredCombatRoomsBase, Row.RequiredCombatRoomsPerFloor};
	}

	bool IsPositiveHalfSecondMultiple(float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f
			&& FMath::IsNearlyEqual(Value * 2.0f, FMath::RoundToFloat(Value * 2.0f));
	}

	bool WardenProfileValid(const FCombatConfigRow& Row)
	{
		return FMath::IsFinite(Row.WardenMaxHP) && Row.WardenMaxHP > 0.0f
			&& FMath::IsFinite(Row.WardenMoveSpeed) && Row.WardenMoveSpeed > 0.0f
			&& FMath::IsFinite(Row.WardenContactDamage) && Row.WardenContactDamage > 0.0f
			&& IsPositiveHalfSecondMultiple(Row.WardenDamageInterval)
			&& FMath::IsFinite(Row.WardenAggroRange) && Row.WardenAggroRange > 0.0f
			&& FMath::IsFinite(Row.WardenLeashRange) && Row.WardenLeashRange > Row.WardenAggroRange
			&& FMath::IsFinite(Row.WardenMeshScale)
			&& Row.WardenMeshScale >= 1.0f && Row.WardenMeshScale <= 3.0f
			&& Row.WardenBaseGuard > 0
			&& Row.WardenGuardReductionPerResolve >= 0
			&& Row.WardenMinimumGuard > 0
			&& Row.WardenBaseGuard >= Row.WardenMinimumGuard
			&& Row.WardenBaseGuard - 2 * Row.WardenGuardReductionPerResolve
				>= Row.WardenMinimumGuard
			&& FMath::IsFinite(Row.WardenStaggerSeconds) && Row.WardenStaggerSeconds > 0.0f
			&& FMath::IsFinite(Row.WardenStaggerDamageMultiplier)
			&& Row.WardenStaggerDamageMultiplier >= 1.0f;
	}

	void LoadOnce()
	{
		if (GLoaded)
		{
			return;
		}
		GLoaded = true;

		const FString CsvPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/CombatConfig.csv"));
		FString CsvText;
		if (FFileHelper::LoadFileToString(CsvText, *CsvPath))
		{
			UDataTable* Table = NewObject<UDataTable>();
			Table->RowStruct = FCombatConfigRow::StaticStruct();
			const TArray<FString> Problems = Table->CreateTableFromCSVString(CsvText);
			const FCombatConfigRow* Row =
				Table->FindRow<FCombatConfigRow>(TEXT("Default"), TEXT("CombatConfig"), false);
			if (Problems.Num() == 0 && Row && m8room::validate(RoomFlowConfig(*Row))
				&& WardenProfileValid(*Row))
			{
				GCachedRow = *Row;
				GFromTable = true;
			}
			else
			{
				for (const FString& P : Problems)
				{
					UE_LOG(LogTemp, Warning, TEXT("[CombatConfig] CSV problem: %s"), *P);
				}
				if (Row && !m8room::validate(RoomFlowConfig(*Row)))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[CombatConfig] CSV problem: M8 recovery fractions must be finite 0..1 and room quota settings 0..1024"));
				}
				if (Row && !WardenProfileValid(*Row))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[CombatConfig] CSV problem: fixed Warden profile is invalid"));
				}
			}
		}

		if (!GFromTable)
		{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
			// Packaged (non-editor/non-dev) build: silently running the compiled struct defaults
			// on a missing/invalid CSV would be a DIFFERENT game (old balance). Fail loudly
			// instead of shipping wrong numbers. Fatal is compiled into Shipping/Test and aborts
			// here, so the compiled-fallback row below can never become the live config.
			UE_LOG(LogTemp, Fatal,
				TEXT("[CombatConfig] FATAL: %s missing/invalid in a packaged build; refusing to run compiled-fallback balance"),
				*CsvPath);
#else
			// Editor/dev build: warn but keep the compiled fallback so development is never blocked.
			UE_LOG(LogTemp, Warning,
				TEXT("[CombatConfig] FALLBACK to compiled defaults - %s missing/invalid"), *CsvPath);
#endif
		}

		// Evidence: echo the active row once, so every combat number in later logs traces here.
		const FCombatConfigRow& R = GCachedRow;
		UE_LOG(LogTemp, Display,
			TEXT("[CombatConfig] source=%s | enemy{MaxHP=%.0f MoveSpeed=%.0f ContactDamage=%.0f DamageInterval=%.2f AggroRange=%.0f LeashRange=%.0f} warden{MaxHP=%.0f MoveSpeed=%.0f AssignedDamage=%.0f CommittedDamage=%d DamageInterval=%.2f AggroRange=%.0f LeashRange=%.0f MeshScale=%.2f Guard=%d/%d/%d Stagger=%.2fs x%.2f} player{MaxHP=%.0f AttackDamage=%.0f AttackRange=%.0f AttackCooldown=%.2f} EnemiesPerRoom=%d PerFloorScaling=%.2f recovery{room=%.2f/%.2f/%.2f/%.2f rewardFloor=%.2f} roomQuota{base=%d perFloor=%d} requireClearToDescend=%s maxFloors=%d"),
			GFromTable ? TEXT("DataTable(CSV)") : TEXT("compiled-fallback"),
			R.EnemyMaxHP, R.EnemyMoveSpeed, R.EnemyContactDamage, R.EnemyDamageInterval,
			R.AggroRange, R.LeashRange,
			R.WardenMaxHP, R.WardenMoveSpeed, R.WardenContactDamage,
			FMath::Max(1, FMath::RoundToInt(R.WardenContactDamage * 0.75f)),
			R.WardenDamageInterval, R.WardenAggroRange, R.WardenLeashRange,
			R.WardenMeshScale, R.WardenBaseGuard, R.WardenGuardReductionPerResolve,
			R.WardenMinimumGuard, R.WardenStaggerSeconds, R.WardenStaggerDamageMultiplier,
			R.PlayerMaxHP, R.PlayerAttackDamage, R.PlayerAttackRange, R.PlayerAttackCooldown,
			R.EnemiesPerRoom, R.PerFloorScaling,
			R.QuietRoomClearHealFraction, R.StandardRoomClearHealFraction,
			R.SkirmishRoomClearHealFraction, R.StrongholdRoomClearHealFraction,
			R.RewardRecoveryFloorFraction,
			R.RequiredCombatRoomsBase, R.RequiredCombatRoomsPerFloor,
			R.bRequireFloorClearToDescend ? TEXT("true") : TEXT("false"), R.MaxFloors);
	}
}

const FCombatConfigRow& FUegameCombatConfig::Get()
{
	LoadOnce();
	return GCachedRow;
}

bool FUegameCombatConfig::IsFromDataTable()
{
	LoadOnce();
	return GFromTable;
}
