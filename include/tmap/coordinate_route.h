#pragma once
#include "tmap/navigation.h"
namespace tmap
{
// A clipped directed road. Full intermediate roads use last_segment=UINT32_MAX.
struct RouteLeg
{
    uint32_t edge = UINT32_MAX, first_segment = 0, last_segment = UINT32_MAX;
    float first_fraction = 0, last_fraction = 1;
};
struct CoordinateRouteCursor
{
    struct Boundary
    {
        Snap snap{};
        uint32_t node = 0, boundary_edge = UINT32_MAX;
        double cost = 0;
        bool partial = false;
    };
    SnapCursor snapping{};
    RouteCursor searching{};
    GraphEdge scratch{}, prepare_base{};
    std::array<Boundary, 2> starts{}, ends{};
    GeoPoint start{}, destination{};
    RouteOptions options{};
    RouteWorkspace workspace{};
    RouteLeg* legs = nullptr;
    size_t capacity = 0, count = 0, expansions = 0;
    double snap_distance_m = 50, cost = 0;
    double direct_cost = 0, direct_first_ascent = 0;
    uint64_t generation = 0;
    uint8_t stage = 0, preparing = 0, start_count = 0, end_count = 0, trial = 0;
    uint8_t direct_pair = 0, direct_stage = 0;
    uint8_t preparing_state = 0;
    bool trial_started = false, complete = true, found = false, coverage_limited = false;
    Result terminal = Result::Invalid;
};
Result beginCoordinateRoute(GraphReader& graph, GeoPoint start, GeoPoint destination, const RouteOptions& options,
                            double snap_distance_m, RouteWorkspace workspace, RouteLeg* legs, size_t capacity,
                            CoordinateRouteCursor& cursor);
Result coordinateRouteStep(GraphReader& graph, CoordinateRouteCursor& cursor, size_t budget);
struct LegGeometryCursor
{
    const RouteLeg* legs = nullptr;
    size_t count = 0, index = 0;
    uint32_t vertex = 0;
    uint8_t stage = 0;
    uint64_t generation = 0;
    GeoPoint previous{};
    GraphEdge scratch{};
    bool has_previous = false, complete = true, loaded = false;
    bool cancelled = false;
};
Result beginLegGeometry(GraphReader& graph, const RouteLeg* legs, size_t count, LegGeometryCursor& cursor);
Result legGeometryStep(GraphReader& graph, LegGeometryCursor& cursor, size_t budget, RoutePointVisitor visitor, void* context);
Result beginLegManeuvers(GraphReader& graph, const RouteLeg* legs, size_t count, ManeuverCursor& cursor);
static_assert(sizeof(RouteLeg) == 20, "Clipped route storage remains compact");
static_assert(sizeof(CoordinateRouteCursor) <= 2048, "Bounded coordinate planner scratch");
static_assert(sizeof(LegGeometryCursor) <= 192, "Bounded geometry continuation");
} // namespace tmap
