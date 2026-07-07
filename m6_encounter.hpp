// m6_encounter.hpp
// Engine-agnostic, deterministic Encounter-Diversity core (M6, batch 1 = M6A).
// Standard C++17 only. NO engine headers or types, and — by contract — NO include of
// dungeon.hpp, m2_adapter.hpp, or m5_loadout.hpp. This core sits ON TOP of the frozen
// M1/M2 layout the way m2_adapter/m5_loadout sit on their frozen cores: it consumes plain
// integers (room_count, start_room, per-placement room indices) and returns semantic tags.
//
// Responsibilities (composition-only; geometry, enemy count, and enemy positions are all
// owned by the frozen layers and are NEVER touched here):
//   1. assign_room_roles  — deterministic composition role per room index over the frozen
//                           layout (start room forced Quiet). Roles are semantic tags only.
//   2. assign_enemy_types — deterministic archetype id per already-placed enemy, biased by
//                           its room's role. One type per placement; count/order preserved.
//   3. validate_*         — data gates for the archetype table and the weight tables.
//   4. roomRoleHash / enemyTypeHash — the two NEW pinned determinism anchors.
//
// Determinism: integer-only arithmetic; own splitmix64 stream seeded by a domain-separated
// seed. The DOMAIN tags below are static_assert'd distinct from each other and from the
// m2/m5 magic constants, so M6 draws are a separate stream that cannot perturb the M1 layout
// RNG, the m2 enemy-position sub-stream, or the M5 offer stream (isolation).

#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>

namespace m6 {

// --- roles / archetypes (batch 1) ------------------------------------------------------
enum class RoleId : std::int32_t { Quiet = 0, Standard = 1, Skirmish = 2, Stronghold = 3 };
enum class TypeId : std::int32_t { Grunt = 0, Runner = 1, Brute = 2 };
constexpr int kRoleCount = 4;
constexpr int kTypeCount = 3;

// --- domain-separated seeding ----------------------------------------------------------
// ASCII-packed big-endian tags: "M6ROMROL" / "M6ENMTYP". Distinct streams for role vs type.
constexpr std::uint64_t DOMAIN_ROOM_ROLE  = 0x4D36524F4D524F4CULL; // "M6ROMROL"
constexpr std::uint64_t DOMAIN_ENEMY_TYPE = 0x4D36454E4D545950ULL; // "M6ENMTYP"

// Isolation (compile-time): the M6 domain tags must differ from each other and from every
// m2/m5 magic constant, so domain_seed() can never alias another subsystem's stream seed.
static_assert(DOMAIN_ROOM_ROLE != DOMAIN_ENEMY_TYPE, "M6 role/type domains must differ");
static_assert(DOMAIN_ROOM_ROLE != 0x9E3779B97F4A7C15ULL && DOMAIN_ENEMY_TYPE != 0x9E3779B97F4A7C15ULL, "vs m2 golden-ratio / enemy sub-stream salt");
static_assert(DOMAIN_ROOM_ROLE != 0xD1B54A32D192ED03ULL && DOMAIN_ENEMY_TYPE != 0xD1B54A32D192ED03ULL, "vs m2 nextRunSeed salt");
static_assert(DOMAIN_ROOM_ROLE != 0xBF58476D1CE4E5B9ULL && DOMAIN_ENEMY_TYPE != 0xBF58476D1CE4E5B9ULL, "vs splitmix constant 1");
static_assert(DOMAIN_ROOM_ROLE != 0x94D049BB133111EBULL && DOMAIN_ENEMY_TYPE != 0x94D049BB133111EBULL, "vs splitmix constant 2");
static_assert(DOMAIN_ROOM_ROLE != 0x4C4F41444F464652ULL && DOMAIN_ENEMY_TYPE != 0x4C4F41444F464652ULL, "vs M5 DOMAIN_LOADOUT_OFFER");

// --- weight tables (integer, non-negative) ---------------------------------------------
using RoleWeights       = std::array<std::int32_t, kRoleCount>;  // weight per RoleId (room-role draw)
using TypeWeights       = std::array<std::int32_t, kTypeCount>;  // weight per TypeId
using TypeWeightsByRole = std::array<TypeWeights, kRoleCount>;   // per role -> archetype weights

// --- archetype schema ------------------------------------------------------------------
// Validated by validate_enemy_archetypes(). NOTE: the assign_* functions never read these
// stats — assignment depends only on weights + role. The concrete balance numbers live in
// EnemyArchetypes.csv and the test/dumper fixtures, NEVER in this header (M5 discipline).
struct Archetype {
    TypeId       id             = TypeId::Grunt;
    std::int32_t max_hp             = 0;
    std::int32_t move_speed         = 0;
    std::int32_t contact_damage     = 0;
    std::int32_t damage_interval_ds = 0;  // deci-seconds; must be a positive multiple of 5 (0.5s tick)
    std::int32_t aggro_range        = 0;
    std::int32_t leash_range        = 0;
    std::int32_t visual_scale_pct   = 0;  // 100 == 1.0x
};

struct ValidationResult {
    bool        ok = false;
    std::string reason;   // empty iff ok
};

// --- public API ------------------------------------------------------------------------
// Single-shot splitmix64 mixer (also used by domain_seed).
std::uint64_t splitmix64(std::uint64_t x);

// Domain-separated stream seed. Distinct (domain, run_seed, floor_index, salt) -> distinct seed.
std::uint64_t domain_seed(std::uint64_t domain, std::uint64_t run_seed,
                          std::int32_t floor_index, std::int32_t salt);

// Assign one composition role per room index. result[start_room] is always Quiet. Other rooms
// draw from role_weights. Pure: same inputs -> byte-identical output. No geometry is touched.
std::vector<RoleId> assign_room_roles(std::int32_t room_count, std::int32_t start_room,
                                      std::uint64_t run_seed, std::int32_t floor_index,
                                      const RoleWeights& role_weights);

// Assign one archetype per already-placed enemy. placement_room_indices[k] is the room index
// of the k-th enemy in the (frozen) m2 enemy plan. Returns a vector the SAME length as the
// input (count preserved; positions untouched — those are owned by the frozen layer).
std::vector<TypeId> assign_enemy_types(const std::vector<std::int32_t>& placement_room_indices,
                                       const std::vector<RoleId>& room_roles,
                                       std::uint64_t run_seed, std::int32_t floor_index,
                                       const TypeWeightsByRole& type_weights_by_role);

// Data gates (mirror m5::validate_affix_pool). ok=false with a reason on any violation.
ValidationResult validate_enemy_archetypes(const std::vector<Archetype>& archetypes);
ValidationResult validate_encounter_weights(const RoleWeights& floor_role_weights,
                                            const TypeWeightsByRole& type_weights_by_role);

// New determinism anchors (FNV-1a 64, same constants as m2 hashes; tile-independent).
std::uint64_t roomRoleHash(const std::vector<RoleId>& roles);
std::uint64_t enemyTypeHash(const std::vector<TypeId>& types);

} // namespace m6
