// m2_adapter_test.cpp
// 引擎无关的 M2 适配层测试 / 验收工具 (可用 g++ 编译运行，不需要 UE)。
//
// 模式：
//   m2test report [seed]     单种子报告：保真度 + M1 网格校验 vs M2 世界校验 + 确定性哈希
//   m2test validate [N]      N 个种子：世界空间全连通 + 网格/世界一致性，打印 P/N
//   m2test determinism [seed] 同种子两次 spawn-plan 哈希对比
//   m2test enemies [seed] [perRoom]   M3: 打印敌人布点 + tile 无关哈希 (排除起始房)
//   m2test enemyDeterminism [N]       M3: N 个种子 x2 生成，哈希/布点全等 + 房内校验
//   m2test floors [runSeed] [n] [--csv <CombatConfig.csv>]
//                                     M4: 楼层 1..n 的种子/布局哈希(tile200)/敌人哈希/缩放值
//                                     —— PIE 验证前预注册的 g++ 参考值; --csv 直接读数据表,
//                                     避免改表后参考值悄悄基于旧配置; 未显式给 n 时
//                                     n 默认取表内 MaxFloors (与 FloorManager 的判赢层数对齐)
//   m2test floorsDeterminism [N]      M4: N 个 runSeed x2 -> 楼层序列逐位相同; 相邻 runSeed 不同

#include "m2_adapter.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace dungeon;

int main(int argc, char** argv) {
    std::string mode = (argc > 1) ? argv[1] : "report";
    m2::WorldConfig wc;  // 默认 tileSize=100, wallHeight=200

    if (mode == "validate") {
        long n = (argc > 2) ? std::strtol(argv[2], nullptr, 10) : 200;
        long worldOk = 0, consistent = 0;
        for (long i = 0; i < n; ++i) {
            Config c; c.seed = static_cast<std::uint64_t>(1 + i);
            Layout L = generate(c);
            ValidationResult vr = validate(L);                 // M1 网格校验
            m2::WorldReach wr = m2::worldReachability(L, wc);  // M2 世界校验
            if (wr.fullyConnected()) ++worldOk;
            // 一致性：M1 与 M2 都判定全连通，且可走点数完全相等
            if (vr.ok && wr.fullyConnected() && vr.reachedCells == wr.reached &&
                vr.passableCells == wr.passable) {
                ++consistent;
            }
        }
        std::cout << "world-space fully-connected : " << worldOk << "/" << n << "\n";
        std::cout << "grid<->world consistency    : " << consistent << "/" << n << "\n";
        return (worldOk == n && consistent == n) ? 0 : 1;
    }

    if (mode == "enemies") {
        std::uint64_t seed = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 7;
        int perRoom = (argc > 3) ? std::atoi(argv[3]) : 2;
        Config c; c.seed = seed;
        Layout L = generate(c);
        std::vector<m2::EnemyPlacement> plan =
            m2::buildEnemyPlan(L, wc, perRoom, L.startRoom);
        long expected = 0;
        for (int r = 0; r < static_cast<int>(L.rooms.size()); ++r) {
            if (r == L.startRoom) continue;
            expected += std::min(perRoom, L.rooms[static_cast<std::size_t>(r)].w *
                                          L.rooms[static_cast<std::size_t>(r)].h);
        }
        std::cout << "seed=" << seed << " perRoom=" << perRoom
                  << " rooms=" << L.rooms.size() << " startRoom=" << L.startRoom
                  << " enemies=" << plan.size()
                  << " expected=" << expected
                  << (static_cast<long>(plan.size()) == expected ? " (exact)" : " (MISMATCH)")
                  << "\n";
        for (const m2::EnemyPlacement& p : plan) {
            std::cout << "  room=" << p.roomIndex << " cell=(" << p.gx << "," << p.gy
                      << ") world=(" << p.wx << "," << p.wy << ")\n";
        }
        std::cout << std::hex << "enemyPlanHash=0x" << m2::enemyPlanHash(plan) << std::dec << "\n";
        return 0;
    }

    if (mode == "enemyDeterminism") {
        long n = (argc > 2) ? std::strtol(argv[2], nullptr, 10) : 500;
        long identical = 0, inRoom = 0, excluded = 0, noDup = 0;
        for (long i = 0; i < n; ++i) {
            Config c; c.seed = static_cast<std::uint64_t>(1 + i);
            Layout L = generate(c);
            std::vector<m2::EnemyPlacement> p1 = m2::buildEnemyPlan(L, wc, 2, L.startRoom);
            std::vector<m2::EnemyPlacement> p2 =
                m2::buildEnemyPlan(generate(c), wc, 2, L.startRoom);
            bool same = (m2::enemyPlanHash(p1) == m2::enemyPlanHash(p2)) &&
                        (p1.size() == p2.size());
            for (std::size_t k = 0; same && k < p1.size(); ++k) {
                same = p1[k].roomIndex == p2[k].roomIndex &&
                       p1[k].gx == p2[k].gx && p1[k].gy == p2[k].gy &&
                       p1[k].wx == p2[k].wx && p1[k].wy == p2[k].wy;
            }
            if (same) ++identical;
            bool okRoom = true, okExcl = true, okDup = true;
            std::set<std::tuple<int, int, int>> cellSet;   // (room, gx, gy)
            for (const m2::EnemyPlacement& p : p1) {
                const Room& rm = L.rooms[static_cast<std::size_t>(p.roomIndex)];
                if (p.gx < rm.x || p.gx >= rm.x + rm.w ||
                    p.gy < rm.y || p.gy >= rm.y + rm.h) okRoom = false;
                if (p.roomIndex == L.startRoom) okExcl = false;
                if (!cellSet.insert({ p.roomIndex, p.gx, p.gy }).second) okDup = false;
            }
            if (okRoom) ++inRoom;
            if (okExcl) ++excluded;
            if (okDup) ++noDup;
        }
        std::cout << "identical plans (2x gen) : " << identical << "/" << n << "\n";
        std::cout << "all placements in-room   : " << inRoom << "/" << n << "\n";
        std::cout << "start room excluded      : " << excluded << "/" << n << "\n";
        std::cout << "no duplicate cells       : " << noDup << "/" << n << "\n";
        return (identical == n && inRoom == n && excluded == n && noDup == n) ? 0 : 1;
    }

    if (mode == "floors") {
        std::uint64_t runSeed = 7;
        int n = 3;
        bool nExplicit = false;
        // 基准值:默认常量必须等于提交时的 CSV;--csv <path> 则直接读数据表 Default 行,
        // 这样改表后预注册参考值随表移动,不会悄悄基于旧配置 (布点数量变 -> 哈希变)。
        int basePerRoom = 2;                // == CSV EnemiesPerRoom
        double baseHP = 30.0;               // == CSV EnemyMaxHP
        double scaling = 1.0;               // == CSV PerFloorScaling
        std::string cfgSrc = "built-in-defaults";
        // 单趟解析:--csv 消费它的取值,剩余按序算位置参数 (runSeed, n)。这样
        // `floors --csv <path>` 不会把路径误当楼层数吞掉 (atoi=0 -> 静默零输出)。
        int positional = 0;
        for (int a = 2; a < argc; ++a) {
            if (std::string(argv[a]) != "--csv") {
                if (positional == 0) runSeed = std::strtoull(argv[a], nullptr, 10);
                else if (positional == 1) { n = std::atoi(argv[a]); nExplicit = true; }
                ++positional;
                continue;
            }
            if (a + 1 >= argc) { std::cerr << "--csv needs a path\n"; return 2; }
            const char* path = argv[++a];
            std::ifstream in(path);
            if (!in) { std::cerr << "cannot open CSV: " << path << "\n"; return 2; }
            std::string header, row, line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (header.empty()) { header = line; continue; }
                // 行名单元格也可能被表格软件加引号: 两种前缀都算 Default 行。
                if (line.rfind("Default,", 0) == 0 || line.rfind("\"Default\",", 0) == 0) { row = line; break; }
            }
            if (header.empty() || row.empty()) {
                std::cerr << "CSV missing header/Default row: " << path << "\n"; return 2;
            }
            auto split = [](const std::string& s) {
                std::vector<std::string> out; std::string cell; std::stringstream ss(s);
                while (std::getline(ss, cell, ',')) {
                    // 表格软件可能给单元格加引号 ("2"): 去外层引号并还原 "" 转义,
                    // 否则 atoi/atof 读到引号得 0, 参考值悄悄归零。
                    if (cell.size() >= 2 && cell.front() == '"' && cell.back() == '"') {
                        cell = cell.substr(1, cell.size() - 2);
                        std::string un; un.reserve(cell.size());
                        for (std::size_t i = 0; i < cell.size(); ++i) {
                            if (cell[i] == '"' && i + 1 < cell.size() && cell[i + 1] == '"') { un += '"'; ++i; }
                            else { un += cell[i]; }
                        }
                        cell = un;
                    }
                    out.push_back(cell);
                }
                return out;
            };
            const std::vector<std::string> hs = split(header), vs = split(row);
            auto col = [&hs](const char* name) -> int {
                for (std::size_t i = 0; i < hs.size(); ++i) if (hs[i] == name) return static_cast<int>(i);
                return -1;
            };
            const int cPerRoom = col("EnemiesPerRoom"), cHP = col("EnemyMaxHP"), cScal = col("PerFloorScaling");
            if (cPerRoom < 0 || cHP < 0 || cScal < 0 ||
                cPerRoom >= static_cast<int>(vs.size()) || cHP >= static_cast<int>(vs.size()) ||
                cScal >= static_cast<int>(vs.size())) {
                std::cerr << "CSV missing EnemiesPerRoom/EnemyMaxHP/PerFloorScaling: " << path << "\n"; return 2;
            }
            basePerRoom = std::atoi(vs[cPerRoom].c_str());
            baseHP = std::atof(vs[cHP].c_str());
            scaling = std::atof(vs[cScal].c_str());
            // 未显式给 n 时楼层数跟随表内 MaxFloors (FloorManager 用它判赢),
            // 避免表改成 5 层后这里还只预注册 1..3。
            const int cMax = col("MaxFloors");
            if (!nExplicit && cMax >= 0 && cMax < static_cast<int>(vs.size())) {
                n = std::atoi(vs[cMax].c_str());
            }
            cfgSrc = std::string("csv(") + path + ")";
        }
        if (n <= 0) { std::cerr << "floor count must be >= 1 (got " << n << ")\n"; return 2; }
        m2::WorldConfig w200; w200.tileSize = 200;   // UE 默认, 预注册值必须按此计算
        std::cout << "runSeed=" << runSeed << " floors=" << n
                  << " basePerRoom=" << basePerRoom << " baseHP=" << baseHP
                  << " scaling=" << scaling << " config=" << cfgSrc << "\n";
        for (int f = 1; f <= n; ++f) {
            std::uint64_t fs = m2::deriveFloorSeed(runSeed, f);
            Config c; c.seed = fs;
            Layout L = generate(c);
            const int effCount = m2::scaledEnemiesPerRoom(basePerRoom, f, scaling);
            const double effHP = baseHP * m2::floorMultiplier(f, scaling);
            std::vector<m2::EnemyPlacement> ep = m2::buildEnemyPlan(L, w200, effCount, L.startRoom);
            std::cout << "floor=" << f
                      << " seed=" << fs
                      << std::hex
                      << " planHash=0x" << m2::spawnPlanHash(L, w200)
                      << " enemyHash=0x" << m2::enemyPlanHash(ep)
                      << std::dec
                      << " effCount=" << effCount
                      << " effHP=" << effHP
                      << " enemies=" << ep.size()
                      << " rooms=" << L.rooms.size() << "\n";
        }
        std::cout << "nextRunSeed(" << runSeed << ")=" << m2::nextRunSeed(runSeed) << "\n";
        return 0;
    }

    if (mode == "floorsDeterminism") {
        long n = (argc > 2) ? std::strtol(argv[2], nullptr, 10) : 200;
        long identical = 0, differs = 0;
        m2::WorldConfig w200; w200.tileSize = 200;
        for (long i = 0; i < n; ++i) {
            std::uint64_t rs = static_cast<std::uint64_t>(1 + i);
            bool same = true;
            std::uint64_t firstFloorHashA = 0, firstFloorHashB = 0;
            for (int f = 1; f <= 3; ++f) {
                Config a; a.seed = m2::deriveFloorSeed(rs, f);
                Config b; b.seed = m2::deriveFloorSeed(rs, f);
                Layout La = generate(a), Lb = generate(b);
                if (m2::spawnPlanHash(La, w200) != m2::spawnPlanHash(Lb, w200)) same = false;
                if (m2::enemyPlanHash(m2::buildEnemyPlan(La, w200, 2, La.startRoom)) !=
                    m2::enemyPlanHash(m2::buildEnemyPlan(Lb, w200, 2, Lb.startRoom))) same = false;
                if (f == 1) firstFloorHashA = m2::spawnPlanHash(La, w200);
            }
            if (same) ++identical;
            // 反例: 相邻 runSeed 的楼层1布局哈希应不同
            Config c2; c2.seed = m2::deriveFloorSeed(rs + 1, 1);
            firstFloorHashB = m2::spawnPlanHash(generate(c2), w200);
            if (firstFloorHashA != firstFloorHashB) ++differs;
        }
        std::cout << "same runSeed => identical floor sequence : " << identical << "/" << n << "\n";
        std::cout << "adjacent runSeed => different floor 1    : " << differs << "/" << n << "\n";
        return (identical == n && differs == n) ? 0 : 1;
    }

    if (mode == "determinism") {
        std::uint64_t seed = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 999;
        Config c; c.seed = seed;
        std::uint64_t h1 = m2::spawnPlanHash(generate(c), wc);
        std::uint64_t h2 = m2::spawnPlanHash(generate(c), wc);
        std::cout << "seed=" << seed << "\n";
        std::cout << std::hex << "plan hash run1 = 0x" << h1 << "\nplan hash run2 = 0x" << h2 << std::dec << "\n";
        std::cout << (h1 == h2 ? "identical" : "MISMATCH") << "\n";
        return (h1 == h2) ? 0 : 1;
    }

    // report
    std::uint64_t seed = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 7;
    Config c; c.seed = seed;
    Layout L = generate(c);
    ValidationResult vr = validate(L);
    m2::WorldReach wr = m2::worldReachability(L, wc);
    std::vector<m2::TilePlacement> plan = m2::buildSpawnPlan(L, wc);

    int floor = 0, wall = 0, corr = 0, door = 0;
    for (const m2::TilePlacement& p : plan) {
        switch (p.kind) {
            case Tile::Floor:    ++floor; break;
            case Tile::Wall:     ++wall;  break;
            case Tile::Corridor: ++corr;  break;
            case Tile::Door:     ++door;  break;
        }
    }
    long long sx = 0, sy = 0; m2::startWorldPos(L, wc, sx, sy);
    std::uint64_t h1 = m2::spawnPlanHash(L, wc);
    std::uint64_t h2 = m2::spawnPlanHash(generate(c), wc);

    std::cout << "seed=" << seed << "  tileSize=" << wc.tileSize
              << "  grid " << L.config.width << "x" << L.config.height << "\n";
    std::cout << "[fidelity ] abstract rooms = " << L.rooms.size()
              << "   connections = " << L.connections.size() << "\n";
    std::cout << "[fidelity ] spawn-plan tiles: floor=" << floor << " corridor=" << corr
              << " door=" << door << " wall=" << wall << "  total=" << plan.size() << "\n";
    std::cout << "[M1 grid ] passable=" << vr.passableCells << " reached=" << vr.reachedCells
              << "  rooms " << vr.reachableRooms << "/" << vr.roomCount
              << "  ok=" << (vr.ok ? "yes" : "no") << "\n";
    std::cout << "[M2 world] passable=" << wr.passable << " reached=" << wr.reached
              << "  rooms " << wr.roomsReached << "/" << wr.rooms
              << "  fully-connected=" << (wr.fullyConnected() ? "yes" : "no") << "\n";
    std::cout << "[adjacency] world check uses 4-connected neighbors (matches M1 corridors/flood-fill)\n";
    std::cout << "[start    ] world pos = (" << sx << ", " << sy << ")\n";
    std::cout << std::hex << "[determ.  ] plan hash = 0x" << h1 << "  (run2 0x" << h2 << ")" << std::dec
              << "  -> " << (h1 == h2 ? "identical" : "MISMATCH") << "\n";
    return 0;
}
