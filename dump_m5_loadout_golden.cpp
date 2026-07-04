// dump_m5_loadout_golden.cpp
// Golden-vector dumper for the M5 loadout core (CTest: m5_loadout_goldenvec).
// Writes a byte-stable textual trace of run_seed=7, floors 1..3, showing each floor's
// 3-choose-1 offer, the (deterministic) choice, and the cumulative resolved stats.
//
// Output is written to argv[1] (or stdout if omitted) via a BINARY std::ofstream so
// the line terminator is a bare '\n' (LF) on every platform — MSVC text-mode would
// otherwise translate '\n' to CRLF and break the strict byte compare in diff_golden.cmake.
//
// All numbers here are TEST/GOLDEN fixtures, never production truth: 15/600/140 are the
// current player values and 500 is a deliberately SYNTHETIC move base (the CSV has no
// player move speed — 240 is EnemyMoveSpeed, an enemy stat). Do not "correct" 500 to 240;
// doing so would silently shift the golden. See m5_loadout.hpp for why the core header
// carries no reference-base constant.

#include "m5_loadout.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kindName(m5::AffixKind k) {
    switch (k) {
        case m5::AffixKind::DamagePct:         return "DamagePct";
        case m5::AffixKind::AttackIntervalPct: return "AttackIntervalPct";
        case m5::AffixKind::MaxHpFlat:         return "MaxHpFlat";
        case m5::AffixKind::MoveSpeedPct:      return "MoveSpeedPct";
    }
    return "?";
}

// Same demo pool as the determinism test — a small, illustrative fixture covering all
// four kinds plus an unlimited-stack damage affix (id 10). Fixture only; not in the header.
std::vector<m5::Affix> demoPool() {
    using K = m5::AffixKind;
    return {
        {  10, K::DamagePct,          15,      100,    0 },
        {  11, K::DamagePct,          25,       60,    3 },
        {  12, K::AttackIntervalPct, -20,       80,    0 },
        {  13, K::AttackIntervalPct, -40,       40,    2 },
        {  14, K::MaxHpFlat,          30,      100,    0 },
        {  15, K::MaxHpFlat,          60,       50,    2 },
        {  16, K::MoveSpeedPct,       10,       70,    0 },
        {  17, K::MoveSpeedPct,       20,       30,    2 },
        {  18, K::DamagePct,           5,        0,    0 },  // weight 0 => never offered
    };
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<m5::Affix> pool = demoPool();
    const m5::BaseStats base = { 15, 600, 140, 500 };
    const std::uint64_t runSeed = 7;

    std::ostringstream o;
    o << "# M5 loadout golden vector (engine-agnostic core, deterministic, integer-only)\n";
    o << "# run_seed=" << runSeed << "\n";
    o << "# base fixture (TEST-ONLY, not production truth): damage=" << base.base_damage
      << " attack_ms=" << base.attack_ms << " max_hp=" << base.max_hp
      << " move_speed=" << base.move_speed << "\n";
    o << "# choice policy: take offer[0] (first drawn) each floor; cumulative loadout\n";

    std::vector<m5::Affix> chosen;
    std::vector<std::uint32_t> chosenIds;
    for (std::uint64_t floor = 1; floor <= 3; ++floor) {
        const std::vector<m5::Affix> offer = m5::generate_offer(pool, chosenIds, runSeed, floor, /*offer_index*/ 0, /*offer_size*/ 3);
        o << "floor " << floor << "\n";
        o << "  offer:\n";
        for (const m5::Affix& a : offer) {
            o << "    id=" << a.id << " kind=" << kindName(a.kind)
              << " mag=" << a.magnitude << " weight=" << a.weight
              << " max_stacks=" << a.max_stacks << "\n";
        }
        if (!offer.empty()) {
            const m5::Affix pick = offer[0];
            chosen.push_back(pick);
            chosenIds.push_back(pick.id);
            o << "  choose: id=" << pick.id << " kind=" << kindName(pick.kind)
              << " mag=" << pick.magnitude << "\n";
        } else {
            o << "  choose: (none - empty offer)\n";
        }
        const m5::ResolvedStats rs = m5::resolve(base, chosen);
        o << "  resolved: damage=" << rs.damage << " attack_ms=" << rs.attack_ms
          << " max_hp=" << rs.max_hp << " move_speed=" << rs.move_speed << "\n";
    }

    // resolve() demonstration: apply resolve() to explicit affix sets (each from base,
    // NOT from RNG offers) so the golden exercises every resolution rule and the safety
    // clamp, and pins the arithmetic. This is a scripted math trace, clearly separate
    // from the RNG offer trace above.
    {
        using K = m5::AffixKind;
        auto show = [&](const char* label, const std::vector<m5::Affix>& picks) {
            const m5::ResolvedStats rs = m5::resolve(base, picks);
            o << "  " << label << ": damage=" << rs.damage << " attack_ms=" << rs.attack_ms
              << " max_hp=" << rs.max_hp << " move_speed=" << rs.move_speed << "\n";
        };
        o << "resolve-demo (resolve(base, explicit picks) -- every rule + clamp)\n";
        show("base",                          {});
        show("DamagePct+15",                  { {0, K::DamagePct, 15, 1, 0} });
        show("DamagePct+15,AtkInterval-25",   { {0, K::DamagePct, 15, 1, 0}, {1, K::AttackIntervalPct, -25, 1, 0} });
        show("DamagePct+30(x2),AtkInt-25",    { {0, K::DamagePct, 15, 1, 0}, {2, K::DamagePct, 15, 1, 0}, {1, K::AttackIntervalPct, -25, 1, 0} });
        show("MaxHpFlat+30,MoveSpeedPct+10",  { {0, K::MaxHpFlat, 30, 1, 0}, {1, K::MoveSpeedPct, 10, 1, 0} });
        show("clamp:AttackIntervalPct-40x50", std::vector<m5::Affix>(50, {0, K::AttackIntervalPct, -40, 1, 0}));
    }

    const std::string text = o.str();
    if (argc >= 2) {
        std::ofstream out(argv[1], std::ios::binary);   // binary => bare LF on all platforms
        if (!out) { std::cerr << "cannot open output file: " << argv[1] << "\n"; return 2; }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) { std::cerr << "write failed: " << argv[1] << "\n"; return 2; }
    } else {
        std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    return 0;
}
