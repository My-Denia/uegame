// m6_encounter.cpp
// Implementation of the engine-agnostic Encounter-Diversity core (M6A).
// Integer-only, deterministic. See m6_encounter.hpp for the contract. This TU does NOT
// include dungeon.hpp / m2_adapter.hpp / m5_loadout.hpp or any engine header.

#include "m6_encounter.hpp"

namespace m6 {

namespace {

// FNV-1a 64 constants (same as m2 hashes, for repo-wide consistency).
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime  = 1099511628211ULL;

// Canonical splitmix64 stream: state increments by the golden ratio, each next() mixes it.
struct SplitMix {
    std::uint64_t state;
    explicit SplitMix(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

// Unbiased weighted pick: returns index in [0,n) with P(i) proportional to weights[i] (>0).
// Uses rejection sampling on the modulo (no modulo bias). Returns -1 if the total weight is 0
// (caller must have validated; assign_* map -1 to a safe default so a bad table cannot crash).
int weighted_pick(const std::int32_t* weights, int n, std::uint64_t seed) {
    std::uint64_t total = 0;
    for (int i = 0; i < n; ++i) {
        if (weights[i] > 0) total += static_cast<std::uint64_t>(weights[i]);
    }
    if (total == 0) return -1;
    SplitMix rng(seed);
    const std::uint64_t limit = (~0ULL / total) * total;  // largest exact multiple of total
    std::uint64_t r;
    do { r = rng.next(); } while (r >= limit);             // reject the biased tail
    const std::uint64_t x = r % total;
    std::uint64_t acc = 0;
    for (int i = 0; i < n; ++i) {
        if (weights[i] <= 0) continue;
        acc += static_cast<std::uint64_t>(weights[i]);
        if (x < acc) return i;
    }
    return n - 1;  // unreachable when total is consistent with the weights
}

} // namespace

std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::uint64_t domain_seed(std::uint64_t domain, std::uint64_t run_seed,
                          std::int32_t floor_index, std::int32_t salt) {
    std::uint64_t h = splitmix64(domain);
    h = splitmix64(h ^ run_seed);
    h = splitmix64(h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(floor_index)));
    h = splitmix64(h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(salt)));
    return h;
}

std::vector<RoleId> assign_room_roles(std::int32_t room_count, std::int32_t start_room,
                                      std::uint64_t run_seed, std::int32_t floor_index,
                                      const RoleWeights& role_weights) {
    std::vector<RoleId> roles;
    if (room_count <= 0) return roles;
    roles.assign(static_cast<std::size_t>(room_count), RoleId::Quiet);
    for (std::int32_t i = 0; i < room_count; ++i) {
        if (i == start_room) { roles[static_cast<std::size_t>(i)] = RoleId::Quiet; continue; }
        const std::uint64_t seed = domain_seed(DOMAIN_ROOM_ROLE, run_seed, floor_index, i);
        const int idx = weighted_pick(role_weights.data(), kRoleCount, seed);
        roles[static_cast<std::size_t>(i)] = (idx < 0) ? RoleId::Quiet
                                                       : static_cast<RoleId>(idx);
    }
    return roles;
}

std::vector<TypeId> assign_enemy_types(const std::vector<std::int32_t>& placement_room_indices,
                                       const std::vector<RoleId>& room_roles,
                                       std::uint64_t run_seed, std::int32_t floor_index,
                                       const TypeWeightsByRole& type_weights_by_role) {
    std::vector<TypeId> types;
    types.reserve(placement_room_indices.size());
    for (std::size_t k = 0; k < placement_room_indices.size(); ++k) {
        const std::int32_t ri = placement_room_indices[k];
        RoleId role = RoleId::Quiet;
        if (ri >= 0 && static_cast<std::size_t>(ri) < room_roles.size()) {
            role = room_roles[static_cast<std::size_t>(ri)];
        }
        const TypeWeights& w = type_weights_by_role[static_cast<std::size_t>(static_cast<int>(role))];
        const std::uint64_t seed =
            domain_seed(DOMAIN_ENEMY_TYPE, run_seed, floor_index, static_cast<std::int32_t>(k));
        const int idx = weighted_pick(w.data(), kTypeCount, seed);
        types.push_back((idx < 0) ? TypeId::Grunt : static_cast<TypeId>(idx));
    }
    return types;
}

ValidationResult validate_enemy_archetypes(const std::vector<Archetype>& archetypes) {
    if (archetypes.empty()) return { false, "empty archetype table" };
    bool seen[kTypeCount] = { false, false, false };
    for (const Archetype& a : archetypes) {
        const int id = static_cast<int>(a.id);
        if (id < 0 || id >= kTypeCount)       return { false, "unknown archetype id" };
        if (seen[id])                          return { false, "duplicate archetype id" };
        seen[id] = true;
        if (a.max_hp <= 0)                     return { false, "max_hp must be > 0" };
        if (a.move_speed <= 0)                 return { false, "move_speed must be > 0" };
        if (a.contact_damage < 0)              return { false, "contact_damage must be >= 0" };
        if (a.damage_interval_ds <= 0)         return { false, "damage_interval must be > 0" };
        if (a.damage_interval_ds % 5 != 0)     return { false, "damage_interval must be a multiple of 0.5s" };
        if (a.aggro_range < 0)                 return { false, "aggro_range must be >= 0" };
        if (a.leash_range < a.aggro_range)     return { false, "leash_range must be >= aggro_range" };
        if (a.visual_scale_pct <= 0)           return { false, "visual_scale must be > 0" };
    }
    for (int i = 0; i < kTypeCount; ++i) {
        if (!seen[i]) return { false, "missing required archetype (need Grunt, Runner, Brute)" };
    }
    return { true, "" };
}

ValidationResult validate_encounter_weights(const RoleWeights& floor_role_weights,
                                            const TypeWeightsByRole& type_weights_by_role) {
    std::uint64_t role_total = 0;
    for (int i = 0; i < kRoleCount; ++i) {
        if (floor_role_weights[i] < 0) return { false, "negative role weight" };
        role_total += static_cast<std::uint64_t>(floor_role_weights[i]);
    }
    if (role_total == 0)                       return { false, "role weights all zero" };
    if (role_total > 0x7FFFFFFFFFFFFFFFULL)     return { false, "role weight overflow" };
    for (int r = 0; r < kRoleCount; ++r) {
        std::uint64_t type_total = 0;
        for (int j = 0; j < kTypeCount; ++j) {
            if (type_weights_by_role[r][j] < 0) return { false, "negative type weight" };
            type_total += static_cast<std::uint64_t>(type_weights_by_role[r][j]);
        }
        if (type_total > 0x7FFFFFFFFFFFFFFFULL) return { false, "type weight overflow" };
        // A role is drawable if it can be assigned to some room: Quiet always (start room),
        // any other role iff its floor weight is positive. A drawable role must have at least
        // one positive type weight, else assign_enemy_types could not pick an archetype.
        const bool reachable = (r == static_cast<int>(RoleId::Quiet)) || (floor_role_weights[r] > 0);
        if (reachable && type_total == 0)      return { false, "reachable role has all-zero type weights" };
    }
    return { true, "" };
}

std::uint64_t roomRoleHash(const std::vector<RoleId>& roles) {
    std::uint64_t h = kFnvOffset;
    for (const RoleId r : roles) {
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(r)));
        h *= kFnvPrime;
    }
    return h;
}

std::uint64_t enemyTypeHash(const std::vector<TypeId>& types) {
    std::uint64_t h = kFnvOffset;
    for (const TypeId t : types) {
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(t)));
        h *= kFnvPrime;
    }
    return h;
}

} // namespace m6
