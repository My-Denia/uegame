// EncounterConfig.h - M6B: loads the two encounter-diversity tables
// (Content/Data/EnemyArchetypes.csv + Content/Data/EncounterWeights.csv) at first use,
// validates them through the engine-agnostic m6 gates plus the Grunt<->CombatConfig-Default
// parity check, echoes the active tables once (evidence), and exposes role/archetype
// assignment for a floor. Mirrors the CombatConfig.h LoadOnce discipline.
//
// This header stays free of m6:: types and of .generated.h - the m6 core is only ever
// included from EncounterConfig.cpp (M5 discipline: engine-agnostic cores never reach
// reflection headers).

#pragma once

#include "CoreMinimal.h"

/** One archetype's resolved stats in UE units (floats; converted from the integer core
 *  units: DamageInterval deci-seconds -> seconds, VisualScale percent -> multiplier). */
struct FEncounterArchetypeStats
{
	float MaxHP = 0.0f;
	float MoveSpeed = 0.0f;
	float ContactDamage = 0.0f;
	float DamageInterval = 0.0f;
	float AggroRange = 0.0f;
	float LeashRange = 0.0f;
	/** BodyMesh-only visual multiplier. NEVER applied to the capsule, nav agent, or ContactRange. */
	float VisualScale = 1.0f;
};

struct FUegameEncounterConfig
{
	static constexpr int32 NumTypes = 3;   // Grunt, Runner, Brute (m6::TypeId order)
	static constexpr int32 NumRoles = 4;   // Quiet, Standard, Skirmish, Stronghold (m6::RoleId order)

	/** True when validated tables are active (CSV, or - dev builds only - the revalidated
	 *  compiled fallback). False = encounter diversity is OFF and spawns stay Default-only. */
	static bool IsAvailable();

	/** True if the active tables came from the CSVs (false = dev compiled fallback). */
	static bool IsFromDataTable();

	/** Resolved stats for a type index (m6::TypeId order). Only valid when IsAvailable(). */
	static const FEncounterArchetypeStats& GetArchetype(int32 TypeIdx);

	static const TCHAR* TypeName(int32 TypeIdx);
	static const TCHAR* RoleName(int32 RoleIdx);

	/** m6::enemyTypeHash over a type-id sequence (m6::TypeId order as int32). Lets the spawner
	 *  hash the ACTUALLY-SPAWNED sequence so Dungeon.EnemyRoster's hash always describes the
	 *  roster it prints, even if a deferred spawn ever fails. Any out-of-range id returns 0. */
	static uint64 TypeSequenceHash(const TArray<int32>& TypeIds);

	/** Run the m6 core assignment for one floor over the FROZEN m2 enemy plan.
	 *  PlacementRoomIndices[k] = room index of the k-th placement, in plan order.
	 *  Outputs one role per room, one type per placement (same length/order as input),
	 *  and the two new determinism anchors. Returns false (outputs untouched) when
	 *  unavailable or inputs are inconsistent. Never mutates the plan itself. */
	static bool AssignForFloor(uint64 RunSeed, int32 FloorIndex,
	                           const TArray<int32>& PlacementRoomIndices,
	                           int32 RoomCount, int32 StartRoom,
	                           TArray<int32>& OutRoomRoles, TArray<int32>& OutEnemyTypes,
	                           uint64& OutRoomRoleHash, uint64& OutEnemyTypeHash);
};
