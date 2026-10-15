#include "tmap/coordinate_route.h"
#include "binary.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tmap
{
using namespace detail;
namespace
{
Result finish(CoordinateRouteCursor& c, Result r)
{
    c.complete = true;
    c.terminal = r;
    return r;
}
RouteLeg startLeg(const CoordinateRouteCursor::Boundary& b)
{
    return {b.snap.edge, b.snap.segment, UINT32_MAX, float(b.snap.segment_fraction), 1};
}
RouteLeg endLeg(const CoordinateRouteCursor::Boundary& b)
{
    return {b.snap.edge, 0, b.snap.segment, 0, float(b.snap.segment_fraction)};
}
Result prepare(GraphReader& graph, CoordinateRouteCursor& c, bool start, uint8_t direction)
{
    auto& dirs = start ? c.starts : c.ends;
    auto& count = start ? c.start_count : c.end_count;
    auto& b = dirs[direction];
    if (!c.preparing_state)
    {
        if (!direction)
        {
            b.snap = c.snapping.best;
            const auto r = graph.edge(b.snap.edge, c.prepare_base);
            if (r != Result::Ok) return r;
            c.scratch = c.prepare_base;
        }
        else
        {
            const auto& base = c.prepare_base;
            if (base.opposite == UINT32_MAX) return Result::Ok;
            b.snap = dirs[0].snap;
            b.snap.edge = base.opposite;
            b.snap.segment = base.points - 2 - b.snap.segment;
            b.snap.segment_fraction = 1 - b.snap.segment_fraction;
            b.snap.along_fraction = 1 - b.snap.along_fraction;
            const auto r = graph.edge(b.snap.edge, c.scratch);
            if (r != Result::Ok) return r;
            if (c.scratch.from != base.to || c.scratch.to != base.from || c.scratch.points != base.points ||
                c.scratch.shape_offset != base.shape_offset || c.scratch.reversed == base.reversed || c.scratch.opposite != dirs[0].snap.edge) return Result::Invalid;
        }
        if (!(c.scratch.access & static_cast<uint8_t>(c.options.mode))) return Result::Ok;
        const auto fraction = b.snap.along_fraction;
        if (fraction < 0 || fraction > 1) return Result::Invalid;
        b.partial = fraction > 0 && fraction < 1;
        b.node = start ? (fraction == 0 ? c.scratch.from : c.scratch.to) : (fraction == 1 ? c.scratch.to : c.scratch.from);
        b.boundary_edge = b.partial ? b.snap.edge : UINT32_MAX;
        b.cost = b.partial ? c.scratch.length_cm / 100.0 * (start ? 1 - fraction : fraction) : 0;
        if (!(b.partial && c.options.ascent_penalty > 0))
        {
            ++count;
            return Result::Ok;
        }
        c.preparing_state = 1;
        return Result::More;
    }
    {
        double prefix = 0;
        const auto r = graph.ascentPrefix(b.snap.edge, b.snap.along_fraction, prefix);
        if (r != Result::Ok) return r;
        b.cost += c.options.ascent_penalty * (start ? c.scratch.ascent_cm / 100.0 - prefix : prefix);
    }
    ++count;
    c.preparing_state = 0;
    return Result::Ok;
}
Result direct(GraphReader& graph, CoordinateRouteCursor& c)
{
    if (c.direct_pair == c.start_count * c.end_count) return Result::Ok;
    const auto& a = c.starts[c.direct_pair / c.end_count].snap;
    const auto& b = c.ends[c.direct_pair % c.end_count].snap;
    if (a.edge != b.edge || a.along_fraction > b.along_fraction)
    {
        ++c.direct_pair;
        return Result::More;
    }
    if (!c.direct_stage)
    {
        auto r = graph.edge(a.edge, c.scratch);
        if (r != Result::Ok) return r;
        c.direct_cost = c.scratch.length_cm / 100.0 * (b.along_fraction - a.along_fraction);
        c.direct_stage = c.options.ascent_penalty > 0 ? 1 : 3;
        return Result::More;
    }
    if (c.direct_stage == 1)
    {
        const auto r = graph.ascentPrefix(a.edge, a.along_fraction, c.direct_first_ascent);
        if (r != Result::Ok) return r;
        c.direct_stage = 2;
        return Result::More;
    }
    if (c.direct_stage == 2)
    {
        double last = 0;
        const auto r = graph.ascentPrefix(b.edge, b.along_fraction, last);
        if (r != Result::Ok) return r;
        c.direct_cost += c.options.ascent_penalty * (last - c.direct_first_ascent);
        c.direct_stage = 3;
        return Result::More;
    }
    if (!c.found || c.direct_cost < c.cost)
    {
        if (!c.capacity) return Result::BudgetExceeded;
        c.legs[0] = {a.edge, a.segment, b.segment, float(a.segment_fraction), float(b.segment_fraction)};
        c.count = 1;
        c.cost = c.direct_cost;
        c.found = true;
    }
    ++c.direct_pair;
    c.direct_stage = 0;
    return Result::More;
}
Result save(CoordinateRouteCursor& c)
{
    const auto& a = c.starts[c.trial / c.end_count];
    const auto& b = c.ends[c.trial % c.end_count];
    const double cost = c.searching.workspace.labels[c.searching.goal_label].cost + a.cost + b.cost;
    if (c.found && cost >= c.cost) return Result::Ok;
    // Finished search no longer needs its heap. Reuse it as edge output scratch.
    size_t count = 0;
    auto r = routeEdges(c.searching, c.workspace.heap, c.workspace.capacity, count);
    if (r != Result::Ok) return r;
    const size_t required = count + size_t(a.partial) + size_t(b.partial);
    if (required > c.capacity) return Result::BudgetExceeded;
    size_t at = 0;
    if (a.partial) c.legs[at++] = startLeg(a);
    for (size_t i = 0; i < count; ++i) c.legs[at++] = {c.workspace.heap[i], 0, UINT32_MAX, 0, 1};
    if (b.partial) c.legs[at++] = endLeg(b);
    c.count = at;
    c.cost = cost;
    c.found = true;
    return Result::Ok;
}
} // namespace
Result beginCoordinateRoute(GraphReader& graph, GeoPoint start, GeoPoint destination, const RouteOptions& options,
                            double snap_distance, RouteWorkspace w, RouteLeg* legs, size_t capacity, CoordinateRouteCursor& c)
{
    reset(c);
    if (!coordinate(start.latitude, start.longitude) || !coordinate(destination.latitude, destination.longitude) ||
        !w.labels || !w.heap || w.capacity < 2 || w.capacity > UINT32_MAX || (!legs && capacity) ||
        !std::isfinite(options.ascent_penalty) || options.ascent_penalty < 0 || !options.max_expansions) return Result::Invalid;
    auto r = beginSnap(graph, start, options.mode, snap_distance, c.snapping);
    if (r != Result::Ok) return r;
    c.start = start;
    c.destination = destination;
    c.options = options;
    c.workspace = w;
    c.legs = legs;
    c.capacity = capacity;
    c.snap_distance_m = snap_distance;
    c.generation = graph.package().generation();
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result coordinateRouteStep(GraphReader& graph, CoordinateRouteCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return finish(c, Result::Cancelled);
    if (c.complete) return c.terminal;
    const auto policy = graph.validateRouting(c.options.mode);
    if (policy == Result::Busy || policy == Result::More) return policy;
    if (policy != Result::Ok) return finish(c, policy);
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.stage == 0 || c.stage == 2)
        {
            const auto r = snapStep(graph, c.snapping, 1);
            if (r == Result::More) continue;
            if (r == Result::Busy) return r;
            if (r != Result::Ok) return finish(c, r);
            ++c.stage;
            c.preparing = 0;
        }
        else if (c.stage == 1 || c.stage == 3)
        {
            if (c.preparing < 2)
            {
                auto r = prepare(graph, c, c.stage == 1, c.preparing);
                if (r == Result::Busy || r == Result::More) return r;
                if (r != Result::Ok) return finish(c, r);
                ++c.preparing;
                continue;
            }
            if (c.stage == 1)
            {
                const auto r = beginSnap(graph, c.destination, c.options.mode, c.snap_distance_m, c.snapping);
                if (r == Result::Busy || r == Result::More) return r;
                if (r != Result::Ok) return finish(c, r);
                c.stage = 2;
            }
            else
            {
                auto r = direct(graph, c);
                if (r == Result::Busy || r == Result::More) return r;
                if (r != Result::Ok) return finish(c, r);
                c.stage = 4;
            }
        }
        else if (c.stage == 4)
        {
            if (c.trial == c.start_count * c.end_count) return finish(c, c.found ? Result::Ok : c.coverage_limited ? Result::OutsideCoverage
                                                                                                                   : Result::NoPath);
            const auto& a = c.starts[c.trial / c.end_count];
            const auto& b = c.ends[c.trial % c.end_count];
            if (!c.trial_started)
            {
                if (c.found && a.cost + b.cost >= c.cost)
                {
                    ++c.trial;
                    continue;
                }
                if (c.expansions >= c.options.max_expansions) return finish(c, Result::BudgetExceeded);
                auto options = c.options;
                options.max_expansions -= uint32_t(c.expansions);
                auto r = beginRouteWithBoundaries(graph, a.node, b.node, a.boundary_edge, b.boundary_edge, options, c.workspace, c.searching);
                if (r == Result::Busy || r == Result::More) return r;
                if (r != Result::Ok) return finish(c, r);
                c.trial_started = true;
            }
            else
            {
                auto r = routeStep(graph, c.searching, 1);
                if (r == Result::More) continue;
                if (r == Result::Busy) return r;
                if (r != Result::Ok && r != Result::NoPath && r != Result::OutsideCoverage) return finish(c, r);
                c.coverage_limited = c.coverage_limited || c.searching.boundary_seen;
                if (r == Result::Ok)
                {
                    r = save(c);
                    if (r != Result::Ok) return finish(c, r);
                }
                c.expansions += c.searching.expansions;
                ++c.trial;
                c.trial_started = false;
            }
        }
        else return finish(c, Result::Invalid);
    }
    return Result::More;
}
Result beginLegGeometry(GraphReader& graph, const RouteLeg* legs, size_t count, LegGeometryCursor& c)
{
    reset(c);
    if (!legs && count) return Result::Invalid;
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.legs = legs;
    c.count = count;
    c.generation = graph.package().generation();
    c.complete = count == 0;
    return Result::Ok;
}
Result beginLegManeuvers(GraphReader& graph, const RouteLeg* legs, size_t count, ManeuverCursor& c)
{
    reset(c);
    if (!legs && count) return Result::Invalid;
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.legs = legs;
    c.count = count;
    c.generation = graph.package().generation();
    c.complete = count == 0;
    c.terminal = c.complete ? Result::Ok : Result::More;
    return Result::Ok;
}
Result legGeometryStep(GraphReader& graph, LegGeometryCursor& c, size_t budget, RoutePointVisitor visitor, void* context)
{
    if (!budget || !visitor) return Result::Invalid;
    if (c.cancelled) return Result::Cancelled;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return Result::Cancelled;
    if (c.complete) return Result::Ok;
    for (size_t work = 0; work < budget; ++work)
    {
        const auto& leg = c.legs[c.index];
        Result r;
        if (!c.loaded)
        {
            r = graph.edge(leg.edge, c.scratch);
            if (r != Result::Ok) return r;
            c.loaded = true;
        }
        const auto last = leg.last_segment == UINT32_MAX ? c.scratch.points - 2 : leg.last_segment;
        if (leg.first_segment > last || last + 1 >= c.scratch.points || !std::isfinite(leg.first_fraction) ||
            !std::isfinite(leg.last_fraction) || leg.first_fraction < 0 || leg.first_fraction > 1 || leg.last_fraction < 0 ||
            leg.last_fraction > 1 || (leg.first_segment == last && leg.first_fraction > leg.last_fraction)) return Result::Invalid;
        GeoPoint p{};
        if (c.stage == 0 || c.stage == 2)
        {
            const auto segment = c.stage == 0 ? leg.first_segment : last;
            GeoPoint a{}, b{};
            r = graph.point(c.scratch, segment, a);
            if (r == Result::Ok) r = graph.point(c.scratch, segment + 1, b);
            if (r != Result::Ok) return r;
            const double t = c.stage == 0 ? leg.first_fraction : leg.last_fraction;
            p = {int32_t(std::llround(a.latitude + (double(b.latitude) - a.latitude) * t)),
                 int32_t(std::llround(a.longitude + (double(b.longitude) - a.longitude) * t))};
        }
        else
        {
            r = graph.point(c.scratch, c.vertex, p);
            if (r != Result::Ok) return r;
        }
        if (!c.has_previous || p.latitude != c.previous.latitude || p.longitude != c.previous.longitude)
        {
            if (!visitor(context, p))
            {
                c.complete = true;
                c.cancelled = true;
                return Result::Cancelled;
            }
            c.previous = p;
            c.has_previous = true;
        }
        if (c.stage == 0)
        {
            c.vertex = leg.first_segment + 1;
            c.stage = c.vertex <= last ? 1 : 2;
        }
        else if (c.stage == 1)
        {
            if (++c.vertex > last) c.stage = 2;
        }
        else
        {
            c.stage = 0;
            c.loaded = false;
            if (++c.index == c.count)
            {
                c.complete = true;
                return Result::Ok;
            }
        }
    }
    return Result::More;
}
} // namespace tmap
