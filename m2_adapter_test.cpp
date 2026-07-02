// m2_adapter_test.cpp
// 引擎无关的 M2 适配层测试 / 验收工具 (可用 g++ 编译运行，不需要 UE)。
//
// 模式：
//   m2test report [seed]     单种子报告：保真度 + M1 网格校验 vs M2 世界校验 + 确定性哈希
//   m2test validate [N]      N 个种子：世界空间全连通 + 网格/世界一致性，打印 P/N
//   m2test determinism [seed] 同种子两次 spawn-plan 哈希对比
//   m2test enemies [seed] [perRoom]   M3: 打印敌人布点 + tile 无关哈希 (排除起始房)
//   m2test enemyDeterminism [N]       M3: N 个种子 x2 生成，哈希/布点全等 + 房内校验

#include "m2_adapter.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
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
        std::cout << "seed=" << seed << " perRoom=" << perRoom
                  << " rooms=" << L.rooms.size() << " startRoom=" << L.startRoom
                  << " enemies=" << plan.size() << "\n";
        for (const m2::EnemyPlacement& p : plan) {
            std::cout << "  room=" << p.roomIndex << " cell=(" << p.gx << "," << p.gy
                      << ") world=(" << p.wx << "," << p.wy << ")\n";
        }
        std::cout << std::hex << "enemyPlanHash=0x" << m2::enemyPlanHash(plan) << std::dec << "\n";
        return 0;
    }

    if (mode == "enemyDeterminism") {
        long n = (argc > 2) ? std::strtol(argv[2], nullptr, 10) : 500;
        long identical = 0, inRoom = 0, excluded = 0;
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
            bool okRoom = true, okExcl = true;
            for (const m2::EnemyPlacement& p : p1) {
                const Room& rm = L.rooms[static_cast<std::size_t>(p.roomIndex)];
                if (p.gx < rm.x || p.gx >= rm.x + rm.w ||
                    p.gy < rm.y || p.gy >= rm.y + rm.h) okRoom = false;
                if (p.roomIndex == L.startRoom) okExcl = false;
            }
            if (okRoom) ++inRoom;
            if (okExcl) ++excluded;
        }
        std::cout << "identical plans (2x gen) : " << identical << "/" << n << "\n";
        std::cout << "all placements in-room   : " << inRoom << "/" << n << "\n";
        std::cout << "start room excluded      : " << excluded << "/" << n << "\n";
        return (identical == n && inRoom == n && excluded == n) ? 0 : 1;
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
