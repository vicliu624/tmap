#pragma once
#include "tmap/integer_endpoint.h"
#include "tmap/route_elevation.h"
namespace tmap
{
struct IntegerLegElevationCursor
{
    const IntegerRouteLeg* legs = nullptr;
    size_t count = 0, index = 0;
    uint64_t generation = 0;
    IntegerEdgeIntervalCursor interval{};
    GraphEdge edge{};
    IntegerRouteElevation total{};
    IntegerEdgeInterval value{};
    uint8_t stage = 0;
    Result terminal = Result::Invalid;
};
Result beginIntegerLegElevation(GraphReader& graph, const IntegerRouteLeg* legs, size_t count, IntegerLegElevationCursor& cursor);
Result integerLegElevationStep(GraphReader& graph, IntegerLegElevationCursor& cursor, size_t budget, IntegerRouteElevation& output);
void cancel(IntegerLegElevationCursor& cursor);
} // namespace tmap
