#pragma once
#include "tmap/integer_geometry.h"
namespace tmap
{
struct IntegerManeuverCursor
{
    IntegerLegConversionCursor conversion{};
    const IntegerRouteLeg* legs = nullptr;
    size_t count = 0, index = 0;
    uint64_t generation = 0, distance_cm = 0;
    GeoPoint start_a{}, start_b{}, finish_a{}, finish_b{}, previous_a{}, previous_b{}, arrival{};
    Maneuver value{};
    uint8_t stage = 0;
    Result terminal = Result::Invalid;
};
Result beginIntegerManeuvers(GraphReader& graph, const IntegerRouteLeg* legs, size_t count, IntegerManeuverCursor& cursor);
Result integerManeuverStep(GraphReader& graph, IntegerManeuverCursor& cursor, size_t budget, ManeuverVisitor visitor, void* context);
void cancel(IntegerManeuverCursor& cursor);
} // namespace tmap
