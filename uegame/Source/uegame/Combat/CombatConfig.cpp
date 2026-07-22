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
			if (Problems.Num() == 0 && Row && m8room::validate(RoomFlowConfig(*Row)))
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
			TEXT("[CombatConfig] source=%s | enemy{MaxHP=%.0f MoveSpeed=%.0f ContactDamage=%.0f DamageInterval=%.2f AggroRange=%.0f LeashRange=%.0f} player{MaxHP=%.0f AttackDamage=%.0f AttackRange=%.0f AttackCooldown=%.2f} EnemiesPerRoom=%d PerFloorScaling=%.2f recovery{room=%.2f/%.2f/%.2f/%.2f rewardFloor=%.2f} roomQuota{base=%d perFloor=%d} requireClearToDescend=%s maxFloors=%d"),
			GFromTable ? TEXT("DataTable(CSV)") : TEXT("compiled-fallback"),
			R.EnemyMaxHP, R.EnemyMoveSpeed, R.EnemyContactDamage, R.EnemyDamageInterval,
			R.AggroRange, R.LeashRange,
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
