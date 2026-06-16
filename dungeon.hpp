// dungeon.hpp
// Engine-agnostic, deterministic procedural dungeon LAYOUT generator.
// Standard C++17 only. No engine headers or types (no FVector/TArray/UObject).
//
// Pipeline:
//   1. Rejection-sample non-overlapping rooms (1-tile padding -> guaranteed wall gaps).
//   2. Build a complete graph over room centers (squared-Euclidean weights).
//   3. Reduce to a Euclidean MST (Prim) -> guaranteed-connected backbone spanning ALL rooms.
//   4. Add a few shortest non-tree edges as loops (optional flavor).
//   5. Carve L-shaped corridors center-to-center; mark corridor<->room thresholds as doors.
//
// Determinism: every random draw comes from one explicitly-seeded std::mt19937_64.
// No time-based seeding, no global rand()/srand(), no static mutable state.
// Bounded draws use (rng() % span) on the standard-specified mt19937_64 engine, so the
// sequence is bit-for-bit reproducible for a given (config, seed) on any conforming compiler.

#pragma once

#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>
#include <string>
#include <set>
#include <tuple>
#include <climits>
#include <stdexcept>

namespace dungeon {

enum class Tile : std::uint8_t {
    Wall     = 0,  // impassable
    Floor    = 1,  // room interior
    Corridor = 2,  // carved passage between rooms
    Door     = 3   // threshold cell where a corridor meets a room
};

inline bool isPassable(Tile t) { return t != Tile::Wall; }

struct Room {
    int x = 0, y = 0, w = 0, h = 0;  // rect on the grid: top-left (x,y) + size (w,h)
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

struct Edge {     // a room-to-room connection
    int a = 0, b = 0;  // room indices
};

struct Config {
    int width        = 64;
    int height       = 40;
    int minRooms     = 8;
    int maxRooms     = 12;
    int minRoomSize  = 5;
    int maxRoomSize  = 10;
    std::uint64_t seed = 0;
    int extraLoopEdges = 2;   // loop edges added on top of the MST backbone
};

struct Layout {
    Config config;
    std::vector<Room> rooms;
    std::vector<Edge> connections;   // MST edges + loop edges (room index pairs)
    std::vector<Tile> grid;          // row-major, size = width * height
    int startRoom = 0;

    Tile at(int x, int y) const { return grid[std::size_t(y) * config.width + x]; }
};

namespace detail {

inline int index(const Config& c, int x, int y) { return y * c.width + x; }

// Axis-aligned rectangle overlap test (true footprints, no padding).
inline bool rectsOverlap(const Room& a, const Room& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

// Overlap test with `pad` cells of required gap (a inflated by pad intersects b).
inline bool tooClose(const Room& a, const Room& b, int pad) {
    Room ai{ a.x - pad, a.y - pad, a.w + 2 * pad, a.h + 2 * pad };
    return rectsOverlap(ai, b);
}

inline long long dist2(const Room& a, const Room& b) {
    long long dx = a.cx() - b.cx();
    long long dy = a.cy() - b.cy();
    return dx * dx + dy * dy;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------
inline Layout generate(const Config& config) {
    using namespace detail;

    Layout layout;
    layout.config = config;

    std::mt19937_64 rng(config.seed);
    // Inclusive bounded draw from the standard mt19937_64 engine -> fully deterministic.
    auto randint = [&rng](int lo, int hi) -> int {
        std::uint64_t span = std::uint64_t(hi - lo) + 1;
        return lo + int(rng() % span);
    };

    const int W = config.width;
    const int H = config.height;

    // --- 1. Place non-overlapping rooms (with 1-tile padding) ---------------
    int target = randint(config.minRooms, config.maxRooms);
    std::vector<Room>& rooms = layout.rooms;
    for (int placed = 0; placed < target; ++placed) {
        bool ok = false;
        for (int attempt = 0; attempt < 300 && !ok; ++attempt) {
            int w = randint(config.minRoomSize, config.maxRoomSize);
            int h = randint(config.minRoomSize, config.maxRoomSize);
            // Keep a 1-cell wall border around the whole grid.
            if (w > W - 2 || h > H - 2) continue;
            int x = randint(1, W - 1 - w);
            int y = randint(1, H - 1 - h);
            Room cand{ x, y, w, h };
            bool bad = false;
            for (const Room& r : rooms) {
                if (tooClose(cand, r, 1)) { bad = true; break; }
            }
            if (!bad) { rooms.push_back(cand); ok = true; }
        }
        if (!ok) break;  // ran out of space for this slot; harness validates the count
    }

    const int n = static_cast<int>(rooms.size());

    // --- 2 & 3. Complete graph -> Euclidean MST (Prim) ---------------------
    std::vector<Edge> mst;
    if (n >= 2) {
        std::vector<char> inTree(n, 0);
        std::vector<long long> best(n, LLONG_MAX);
        std::vector<int> parent(n, -1);
        best[0] = 0;
        for (int it = 0; it < n; ++it) {
            int u = -1;
            long long bu = LLONG_MAX;
            for (int v = 0; v < n; ++v) {
                if (!inTree[v] && best[v] < bu) { bu = best[v]; u = v; }
            }
            if (u == -1) break;          // disconnected guard (cannot happen on complete graph)
            inTree[u] = 1;
            if (parent[u] != -1) mst.push_back({ parent[u], u });
            for (int v = 0; v < n; ++v) {
                if (inTree[v]) continue;
                long long wgt = dist2(rooms[u], rooms[v]);
                if (wgt < best[v]) { best[v] = wgt; parent[v] = u; }
            }
        }
    }

    // --- 4. A few loop edges: shortest non-tree edges ----------------------
    std::vector<Edge>& connections = layout.connections;
    connections = mst;
    if (n >= 2 && config.extraLoopEdges > 0) {
        std::set<std::pair<int, int>> tree;
        for (const Edge& e : mst) {
            tree.insert({ std::min(e.a, e.b), std::max(e.a, e.b) });
        }
        std::vector<std::tuple<long long, int, int>> cand;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (tree.count({ i, j })) continue;
                cand.push_back({ dist2(rooms[i], rooms[j]), i, j });
            }
        }
        std::sort(cand.begin(), cand.end());
        for (int k = 0; k < config.extraLoopEdges && k < static_cast<int>(cand.size()); ++k) {
            connections.push_back({ std::get<1>(cand[k]), std::get<2>(cand[k]) });
        }
    }

    // --- 5. Carve grid -----------------------------------------------------
    std::vector<Tile>& grid = layout.grid;
    grid.assign(std::size_t(W) * H, Tile::Wall);

    // Room interiors.
    for (const Room& r : rooms) {
        for (int yy = r.y; yy < r.y + r.h; ++yy) {
            for (int xx = r.x; xx < r.x + r.w; ++xx) {
                grid[index(config, xx, yy)] = Tile::Floor;
            }
        }
    }

    auto carveH = [&](int xa, int xb, int y) {
        if (xa > xb) std::swap(xa, xb);
        for (int x = xa; x <= xb; ++x) {
            Tile& c = grid[index(config, x, y)];
            if (c == Tile::Wall) c = Tile::Corridor;  // never overwrite room floor
        }
    };
    auto carveV = [&](int ya, int yb, int x) {
        if (ya > yb) std::swap(ya, yb);
        for (int y = ya; y <= yb; ++y) {
            Tile& c = grid[index(config, x, y)];
            if (c == Tile::Wall) c = Tile::Corridor;
        }
    };

    // L-shaped corridor between the two room centers; bend direction from RNG.
    for (const Edge& e : connections) {
        int x1 = rooms[e.a].cx(), y1 = rooms[e.a].cy();
        int x2 = rooms[e.b].cx(), y2 = rooms[e.b].cy();
        if (rng() & 1ull) {
            carveH(x1, x2, y1);
            carveV(y1, y2, x2);
        } else {
            carveV(y1, y2, x1);
            carveH(x1, x2, y2);
        }
    }

    // Doors: corridor cells orthogonally adjacent to room floor (computed then applied,
    // so the scan order cannot influence the result).
    std::vector<int> doorCells;
    const int dx4[4] = { 1, -1, 0, 0 };
    const int dy4[4] = { 0, 0, 1, -1 };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (grid[index(config, x, y)] != Tile::Corridor) continue;
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx4[d], ny = y + dy4[d];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                if (grid[index(config, nx, ny)] == Tile::Floor) {
                    doorCells.push_back(index(config, x, y));
                    break;
                }
            }
        }
    }
    for (int i : doorCells) grid[i] = Tile::Door;

    layout.startRoom = 0;
    return layout;
}

// ---------------------------------------------------------------------------
// Validation (independent re-derivation of correctness from the grid + rooms)
// ---------------------------------------------------------------------------
struct ValidationResult {
    bool ok = true;
    std::string reason;
    int roomCount = 0;
    int reachableRooms = 0;
    int passableCells = 0;
    int reachedCells = 0;
};

inline ValidationResult validate(const Layout& L) {
    using namespace detail;
    const Config& c = L.config;
    ValidationResult r;
    const int W = c.width, H = c.height;
    r.roomCount = static_cast<int>(L.rooms.size());

    // (a) room count within configured bounds
    if (r.roomCount < c.minRooms || r.roomCount > c.maxRooms) {
        r.ok = false;
        r.reason = "room count " + std::to_string(r.roomCount) + " outside [" +
                   std::to_string(c.minRooms) + "," + std::to_string(c.maxRooms) + "]";
        return r;
    }

    // (b) room sizes within bounds and rooms inside the 1-cell border
    for (int i = 0; i < r.roomCount; ++i) {
        const Room& rm = L.rooms[i];
        if (rm.w < c.minRoomSize || rm.w > c.maxRoomSize ||
            rm.h < c.minRoomSize || rm.h > c.maxRoomSize) {
            r.ok = false;
            r.reason = "room " + std::to_string(i) + " size " + std::to_string(rm.w) +
                       "x" + std::to_string(rm.h) + " outside [" +
                       std::to_string(c.minRoomSize) + "," + std::to_string(c.maxRoomSize) + "]";
            return r;
        }
        if (rm.x < 1 || rm.y < 1 || rm.x + rm.w > W - 1 || rm.y + rm.h > H - 1) {
            r.ok = false;
            r.reason = "room " + std::to_string(i) + " out of grid bounds";
            return r;
        }
    }

    // (c) no overlapping rooms (true footprints)
    for (int i = 0; i < r.roomCount; ++i) {
        for (int j = i + 1; j < r.roomCount; ++j) {
            if (rectsOverlap(L.rooms[i], L.rooms[j])) {
                r.ok = false;
                r.reason = "rooms " + std::to_string(i) + " and " + std::to_string(j) + " overlap";
                return r;
            }
        }
    }

    if (r.roomCount == 0) { r.ok = false; r.reason = "no rooms"; return r; }

    // (d) flood-fill from the start room's center over passable tiles (4-connectivity)
    for (const Tile& t : L.grid) if (isPassable(t)) ++r.passableCells;

    std::vector<char> seen(std::size_t(W) * H, 0);
    int sx = L.rooms[L.startRoom].cx();
    int sy = L.rooms[L.startRoom].cy();
    std::vector<int> stack;
    int startIdx = index(c, sx, sy);
    if (!isPassable(L.grid[startIdx])) {
        r.ok = false; r.reason = "start cell not passable"; return r;
    }
    seen[startIdx] = 1;
    stack.push_back(startIdx);
    const int dx4[4] = { 1, -1, 0, 0 };
    const int dy4[4] = { 0, 0, 1, -1 };
    while (!stack.empty()) {
        int cur = stack.back(); stack.pop_back();
        ++r.reachedCells;
        int cxp = cur % W, cyp = cur / W;
        for (int d = 0; d < 4; ++d) {
            int nx = cxp + dx4[d], ny = cyp + dy4[d];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            int ni = index(c, nx, ny);
            if (seen[ni]) continue;
            if (!isPassable(L.grid[ni])) continue;
            seen[ni] = 1;
            stack.push_back(ni);
        }
    }

    // (e) no unreachable passable cells (no isolated floor/corridor/door)
    if (r.reachedCells != r.passableCells) {
        r.ok = false;
        r.reason = "unreachable passable cells: reached " + std::to_string(r.reachedCells) +
                   " of " + std::to_string(r.passableCells);
        return r;
    }

    // (f) every room reachable from the start room (no isolated rooms)
    for (int i = 0; i < r.roomCount; ++i) {
        int ci = index(c, L.rooms[i].cx(), L.rooms[i].cy());
        if (seen[ci]) ++r.reachableRooms;
    }
    if (r.reachableRooms != r.roomCount) {
        r.ok = false;
        r.reason = "isolated rooms: reached " + std::to_string(r.reachableRooms) +
                   " of " + std::to_string(r.roomCount);
        return r;
    }

    return r;
}

// ---------------------------------------------------------------------------
// Rendering / serialization
// ---------------------------------------------------------------------------
inline char tileChar(Tile t) {
    switch (t) {
        case Tile::Wall:     return '#';
        case Tile::Floor:    return '.';
        case Tile::Corridor: return '+';
        case Tile::Door:     return '/';
    }
    return '?';
}

// Human-readable ASCII map. 'S' overlays the start room center.
inline std::string asciiArt(const Layout& L) {
    const Config& c = L.config;
    std::string out;
    out.reserve(std::size_t(c.width + 1) * c.height);
    std::vector<char> buf(std::size_t(c.width) * c.height);
    for (std::size_t i = 0; i < L.grid.size(); ++i) buf[i] = tileChar(L.grid[i]);
    if (!L.rooms.empty()) {
        int s = detail::index(c, L.rooms[L.startRoom].cx(), L.rooms[L.startRoom].cy());
        buf[s] = 'S';
    }
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) out += buf[std::size_t(y) * c.width + x];
        out += '\n';
    }
    return out;
}

// Canonical, fully-deterministic textual dump (used for the determinism diff).
inline std::string dump(const Layout& L) {
    const Config& c = L.config;
    std::string o;
    o += "config " + std::to_string(c.width) + " " + std::to_string(c.height) + " " +
         std::to_string(c.minRooms) + " " + std::to_string(c.maxRooms) + " " +
         std::to_string(c.minRoomSize) + " " + std::to_string(c.maxRoomSize) + " " +
         std::to_string(c.seed) + " " + std::to_string(c.extraLoopEdges) + "\n";
    o += "start " + std::to_string(L.startRoom) + "\n";
    o += "rooms " + std::to_string(L.rooms.size()) + "\n";
    for (std::size_t i = 0; i < L.rooms.size(); ++i) {
        const Room& r = L.rooms[i];
        o += "  " + std::to_string(i) + " " + std::to_string(r.x) + " " + std::to_string(r.y) +
             " " + std::to_string(r.w) + " " + std::to_string(r.h) + "\n";
    }
    o += "connections " + std::to_string(L.connections.size()) + "\n";
    for (const Edge& e : L.connections) {
        o += "  " + std::to_string(e.a) + " " + std::to_string(e.b) + "\n";
    }
    o += "grid\n";
    o += asciiArt(L);
    return o;
}

} // namespace dungeon
