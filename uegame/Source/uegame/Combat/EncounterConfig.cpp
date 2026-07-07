// EncounterConfig.cpp - see header.
//
// Parsing mirrors the M6A reference parser (test_m6_encounter.cpp loadArchetypes/loadWeights)
// cell-for-cell: header column lookup by name, rows keyed by their first cell, atoi/atof
// numeric semantics, DamageInterval seconds -> deci-seconds via x10+0.5, VisualScale ->
// percent via x100+0.5. Every archetype comes from an EXPLICIT Grunt/Runner/Brute row -
// there is NO sentinel fallback to the CombatConfig Default row (M6 owner decision).
//
// Validation = the engine-agnostic m6 gates (validate_enemy_archetypes /
// validate_encounter_weights, already CTest-covered) PLUS the Grunt<->CombatConfig-Default
// parity gate: Grunt must equal the ACTIVE Default enemy row field-for-field, so the Grunt
// path is behaviourally identical to pre-M6 spawns.
//
// Failure policy mirrors CombatConfig.cpp: Shipping/Test -> Fatal (never run divergent
// balance silently); editor/dev -> loud Warning + compiled fallback, and the fallback is
// accepted ONLY if it passes the same validators + parity gate, else encounter diversity
// stays unavailable (spawns remain Default-only, forensic verbs report unavailable).

#include "EncounterConfig.h"

#include "CombatConfig.h"
#include "CombatTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Engine-agnostic M6 core (repo root, on the module's private include path). Kept out of
// every header; compiled once by M6EncounterCore.cpp (ODR).
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "m6_encounter.hpp"

#include <string>
#include <vector>

namespace
{
	const TCHAR* GTypeNames[FUegameEncounterConfig::NumTypes] = { TEXT("Grunt"), TEXT("Runner"), TEXT("Brute") };
	const TCHAR* GRoleNames[FUegameEncounterConfig::NumRoles] = { TEXT("Quiet"), TEXT("Standard"), TEXT("Skirmish"), TEXT("Stronghold") };

	bool GLoaded = false;
	bool GAvailable = false;
	bool GFromTable = false;
	std::vector<m6::Archetype> GArchetypes;                       // indexed by scan order; validated to cover all 3 ids
	m6::Archetype GByType[FUegameEncounterConfig::NumTypes];      // indexed by m6::TypeId after validation
	FEncounterArchetypeStats GStats[FUegameEncounterConfig::NumTypes];
	m6::RoleWeights GFloorRoleW = { 0, 0, 0, 0 };
	m6::TypeWeightsByRole GTypeW = {};

	// --- reference-parser numeric semantics (test_m6_encounter.cpp:55-56) ---
	int32 ToDs(const FString& S)  { return static_cast<int32>(FCString::Atod(*S) * 10.0 + 0.5); }
	int32 ToPct(const FString& S) { return static_cast<int32>(FCString::Atod(*S) * 100.0 + 0.5); }

	void SplitCsvLine(const FString& Line, TArray<FString>& Out)
	{
		Out.Reset();
		FString Work = Line;
		Work.ReplaceInline(TEXT("\r"), TEXT(""));
		Work.ParseIntoArray(Out, TEXT(","), /*CullEmpty=*/false);
	}

	int32 ColumnIndex(const TArray<FString>& Header, const TCHAR* Name)
	{
		for (int32 i = 0; i < Header.Num(); ++i)
		{
			if (Header[i] == Name)
			{
				return i;
			}
		}
		return -1;
	}

	int32 TypeIndexByName(const FString& Name)
	{
		for (int32 i = 0; i < FUegameEncounterConfig::NumTypes; ++i)
		{
			if (Name == GTypeNames[i])
			{
				return i;
			}
		}
		return -1;
	}

	int32 RoleIndexByName(const FString& Name)
	{
		for (int32 i = 0; i < FUegameEncounterConfig::NumRoles; ++i)
		{
			if (Name == GRoleNames[i])
			{
				return i;
			}
		}
		return -1;
	}

	bool LoadArchetypesCsv(const FString& Path, std::vector<m6::Archetype>& Out, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*CullEmpty=*/true);
		TArray<FString> Header;
		int32 CHp = -1, CMv = -1, CDmg = -1, CInt = -1, CAg = -1, CLe = -1, CSc = -1;
		TArray<FString> Cells;
		for (const FString& Line : Lines)
		{
			if (Header.Num() == 0)
			{
				SplitCsvLine(Line, Header);
				CHp = ColumnIndex(Header, TEXT("MaxHP"));
				CMv = ColumnIndex(Header, TEXT("MoveSpeed"));
				CDmg = ColumnIndex(Header, TEXT("ContactDamage"));
				CInt = ColumnIndex(Header, TEXT("DamageInterval"));
				CAg = ColumnIndex(Header, TEXT("AggroRange"));
				CLe = ColumnIndex(Header, TEXT("LeashRange"));
				CSc = ColumnIndex(Header, TEXT("VisualScale"));
				if (CHp < 0 || CMv < 0 || CDmg < 0 || CInt < 0 || CAg < 0 || CLe < 0 || CSc < 0)
				{
					OutError = FString::Printf(TEXT("%s missing a required column"), *Path);
					return false;
				}
				continue;
			}
			SplitCsvLine(Line, Cells);
			if (Cells.Num() <= CSc)
			{
				continue;
			}
			const int32 Ti = TypeIndexByName(Cells[0]);
			if (Ti < 0)
			{
				// Explicit-row contract: only Grunt/Runner/Brute rows are legal - an unknown
				// row name is a table error, never a silent skip or a Default read-back.
				OutError = FString::Printf(TEXT("%s has unknown archetype row '%s'"), *Path, *Cells[0]);
				return false;
			}
			m6::Archetype A;
			A.id = static_cast<m6::TypeId>(Ti);
			A.max_hp = FCString::Atoi(*Cells[CHp]);
			A.move_speed = FCString::Atoi(*Cells[CMv]);
			A.contact_damage = FCString::Atoi(*Cells[CDmg]);
			A.damage_interval_ds = ToDs(Cells[CInt]);
			A.aggro_range = FCString::Atoi(*Cells[CAg]);
			A.leash_range = FCString::Atoi(*Cells[CLe]);
			A.visual_scale_pct = ToPct(Cells[CSc]);
			Out.push_back(A);
		}
		if (Out.empty())
		{
			OutError = FString::Printf(TEXT("%s has no archetype rows"), *Path);
			return false;
		}
		return true;
	}

	bool LoadWeightsCsv(const FString& Path, m6::RoleWeights& FloorRoleW, m6::TypeWeightsByRole& TypeW, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*CullEmpty=*/true);
		TArray<FString> Header;
		int32 CKind = -1, CG = -1, CR = -1, CB = -1, CQ = -1, CS = -1, CSk = -1, CSt = -1;
		bool bGotFloor = false;
		bool bGotType[FUegameEncounterConfig::NumRoles] = { false, false, false, false };
		TArray<FString> Cells;
		for (const FString& Line : Lines)
		{
			if (Header.Num() == 0)
			{
				SplitCsvLine(Line, Header);
				CKind = ColumnIndex(Header, TEXT("Kind"));
				CG = ColumnIndex(Header, TEXT("WGrunt"));
				CR = ColumnIndex(Header, TEXT("WRunner"));
				CB = ColumnIndex(Header, TEXT("WBrute"));
				CQ = ColumnIndex(Header, TEXT("WQuiet"));
				CS = ColumnIndex(Header, TEXT("WStandard"));
				CSk = ColumnIndex(Header, TEXT("WSkirmish"));
				CSt = ColumnIndex(Header, TEXT("WStronghold"));
				if (CKind < 0 || CG < 0 || CR < 0 || CB < 0 || CQ < 0 || CS < 0 || CSk < 0 || CSt < 0)
				{
					OutError = FString::Printf(TEXT("%s missing a required column"), *Path);
					return false;
				}
				continue;
			}
			SplitCsvLine(Line, Cells);
			if (Cells.Num() <= CSt)
			{
				continue;
			}
			const FString Kind = Cells[CKind];
			if (Kind == TEXT("type"))
			{
				const int32 Ri = RoleIndexByName(Cells[0]);
				if (Ri < 0)
				{
					OutError = FString::Printf(TEXT("%s has unknown role row '%s'"), *Path, *Cells[0]);
					return false;
				}
				TypeW[Ri] = { FCString::Atoi(*Cells[CG]), FCString::Atoi(*Cells[CR]), FCString::Atoi(*Cells[CB]) };
				bGotType[Ri] = true;
			}
			else if (Kind == TEXT("floor"))
			{
				FloorRoleW = { FCString::Atoi(*Cells[CQ]), FCString::Atoi(*Cells[CS]),
				               FCString::Atoi(*Cells[CSk]), FCString::Atoi(*Cells[CSt]) };
				bGotFloor = true;
			}
		}
		if (!bGotFloor)
		{
			OutError = FString::Printf(TEXT("%s has no floor role-weight row"), *Path);
			return false;
		}
		for (int32 i = 0; i < FUegameEncounterConfig::NumRoles; ++i)
		{
			if (!bGotType[i])
			{
				OutError = FString::Printf(TEXT("%s missing the type-weight row for role %s"), *Path, GRoleNames[i]);
				return false;
			}
		}
		return true;
	}

	/** Grunt<->Default parity gate: Grunt must equal the ACTIVE CombatConfig Default enemy
	 *  fields under the reference integer conversions, so Grunt spawns are behaviourally
	 *  identical to pre-M6 spawns. Any mismatch is a validation FAIL, not a warning. */
	bool GruntMatchesCombatDefault(const m6::Archetype& Grunt, FString& OutError)
	{
		const FCombatConfigRow& D = FUegameCombatConfig::Get();
		const int32 DHp = FMath::RoundToInt(D.EnemyMaxHP);
		const int32 DMv = FMath::RoundToInt(D.EnemyMoveSpeed);
		const int32 DDmg = FMath::RoundToInt(D.EnemyContactDamage);
		const int32 DInt = static_cast<int32>(D.EnemyDamageInterval * 10.0f + 0.5f);
		const int32 DAg = FMath::RoundToInt(D.AggroRange);
		const int32 DLe = FMath::RoundToInt(D.LeashRange);
		if (Grunt.max_hp != DHp || Grunt.move_speed != DMv || Grunt.contact_damage != DDmg ||
		    Grunt.damage_interval_ds != DInt || Grunt.aggro_range != DAg || Grunt.leash_range != DLe)
		{
			OutError = FString::Printf(
				TEXT("Grunt/Default parity FAIL: Grunt{hp=%d mv=%d dmg=%d int_ds=%d aggro=%d leash=%d} vs Default{hp=%d mv=%d dmg=%d int_ds=%d aggro=%d leash=%d}"),
				Grunt.max_hp, Grunt.move_speed, Grunt.contact_damage, Grunt.damage_interval_ds,
				Grunt.aggro_range, Grunt.leash_range, DHp, DMv, DDmg, DInt, DAg, DLe);
			return false;
		}
		return true;
	}

	/** Full gate over a candidate table set. On success fills GByType/GStats. */
	bool ValidateAndAdopt(const std::vector<m6::Archetype>& Archetypes,
	                      const m6::RoleWeights& FloorRoleW, const m6::TypeWeightsByRole& TypeW,
	                      FString& OutError)
	{
		const m6::ValidationResult AV = m6::validate_enemy_archetypes(Archetypes);
		if (!AV.ok)
		{
			OutError = FString::Printf(TEXT("archetype table invalid: %s"), UTF8_TO_TCHAR(AV.reason.c_str()));
			return false;
		}
		const m6::ValidationResult WV = m6::validate_encounter_weights(FloorRoleW, TypeW);
		if (!WV.ok)
		{
			OutError = FString::Printf(TEXT("weight tables invalid: %s"), UTF8_TO_TCHAR(WV.reason.c_str()));
			return false;
		}
		m6::Archetype ByType[FUegameEncounterConfig::NumTypes];
		for (const m6::Archetype& A : Archetypes)
		{
			ByType[static_cast<int32>(A.id)] = A;   // ids validated unique + complete above
		}
		if (!GruntMatchesCombatDefault(ByType[static_cast<int32>(m6::TypeId::Grunt)], OutError))
		{
			return false;
		}
		for (int32 i = 0; i < FUegameEncounterConfig::NumTypes; ++i)
		{
			GByType[i] = ByType[i];
			GStats[i].MaxHP = static_cast<float>(ByType[i].max_hp);
			GStats[i].MoveSpeed = static_cast<float>(ByType[i].move_speed);
			GStats[i].ContactDamage = static_cast<float>(ByType[i].contact_damage);
			GStats[i].DamageInterval = static_cast<float>(ByType[i].damage_interval_ds) / 10.0f;
			GStats[i].AggroRange = static_cast<float>(ByType[i].aggro_range);
			GStats[i].LeashRange = static_cast<float>(ByType[i].leash_range);
			GStats[i].VisualScale = static_cast<float>(ByType[i].visual_scale_pct) / 100.0f;
		}
		GFloorRoleW = FloorRoleW;
		GTypeW = TypeW;
		return true;
	}

	/** Dev-only compiled fallback candidate. Grunt derives from the ACTIVE CombatConfig row
	 *  (parity by construction); Runner/Brute and all weights repeat the committed CSV values.
	 *  It is adopted only if it passes the SAME validators + parity gate as the CSVs. */
	void BuildFallback(std::vector<m6::Archetype>& Archetypes,
	                   m6::RoleWeights& FloorRoleW, m6::TypeWeightsByRole& TypeW)
	{
		const FCombatConfigRow& D = FUegameCombatConfig::Get();
		m6::Archetype Grunt;
		Grunt.id = m6::TypeId::Grunt;
		Grunt.max_hp = FMath::RoundToInt(D.EnemyMaxHP);
		Grunt.move_speed = FMath::RoundToInt(D.EnemyMoveSpeed);
		Grunt.contact_damage = FMath::RoundToInt(D.EnemyContactDamage);
		Grunt.damage_interval_ds = static_cast<int32>(D.EnemyDamageInterval * 10.0f + 0.5f);
		Grunt.aggro_range = FMath::RoundToInt(D.AggroRange);
		Grunt.leash_range = FMath::RoundToInt(D.LeashRange);
		Grunt.visual_scale_pct = 100;
		m6::Archetype Runner;
		Runner.id = m6::TypeId::Runner;
		Runner.max_hp = 20; Runner.move_speed = 300; Runner.contact_damage = 5;
		Runner.damage_interval_ds = 15; Runner.aggro_range = 900; Runner.leash_range = 1400;
		Runner.visual_scale_pct = 80;
		m6::Archetype Brute;
		Brute.id = m6::TypeId::Brute;
		Brute.max_hp = 60; Brute.move_speed = 180; Brute.contact_damage = 10;
		Brute.damage_interval_ds = 20; Brute.aggro_range = 900; Brute.leash_range = 1400;
		Brute.visual_scale_pct = 140;
		Archetypes = { Grunt, Runner, Brute };
		FloorRoleW = { 0, 60, 25, 15 };
		TypeW[static_cast<int32>(m6::RoleId::Quiet)] = { 100, 0, 0 };
		TypeW[static_cast<int32>(m6::RoleId::Standard)] = { 70, 20, 10 };
		TypeW[static_cast<int32>(m6::RoleId::Skirmish)] = { 30, 60, 10 };
		TypeW[static_cast<int32>(m6::RoleId::Stronghold)] = { 30, 10, 60 };
	}

	void LoadOnce()
	{
		if (GLoaded)
		{
			return;
		}
		GLoaded = true;

		const FString ArchPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/EnemyArchetypes.csv"));
		const FString WtsPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/EncounterWeights.csv"));

		FString Error;
		std::vector<m6::Archetype> Archetypes;
		m6::RoleWeights FloorRoleW = { 0, 0, 0, 0 };
		m6::TypeWeightsByRole TypeW = {};
		const bool bParsed = LoadArchetypesCsv(ArchPath, Archetypes, Error) &&
		                     LoadWeightsCsv(WtsPath, FloorRoleW, TypeW, Error);
		if (bParsed && ValidateAndAdopt(Archetypes, FloorRoleW, TypeW, Error))
		{
			GAvailable = true;
			GFromTable = true;
		}

		if (!GAvailable)
		{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
			// Packaged build: running without (or with divergent) encounter tables would be a
			// DIFFERENT game. Fail loudly; the dev fallback below is never even attempted.
			UE_LOG(LogTemp, Fatal,
				TEXT("[EncounterConfig] FATAL: encounter tables missing/invalid in a packaged build (%s) - %s / %s"),
				*Error, *ArchPath, *WtsPath);
#else
			UE_LOG(LogTemp, Warning,
				TEXT("[EncounterConfig] CSV load/validate FAILED (%s) - trying compiled fallback (dev builds only)"),
				*Error);
			std::vector<m6::Archetype> FbArchetypes;
			m6::RoleWeights FbFloorRoleW = { 0, 0, 0, 0 };
			m6::TypeWeightsByRole FbTypeW = {};
			BuildFallback(FbArchetypes, FbFloorRoleW, FbTypeW);
			FString FbError;
			if (ValidateAndAdopt(FbArchetypes, FbFloorRoleW, FbTypeW, FbError))
			{
				GAvailable = true;
				GFromTable = false;
				UE_LOG(LogTemp, Warning,
					TEXT("[EncounterConfig] FALLBACK to compiled encounter tables (validated + Grunt parity ok) - fix the CSVs"));
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[EncounterConfig] fallback ALSO failed validation (%s) - encounter diversity UNAVAILABLE, spawns stay Default-only"),
					*FbError);
			}
#endif
		}

		// Evidence: echo the active tables once, so every encounter number in later logs traces here.
		if (GAvailable)
		{
			for (int32 i = 0; i < FUegameEncounterConfig::NumTypes; ++i)
			{
				const FEncounterArchetypeStats& S = GStats[i];
				UE_LOG(LogTemp, Display,
					TEXT("[EncounterConfig] source=%s archetype=%s hp=%.0f speed=%.0f dmg=%.0f interval=%.2f aggro=%.0f leash=%.0f visualScale=%.2f"),
					GFromTable ? TEXT("DataTable(CSV)") : TEXT("compiled-fallback"),
					GTypeNames[i], S.MaxHP, S.MoveSpeed, S.ContactDamage, S.DamageInterval,
					S.AggroRange, S.LeashRange, S.VisualScale);
			}
			UE_LOG(LogTemp, Display,
				TEXT("[EncounterConfig] source=%s floorRoleWeights[Q,S,Sk,St]=%d,%d,%d,%d typeW Quiet=%d,%d,%d Standard=%d,%d,%d Skirmish=%d,%d,%d Stronghold=%d,%d,%d"),
				GFromTable ? TEXT("DataTable(CSV)") : TEXT("compiled-fallback"),
				GFloorRoleW[0], GFloorRoleW[1], GFloorRoleW[2], GFloorRoleW[3],
				GTypeW[0][0], GTypeW[0][1], GTypeW[0][2],
				GTypeW[1][0], GTypeW[1][1], GTypeW[1][2],
				GTypeW[2][0], GTypeW[2][1], GTypeW[2][2],
				GTypeW[3][0], GTypeW[3][1], GTypeW[3][2]);
		}
	}
}

bool FUegameEncounterConfig::IsAvailable()
{
	LoadOnce();
	return GAvailable;
}

bool FUegameEncounterConfig::IsFromDataTable()
{
	LoadOnce();
	return GFromTable;
}

const FEncounterArchetypeStats& FUegameEncounterConfig::GetArchetype(int32 TypeIdx)
{
	LoadOnce();
	check(TypeIdx >= 0 && TypeIdx < NumTypes);
	return GStats[TypeIdx];
}

const TCHAR* FUegameEncounterConfig::TypeName(int32 TypeIdx)
{
	return (TypeIdx >= 0 && TypeIdx < NumTypes) ? GTypeNames[TypeIdx] : TEXT("?");
}

const TCHAR* FUegameEncounterConfig::RoleName(int32 RoleIdx)
{
	return (RoleIdx >= 0 && RoleIdx < NumRoles) ? GRoleNames[RoleIdx] : TEXT("?");
}

bool FUegameEncounterConfig::AssignForFloor(uint64 RunSeed, int32 FloorIndex,
                                            const TArray<int32>& PlacementRoomIndices,
                                            int32 RoomCount, int32 StartRoom,
                                            TArray<int32>& OutRoomRoles, TArray<int32>& OutEnemyTypes,
                                            uint64& OutRoomRoleHash, uint64& OutEnemyTypeHash)
{
	LoadOnce();
	if (!GAvailable || RoomCount <= 0 || StartRoom < 0 || StartRoom >= RoomCount)
	{
		return false;
	}

	const std::vector<m6::RoleId> Roles = m6::assign_room_roles(
		RoomCount, StartRoom, RunSeed, FloorIndex, GFloorRoleW);

	std::vector<int32_t> RoomIdx;
	RoomIdx.reserve(PlacementRoomIndices.Num());
	for (const int32 R : PlacementRoomIndices)
	{
		if (R < 0 || R >= RoomCount)
		{
			return false;
		}
		RoomIdx.push_back(R);
	}

	const std::vector<m6::TypeId> Types = m6::assign_enemy_types(
		RoomIdx, Roles, RunSeed, FloorIndex, GTypeW);
	if (Types.size() != RoomIdx.size())
	{
		return false;   // core contract: count preserved; treat any drift as unavailable
	}

	OutRoomRoles.Reset(RoomCount);
	for (const m6::RoleId R : Roles)
	{
		OutRoomRoles.Add(static_cast<int32>(R));
	}
	OutEnemyTypes.Reset(PlacementRoomIndices.Num());
	for (const m6::TypeId T : Types)
	{
		OutEnemyTypes.Add(static_cast<int32>(T));
	}
	OutRoomRoleHash = m6::roomRoleHash(Roles);
	OutEnemyTypeHash = m6::enemyTypeHash(Types);
	return true;
}
