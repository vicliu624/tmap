#include "tmap/integer_endpoint.h"
#include <cmath>
namespace tmap
{
Result beginIntegerEndpoint(GraphReader& graph, const Snap& snap, bool departure, TravelMode mode, AscentRatio ratio, IntegerEndpointCursor& c)
{
    c.terminal = Result::Invalid;
    if (!ratio.denominator || (mode != TravelMode::Foot && mode != TravelMode::Bicycle) || !std::isfinite(snap.along_fraction) ||
        snap.along_fraction < 0 || snap.along_fraction > 1) return Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    if (snap.edge >= graph.info().edges) return Result::Invalid;
    c.snap = snap;
    c.departure = departure;
    c.mode = mode;
    c.ratio = ratio;
    c.generation = graph.package().generation();
    c.value.count = 0;
    c.value.generation = c.generation;
    c.value.ratio = ratio;
    c.value.mode = mode;
    c.value.departure = departure;
    c.direction = c.stage = 0;
    c.terminal = Result::More;
    return Result::Ok;
}
Result integerEndpointStep(GraphReader& graph, IntegerEndpointCursor& c, size_t budget, IntegerEndpointCandidates& output)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.value;
        return c.terminal;
    }
    const auto policy = graph.validateRouting(c.mode);
    if (policy == Result::Busy || policy == Result::More) return policy;
    if (policy != Result::Ok) return c.terminal = policy;
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.direction == 2)
        {
            if (!c.value.count) return c.terminal = Result::NoPath;
            output = c.value;
            return c.terminal = Result::Ok;
        }
        Result result;
        auto& candidate = c.value.choices[c.value.count];
        if (c.stage == 0)
        {
            const auto row = c.direction ? c.base.opposite : c.snap.edge;
            if (row == UINT32_MAX)
            {
                ++c.direction;
                continue;
            }
            result = graph.edge(row, c.edge);
            if (result == Result::Ok)
            {
                if (!c.direction)
                {
                    c.base = c.edge;
                    c.position_cm = static_cast<uint32_t>(std::llround(c.snap.along_fraction * c.edge.length_cm));
                }
                else if (c.edge.from != c.base.to || c.edge.to != c.base.from || c.edge.points != c.base.points ||
                         c.edge.shape_offset != c.base.shape_offset || c.edge.reversed == c.base.reversed || c.edge.opposite != c.snap.edge ||
                         c.edge.length_cm != c.base.length_cm)
                    return c.terminal = Result::Invalid;
                if (!(c.edge.access & uint8_t(c.mode)))
                {
                    ++c.direction;
                    continue;
                }
                const auto position = c.direction ? c.edge.length_cm - c.position_cm : c.position_cm;
                candidate.leg.edge = row;
                candidate.position_cm = position;
                candidate.partial = position > 0 && position < c.edge.length_cm;
                candidate.node = c.departure ? (position == 0 ? c.edge.from : c.edge.to) : (position == c.edge.length_cm ? c.edge.to : c.edge.from);
                candidate.boundary_edge = candidate.partial ? row : UINT32_MAX;
                candidate.leg.first_cm = candidate.partial && c.departure ? position : 0;
                candidate.leg.last_cm = candidate.partial ? (c.departure ? c.edge.length_cm : position) : 0;
                candidate.cost_units = 0;
                if (!candidate.partial)
                {
                    if (!c.value.count || c.value.choices[0].partial || c.value.choices[0].node != candidate.node) ++c.value.count;
                    ++c.direction;
                    continue;
                }
                if (!c.ratio.numerator)
                {
                    result = integerEdgeCost(candidate.leg.last_cm - candidate.leg.first_cm, 0, false, c.ratio, candidate.cost_units);
                    if (result == Result::Ok)
                    {
                        ++c.value.count;
                        ++c.direction;
                        continue;
                    }
                }
                else c.stage = 1;
            }
        }
        else if (c.stage == 1)
        {
            result = beginIntegerEdgeInterval(graph, candidate.leg.edge, candidate.leg.first_cm, candidate.leg.last_cm, c.ratio, c.interval);
            if (result == Result::Ok) c.stage = 2;
        }
        else
        {
            IntegerEdgeInterval value{};
            result = integerEdgeIntervalStep(graph, c.interval, 1, value);
            if (result == Result::Ok)
            {
                candidate.cost_units = value.cost_units;
                ++c.value.count;
                ++c.direction;
                c.stage = 0;
            }
        }
        if (result == Result::Busy) return result;
        if (result != Result::Ok && result != Result::More) return c.terminal = result;
    }
    return Result::More;
}
void cancel(IntegerEndpointCursor& c)
{
    cancel(c.interval);
    c.terminal = Result::Cancelled;
}
Result beginIntegerDirectRoute(GraphReader& graph, const IntegerEndpointCandidates& starts, const IntegerEndpointCandidates& ends,
                               TravelMode mode, AscentRatio ratio, IntegerDirectCursor& c)
{
    c.terminal = Result::Invalid;
    if (!starts.count || starts.count > 2 || !ends.count || ends.count > 2 || !ratio.denominator ||
        (mode != TravelMode::Foot && mode != TravelMode::Bicycle)) return Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    if (starts.generation != graph.package().generation() || ends.generation != graph.package().generation() ||
        starts.mode != mode || ends.mode != mode) return Result::Incompatible;
    c.starts = &starts;
    c.ends = &ends;
    c.mode = mode;
    c.ratio = ratio;
    c.generation = graph.package().generation();
    c.pair = c.stage = 0;
    c.found = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result integerDirectRouteStep(GraphReader& graph, IntegerDirectCursor& c, size_t budget, IntegerDirectRoute& output)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.best;
        return c.terminal;
    }
    const auto policy = graph.validateRouting(c.mode);
    if (policy == Result::Busy || policy == Result::More) return policy;
    if (policy != Result::Ok) return c.terminal = policy;
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.pair == c.starts->count * c.ends->count)
        {
            if (!c.found) return c.terminal = Result::NoPath;
            output = c.best;
            return c.terminal = Result::Ok;
        }
        const auto& a = c.starts->choices[c.pair / c.ends->count];
        const auto& b = c.ends->choices[c.pair % c.ends->count];
        if (a.leg.edge != b.leg.edge || a.position_cm > b.position_cm)
        {
            ++c.pair;
            continue;
        }
        Result result;
        uint64_t cost = 0;
        bool complete = false;
        if (!c.stage)
        {
            result = graph.edge(a.leg.edge, c.edge);
            if (result == Result::Ok)
            {
                if (b.position_cm > c.edge.length_cm) return c.terminal = Result::Invalid;
                if (!(c.edge.access & uint8_t(c.mode)))
                {
                    ++c.pair;
                    continue;
                }
                if (!c.ratio.numerator)
                {
                    result = integerEdgeCost(b.position_cm - a.position_cm, 0, false, c.ratio, cost);
                    complete = result == Result::Ok;
                }
                else c.stage = 1;
            }
        }
        else if (c.stage == 1)
        {
            result = beginIntegerEdgeInterval(graph, a.leg.edge, a.position_cm, b.position_cm, c.ratio, c.interval);
            if (result == Result::Ok) c.stage = 2;
        }
        else
        {
            IntegerEdgeInterval value{};
            result = integerEdgeIntervalStep(graph, c.interval, 1, value);
            if (result == Result::Ok)
            {
                cost = value.cost_units;
                complete = true;
            }
        }
        if (result == Result::Busy) return result;
        if (result != Result::Ok && result != Result::More) return c.terminal = result;
        if (complete)
        {
            if (!c.found || cost < c.best.cost_units)
            {
                c.best.leg.edge = a.leg.edge;
                c.best.leg.first_cm = a.position_cm;
                c.best.leg.last_cm = b.position_cm;
                c.best.cost_units = cost;
                c.found = true;
            }
            ++c.pair;
            c.stage = 0;
        }
    }
    return Result::More;
}
void cancel(IntegerDirectCursor& c)
{
    cancel(c.interval);
    c.terminal = Result::Cancelled;
}
} // namespace tmap
