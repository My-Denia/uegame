// test_m6_encounter.cpp
// Determinism + fuzz + anchor gate for the M6 encounter core (CTest: m6_encounter_determinism).
// Emits COUNTED, named sub-checks `PASS[k/N] <tag>` / `FAIL[k/N] <tag> :: <detail>`.
// The gate is: exit 0 AND exactly N `PASS[` lines AND zero `FAIL[` lines.
//
// This TU includes dungeon.hpp + m2_adapter.hpp purely to build the seed-7 witness and to
// PROVE the frozen M1/M2 anchors are unperturbed (isolation) — including the header does not
// edit it, and the production core (m6_encounter.*) never includes it. CSV paths come from
// argv (defaults are repo-root relative) so the same binary runs from the build dir under ctest.

#include "m6_encounter.hpp"
#include "dungeon.hpp"       // TEST-ONLY: seed-7 layout witness
#include "m2_adapter.hpp"    // TEST-ONLY: enemy plan + preserved-anchor witness

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kTotalChecks = 16;
int g_passed = 0;
int g_failed = 0;

void report(int idx, const char* tag, bool ok, const std::string& detail = "") {
    if (ok) { ++g_passed; std::printf("PASS[%d/%d] %s\n", idx, kTotalChecks, tag); }
    else    { ++g_failed; std::printf("FAIL[%d/%d] %s :: %s\n", idx, kTotalChecks, tag, detail.c_str()); }
    std::fflush(stdout);
}

// --- CSV helpers -----------------------------------------------------------------------
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out; std::string cell; std::stringstream ss(s);
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    return out;
}
int roleIndex(const std::string& n) {
    if (n == "Quiet")      return 0;
    if (n == "Standard")   return 1;
    if (n == "Skirmish")   return 2;
    if (n == "Stronghold") return 3;
    return -1;
}
int typeIndex(const std::string& n) {
    if (n == "Grunt")  return 0;
    if (n == "Runner") return 1;
    if (n == "Brute")  return 2;
    return -1;
}
int toDs(const std::string& s)  { return static_cast<int>(std::atof(s.c_str()) * 10.0 + 0.5); }
int toPct(const std::string& s) { return static_cast<int>(std::atof(s.c_str()) * 100.0 + 0.5); }

bool loadWeights(const char* path, m6::RoleWeights& floorRoleW, m6::TypeWeightsByRole& typeW) {
    std::ifstream in(path); if (!in) return false;
    std::string line, header; std::vector<std::string> hs;
    bool gotFloor = false; bool gotType[m6::kRoleCount] = { false, false, false, false };
    int cKind=-1,cG=-1,cR=-1,cB=-1,cQ=-1,cS=-1,cSk=-1,cSt=-1;
    auto col=[&hs](const char* n)->int{ for(std::size_t i=0;i<hs.size();++i) if(hs[i]==n) return (int)i; return -1; };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty()) continue;
        if (header.empty()) {
            header=line; hs=splitCsv(line);
            cKind=col("Kind"); cG=col("WGrunt"); cR=col("WRunner"); cB=col("WBrute");
            cQ=col("WQuiet"); cS=col("WStandard"); cSk=col("WSkirmish"); cSt=col("WStronghold");
            if (cKind<0||cG<0||cR<0||cB<0||cQ<0||cS<0||cSk<0||cSt<0) return false;
            continue;
        }
        std::vector<std::string> v=splitCsv(line);
        if ((int)v.size()<=cSt) continue;
        const std::string kind=v[(std::size_t)cKind];
        if (kind=="type") {
            int ri=roleIndex(v[0]); if (ri<0) return false;
            typeW[(std::size_t)ri]={ std::atoi(v[(std::size_t)cG].c_str()),
                                     std::atoi(v[(std::size_t)cR].c_str()),
                                     std::atoi(v[(std::size_t)cB].c_str()) };
            gotType[ri]=true;
        } else if (kind=="floor") {
            floorRoleW={ std::atoi(v[(std::size_t)cQ].c_str()), std::atoi(v[(std::size_t)cS].c_str()),
                         std::atoi(v[(std::size_t)cSk].c_str()), std::atoi(v[(std::size_t)cSt].c_str()) };
            gotFloor=true;
        }
    }
    if (!gotFloor) return false;
    for (int i=0;i<m6::kRoleCount;++i) if(!gotType[i]) return false;
    return true;
}

bool loadArchetypes(const char* path, std::vector<m6::Archetype>& out) {
    std::ifstream in(path); if (!in) return false;
    std::string line, header; std::vector<std::string> hs;
    int cHP=-1,cMv=-1,cDmg=-1,cInt=-1,cAg=-1,cLe=-1,cSc=-1;
    auto col=[&hs](const char* n)->int{ for(std::size_t i=0;i<hs.size();++i) if(hs[i]==n) return (int)i; return -1; };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty()) continue;
        if (header.empty()) {
            header=line; hs=splitCsv(line);
            cHP=col("MaxHP"); cMv=col("MoveSpeed"); cDmg=col("ContactDamage"); cInt=col("DamageInterval");
            cAg=col("AggroRange"); cLe=col("LeashRange"); cSc=col("VisualScale");
            if (cHP<0||cMv<0||cDmg<0||cInt<0||cAg<0||cLe<0||cSc<0) return false;
            continue;
        }
        std::vector<std::string> v=splitCsv(line);
        if ((int)v.size()<=cSc) continue;
        int ti=typeIndex(v[0]); if (ti<0) return false;
        m6::Archetype a;
        a.id=(m6::TypeId)ti;
        a.max_hp=std::atoi(v[(std::size_t)cHP].c_str());
        a.move_speed=std::atoi(v[(std::size_t)cMv].c_str());
        a.contact_damage=std::atoi(v[(std::size_t)cDmg].c_str());
        a.damage_interval_ds=toDs(v[(std::size_t)cInt]);
        a.aggro_range=std::atoi(v[(std::size_t)cAg].c_str());
        a.leash_range=std::atoi(v[(std::size_t)cLe].c_str());
        a.visual_scale_pct=toPct(v[(std::size_t)cSc]);
        out.push_back(a);
    }
    return !out.empty();
}

struct DefaultEnemy { int hp=0, move=0, dmg=0, interval_ds=0, aggro=0, leash=0; bool ok=false; };
DefaultEnemy loadCombatDefault(const char* path) {
    DefaultEnemy d; std::ifstream in(path); if (!in) return d;
    std::string line, header; std::vector<std::string> hs;
    auto col=[&hs](const char* n)->int{ for(std::size_t i=0;i<hs.size();++i) if(hs[i]==n) return (int)i; return -1; };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty()) continue;
        if (header.empty()) { header=line; hs=splitCsv(line); continue; }
        if (line.rfind("Default,",0)!=0) continue;
        std::vector<std::string> v=splitCsv(line);
        int cHP=col("EnemyMaxHP"),cMv=col("EnemyMoveSpeed"),cDmg=col("EnemyContactDamage"),
            cInt=col("EnemyDamageInterval"),cAg=col("AggroRange"),cLe=col("LeashRange");
        if (cHP<0||cMv<0||cDmg<0||cInt<0||cAg<0||cLe<0) return d;
        if ((int)v.size()<=cLe) return d;
        d.hp=std::atoi(v[(std::size_t)cHP].c_str()); d.move=std::atoi(v[(std::size_t)cMv].c_str());
        d.dmg=std::atoi(v[(std::size_t)cDmg].c_str()); d.interval_ds=toDs(v[(std::size_t)cInt]);
        d.aggro=std::atoi(v[(std::size_t)cAg].c_str()); d.leash=std::atoi(v[(std::size_t)cLe].c_str());
        d.ok=true; return d;
    }
    return d;
}

// --- seed-7 (and generic) witness ------------------------------------------------------
struct Witness {
    int roomCount=0, startRoom=0;
    std::vector<m6::RoleId> roles;
    std::vector<std::int32_t> roomIdx;   // per-placement room index (from m2 plan)
    std::vector<m6::TypeId> types;
    std::uint64_t spawnHash=0, enemyHash=0;
    std::size_t enemyCount=0;
};
Witness buildWitness(std::uint64_t runSeed, int floor,
                     const m6::RoleWeights& floorRoleW, const m6::TypeWeightsByRole& typeW) {
    Witness w;
    const std::uint64_t fs = m2::deriveFloorSeed(runSeed, floor);
    dungeon::Config c; c.seed = fs;
    const dungeon::Layout L = dungeon::generate(c);
    w.roomCount = (int)L.rooms.size();
    w.startRoom = L.startRoom;
    w.roles = m6::assign_room_roles(w.roomCount, w.startRoom, runSeed, floor, floorRoleW);
    m2::WorldConfig w200; w200.tileSize = 200;
    const int effCount = m2::scaledEnemiesPerRoom(2, floor, 0.5);
    const std::vector<m2::EnemyPlacement> ep = m2::buildEnemyPlan(L, w200, effCount, w.startRoom);
    w.enemyCount = ep.size();
    for (const m2::EnemyPlacement& p : ep) w.roomIdx.push_back(p.roomIndex);
    w.types = m6::assign_enemy_types(w.roomIdx, w.roles, runSeed, floor, typeW);
    w.spawnHash = m2::spawnPlanHash(L, w200);
    w.enemyHash = m2::enemyPlanHash(ep);
    return w;
}

// Pinned seed-7 anchors. Preserved (spawn/enemy) come from the pre-M6 baseline
// (m2test floors 7 --csv CombatConfig.csv). New (roomRole/enemyType) come from the committed
// golden. If any drift, the corresponding check FAILS.
struct FloorAnchor { std::uint64_t spawn, enemy, roomRole, enemyType; int enemies; };
const FloorAnchor kSeed7[3] = {
    { 0x83095029d88b10d0ULL, 0x748fa4c756a110c2ULL, 0x317866b6470f9109ULL, 0xaade4446c5dd714dULL, 16 },
    { 0x5fc3525e6420c061ULL, 0xb2baaf1d1864e619ULL, 0x467f001ddf7613d8ULL, 0xcd7b41918d8f702fULL, 24 },
    { 0x642f59a16031ffbeULL, 0x928a1e2b9a59a5c7ULL, 0x36833ce317404855ULL, 0xce030e725b790c9aULL, 36 },
};

std::string hx(std::uint64_t v) { std::ostringstream o; o<<std::hex<<"0x"<<v; return o.str(); }

} // namespace

int main(int argc, char** argv) {
    const char* pArch = (argc>1)? argv[1] : "uegame/Content/Data/EnemyArchetypes.csv";
    const char* pWts  = (argc>2)? argv[2] : "uegame/Content/Data/EncounterWeights.csv";
    const char* pCmb  = (argc>3)? argv[3] : "uegame/Content/Data/CombatConfig.csv";

    m6::RoleWeights floorRoleW{};
    m6::TypeWeightsByRole typeW{};
    const bool wtsOk = loadWeights(pWts, floorRoleW, typeW);
    std::vector<m6::Archetype> arch;
    const bool archOk = loadArchetypes(pArch, arch);

    // -- #1 roles_pure -----------------------------------------------------------------
    if (!wtsOk) { report(1,"roles_pure",false,"could not load EncounterWeights.csv"); }
    else {
        auto a = m6::assign_room_roles(9,0,7,1,floorRoleW);
        auto b = m6::assign_room_roles(9,0,7,1,floorRoleW);
        report(1,"roles_pure", a==b, "assign_room_roles not pure");
    }

    // -- #2 types_pure -----------------------------------------------------------------
    if (!wtsOk) { report(2,"types_pure",false,"no weights"); }
    else {
        Witness w = buildWitness(7,1,floorRoleW,typeW);
        auto t2 = m6::assign_enemy_types(w.roomIdx, w.roles, 7, 1, typeW);
        report(2,"types_pure", w.types==t2, "assign_enemy_types not pure");
    }

    // -- #3 roles_vary_floor -----------------------------------------------------------
    if (!wtsOk) { report(3,"roles_vary_floor",false,"no weights"); }
    else {
        std::uint64_t h1=m6::roomRoleHash(buildWitness(7,1,floorRoleW,typeW).roles);
        std::uint64_t h2=m6::roomRoleHash(buildWitness(7,2,floorRoleW,typeW).roles);
        std::uint64_t h3=m6::roomRoleHash(buildWitness(7,3,floorRoleW,typeW).roles);
        report(3,"roles_vary_floor", !(h1==h2 && h2==h3), "roomRoleHash identical across floors");
    }

    // -- #4 types_vary_seed ------------------------------------------------------------
    if (!wtsOk) { report(4,"types_vary_seed",false,"no weights"); }
    else {
        std::uint64_t a=m6::enemyTypeHash(buildWitness(7,1,floorRoleW,typeW).types);
        std::uint64_t b=m6::enemyTypeHash(buildWitness(8,1,floorRoleW,typeW).types);
        std::uint64_t c=m6::enemyTypeHash(buildWitness(9,1,floorRoleW,typeW).types);
        report(4,"types_vary_seed", !(a==b && b==c), "enemyTypeHash identical across seeds");
    }

    // -- #5 start_room_quiet -----------------------------------------------------------
    if (!wtsOk) { report(5,"start_room_quiet",false,"no weights"); }
    else {
        bool ok=true; std::string why;
        for (std::uint64_t s=1; s<=200 && ok; ++s) for (int f=1; f<=3 && ok; ++f) {
            Witness w=buildWitness(s,f,floorRoleW,typeW);
            if (w.startRoom>=0 && w.startRoom<(int)w.roles.size() &&
                w.roles[(std::size_t)w.startRoom]!=m6::RoleId::Quiet) { ok=false; why="seed "+std::to_string(s); }
        }
        report(5,"start_room_quiet", ok, why);
    }

    // -- #6 start_room_no_enemies ------------------------------------------------------
    if (!wtsOk) { report(6,"start_room_no_enemies",false,"no weights"); }
    else {
        bool ok=true; std::string why;
        for (std::uint64_t s=1; s<=200 && ok; ++s) for (int f=1; f<=3 && ok; ++f) {
            Witness w=buildWitness(s,f,floorRoleW,typeW);
            for (std::int32_t ri : w.roomIdx) if (ri==w.startRoom) { ok=false; why="seed "+std::to_string(s); break; }
        }
        report(6,"start_room_no_enemies", ok, why);
    }

    // -- #7 weight_zero_never_drawn ----------------------------------------------------
    // Crafted tables: a role weight of 0 (Skirmish) must never be assigned to a non-start
    // room; a type weight of 0 (Runner within Standard) must never be drawn.
    {
        m6::RoleWeights rw = { 0, 60, 0, 40 };            // Skirmish weight 0
        m6::TypeWeightsByRole tw{};
        tw[0]={100,0,0}; tw[1]={70,0,30}; tw[2]={0,100,0}; tw[3]={30,10,60}; // Standard Runner weight 0
        bool ok=true; std::string why;
        for (std::uint64_t s=1; s<=300 && ok; ++s) for (int f=1; f<=3 && ok; ++f) {
            Witness w=buildWitness(s,f,rw,tw);
            for (std::size_t i=0;i<w.roles.size() && ok;++i) {
                if ((int)i==w.startRoom) continue;
                if (w.roles[i]==m6::RoleId::Skirmish) { ok=false; why="zero-weight role Skirmish drawn"; }
            }
            for (std::size_t k=0;k<w.types.size() && ok;++k) {
                if (w.roles[(std::size_t)w.roomIdx[k]]==m6::RoleId::Standard &&
                    w.types[k]==m6::TypeId::Runner) { ok=false; why="zero-weight type Runner drawn in Standard"; }
            }
        }
        report(7,"weight_zero_never_drawn", ok, why);
    }

    // -- #8 validate_archetypes_gate ---------------------------------------------------
    if (!archOk) { report(8,"validate_archetypes_gate",false,"could not load EnemyArchetypes.csv"); }
    else {
        bool ok = m6::validate_enemy_archetypes(arch).ok;                        // good table accepts
        ok = ok && !m6::validate_enemy_archetypes({}).ok;                        // empty rejects
        { auto dup=arch; dup.push_back(arch[0]); ok = ok && !m6::validate_enemy_archetypes(dup).ok; } // dup id
        { auto bad=arch; bad[0].id=(m6::TypeId)5; ok = ok && !m6::validate_enemy_archetypes(bad).ok; } // unknown id
        { std::vector<m6::Archetype> miss={arch[0],arch[1]}; ok = ok && !m6::validate_enemy_archetypes(miss).ok; } // missing Brute
        { auto bad=arch; bad[0].damage_interval_ds=13; ok = ok && !m6::validate_enemy_archetypes(bad).ok; } // not 0.5-mult
        { auto bad=arch; bad[0].max_hp=0; ok = ok && !m6::validate_enemy_archetypes(bad).ok; }            // hp<=0
        report(8,"validate_archetypes_gate", ok, "archetype validation wrong verdict");
    }

    // -- #9 validate_weights_gate ------------------------------------------------------
    if (!wtsOk) { report(9,"validate_weights_gate",false,"no weights"); }
    else {
        bool ok = m6::validate_encounter_weights(floorRoleW, typeW).ok;               // good accepts
        { m6::RoleWeights z={0,0,0,0}; ok = ok && !m6::validate_encounter_weights(z,typeW).ok; } // all-zero roles
        { m6::RoleWeights n={0,-1,25,15}; ok = ok && !m6::validate_encounter_weights(n,typeW).ok; } // negative role
        { auto tw=typeW; tw[1]={-1,0,0}; ok = ok && !m6::validate_encounter_weights(floorRoleW,tw).ok; } // negative type
        { auto tw=typeW; tw[1]={0,0,0}; ok = ok && !m6::validate_encounter_weights(floorRoleW,tw).ok; } // reachable role, zero types
        report(9,"validate_weights_gate", ok, "weight validation wrong verdict");
    }

    // -- #10 no_density_no_position ----------------------------------------------------
    if (!wtsOk) { report(10,"no_density_no_position",false,"no weights"); }
    else {
        bool ok=true; std::string why;
        for (int f=1; f<=3 && ok; ++f) {
            Witness w=buildWitness(7,f,floorRoleW,typeW);
            if (w.types.size()!=w.enemyCount) { ok=false; why="type count != placement count"; }
            else if ((int)w.enemyCount!=kSeed7[f-1].enemies) { ok=false; why="placement count != baseline"; }
            else if (w.enemyHash!=kSeed7[f-1].enemy) { ok=false; why="enemyPlanHash drift f"+std::to_string(f); }
        }
        report(10,"no_density_no_position", ok, why);
    }

    // -- #11 geometry_anchor_unchanged -------------------------------------------------
    if (!wtsOk) { report(11,"geometry_anchor_unchanged",false,"no weights"); }
    else {
        bool ok=true; std::string why;
        for (int f=1; f<=3 && ok; ++f) {
            Witness w=buildWitness(7,f,floorRoleW,typeW);
            if (w.spawnHash!=kSeed7[f-1].spawn) { ok=false; why="spawnPlanHash drift f"+std::to_string(f)+" got "+hx(w.spawnHash); }
        }
        report(11,"geometry_anchor_unchanged", ok, why);
    }

    // -- #12 new_anchors_pinned --------------------------------------------------------
    if (!wtsOk) { report(12,"new_anchors_pinned",false,"no weights"); }
    else {
        bool ok=true; std::string why;
        for (int f=1; f<=3 && ok; ++f) {
            Witness w=buildWitness(7,f,floorRoleW,typeW);
            std::uint64_t rr=m6::roomRoleHash(w.roles), et=m6::enemyTypeHash(w.types);
            if (rr!=kSeed7[f-1].roomRole) { ok=false; why="roomRoleHash f"+std::to_string(f)+" got "+hx(rr); }
            else if (et!=kSeed7[f-1].enemyType) { ok=false; why="enemyTypeHash f"+std::to_string(f)+" got "+hx(et); }
        }
        report(12,"new_anchors_pinned", ok, why);
    }

    // -- #13 grunt_default_parity ------------------------------------------------------
    {
        DefaultEnemy d = loadCombatDefault(pCmb);
        if (!archOk || !d.ok) { report(13,"grunt_default_parity",false,"could not load CSVs"); }
        else {
            const m6::Archetype* g=nullptr;
            for (const auto& a : arch) if (a.id==m6::TypeId::Grunt) g=&a;
            bool ok = g && g->max_hp==d.hp && g->move_speed==d.move && g->contact_damage==d.dmg &&
                      g->damage_interval_ds==d.interval_ds && g->aggro_range==d.aggro && g->leash_range==d.leash;
            report(13,"grunt_default_parity", ok, "Grunt row != CombatConfig Default enemy fields");
        }
    }

    // -- #14 all_grunt_reproduction ----------------------------------------------------
    // Forcing every role's type weights to Grunt-only yields all-Grunt placements; combined
    // with #13 (Grunt == Default) this reproduces the M5 enemy effective stats.
    if (!wtsOk) { report(14,"all_grunt_reproduction",false,"no weights"); }
    else {
        m6::TypeWeightsByRole allG{}; for (int r=0;r<m6::kRoleCount;++r) allG[r]={1,0,0};
        bool ok=true; std::string why;
        for (int f=1; f<=3 && ok; ++f) {
            Witness w=buildWitness(7,f,floorRoleW,allG);
            for (m6::TypeId t : w.types) if (t!=m6::TypeId::Grunt) { ok=false; why="non-Grunt under all-Grunt weights"; break; }
        }
        report(14,"all_grunt_reproduction", ok, why);
    }

    // -- #15 isolation (m6 draws do not perturb the frozen m1/m2 streams) --------------
    if (!wtsOk) { report(15,"isolation",false,"no weights"); }
    else {
        // Capture m2 witnesses, interleave thousands of m6 draws, recapture, require identity.
        auto m2hash=[&](std::uint64_t s,int f)->std::uint64_t{
            dungeon::Config c; c.seed=m2::deriveFloorSeed(s,f);
            dungeon::Layout L=dungeon::generate(c);
            m2::WorldConfig w200; w200.tileSize=200;
            return m2::enemyPlanHash(m2::buildEnemyPlan(L,w200,m2::scaledEnemiesPerRoom(2,f,0.5),L.startRoom));
        };
        std::uint64_t before[6]; int bi=0;
        for (std::uint64_t s=7; s<=8; ++s) for (int f=1; f<=3; ++f) before[bi++]=m2hash(s,f);
        volatile std::uint64_t sink=0;
        for (std::uint64_t s=1; s<=5000; ++s) {
            Witness w=buildWitness(s,(int)(s%3)+1,floorRoleW,typeW);
            sink ^= m6::roomRoleHash(w.roles) ^ m6::enemyTypeHash(w.types);
        }
        (void)sink;
        std::uint64_t after[6]; int ai=0; bool ok=true;
        for (std::uint64_t s=7; s<=8; ++s) for (int f=1; f<=3; ++f) after[ai++]=m2hash(s,f);
        for (int i=0;i<6;++i) if (before[i]!=after[i]) ok=false;
        report(15,"isolation", ok, "m2 enemyPlanHash changed across m6 draws");
    }

    // -- #16 fuzz_invariants (>= 60000 seeds) ------------------------------------------
    if (!wtsOk) { report(16,"fuzz_invariants",false,"no weights"); }
    else {
        const std::uint64_t N=60000; std::uint64_t okCount=0; std::string why;
        for (std::uint64_t s=1; s<=N; ++s) {
            const int f=(int)(s%3)+1;
            Witness w=buildWitness(s,f,floorRoleW,typeW);
            bool good = (w.roles.size()==(std::size_t)w.roomCount);
            if (good && w.startRoom>=0 && w.startRoom<(int)w.roles.size())
                good = (w.roles[(std::size_t)w.startRoom]==m6::RoleId::Quiet);
            for (std::size_t i=0; good && i<w.roles.size(); ++i) {
                int r=(int)w.roles[i];
                if (r<0||r>=m6::kRoleCount) good=false;
                else if ((int)i!=w.startRoom && floorRoleW[r]==0) good=false; // zero-weight role never on non-start
            }
            if (good) good = (w.types.size()==w.enemyCount);
            for (std::size_t k=0; good && k<w.types.size(); ++k) {
                int t=(int)w.types[k];
                if (t<0||t>=m6::kTypeCount) { good=false; break; }
                int role=(int)w.roles[(std::size_t)w.roomIdx[k]];
                if (typeW[(std::size_t)role][(std::size_t)t]==0) good=false;   // zero-weight type never drawn
            }
            if (good) ++okCount; else if (why.empty()) why="first fail at seed "+std::to_string(s);
        }
        report(16,"fuzz_invariants", okCount==N,
               why.empty()? ("only "+std::to_string(okCount)+"/"+std::to_string(N)) : why);
    }

    std::printf("\nM6 encounter core: %d passed, %d failed (of %d)\n", g_passed, g_failed, kTotalChecks);
    return (g_failed==0 && g_passed==kTotalChecks) ? 0 : 1;
}
