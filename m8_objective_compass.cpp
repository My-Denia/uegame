// Header-only so both UBT's HUD translation unit and standalone CTest use identical rules.
#include "m8_objective_compass.hpp"

static_assert(static_cast<int>(m8objective::Direction8::AheadLeft) == 7,
	"Objective direction enum must remain an eight-sector contiguous domain.");
static_assert(m8objective::kRouteQueryMaximumPerSecond == 5,
	"Objective route-query rolling window must remain capped at five.");
