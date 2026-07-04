// M5LoadoutCore.cpp - the SOLE translation unit that compiles the engine-agnostic M5 loadout core
// (PR #12A, repo root) into the uegame runtime module.
//
// Why a wrapper: m5_loadout.cpp lives at the repo ROOT (one level above the .uproject), shared
// byte-for-byte with the standalone g++ build and its CTest gates. It sits OUTSIDE uegame/Source/,
// so UBT's module glob never compiles it directly. #include'ing it here pulls it into the module and
// defines every m5:: symbol exactly ONCE (ODR): do NOT #include "m5_loadout.cpp" from any other TU and
// do NOT add it to the module source list. The core itself is UNCHANGED by #12B - this file only builds it.
//
// The core is standard C++17 (only <cstdint>/<string>/<vector>/<algorithm>/<map>/<random>/<set>), fully
// inside namespace m5, with no engine type - nothing leaks into UHT reflection or the unity build. The one
// UE-specific hazard is the Windows <windows.h> min/max FUNCTION macros (pulled in via the shared PCH),
// which would break `std::min(...)` inside generate_offer(). Neutralise them for this TU before including
// the core (a no-op if NOMINMAX is already in effect; #undef of an undefined macro is legal and silent).
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "m5_loadout.cpp"
