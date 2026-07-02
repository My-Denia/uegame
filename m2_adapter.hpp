// m2_adapter.hpp
// 引擎无关的 M1 Layout -> 世界坐标适配层 (M2)。
// 只依赖标准库 + dungeon.hpp。绝不包含任何 UE 类型 (FVector/TArray/UObject)。
// UE 侧的 spawner 会 #include 本文件；M1 引擎无关核心 (dungeon.hpp) 保持不变。
//
// 职责：
//   1. 把 Layout 的每个格子映射成一个世界放置点 (tileSize = 网格步长)。
//   2. 提供"世界空间"的 4-邻接可达性校验 —— 独立于 M1 的网格 flood-fill，
//      用来证明坐标映射忠实保留了 M1 的 4-连通规则 (carry-forward: 邻接一致性)。
//   3. 提供 spawn-plan 的确定性哈希，用于 M2 验收 #4 (同种子两次 -> 同布局)。

#pragma once

#include "dungeon.hpp"

#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <utility>

namespace m2 {

struct WorldConfig {
    long long tileSize   = 100;   // 每个网格格子的世界尺寸 (UE 单位 cm)
    long long wallHeight = 200;   // 墙体高度
    long long originX    = 0;
    long long originY    = 0;
};

struct TilePlacement {
    dungeon::Tile kind;
    int gx, gy;                   // 网格坐标
    long long wx, wy, wz;         // 世界坐标 (格子中心)
};

// 把每个格子映射成世界放置点。floor/corridor/door 落在 z=0 (可走)，
// wall 抬到 z = wallHeight/2 (立方体坐在地面上)。
inline std::vector<TilePlacement> buildSpawnPlan(const dungeon::Layout& L, const WorldConfig& wc) {
    std::vector<TilePlacement> plan;
    const dungeon::Config& c = L.config;
    plan.reserve(static_cast<std::size_t>(c.width) * c.height);
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            dungeon::Tile t = L.at(x, y);
            long long wx = wc.originX + static_cast<long long>(x) * wc.tileSize + wc.tileSize / 2;
            long long wy = wc.originY + static_cast<long long>(y) * wc.tileSize + wc.tileSize / 2;
            long long wz = (t == dungeon::Tile::Wall) ? wc.wallHeight / 2 : 0;
            plan.push_back({ t, x, y, wx, wy, wz });
        }
    }
    return plan;
}

// 起点房间中心的世界坐标。
inline void startWorldPos(const dungeon::Layout& L, const WorldConfig& wc,
                          long long& wx, long long& wy) {
    const dungeon::Room& r = L.rooms[L.startRoom];
    wx = wc.originX + static_cast<long long>(r.cx()) * wc.tileSize + wc.tileSize / 2;
    wy = wc.originY + static_cast<long long>(r.cy()) * wc.tileSize + wc.tileSize / 2;
}

struct WorldReach {
    int passable     = 0;  // 可走放置点总数
    int reached      = 0;  // 从起点在世界坐标上洪水填充到达的可走点数
    int rooms        = 0;  // 房间数
    int roomsReached = 0;  // 房间中心被到达数
    bool fullyConnected() const { return passable == reached && rooms == roomsReached; }
};

// 在【世界坐标】上做 4-邻接洪水填充：邻居 = 世界距离恰为 tileSize 的正交点。
// 这刻意不读网格索引，而是用映射后的世界坐标重建连通性 —— 若映射破坏了 4-连通
// (例如把相邻格挪开)，本校验就会发现 reached < passable 或房间未到达。
inline WorldReach worldReachability(const dungeon::Layout& L, const WorldConfig& wc) {
    WorldReach wr;
    std::vector<TilePlacement> plan = buildSpawnPlan(L, wc);

    using Key = std::pair<long long, long long>;
    std::map<Key, bool> passable;   // 世界坐标 -> 是否可走 (std::map 避免哈希碰撞误判)
    for (const TilePlacement& p : plan) {
        bool walk = dungeon::isPassable(p.kind);
        passable[{ p.wx, p.wy }] = walk;
        if (walk) ++wr.passable;
    }

    wr.rooms = static_cast<int>(L.rooms.size());
    if (wr.rooms == 0) return wr;

    long long sx = 0, sy = 0;
    startWorldPos(L, wc, sx, sy);

    std::set<Key> seen;
    std::vector<Key> stack;
    Key start{ sx, sy };
    auto sit = passable.find(start);
    if (sit != passable.end() && sit->second) {
        seen.insert(start);
        stack.push_back(start);
    }
    const long long ts = wc.tileSize;
    while (!stack.empty()) {
        Key cur = stack.back();
        stack.pop_back();
        ++wr.reached;
        Key nb[4] = {
            { cur.first + ts, cur.second }, { cur.first - ts, cur.second },
            { cur.first, cur.second + ts }, { cur.first, cur.second - ts }
        };
        for (const Key& n : nb) {
            if (seen.count(n)) continue;
            auto it = passable.find(n);
            if (it == passable.end() || !it->second) continue;
            seen.insert(n);
            stack.push_back(n);
        }
    }

    for (int i = 0; i < wr.rooms; ++i) {
        long long rx = wc.originX + static_cast<long long>(L.rooms[i].cx()) * ts + ts / 2;
        long long ry = wc.originY + static_cast<long long>(L.rooms[i].cy()) * ts + ts / 2;
        if (seen.count({ rx, ry })) ++wr.roomsReached;
    }
    return wr;
}

// spawn-plan 的 FNV-1a 64 位哈希。同 (Layout, WorldConfig) -> 同哈希。
// 用于 M2 验收 #4：同种子两次生成 -> 哈希相同 (确定性)。
inline std::uint64_t spawnPlanHash(const dungeon::Layout& L, const WorldConfig& wc) {
    std::vector<TilePlacement> plan = buildSpawnPlan(L, wc);
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](std::uint64_t v) { h ^= v; h *= 1099511628211ULL; };
    for (const TilePlacement& p : plan) {
        mix(static_cast<std::uint64_t>(p.kind));
        mix(static_cast<std::uint32_t>(p.gx));
        mix(static_cast<std::uint32_t>(p.gy));
        mix(static_cast<std::uint64_t>(p.wx));
        mix(static_cast<std::uint64_t>(p.wy));
        mix(static_cast<std::uint64_t>(p.wz));
    }
    return h;
}

// ---------------------------------------------------------------------------
// M3: 确定性敌人布点 (引擎无关)
// ---------------------------------------------------------------------------

struct EnemyPlacement {
    int roomIndex = 0;
    int gx = 0, gy = 0;        // 房间矩形内部的格子
    long long wx = 0, wy = 0;  // 该格子的世界中心 (由 gx/gy 派生, 不参与哈希)
};

// 敌人布点：与地牢同源的种子子流 (config.seed ^ 黄金比例常数)，
// 与 generate() 内部的抽取序列解耦。同 (layout, enemiesPerRoom, excludeRoom)
// -> 布点逐字节相同。无时间/全局/未播种 RNG。
inline std::vector<EnemyPlacement> buildEnemyPlan(const dungeon::Layout& L,
                                                  const WorldConfig& wc,
                                                  int enemiesPerRoom,
                                                  int excludeRoomIndex) {
    std::vector<EnemyPlacement> plan;
    if (enemiesPerRoom <= 0) return plan;
    std::mt19937_64 rng(L.config.seed ^ 0x9E3779B97F4A7C15ULL);
    const long long ts = wc.tileSize;
    for (int r = 0; r < static_cast<int>(L.rooms.size()); ++r) {
        if (r == excludeRoomIndex) continue;
        const dungeon::Room& rm = L.rooms[r];
        std::set<std::pair<int, int>> usedCells;   // 每房无放回抽样 (同格双敌会被引擎
                                                   // 碰撞调整挪走, 破坏确定性布点契约)
        // 布点数以房间唯一格数封顶: 配置超出时截断而不是退化为重复格。
        const int maxUnique = rm.w * rm.h;
        const int countForRoom = std::min(enemiesPerRoom, maxUnique);
        for (int k = 0; k < countForRoom; ++k) {
            int gx = 0, gy = 0;
            bool unique = false;
            // 命中已占格则重抽 (重抽同样消耗 rng, 保持确定性)。重抽预算耗尽仍未
            // 找到空格时跳过该布点 (宁缺毋重复), 保证计划内永无重复格。
            for (int attempt = 0; attempt < 64 && !unique; ++attempt) {
                gx = rm.x + static_cast<int>(rng() % static_cast<std::uint64_t>(rm.w));
                gy = rm.y + static_cast<int>(rng() % static_cast<std::uint64_t>(rm.h));
                unique = usedCells.insert({ gx, gy }).second;
            }
            if (!unique) continue;
            EnemyPlacement p;
            p.roomIndex = r;
            p.gx = gx;
            p.gy = gy;
            p.wx = wc.originX + static_cast<long long>(gx) * ts + ts / 2;
            p.wy = wc.originY + static_cast<long long>(gy) * ts + ts / 2;
            plan.push_back(p);
        }
    }
    return plan;
}

// 敌人布点哈希：只混入 tile 无关字段 (roomIndex, gx, gy)，因此 g++ (任意 tileSize)
// 与 UE (tileSize=200) 对同一种子打出同一个值；世界坐标由这些字段确定性派生。
inline std::uint64_t enemyPlanHash(const std::vector<EnemyPlacement>& plan) {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](std::uint64_t v) { h ^= v; h *= 1099511628211ULL; };
    for (const EnemyPlacement& p : plan) {
        mix(static_cast<std::uint32_t>(p.roomIndex));
        mix(static_cast<std::uint32_t>(p.gx));
        mix(static_cast<std::uint32_t>(p.gy));
    }
    return h;
}

} // namespace m2
