#include "tmap/integer_coordinate_route.h"
#include <cmath>
#include <new>
namespace tmap
{
Result beginIntegerCoordinateRoute(GraphReader& graph, GeoPoint start, GeoPoint destination, IntegerRouteOptions options,
                                   double radius, IntegerRouteWorkspace workspace, IntegerRouteLeg* legs, size_t capacity,
                                   IntegerCoordinateRouteCursor& c)
{
    c.terminal = Result::Invalid;
    if (!workspace.labels || (!workspace.queue && options.algorithm != IntegerRouteAlgorithm::LabelScanDijkstra) || workspace.capacity < 2 || workspace.capacity > UINT32_MAX || !legs || !capacity ||
        !options.ascent.denominator || !options.max_expansions || (options.mode != TravelMode::Foot && options.mode != TravelMode::Bicycle) ||
        uint8_t(options.algorithm) > uint8_t(IntegerRouteAlgorithm::LabelScanDijkstra) ||
        (options.algorithm == IntegerRouteAlgorithm::Landmarks && (!workspace.lower_bounds || !workspace.landmarks)) ||
        !std::isfinite(radius) || radius <= 0 || radius > 10000 || start.latitude < -900000000 || start.latitude > 900000000 ||
        destination.latitude < -900000000 || destination.latitude > 900000000 || start.longitude < -1800000000 || start.longitude > 1800000000 ||
        destination.longitude < -1800000000 || destination.longitude > 1800000000) return Result::Invalid;
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.options = options;
    c.workspace = workspace;
    c.start = start;
    c.destination = destination;
    c.legs = legs;
    c.capacity = capacity;
    c.generation = graph.package().generation();
    c.snap_distance_m = radius;
    c.departure = true;
    c.stage = 0;
    ::new (static_cast<void*>(&c.scratch.snapping)) SnapCursor{};
    c.terminal = Result::More;
    return Result::Ok;
}
void cancel(IntegerCoordinateRouteCursor& c) { c.terminal = Result::Cancelled; }
Result integerCoordinateRouteResult(const IntegerCoordinateRouteCursor& c, const IntegerRouteLeg*& legs, size_t& count, uint64_t& cost)
{
    legs = nullptr;
    count = 0;
    cost = 0;
    if (c.terminal != Result::Ok) return c.terminal;
    return integerPreparedRouteResult(c.scratch.searching, legs, count, cost);
}
Result integerCoordinateRouteStep(GraphReader& graph, IntegerCoordinateRouteCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More) return c.terminal;
    const auto policy = graph.validateRouting(c.options.mode);
    if (policy == Result::Busy || policy == Result::More) return policy;
    if (policy != Result::Ok) return c.terminal = policy;
    for (size_t work = 0; work < budget; ++work)
    {
        Result r;
        if (c.stage == 0)
        {
            r = beginSnap(graph, c.departure ? c.start : c.destination, c.options.mode, c.snap_distance_m, c.scratch.snapping);
            if (r == Result::Ok) c.stage = 1;
        }
        else if (c.stage == 1)
        {
            r = snapStep(graph, c.scratch.snapping, 1);
            if (r == Result::Ok)
            {
                c.snapped = c.scratch.snapping.best;
                ::new (static_cast<void*>(&c.scratch.endpoint)) IntegerEndpointCursor{};
                c.stage = 2;
            }
        }
        else if (c.stage == 2)
        {
            r = beginIntegerEndpoint(graph, c.snapped, c.departure, c.options.mode, c.options.ascent, c.scratch.endpoint);
            if (r == Result::Ok) c.stage = 3;
        }
        else if (c.stage == 3)
        {
            r = integerEndpointStep(graph, c.scratch.endpoint, 1, c.departure ? c.starts : c.ends);
            if (r == Result::Ok)
            {
                if (c.departure)
                {
                    c.departure = false;
                    ::new (static_cast<void*>(&c.scratch.snapping)) SnapCursor{};
                    c.stage = 0;
                }
                else
                {
                    ::new (static_cast<void*>(&c.scratch.searching)) IntegerPreparedRouteCursor{};
                    c.stage = 4;
                }
            }
        }
        else if (c.stage == 4)
        {
            r = beginIntegerPreparedRoute(graph, c.starts, c.ends, c.options, c.workspace, c.legs, c.capacity, c.scratch.searching);
            if (r == Result::Ok) c.stage = 5;
        }
        else
        {
            r = integerPreparedRouteStep(graph, c.scratch.searching, 1);
            if (r == Result::Ok) return c.terminal = Result::Ok;
        }
        if (r == Result::Busy) return r;
        if (r != Result::Ok && r != Result::More) return c.terminal = r;
    }
    return Result::More;
}
} // namespace tmap
