#pragma once
#include "tmap/coordinate_route.h"

namespace tmap
{
struct RouteElevation
{
    double length_m = 0, ascent_m = 0, descent_m = 0;
};
struct RouteElevationCursor
{
    const RouteLeg* legs = nullptr;
    size_t count = 0, index = 0;
    uint64_t generation = 0;
    GraphEdge edge{};
    GeoPoint previous{};
    RouteElevation total{};
    double shape_length = 0, first_distance = 0, last_distance = 0;
    double first_fraction = 0, last_fraction = 0, first_ascent = 0, last_ascent = 0, first_descent = 0;
    uint32_t segment = 0, last_segment = 0;
    uint8_t stage = 0;
    bool complete = true;
    Result terminal = Result::Invalid;
};
Result beginRouteElevation(GraphReader& graph, const RouteLeg* legs, size_t count, RouteElevationCursor& cursor);
Result routeElevationStep(GraphReader& graph, RouteElevationCursor& cursor, size_t work_budget, RouteElevation& output);
void cancel(RouteElevationCursor& cursor);
struct IntegerRouteElevation
{
    uint64_t length_cm = 0, ascent_cm = 0, descent_cm = 0;
};
struct IntegerRouteElevationCursor
{
    const uint32_t* edges = nullptr;
    size_t count = 0, index = 0;
    uint64_t generation = 0;
    GraphEdge scratch{};
    IntegerRouteElevation total{};
    bool complete = true;
    Result terminal = Result::Invalid;
};
Result beginIntegerRouteElevation(GraphReader& graph, const uint32_t* edges, size_t count, IntegerRouteElevationCursor& cursor);
Result integerRouteElevationStep(GraphReader& graph, IntegerRouteElevationCursor& cursor, size_t work_budget, IntegerRouteElevation& output);
void cancel(IntegerRouteElevationCursor& cursor);
} // namespace tmap
