// dump_m6_encounter_golden.cpp
// Golden-vector dumper for the M6 encounter core (CTest: m6_encounter_goldenvec).
// Emits a byte-stable trace of run_seed=7, floors 1..3: per-room roles, per-archetype tally,
// the NEW anchors (roomRoleHash / enemyTypeHash), and the PRESERVED m2 anchors
// (spawnPlanHash / enemyPlanHash) so the golden itself witnesses that geometry + positions
// are untouched.
//
// This dumper (a TEST/witness tool, not the core) is allowed to include dungeon.hpp and
// m2_adapter.hpp to build the seed-7 layout + enemy plan. The core (m6_encounter.*) does not.
//
// The weight tables are read from EncounterWeights.csv (argv[2]) so the golden is DATA-DRIVEN:
// the committed CSV is the single source of truth. base per-room (2) and per-floor scaling
// (0.5) equal the frozen CombatConfig.csv Default (EnemiesPerRoom / PerFloorScaling); the
// preserved-anchor lines below must match the seed-7 baseline captured before any M6 change.
//
// Output is written to argv[1] via a BINARY std::ofstream (bare LF) so the strict byte compare
// in diff_m6_golden.cmake is EOL-stable on Windows (MSVC), WSL/Linux (g++), and CI alike.

#include "m6_encounter.hpp"
#include "dungeon.hpp"
#include "m2_adapter.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* roleName(m6::RoleId r) {
    switch (r) {
        case m6::RoleId::Quiet:      return "Quiet";
        case m6::RoleId::Standard:   return "Standard";
        case m6::RoleId::Skirmish:   return "Skirmish";
        case m6::RoleId::Stronghold: return "Stronghold";
    }
    return "?";
}
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::string cell;
    std::stringstream ss(s);
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    return out;
}

int roleIndex(const std::string& name) {
    if (name == "Quiet")      return 0;
    if (name == "Standard")   return 1;
    if (name == "Skirmish")   return 2;
    if (name == "Stronghold") return 3;
    return -1;
}

// Parse EncounterWeights.csv into the per-floor role weights + per-role type weights.
// Returns false (with a message on stderr) if the file is missing rows/columns.
bool loadWeights(const char* path, m6::RoleWeights& floorRoleW, m6::TypeWeightsByRole& typeW) {
    std::ifstream in(path);
    if (!in) { std::cerr << "cannot open weights CSV: " << path << "\n"; return false; }
    std::string line, header;
    std::vector<std::string> hs;
    bool gotFloor = false;
    bool gotType[m6::kRoleCount] = { false, false, false, false };
    auto col = [&hs](const char* name) -> int {
        for (std::size_t i = 0; i < hs.size(); ++i) if (hs[i] == name) return static_cast<int>(i);
        return -1;
    };
    int cKind = -1, cG = -1, cR = -1, cB = -1, cQ = -1, cS = -1, cSk = -1, cSt = -1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (header.empty()) {
            header = line;
            hs = splitCsv(line);
            cKind = col("Kind");
            cG = col("WGrunt"); cR = col("WRunner"); cB = col("WBrute");
            cQ = col("WQuiet"); cS = col("WStandard"); cSk = col("WSkirmish"); cSt = col("WStronghold");
            if (cKind < 0 || cG < 0 || cR < 0 || cB < 0 || cQ < 0 || cS < 0 || cSk < 0 || cSt < 0) {
                std::cerr << "weights CSV missing required columns\n"; return false;
            }
            continue;
        }
        const std::vector<std::string> v = splitCsv(line);
        const int need = cSt; // largest column index used
        if (static_cast<int>(v.size()) <= need) continue;
        const std::string rowName = v[0];
        const std::string kind = v[static_cast<std::size_t>(cKind)];
        if (kind == "type") {
            const int ri = roleIndex(rowName);
            if (ri < 0) { std::cerr << "unknown role in weights CSV: " << rowName << "\n"; return false; }
            typeW[static_cast<std::size_t>(ri)] = {
                std::atoi(v[static_cast<std::size_t>(cG)].c_str()),
                std::atoi(v[static_cast<std::size_t>(cR)].c_str()),
                std::atoi(v[static_cast<std::size_t>(cB)].c_str())
            };
            gotType[ri] = true;
        } else if (kind == "floor") {
            floorRoleW = {
                std::atoi(v[static_cast<std::size_t>(cQ)].c_str()),
                std::atoi(v[static_cast<std::size_t>(cS)].c_str()),
                std::atoi(v[static_cast<std::size_t>(cSk)].c_str()),
                std::atoi(v[static_cast<std::size_t>(cSt)].c_str())
            };
            gotFloor = true;
        }
    }
    if (!gotFloor) { std::cerr << "weights CSV has no floor row\n"; return false; }
    for (int i = 0; i < m6::kRoleCount; ++i) {
        if (!gotType[i]) { std::cerr << "weights CSV missing a role type row\n"; return false; }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << (argc ? argv[0] : "dump_m6_encounter_golden")
                  << " <out.txt> <EncounterWeights.csv>\n";
        return 2;
    }
    m6::RoleWeights floorRoleW{};
    m6::TypeWeightsByRole typeW{};
    if (!loadWeights(argv[2], floorRoleW, typeW)) return 2;

    const std::uint64_t runSeed = 7;
    const int basePerRoom = 2;      // == CombatConfig.csv Default EnemiesPerRoom (frozen)
    const double scaling  = 0.5;    // == CombatConfig.csv Default PerFloorScaling (frozen)

    m2::WorldConfig w200;
    w200.tileSize = 200;            // UE runtime tile size; matches the seed-7 baseline anchors

    std::ostringstream o;
    o << "# M6 encounter golden vector (engine-agnostic core, deterministic, integer-only)\n";
    o << "# run_seed=" << runSeed << "  tileSize=200  base_per_room=2  per_floor_scaling=0.5\n";
    o << "# roles: Quiet Standard Skirmish Stronghold ; archetypes: Grunt Runner Brute\n";
    o << "# floorRoleWeights[Quiet,Standard,Skirmish,Stronghold]="
      << floorRoleW[0] << "," << floorRoleW[1] << "," << floorRoleW[2] << "," << floorRoleW[3] << "\n";
    o << "# typeWeights Standard[G,R,B]=" << typeW[1][0] << "," << typeW[1][1] << "," << typeW[1][2]
      << "  Skirmish=" << typeW[2][0] << "," << typeW[2][1] << "," << typeW[2][2]
      << "  Stronghold=" << typeW[3][0] << "," << typeW[3][1] << "," << typeW[3][2]
      << "  Quiet=" << typeW[0][0] << "," << typeW[0][1] << "," << typeW[0][2] << "\n";
    o << "# preserved m2 anchors (spawnPlanHash/enemyPlanHash) MUST equal the pre-M6 baseline\n";

    for (int f = 1; f <= 3; ++f) {
        const std::uint64_t fs = m2::deriveFloorSeed(runSeed, f);
        dungeon::Config c; c.seed = fs;
        const dungeon::Layout L = dungeon::generate(c);
        const int roomCount = static_cast<int>(L.rooms.size());
        const int startRoom = L.startRoom;

        const std::vector<m6::RoleId> roles =
            m6::assign_room_roles(roomCount, startRoom, runSeed, f, floorRoleW);

        const int effCount = m2::scaledEnemiesPerRoom(basePerRoom, f, scaling);
        const std::vector<m2::EnemyPlacement> ep =
            m2::buildEnemyPlan(L, w200, effCount, startRoom);
        std::vector<std::int32_t> roomIdx;
        roomIdx.reserve(ep.size());
        for (const m2::EnemyPlacement& p : ep) roomIdx.push_back(p.roomIndex);

        const std::vector<m6::TypeId> types =
            m6::assign_enemy_types(roomIdx, roles, runSeed, f, typeW);

        int roleTally[m6::kRoleCount] = { 0, 0, 0, 0 };
        for (const m6::RoleId r : roles) roleTally[static_cast<int>(r)]++;
        int typeTally[m6::kTypeCount] = { 0, 0, 0 };
        for (const m6::TypeId t : types) typeTally[static_cast<int>(t)]++;

        o << "floor " << f << "\n";
        o << "  layout: rooms=" << roomCount << " startRoom=" << startRoom
          << " effCount=" << effCount << " enemies=" << ep.size() << "\n";
        o << std::hex
          << "  m2anchors: spawnPlanHash=0x" << m2::spawnPlanHash(L, w200)
          << " enemyPlanHash=0x" << m2::enemyPlanHash(ep) << std::dec << "\n";
        o << "  roles:";
        for (const m6::RoleId r : roles) o << " " << roleName(r);
        o << "\n";
        o << "  roleTally: Quiet=" << roleTally[0] << " Standard=" << roleTally[1]
          << " Skirmish=" << roleTally[2] << " Stronghold=" << roleTally[3] << "\n";
        o << "  typeTally: Grunt=" << typeTally[0] << " Runner=" << typeTally[1]
          << " Brute=" << typeTally[2] << "\n";
        o << std::hex
          << "  m6anchors: roomRoleHash=0x" << m6::roomRoleHash(roles)
          << " enemyTypeHash=0x" << m6::enemyTypeHash(types) << std::dec << "\n";
    }
    o << "nextRunSeed(" << runSeed << ")=" << m2::nextRunSeed(runSeed) << "\n";

    const std::string text = o.str();
    std::ofstream out(argv[1], std::ios::binary);   // binary => bare LF on all platforms
    if (!out) { std::cerr << "cannot open output file: " << argv[1] << "\n"; return 2; }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) { std::cerr << "write failed: " << argv[1] << "\n"; return 2; }
    return 0;
}
