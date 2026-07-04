// test_m5_loadout.cpp
// Determinism + fuzz gate for the M5 loadout core (CTest: m5_loadout_determinism).
// Emits COUNTED, named sub-checks `PASS[k/N] <tag>` / `FAIL[k/N] <tag> :: <detail>`.
// The gate is: exit 0  AND  exactly N `PASS[` lines  AND  zero `FAIL[` lines.
//
// This is the ONLY translation unit that includes m2_adapter.hpp, and it does so
// purely to PROVE domain isolation (sub-check #13): it computes the M4/M3 seed/hash
// witnesses before and after thousands of interleaved M5 offer draws and asserts
// they are bit-identical. Including the header does not edit it; the production core
// (m5_loadout.*) and the golden dumper never include it.

#include "m5_loadout.hpp"
#include "m2_adapter.hpp"   // TEST-ONLY: for the isolation witness (deriveFloorSeed / enemyPlanHash)

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Tiny counted-assertion harness
// ---------------------------------------------------------------------------
namespace {

constexpr int kTotalChecks = 14;
int g_passed = 0;
int g_failed = 0;

void report(int idx, const char* tag, bool ok, const std::string& detail = "") {
    if (ok) {
        ++g_passed;
        std::printf("PASS[%d/%d] %s\n", idx, kTotalChecks, tag);
    } else {
        ++g_failed;
        std::printf("FAIL[%d/%d] %s :: %s\n", idx, kTotalChecks, tag, detail.c_str());
    }
    std::fflush(stdout);   // flush per check so a later crash still shows progress
}

std::vector<std::uint32_t> ids_of(const std::vector<m5::Affix>& v) {
    std::vector<std::uint32_t> r;
    r.reserve(v.size());
    for (const m5::Affix& a : v) r.push_back(a.id);
    return r;
}

bool contains_id(const std::vector<m5::Affix>& v, std::uint32_t id) {
    for (const m5::Affix& a : v) if (a.id == id) return true;
    return false;
}

// Demo affix pool — TEST FIXTURE ONLY (never in the production header). Covers all
// four kinds, distinct ids, an unlimited (max_stacks=0) high-weight affix (id 10),
// N-capped affixes (id 13/15/17), a strong negative-interval affix (id 13), and a
// zero-weight affix (id 18) that must never be drawn.
std::vector<m5::Affix> demoPool() {
    using K = m5::AffixKind;
    return {
        // id, kind,               magnitude, weight, max_stacks
        {  10, K::DamagePct,          15,      100,    0 },  // unlimited stacking
        {  11, K::DamagePct,          25,       60,    3 },
        {  12, K::AttackIntervalPct, -20,       80,    0 },
        {  13, K::AttackIntervalPct, -40,       40,    2 },  // strong attack-speed, capped
        {  14, K::MaxHpFlat,          30,      100,    0 },
        {  15, K::MaxHpFlat,          60,       50,    2 },
        {  16, K::MoveSpeedPct,       10,       70,    0 },
        {  17, K::MoveSpeedPct,       20,       30,    2 },
        {  18, K::DamagePct,           5,        0,    0 },  // weight 0 => never drawn
    };
}

// Test base fixture. 15/600/140 are the current player numbers; 500 is a deliberately
// SYNTHETIC move base (the CSV has no player move speed — 240 is EnemyMoveSpeed). These
// values live only here and in the golden dumper, never in the production header (req #3).
m5::BaseStats fixtureBase() { return { 15, 600, 140, 500 }; }

} // namespace

int main() {
    const std::vector<m5::Affix> pool = demoPool();
    const m5::BaseStats base = fixtureBase();

    // -- #1 offer_pure: same inputs -> identical output --------------------------------
    {
        auto a = m5::generate_offer(pool, {}, 7, 1, 0, 3);
        auto b = m5::generate_offer(pool, {}, 7, 1, 0, 3);
        report(1, "offer_pure", ids_of(a) == ids_of(b),
               "generate_offer not a pure function of its inputs");
    }

    // -- #2 offer_varies_floor: floor_index changes the offer --------------------------
    {
        std::set<std::vector<std::uint32_t>> sigs;
        for (std::uint64_t f = 1; f <= 8; ++f) sigs.insert(ids_of(m5::generate_offer(pool, {}, 7, f, 0, 3)));
        report(2, "offer_varies_floor", sigs.size() > 1,
               "offers identical across all floor indices 1..8");
    }

    // -- #3 offer_varies_index: offer_index changes the offer --------------------------
    {
        std::set<std::vector<std::uint32_t>> sigs;
        for (std::uint64_t i = 0; i < 8; ++i) sigs.insert(ids_of(m5::generate_offer(pool, {}, 7, 1, i, 3)));
        report(3, "offer_varies_index", sigs.size() > 1,
               "offers identical across all offer indices 0..7");
    }

    // -- #4 offer_no_dup: no duplicate affix within a single offer ---------------------
    {
        bool ok = true;
        std::string detail;
        for (std::uint64_t s = 0; s < 2000 && ok; ++s) {
            for (std::uint64_t f = 1; f <= 3 && ok; ++f) {
                auto off = m5::generate_offer(pool, {}, s, f, 0, 3);
                std::vector<std::uint32_t> v = ids_of(off);
                std::set<std::uint32_t> uniq(v.begin(), v.end());
                if (uniq.size() != v.size()) { ok = false; detail = "dup in offer at seed " + std::to_string(s); }
            }
        }
        report(4, "offer_no_dup", ok, detail);
    }

    // -- #5 maxstacks_N_excluded: id 13 (max_stacks=2) gone once chosen twice ----------
    {
        std::vector<std::uint32_t> chosen = { 13, 13 };  // reached the cap
        bool everOffered = false;
        for (std::uint64_t s = 0; s < 3000 && !everOffered; ++s)
            for (std::uint64_t f = 1; f <= 3 && !everOffered; ++f)
                if (contains_id(m5::generate_offer(pool, chosen, s, f, 0, 3), 13)) everOffered = true;
        report(5, "maxstacks_N_excluded", !everOffered,
               "capped affix id 13 was still offered after being chosen max_stacks times");
    }

    // -- #6 maxstacks_0_unlimited: id 10 (max_stacks=0) still offered after 100 picks ---
    {
        std::vector<std::uint32_t> chosen(100, 10);  // chosen 100 times; unlimited => still eligible
        bool everOffered = false;
        for (std::uint64_t s = 0; s < 3000 && !everOffered; ++s)
            for (std::uint64_t f = 1; f <= 3 && !everOffered; ++f)
                if (contains_id(m5::generate_offer(pool, chosen, s, f, 0, 3), 10)) everOffered = true;
        report(6, "maxstacks_0_unlimited", everOffered,
               "unlimited affix id 10 vanished from offers after many picks");
    }

    // -- #7 weight0_never: id 18 (weight 0) never appears -------------------------------
    {
        bool everOffered = false;
        for (std::uint64_t s = 0; s < 20000 && !everOffered; ++s)
            for (std::uint64_t f = 1; f <= 3 && !everOffered; ++f)
                if (contains_id(m5::generate_offer(pool, {}, s, f, 0, 3), 18)) everOffered = true;
        report(7, "weight0_never", !everOffered, "weight-0 affix id 18 was drawn");
    }

    // -- #8 empty_pool_safe: empty pool & all-ineligible pool -> empty, no crash --------
    {
        bool ok = true;
        std::string detail;
        if (!m5::generate_offer({}, {}, 7, 1, 0, 3).empty()) { ok = false; detail = "empty pool produced an offer"; }
        std::vector<m5::Affix> allZero = { {1, m5::AffixKind::DamagePct, 10, 0, 0},
                                           {2, m5::AffixKind::MaxHpFlat, 10, 0, 0} };
        if (ok && !m5::generate_offer(allZero, {}, 7, 1, 0, 3).empty()) { ok = false; detail = "all-zero-weight pool produced an offer"; }
        // all stack-capped (each chosen to its cap)
        std::vector<m5::Affix> capped = { {1, m5::AffixKind::DamagePct, 10, 5, 1},
                                          {2, m5::AffixKind::MaxHpFlat, 10, 5, 1} };
        if (ok && !m5::generate_offer(capped, {1, 2}, 7, 1, 0, 3).empty()) { ok = false; detail = "fully-capped pool produced an offer"; }
        report(8, "empty_pool_safe", ok, detail);
    }

    // -- #9 order_independent: resolve is invariant under permutation of `chosen` -------
    {
        std::vector<m5::Affix> chosen = {
            {10, m5::AffixKind::DamagePct,         15, 1, 0},
            {12, m5::AffixKind::AttackIntervalPct,-20, 1, 0},
            {14, m5::AffixKind::MaxHpFlat,         30, 1, 0},
            {16, m5::AffixKind::MoveSpeedPct,      10, 1, 0},
            {11, m5::AffixKind::DamagePct,         25, 1, 0},
        };
        std::sort(chosen.begin(), chosen.end(),
                  [](const m5::Affix& a, const m5::Affix& b) { return a.id < b.id; });
        m5::ResolvedStats ref = m5::resolve(base, chosen);
        bool ok = true;
        int perms = 0;
        do {
            m5::ResolvedStats r = m5::resolve(base, chosen);
            if (r.damage != ref.damage || r.attack_ms != ref.attack_ms ||
                r.max_hp != ref.max_hp || r.move_speed != ref.move_speed) ok = false;
            ++perms;
        } while (ok && std::next_permutation(
                     chosen.begin(), chosen.end(),
                     [](const m5::Affix& a, const m5::Affix& b) { return a.id < b.id; }));
        report(9, "order_independent", ok && perms == 120,
               "resolve changed under a permutation (perms=" + std::to_string(perms) + ")");
    }

    // -- #10 softlock_attack_ms: attack_ms >= 150 no matter how much haste is stacked ---
    {
        bool ok = true;
        // stack many strongest negative-interval affixes directly
        std::vector<m5::Affix> haste(50, {13, m5::AffixKind::AttackIntervalPct, -40, 1, 0});
        if (m5::resolve(base, haste).attack_ms < 150) ok = false;
        // and across simulated runs
        for (std::uint64_t s = 0; s < 5000 && ok; ++s) {
            std::vector<m5::Affix> chosen;
            std::vector<std::uint32_t> chosenIds;
            for (std::uint64_t f = 1; f <= 8; ++f) {
                auto off = m5::generate_offer(pool, chosenIds, s, f, 0, 3);
                if (!off.empty()) { chosen.push_back(off[0]); chosenIds.push_back(off[0].id); }
                if (m5::resolve(base, chosen).attack_ms < 150) ok = false;
            }
        }
        report(10, "softlock_attack_ms", ok, "attack_ms fell below the 150ms hard floor");
    }

    // -- #11 fuzz_invariants: 60000 seeds x 8 floors + extreme-negative saturation ------
    {
        bool ok = true;
        std::string detail;
        const std::uint64_t FUZZ_SEEDS = 60000;
        for (std::uint64_t s = 0; s < FUZZ_SEEDS && ok; ++s) {
            std::vector<m5::Affix> chosen;
            std::vector<std::uint32_t> chosenIds;
            for (std::uint64_t f = 1; f <= 8 && ok; ++f) {
                auto off = m5::generate_offer(pool, chosenIds, s, f, 0, 3);
                if (!off.empty()) { chosen.push_back(off[0]); chosenIds.push_back(off[0].id); }
                m5::ResolvedStats r = m5::resolve(base, chosen);
                if (r.damage < 1 || r.max_hp < 1 || r.move_speed < 1 || r.attack_ms < 150) {
                    ok = false; detail = "invariant broken at seed " + std::to_string(s) + " floor " + std::to_string(f);
                }
            }
        }
        // Extreme-negative saturation: crafted magnitudes far below INT32_MIN must
        // saturate to the safety floor, NOT wrap to a large positive (req #8).
        if (ok) {
            std::vector<m5::Affix> evilDmg (5, {900, m5::AffixKind::DamagePct,         -2000000000LL, 1, 0});
            std::vector<m5::Affix> evilAtk (5, {901, m5::AffixKind::AttackIntervalPct, -2000000000LL, 1, 0});
            std::vector<m5::Affix> evilMove(5, {902, m5::AffixKind::MoveSpeedPct,      -2000000000LL, 1, 0});
            std::vector<m5::Affix> evilHp  (5, {903, m5::AffixKind::MaxHpFlat,         -2000000000LL, 1, 0});
            if (m5::resolve(base, evilDmg ).damage     != 1)   { ok = false; detail = "damage did not saturate to floor"; }
            if (ok && m5::resolve(base, evilAtk ).attack_ms  != 150) { ok = false; detail = "attack_ms did not saturate to floor"; }
            if (ok && m5::resolve(base, evilMove).move_speed != 1)   { ok = false; detail = "move_speed did not saturate to floor"; }
            if (ok && m5::resolve(base, evilHp  ).max_hp     != 1)   { ok = false; detail = "max_hp did not saturate to floor"; }
        }
        // Direct saturate_i32 both-bounds + upper-bound via resolve.
        if (ok) {
            if (m5::saturate_i32(INT64_MIN) != INT32_MIN) { ok = false; detail = "saturate_i32(INT64_MIN)"; }
            if (ok && m5::saturate_i32(INT64_MAX) != INT32_MAX) { ok = false; detail = "saturate_i32(INT64_MAX)"; }
            if (ok && m5::saturate_i32(42) != 42) { ok = false; detail = "saturate_i32(42)"; }
            // upper saturation through resolve: huge base * huge positive pct
            std::vector<m5::Affix> huge(5, {904, m5::AffixKind::DamagePct, 1000000000LL, 1, 0});
            m5::BaseStats bigBase = { 2000000000LL, 600, 140, 500 };
            if (ok && m5::resolve(bigBase, huge).damage != INT32_MAX) { ok = false; detail = "damage did not saturate to INT32_MAX"; }
        }
        report(11, "fuzz_invariants", ok, detail);
    }

    // -- #12 sort_stable: shuffling POOL input order yields identical offers ------------
    {
        std::vector<m5::Affix> shuffled = pool;
        std::mt19937_64 shuf(123456789ULL);
        std::shuffle(shuffled.begin(), shuffled.end(), shuf);
        bool ok = true;
        std::string detail;
        for (std::uint64_t s = 0; s < 2000 && ok; ++s) {
            for (std::uint64_t f = 1; f <= 3 && ok; ++f) {
                auto a = m5::generate_offer(pool, {}, s, f, 0, 3);
                auto b = m5::generate_offer(shuffled, {}, s, f, 0, 3);
                if (ids_of(a) != ids_of(b)) { ok = false; detail = "offer differs by pool order at seed " + std::to_string(s); }
            }
        }
        report(12, "sort_stable", ok, detail);
    }

    // -- #13 isolation_no_rng_consume: M5 draws do NOT perturb M1/M3/M4 streams ---------
    // Also pins the seed-7 floor seeds to the captured baseline anchors.
    {
        const std::uint64_t runSeed = 7;
        m2::WorldConfig wc;  // defaults (tileSize=100)

        auto witness = [&](std::uint64_t& fs1, std::uint64_t& fs2, std::uint64_t& fs3,
                           std::uint64_t& eh1) {
            fs1 = m2::deriveFloorSeed(runSeed, 1);
            fs2 = m2::deriveFloorSeed(runSeed, 2);
            fs3 = m2::deriveFloorSeed(runSeed, 3);
            dungeon::Config cfg;               // engine-agnostic defaults
            cfg.seed = fs1;
            dungeon::Layout L = dungeon::generate(cfg);
            eh1 = m2::enemyPlanHash(m2::buildEnemyPlan(L, wc, 2, L.startRoom));
        };

        std::uint64_t a1, a2, a3, ae, b1, b2, b3, be;
        witness(a1, a2, a3, ae);
        // Interleave thousands of M5 offer draws (each uses its own function-local engine).
        volatile std::size_t sink = 0;
        for (std::uint64_t s = 0; s < 5000; ++s)
            for (std::uint64_t f = 1; f <= 4; ++f)
                sink += m5::generate_offer(demoPool(), {}, s, f, s % 3, 3).size();
        (void)sink;
        witness(b1, b2, b3, be);

        bool invariant = (a1 == b1) && (a2 == b2) && (a3 == b3) && (ae == be);
        // Pin to the baseline anchors captured before any M5 work.
        bool anchors = (a1 == 8581286081765471666ULL) &&
                       (a2 == 1988111358474182198ULL) &&
                       (a3 == 16753576447339095367ULL) &&
                       (m2::nextRunSeed(runSeed) == 6951516134914417455ULL);
        report(13, "isolation_no_rng_consume", invariant && anchors,
               invariant ? "seed-7 anchor value drift" : "M5 draws perturbed M1/M3/M4 streams");
    }

    // -- #14 validate_pool_gate: one accept + eight distinct rejections ------------------
    {
        using K = m5::AffixKind;
        bool ok = true;
        std::string detail;

        auto expectOk = [&](const std::vector<m5::Affix>& p, bool wantOk, const char* name) {
            if (m5::validate_affix_pool(p).ok != wantOk) { ok = false; detail = name; }
        };

        expectOk(demoPool(), true, "valid pool rejected");                                   // accept
        expectOk({}, false, "empty pool accepted");                                          // empty
        expectOk({ {1, K::DamagePct, 10, 5, 0}, {1, K::MaxHpFlat, 10, 5, 0} }, false, "dup id accepted");   // dup id
        expectOk({ {1, static_cast<K>(4), 10, 5, 0} }, false, "unknown kind accepted");       // out-of-range enum
        expectOk({ {1, K::DamagePct, 10, -1, 0} }, false, "negative weight accepted");        // weight < 0
        expectOk({ {1, K::DamagePct, 10, 0, 0}, {2, K::MaxHpFlat, 10, 0, 0} }, false, "all-zero weight accepted"); // no positive weight
        expectOk({ {1, K::DamagePct, m5::kMaxAbsMagnitude + 1, 5, 0} }, false, "magnitude overflow accepted");     // magnitude range
        expectOk({ {1, K::DamagePct, 10, 5, -1} }, false, "negative max_stacks accepted");    // max_stacks < 0
        {
            std::vector<m5::Affix> big;
            big.reserve(m5::kMaxPoolSize + 1);
            for (std::uint32_t i = 0; i < m5::kMaxPoolSize + 1; ++i)
                big.push_back({ i, K::DamagePct, 10, 5, 0 });
            expectOk(big, false, "oversized pool accepted");                                  // pool size
        }
        report(14, "validate_pool_gate", ok, detail);
    }

    std::printf("---\nSUMMARY passed=%d failed=%d total=%d\n", g_passed, g_failed, kTotalChecks);
    return (g_failed == 0 && g_passed == kTotalChecks) ? 0 : 1;
}
