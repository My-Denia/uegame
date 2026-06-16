// main.cpp
// Standalone CLI / test harness for the engine-agnostic dungeon layout generator.
//
// Modes:
//   dungeon ascii [seed]              print one layout as ASCII (default seed 1)
//   dungeon dump  [seed]              print the canonical layout dump (for external diffing)
//   dungeon determinism [seed]       generate twice, compare dumps, print identical / first diff
//   dungeon validate <count> [base]  validate `count` distinct seeds (base .. base+count-1),
//                                    print "<passed>/<count> passed" + aggregate stats
//
// Exit code is 0 on success, non-zero on any failure (so it can gate CI / scripts).

#include "dungeon.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace dungeon;

static Config baseConfig() {
    return Config{};  // documented defaults: 64x40, rooms[8..12], size[5..10], seed 0, loops 2
}

static std::uint64_t parseU64(const char* s, std::uint64_t fallback) {
    if (!s) return fallback;
    return std::strtoull(s, nullptr, 10);
}

static int usage() {
    std::cout <<
        "usage:\n"
        "  dungeon ascii [seed]\n"
        "  dungeon dump [seed]\n"
        "  dungeon determinism [seed]\n"
        "  dungeon validate <count> [base]\n";
    return 2;
}

static void printConfig(const Config& c) {
    std::cout << "config: grid " << c.width << "x" << c.height
              << "  rooms[" << c.minRooms << ".." << c.maxRooms << "]"
              << "  size[" << c.minRoomSize << ".." << c.maxRoomSize << "]"
              << "  loopEdges " << c.extraLoopEdges << "\n";
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string mode = argv[1];

    if (mode == "ascii") {
        Config c = baseConfig();
        c.seed = parseU64(argc > 2 ? argv[2] : nullptr, 1);
        Layout L = generate(c);
        printConfig(c);
        std::cout << "seed: " << c.seed
                  << "   rooms: " << L.rooms.size()
                  << "   connections: " << L.connections.size() << "\n";
        std::cout << "legend: # wall   . floor   + corridor   / door   S start\n";
        std::cout << asciiArt(L);
        return 0;
    }

    if (mode == "dump") {
        Config c = baseConfig();
        c.seed = parseU64(argc > 2 ? argv[2] : nullptr, 1);
        std::cout << dump(generate(c));
        return 0;
    }

    if (mode == "determinism") {
        Config c = baseConfig();
        c.seed = parseU64(argc > 2 ? argv[2] : nullptr, 1);
        std::string d1 = dump(generate(c));
        std::string d2 = dump(generate(c));
        printConfig(c);
        if (d1 == d2) {
            std::cout << "DETERMINISM seed=" << c.seed
                      << ": identical (" << d1.size() << " bytes, two independent generations)\n";
            return 0;
        }
        std::cout << "DETERMINISM seed=" << c.seed << ": MISMATCH\n";
        std::size_t i = 0;
        while (i < d1.size() && i < d2.size() && d1[i] == d2[i]) ++i;
        std::cout << "first difference at byte " << i << "\n";
        return 1;
    }

    if (mode == "validate") {
        if (argc < 3) return usage();
        long count = std::strtol(argv[2], nullptr, 10);
        std::uint64_t base = parseU64(argc > 3 ? argv[3] : nullptr, 1);
        if (count <= 0) { std::cout << "count must be positive\n"; return 2; }

        Config c = baseConfig();
        printConfig(c);
        std::cout << "validating " << count << " distinct seeds: "
                  << base << " .. " << (base + std::uint64_t(count) - 1) << "\n";

        long passed = 0;
        int failuresShown = 0;
        int minRooms = INT_MAX, maxRooms = 0;
        long totalRooms = 0;

        for (long i = 0; i < count; ++i) {
            std::uint64_t seed = base + std::uint64_t(i);
            Config cs = c;
            cs.seed = seed;
            try {
                Layout L = generate(cs);
                ValidationResult vr = validate(L);
                int rc = static_cast<int>(L.rooms.size());
                minRooms = std::min(minRooms, rc);
                maxRooms = std::max(maxRooms, rc);
                totalRooms += rc;
                if (vr.ok) {
                    ++passed;
                } else if (failuresShown < 20) {
                    std::cout << "  FAIL seed=" << seed << ": " << vr.reason << "\n";
                    ++failuresShown;
                }
            } catch (const std::exception& e) {
                if (failuresShown < 20) {
                    std::cout << "  CRASH seed=" << seed << ": " << e.what() << "\n";
                    ++failuresShown;
                }
            } catch (...) {
                if (failuresShown < 20) {
                    std::cout << "  CRASH seed=" << seed << ": unknown exception\n";
                    ++failuresShown;
                }
            }
        }

        if (passed == count && count > 0) {
            std::cout << "room-count range across seeds: " << minRooms << ".." << maxRooms
                      << "  (avg " << (double(totalRooms) / double(count)) << ")\n";
        }
        std::cout << passed << "/" << count << " passed\n";
        return passed == count ? 0 : 1;
    }

    return usage();
}
