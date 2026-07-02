// CombatConfig.h - loads the single combat config row from Content/Data/CombatConfig.csv
// at first use (runtime CSV -> UDataTable), echoes the row to the log once (evidence),
// and falls back to compiled defaults ONLY with a loud warning.

#pragma once

#include "CoreMinimal.h"
#include "CombatTypes.h"

struct FUegameCombatConfig
{
	/** The active config row. First call loads + logs; later calls return the cache. */
	static const FCombatConfigRow& Get();

	/** True if the row came from the CSV (false = compiled fallback defaults). */
	static bool IsFromDataTable();
};
