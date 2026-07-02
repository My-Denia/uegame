// CombatConfig.cpp - see header.

#include "CombatConfig.h"

#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FCombatConfigRow GCachedRow;
	bool GLoaded = false;
	bool GFromTable = false;

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
			if (Problems.Num() == 0 && Row)
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
			}
		}

		if (!GFromTable)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CombatConfig] FALLBACK to compiled defaults - %s missing/invalid"), *CsvPath);
		}

		// Evidence: echo the active row once, so every combat number in later logs traces here.
		const FCombatConfigRow& R = GCachedRow;
		UE_LOG(LogTemp, Display,
			TEXT("[CombatConfig] source=%s | enemy{MaxHP=%.0f MoveSpeed=%.0f ContactDamage=%.0f DamageInterval=%.2f} player{MaxHP=%.0f AttackDamage=%.0f AttackRange=%.0f AttackCooldown=%.2f} EnemiesPerRoom=%d PerFloorScaling=%.2f (reserved M4)"),
			GFromTable ? TEXT("DataTable(CSV)") : TEXT("compiled-fallback"),
			R.EnemyMaxHP, R.EnemyMoveSpeed, R.EnemyContactDamage, R.EnemyDamageInterval,
			R.PlayerMaxHP, R.PlayerAttackDamage, R.PlayerAttackRange, R.PlayerAttackCooldown,
			R.EnemiesPerRoom, R.PerFloorScaling);
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
