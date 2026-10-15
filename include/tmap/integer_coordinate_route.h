#pragma once
#include "tmap/integer_prepared_route.h"
#include <type_traits>
namespace tmap
{
static_assert(std::is_trivially_destructible<SnapCursor>::value &&
                  std::is_trivially_destructible<IntegerEndpointCursor>::value &&
                  std::is_trivially_destructible<IntegerPreparedRouteCursor>::value,
              "Phase scratch replacement requires trivial destruction");
struct IntegerCoordinateRouteCursor
{
    union Scratch
    {
        SnapCursor snapping;
        IntegerEndpointCursor endpoint;
        IntegerPreparedRouteCursor searching;
        Scratch() : snapping{} {}
    } scratch;
    IntegerEndpointCandidates starts{}, ends{};
    Snap snapped{};
    IntegerRouteOptions options{};
    IntegerRouteWorkspace workspace{};
    GeoPoint start{}, destination{};
    union
    {
        IntegerRouteLeg* legs = nullptr;
        IntegerRouteLegSink* sink;
    };
    size_t capacity = 0;
    uint64_t generation = 0;
    double snap_distance_m = 50;
    uint8_t stage = 0;
    bool departure = true;
    Result terminal = Result::Invalid;
    bool sink_output = false;
    IntegerCoordinateRouteCursor() = default;
    IntegerCoordinateRouteCursor(const IntegerCoordinateRouteCursor&) = delete;
    IntegerCoordinateRouteCursor& operator=(const IntegerCoordinateRouteCursor&) = delete;
};
Result beginIntegerCoordinateRoute(GraphReader& graph, GeoPoint start, GeoPoint destination, IntegerRouteOptions options,
                                   double snap_distance_m, IntegerRouteWorkspace workspace, IntegerRouteLeg* legs,
                                   size_t capacity, IntegerCoordinateRouteCursor& cursor);
Result integerCoordinateRouteStep(GraphReader& graph, IntegerCoordinateRouteCursor& cursor, size_t budget);
Result beginIntegerCoordinateRouteToSink(GraphReader& graph, GeoPoint start, GeoPoint destination,
                                         IntegerRouteOptions options, double snap_distance_m,
                                         IntegerRouteWorkspace workspace, IntegerRouteLegSink& sink, size_t capacity,
                                         IntegerCoordinateRouteCursor& cursor);
Result integerCoordinateRouteResult(const IntegerCoordinateRouteCursor& cursor, const IntegerRouteLeg*& legs,
                                    size_t& count, uint64_t& cost_units);
void cancel(IntegerCoordinateRouteCursor& cursor);
Result integerCoordinateRouteSummary(const IntegerCoordinateRouteCursor& cursor, size_t& count, uint64_t& cost_units);
} // namespace tmap
