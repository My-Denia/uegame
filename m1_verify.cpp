// Independent external verification of M1's own correctness claims.
// Uses ONLY the dungeon:: public API (generate/validate/dump). Not the agent's harness.
#include "dungeon.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#include <climits>
using namespace dungeon;

int main(int argc, char** argv) {
    long N    = (argc > 1) ? std::strtol(argv[1], nullptr, 10) : 1000;
    long base = (argc > 2) ? std::strtol(argv[2], nullptr, 10) : 1;

    long okCount = 0, detCount = 0, firstFails = 0;
    int  minRooms = INT_MAX, maxRooms = 0;
    int  minPass  = INT_MAX, maxPass  = 0;

    for (long i = 0; i < N; ++i) {
        Config c; c.seed = static_cast<std::uint64_t>(base + i);
        Layout L = generate(c);
        ValidationResult vr = validate(L);
        if (vr.ok) ++okCount;
        else if (firstFails < 5) {
            std::cout << "  VALIDATE FAIL seed " << c.seed << ": " << vr.reason << "\n";
            ++firstFails;
        }
        // determinism: regenerate, compare the full structure byte-for-byte
        Layout L2 = generate(c);
        bool same = (L.grid == L2.grid) && (L.rooms.size() == L2.rooms.size())
                    && (L.connections.size() == L2.connections.size()) && (L.startRoom == L2.startRoom);
        if (same) {
            for (std::size_t k = 0; same && k < L.rooms.size(); ++k)
                same = (L.rooms[k].x == L2.rooms[k].x && L.rooms[k].y == L2.rooms[k].y
                        && L.rooms[k].w == L2.rooms[k].w && L.rooms[k].h == L2.rooms[k].h);
        }
        if (same) ++detCount;
        else if (firstFails < 5) { std::cout << "  NONDETERMINISTIC seed " << c.seed << "\n"; ++firstFails; }

        int rc = static_cast<int>(L.rooms.size());
        if (rc < minRooms) minRooms = rc;
        if (rc > maxRooms) maxRooms = rc;
        if (vr.passableCells < minPass) minPass = vr.passableCells;
        if (vr.passableCells > maxPass) maxPass = vr.passableCells;
    }

    std::cout << "seeds tested       : " << N << "  (base=" << base << ")\n";
    std::cout << "validate ok        : " << okCount  << "/" << N << "\n";
    std::cout << "determinism ok     : " << detCount << "/" << N << "\n";
    std::cout << "room count range   : [" << minRooms << "," << maxRooms << "]"
              << "   (min>0 => 0-room vacuity is unreachable here)\n";
    std::cout << "passable range     : [" << minPass << "," << maxPass << "]\n";
    return (okCount == N && detCount == N) ? 0 : 1;
}
