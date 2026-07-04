// m5_loadout.cpp
// Implementation of the engine-agnostic loadout core. Standard C++17 only.
// Does NOT include dungeon.hpp or m2_adapter.hpp — the loadout RNG stream is
// entirely separate (see m5_loadout.hpp header contract).

#include "m5_loadout.hpp"

#include <algorithm>
#include <map>
#include <random>
#include <set>

namespace m5 {

// ---------------------------------------------------------------------------
// Seed primitives
// ---------------------------------------------------------------------------
std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::uint64_t domain_seed(std::uint64_t run_seed, std::uint64_t domain,
                          std::uint64_t floor_index, std::uint64_t offer_index) {
    // Fold every input through splitmix64. Starting from (run_seed ^ domain) and
    // additionally folding offer_index makes this provably distinct from
    // m2::deriveFloorSeed(runSeed, floor) = mix64(runSeed ^ mix64(floor)).
    std::uint64_t h = splitmix64(run_seed ^ domain);
    h = splitmix64(h ^ splitmix64(floor_index));
    h = splitmix64(h ^ splitmix64(offer_index));
    return h;
}

// ---------------------------------------------------------------------------
// Saturating / clamping helpers
// ---------------------------------------------------------------------------
namespace {

inline std::int64_t clamp_i64(std::int64_t v, std::int64_t lo, std::int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Accumulate `add` into a per-kind running total kept inside [-kTotalCap, kTotalCap].
// Each contribution is clamped first, so `total + add` can never overflow int64
// (|total|,|add| <= kTotalCap => |sum| <= 2*kTotalCap, far below INT64_MAX) for ANY
// int64 input — including INT64_MIN in the adversarial saturation test.
inline std::int64_t sat_accumulate(std::int64_t total, std::int64_t add) {
    add = clamp_i64(add, -kTotalCap, kTotalCap);
    return clamp_i64(total + add, -kTotalCap, kTotalCap);
}

// Unbiased bounded draw in [0, n) from a raw mt19937_64. Rejects the low
// (2^64 mod n) values so every residue class is equally likely — no modulo bias,
// and NO std::uniform_int_distribution. Deterministic in the engine sequence.
// Precondition: n > 0.
inline std::uint64_t bounded(std::mt19937_64& rng, std::uint64_t n) {
    const std::uint64_t threshold = (0ULL - n) % n;  // == 2^64 mod n in unsigned arithmetic
    std::uint64_t x;
    do {
        x = rng();
    } while (x < threshold);
    return x % n;
}

} // namespace

std::int32_t saturate_i32(std::int64_t v) {
    if (v > static_cast<std::int64_t>(INT32_MAX)) return INT32_MAX;
    if (v < static_cast<std::int64_t>(INT32_MIN)) return INT32_MIN;
    return static_cast<std::int32_t>(v);
}

// ---------------------------------------------------------------------------
// Pool validation
// ---------------------------------------------------------------------------
ValidationResult validate_affix_pool(const std::vector<Affix>& pool) {
    ValidationResult r;
    if (pool.empty()) {
        r.ok = false; r.reason = "empty pool"; return r;
    }
    if (pool.size() > kMaxPoolSize) {
        r.ok = false; r.reason = "pool size " + std::to_string(pool.size()) +
                                 " exceeds " + std::to_string(kMaxPoolSize);
        return r;
    }
    std::set<std::uint32_t> ids;
    bool anyPositive = false;
    for (const Affix& a : pool) {
        if (!ids.insert(a.id).second) {
            r.ok = false; r.reason = "duplicate id " + std::to_string(a.id); return r;
        }
        if (a.weight < 0) {
            r.ok = false; r.reason = "negative weight on id " + std::to_string(a.id); return r;
        }
        if (a.weight > kMaxWeight) {
            r.ok = false; r.reason = "weight overflow on id " + std::to_string(a.id); return r;
        }
        if (a.weight > 0) anyPositive = true;
        if (a.magnitude < -kMaxAbsMagnitude || a.magnitude > kMaxAbsMagnitude) {
            r.ok = false; r.reason = "magnitude out of range on id " + std::to_string(a.id); return r;
        }
        if (a.max_stacks < 0) {
            r.ok = false; r.reason = "negative max_stacks on id " + std::to_string(a.id); return r;
        }
    }
    if (!anyPositive) {
        r.ok = false; r.reason = "no positive-weight affix (offers would be empty)"; return r;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Offer generation
// ---------------------------------------------------------------------------
std::vector<Affix> generate_offer(const std::vector<Affix>& pool,
                                  const std::vector<std::uint32_t>& chosen_ids,
                                  std::uint64_t run_seed,
                                  std::uint64_t floor_index,
                                  std::uint64_t offer_index,
                                  int offer_size) {
    std::vector<Affix> offer;
    if (offer_size <= 0) return offer;

    // times each id has been chosen this run (for max_stacks accounting)
    std::map<std::uint32_t, int> chosenCount;
    for (std::uint32_t id : chosen_ids) ++chosenCount[id];

    // Build the eligible set: positive weight AND not stack-capped.
    std::vector<Affix> eligible;
    eligible.reserve(pool.size());
    for (const Affix& a : pool) {
        if (a.weight <= 0) continue;                          // req #9.5: weight 0 never drawn
        if (a.max_stacks != 0) {                              // req #6: N-cap (0 => unlimited)
            auto it = chosenCount.find(a.id);
            int cnt = (it == chosenCount.end()) ? 0 : it->second;
            if (cnt >= a.max_stacks) continue;
        }
        eligible.push_back(a);
    }

    // Stable sampling order: sort by id, independent of pool/DataTable order (req #2).
    std::sort(eligible.begin(), eligible.end(),
              [](const Affix& x, const Affix& y) { return x.id < y.id; });

    std::mt19937_64 rng(domain_seed(run_seed, DOMAIN_LOADOUT_OFFER, floor_index, offer_index));

    const int k = std::min(offer_size, static_cast<int>(eligible.size()));
    for (int drawn = 0; drawn < k; ++drawn) {
        std::uint64_t totalWeight = 0;
        for (const Affix& a : eligible) totalWeight += static_cast<std::uint64_t>(a.weight);
        if (totalWeight == 0) break;                          // no positive weight left

        const std::uint64_t roll = bounded(rng, totalWeight);
        std::uint64_t acc = 0;
        std::size_t pick = eligible.size() - 1;               // fallback = last (roll < total guarantees a hit)
        for (std::size_t j = 0; j < eligible.size(); ++j) {
            acc += static_cast<std::uint64_t>(eligible[j].weight);
            if (roll < acc) { pick = j; break; }
        }
        offer.push_back(eligible[pick]);
        eligible.erase(eligible.begin() + static_cast<std::ptrdiff_t>(pick)); // without replacement (req #6)
    }
    return offer;
}

// ---------------------------------------------------------------------------
// Stat resolution
// ---------------------------------------------------------------------------
ResolvedStats resolve(const BaseStats& base, const std::vector<Affix>& chosen) {
    std::int64_t sumDamagePct   = 0;
    std::int64_t sumIntervalPct = 0;
    std::int64_t sumHpFlat      = 0;
    std::int64_t sumMovePct     = 0;

    for (const Affix& a : chosen) {
        switch (a.kind) {
            case AffixKind::DamagePct:         sumDamagePct   = sat_accumulate(sumDamagePct,   a.magnitude); break;
            case AffixKind::AttackIntervalPct: sumIntervalPct = sat_accumulate(sumIntervalPct, a.magnitude); break;
            case AffixKind::MaxHpFlat:         sumHpFlat      = sat_accumulate(sumHpFlat,      a.magnitude); break;
            case AffixKind::MoveSpeedPct:      sumMovePct     = sat_accumulate(sumMovePct,     a.magnitude); break;
        }
    }

    // Clamp base into a safe range so base * (100 + Sigma) cannot overflow int64:
    // |base| <= 2e9, |100 + Sigma| <= 2e9 + 100  =>  product <= ~4e18 < INT64_MAX.
    const std::int64_t bDmg  = clamp_i64(base.base_damage, -kMaxBaseStat, kMaxBaseStat);
    const std::int64_t bAtk  = clamp_i64(base.attack_ms,   -kMaxBaseStat, kMaxBaseStat);
    const std::int64_t bHp   = clamp_i64(base.max_hp,      -kMaxBaseStat, kMaxBaseStat);
    const std::int64_t bMove = clamp_i64(base.move_speed,  -kMaxBaseStat, kMaxBaseStat);

    std::int64_t damage    = bDmg  * (100 + sumDamagePct)   / 100;
    std::int64_t attackMs  = bAtk  * (100 + sumIntervalPct) / 100;
    std::int64_t maxHp     = bHp   + sumHpFlat;
    std::int64_t moveSpeed = bMove * (100 + sumMovePct)     / 100;

    // Safety floors (applied on the int64 value before narrowing).
    damage    = std::max<std::int64_t>(damage,    kMinDamage);
    attackMs  = std::max<std::int64_t>(attackMs,  kMinAttackMs);
    maxHp     = std::max<std::int64_t>(maxHp,     kMinMaxHp);
    moveSpeed = std::max<std::int64_t>(moveSpeed, kMinMoveSpeed);

    ResolvedStats out;
    out.damage     = saturate_i32(damage);
    out.attack_ms  = saturate_i32(attackMs);
    out.max_hp     = saturate_i32(maxHp);
    out.move_speed = saturate_i32(moveSpeed);
    return out;
}

} // namespace m5
