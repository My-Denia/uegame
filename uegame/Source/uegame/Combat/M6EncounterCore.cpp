// M6EncounterCore.cpp - the SOLE translation unit that compiles the engine-agnostic M6 encounter
// core (M6A, repo root) into the uegame runtime module.
//
// Why a wrapper: m6_encounter.cpp lives at the repo ROOT (one level above the .uproject), shared
// byte-for-byte with the standalone g++ build and its CTest gates (m6_encounter_determinism,
// m6_encounter_goldenvec). It sits OUTSIDE uegame/Source/, so UBT's module glob never compiles it
// directly. #include'ing it here pulls it into the module and defines every m6:: symbol exactly
// ONCE (ODR): do NOT #include "m6_encounter.cpp" from any other TU and do NOT add it to the module
// source list. The core itself is UNCHANGED by M6B - this file only builds it.
//
// Same Windows min/max FUNCTION-macro hazard as M5LoadoutCore.cpp: neutralise for this TU before
// including the core (a no-op when NOMINMAX is already in effect).
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "m6_encounter.cpp"
