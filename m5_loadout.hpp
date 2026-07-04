// m5_loadout.hpp
// Engine-agnostic, deterministic Build-diversity (loadout) CORE. Standard C++17 only.
// No engine headers or types (no FVector/TArray/UObject). No game numbers baked in:
// the core accepts an externally-supplied BaseStats; the current player numbers
// (15 dmg / 600 ms / 140 hp / 500 move) live ONLY in the tests and the golden dumper,
// never in this header (see req #3 — fixture must not become a second source of truth).
//
// Model:  the game offers 3-choose-1 upgrades (Affix) per reward; the player's picks
//         form a loadout that resolve()s into ResolvedStats.
//
// Determinism:
//   * Every random draw comes from a std::mt19937_64 seeded via a DOMAIN-SEPARATED
//     splitmix64 of (run_seed, DOMAIN_LOADOUT_OFFER, floor_index, offer_index). This is a
//     SEPARATE stream from dungeon.hpp / m2_adapter.hpp — this file never includes them
//     and never touches their RNG state (all engines here are function-local, no globals).
//   * Bounded draws use unbiased rejection sampling on the raw engine output
//     (NO std::uniform_int_distribution, NO modulo bias).
//   * resolve() is integer-only (no floats) => bit-for-bit identical output on any
//     conforming compiler (g++, MSVC, clang), which is what makes the golden vector stable.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace m5 {

// ---------------------------------------------------------------------------
// Affix taxonomy
// ---------------------------------------------------------------------------
enum class AffixKind : std::uint8_t {
    DamagePct         = 0,   // +magnitude % to damage
    AttackIntervalPct = 1,   // +magnitude % to attack interval (negative magnitude = faster)
    MaxHpFlat         = 2,   // +magnitude flat to max HP
    MoveSpeedPct      = 3,   // +magnitude % to move speed
};

struct Affix {
    std::uint32_t id         = 0;                     // globally-unique, stable sort key
    AffixKind     kind       = AffixKind::DamagePct;
    std::int64_t  magnitude  = 0;                     // percent for *Pct kinds; flat for MaxHpFlat
    std::int64_t  weight     = 0;                     // sampling weight; 0 => never drawn (validate: >= 0)
    std::int32_t  max_stacks = 0;                     // 0 => unlimited cross-floor; N>0 => excluded once chosen >= N
};

// External inputs only — deliberately NO reference-base constant in this header (req #3).
struct BaseStats {
    std::int64_t base_damage = 0;
    std::int64_t attack_ms   = 0;
    std::int64_t max_hp      = 0;
    std::int64_t move_speed  = 0;
};

struct ResolvedStats {
    std::int32_t damage     = 0;
    std::int32_t attack_ms  = 0;
    std::int32_t max_hp     = 0;
    std::int32_t move_speed = 0;
};

// ---------------------------------------------------------------------------
// Safety invariants (CORE clamps, not game tuning). attack_ms >= 150 is the hard
// anti-softlock invariant: unbounded attack-speed stacking would drive attack_ms
// toward 0 and DPS toward infinity.
// ---------------------------------------------------------------------------
constexpr std::int64_t kMinDamage    = 1;
constexpr std::int64_t kMinAttackMs  = 150;
constexpr std::int64_t kMinMaxHp     = 1;
constexpr std::int64_t kMinMoveSpeed = 1;

// Validation / overflow-safety bounds. These keep every int64 intermediate well
// inside range even before resolve()'s saturating accumulation (belt + suspenders).
constexpr std::int64_t kMaxAbsMagnitude = 1'000'000'000LL;         // |Affix.magnitude| ceiling
constexpr std::int64_t kMaxWeight       = 1'000'000'000'000LL;     // per-affix weight ceiling (sum can't overflow uint64)
constexpr std::size_t  kMaxPoolSize     = 4096;                    // sane pool-size ceiling
constexpr std::int64_t kMaxBaseStat     = 2'000'000'000LL;         // base sanity ceiling (< INT32_MAX)
constexpr std::int64_t kTotalCap        = 2'000'000'000LL;         // per-kind accumulated-total clamp

// ---------------------------------------------------------------------------
// Domain-separated seed primitive (own splitmix64; this header does NOT include
// m2_adapter.hpp). DOMAIN_LOADOUT_OFFER is pinned distinct from every m2 magic
// constant so the loadout stream cannot alias the enemy / floor / nextRun streams.
// ---------------------------------------------------------------------------
constexpr std::uint64_t DOMAIN_LOADOUT_OFFER = 0x4C4F41444F464652ULL; // ASCII "LOADOFFR"
static_assert(DOMAIN_LOADOUT_OFFER != 0x9E3779B97F4A7C15ULL, "collides with m2 enemy/entropy constant");
static_assert(DOMAIN_LOADOUT_OFFER != 0xD1B54A32D192ED03ULL, "collides with m2 nextRunSeed constant");
static_assert(DOMAIN_LOADOUT_OFFER != 0xBF58476D1CE4E5B9ULL, "collides with splitmix64 constant 1");
static_assert(DOMAIN_LOADOUT_OFFER != 0x94D049BB133111EBULL, "collides with splitmix64 constant 2");

// splitmix64 integer mixer (public constants). Pure integer mixing — advances no RNG stream.
std::uint64_t splitmix64(std::uint64_t x);

// Offer seed for (run_seed, floor_index, offer_index) in the loadout domain.
// Independent of m2::deriveFloorSeed by construction (folds DOMAIN + offer_index).
std::uint64_t domain_seed(std::uint64_t run_seed, std::uint64_t domain,
                          std::uint64_t floor_index, std::uint64_t offer_index);

// Saturating int64 -> int32 (BOTH bounds; no implementation-defined narrowing on
// extreme negatives — req #8).
std::int32_t saturate_i32(std::int64_t v);

// ---------------------------------------------------------------------------
// Pool validation gate (req #7) — a real function, not a comment.
// ---------------------------------------------------------------------------
struct ValidationResult {
    bool        ok = true;
    std::string reason;   // human-readable failure cause (empty when ok)
};

// Checks: non-empty & size <= kMaxPoolSize; id uniqueness; weight in [0, kMaxWeight];
// at least one positive-weight affix; |magnitude| <= kMaxAbsMagnitude; max_stacks >= 0.
ValidationResult validate_affix_pool(const std::vector<Affix>& pool);

// ---------------------------------------------------------------------------
// Offer generation — PURE function of its explicit inputs (req #9.1).
// Returns up to offer_size affixes, WITHOUT replacement within the offer (req #6),
// weighted by Affix.weight over the eligible set:
//     eligible = weight > 0 AND (max_stacks == 0 OR times-chosen < max_stacks)
// The eligible set is sorted by Affix.id before drawing, so the result is
// independent of pool insertion / DataTable order (req #2). weight == 0 => never
// drawn (req #9.5). An empty eligible set yields an empty offer, never a crash (req #9.6).
//
// chosen_ids: the ids the player has already picked this run (repeats allowed) —
// used only for max_stacks accounting.
std::vector<Affix> generate_offer(const std::vector<Affix>& pool,
                                  const std::vector<std::uint32_t>& chosen_ids,
                                  std::uint64_t run_seed,
                                  std::uint64_t floor_index,
                                  std::uint64_t offer_index,
                                  int offer_size);

// ---------------------------------------------------------------------------
// Stat resolution — ORDER-INDEPENDENT (req #7): sum each kind's magnitudes, then
// apply once. Permuting `chosen` cannot change the result.
//   DamagePct:         damage    = base_damage * (100 + Sigma_pct) / 100
//   AttackIntervalPct: attack_ms = base_attack_ms * (100 + Sigma_pct) / 100
//   MaxHpFlat:         max_hp    = base_max_hp + Sigma_flat
//   MoveSpeedPct:      move_speed= base_move_speed * (100 + Sigma_pct) / 100
// Then clamp to the safety floors and saturating-narrow to int32. Overflow-proof
// for ANY chosen-list length / magnitude via clamped base + saturating accumulation.
ResolvedStats resolve(const BaseStats& base, const std::vector<Affix>& chosen);

} // namespace m5
