#include "tmap/integer_prepared_route.h"
namespace tmap
{
namespace
{
Result writeLeg(IntegerPreparedRouteCursor& c, size_t index, const IntegerRouteLeg& leg)
{
    if (index >= c.capacity) return Result::BudgetExceeded;
    if (c.sink_output) return c.sink->writeLeg(static_cast<uint32_t>(index), leg);
    c.legs[index] = leg;
    return Result::Ok;
}
Result finish(IntegerPreparedRouteCursor& c, Result result)
{
    c.terminal = result;
    if (result != Result::Ok) c.count = 0;
    return result;
}
} // namespace
Result beginIntegerPreparedRoute(GraphReader& graph, const IntegerEndpointCandidates& starts, const IntegerEndpointCandidates& ends,
                                 IntegerRouteOptions options, IntegerRouteWorkspace workspace, IntegerRouteLeg* legs, size_t capacity, IntegerPreparedRouteCursor& c)
{
    c.terminal = Result::Invalid;
    c.count = 0;
    if (!starts.count || starts.count > 2 || !ends.count || ends.count > 2 || !starts.departure || ends.departure ||
        !workspace.labels || (!workspace.queue && options.algorithm != IntegerRouteAlgorithm::LabelScanDijkstra) || workspace.capacity < 2 || workspace.capacity > UINT32_MAX || !legs || !capacity ||
        !options.ascent.denominator || !options.max_expansions || (options.mode != TravelMode::Foot && options.mode != TravelMode::Bicycle) || uint8_t(options.algorithm) > uint8_t(IntegerRouteAlgorithm::LabelScanDijkstra) ||
        (options.algorithm == IntegerRouteAlgorithm::Landmarks && (!workspace.lower_bounds || !workspace.landmarks))) return Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    if (starts.generation != graph.package().generation() || ends.generation != graph.package().generation() ||
        starts.mode != options.mode || ends.mode != options.mode || starts.ratio.numerator != options.ascent.numerator ||
        ends.ratio.numerator != options.ascent.numerator || starts.ratio.denominator != options.ascent.denominator ||
        ends.ratio.denominator != options.ascent.denominator) return Result::Incompatible;
    c.starts = &starts;
    c.ends = &ends;
    c.options = options;
    c.workspace = workspace;
    c.legs = legs;
    c.sink_output = false;
    c.capacity = capacity;
    c.generation = graph.package().generation();
    c.pair = c.stage = 0;
    c.found = c.coverage_limited = false;
    c.expansions = 0;
    c.terminal = Result::More;
    return Result::Ok;
}
Result beginIntegerPreparedRouteToSink(GraphReader& graph, const IntegerEndpointCandidates& starts,
                                       const IntegerEndpointCandidates& ends, IntegerRouteOptions options,
                                       IntegerRouteWorkspace workspace, IntegerRouteLegSink& sink, size_t capacity,
                                       IntegerPreparedRouteCursor& c)
{
    if (capacity > UINT32_MAX) return Result::Invalid;
    const auto r = beginIntegerPreparedRoute(graph, starts, ends, options, workspace, &c.direct.best.leg, capacity, c);
    if (r == Result::Ok)
    {
        c.sink = &sink;
        c.sink_output = true;
    }
    return r;
}
Result integerPreparedRouteStep(GraphReader& graph, IntegerPreparedRouteCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return finish(c, Result::Cancelled);
    if (c.terminal != Result::More) return c.terminal;
    const auto policy = graph.validateRouting(c.options.mode);
    if (policy == Result::Busy || policy == Result::More) return policy;
    if (policy != Result::Ok) return finish(c, policy);
    for (size_t work = 0; work < budget; ++work)
    {
        Result result;
        if (c.stage == 0)
        {
            result = beginIntegerDirectRoute(graph, *c.starts, *c.ends, c.options.mode, c.options.ascent, c.direct);
            if (result == Result::Ok) c.stage = 1;
        }
        else if (c.stage == 1)
        {
            IntegerDirectRoute direct{};
            result = integerDirectRouteStep(graph, c.direct, 1, direct);
            if (result == Result::Ok)
            {
                result = writeLeg(c, 0, direct.leg);
                if (result == Result::Busy || result == Result::More) return result;
                if (result != Result::Ok) return finish(c, result);
                c.count = 1;
                c.cost_units = direct.cost_units;
                c.found = true;
                c.stage = 2;
            }
            else if (result == Result::NoPath)
            {
                c.stage = 2;
                result = Result::Ok;
            }
        }
        else
        {
            if (c.pair == c.starts->count * c.ends->count)
                return finish(c, c.found ? Result::Ok : c.coverage_limited ? Result::OutsideCoverage
                                                                           : Result::NoPath);
            const auto& start = c.starts->choices[c.pair / c.ends->count];
            const auto& end = c.ends->choices[c.pair % c.ends->count];
            if (c.stage == 2)
            {
                uint64_t bound = 0;
                result = integerAccumulateCost(start.cost_units, end.cost_units, bound);
                if (result != Result::Ok) return finish(c, result);
                if (c.found && bound >= c.cost_units)
                {
                    ++c.pair;
                    continue;
                }
                if (c.expansions >= c.options.max_expansions) return finish(c, Result::BudgetExceeded);
                auto options = c.options;
                options.max_expansions -= c.expansions;
                result = beginIntegerRouteWithBoundaries(graph, start.node, end.node, start.boundary_edge, end.boundary_edge, options, c.workspace, c.search);
                if (result == Result::Ok) c.stage = 3;
            }
            else if (c.stage == 3)
            {
                const auto before = c.search.expansions;
                result = integerRouteStep(graph, c.search, 1);
                c.expansions += c.search.expansions - before;
                if (result == Result::NoPath || result == Result::OutsideCoverage)
                {
                    c.coverage_limited = c.coverage_limited || result == Result::OutsideCoverage;
                    ++c.pair;
                    c.stage = 2;
                    result = Result::Ok;
                }
                else if (result == Result::Ok)
                {
                    result = integerAccumulateCost(start.cost_units, c.workspace.labels[c.search.goal].cost, c.pending_cost);
                    if (result == Result::Ok) result = integerAccumulateCost(c.pending_cost, end.cost_units, c.pending_cost);
                    if (result == Result::Ok)
                    {
                        if (c.found && c.pending_cost >= c.cost_units)
                        {
                            ++c.pair;
                            c.stage = 2;
                        }
                        else
                        {
                            c.walk = c.search.goal;
                            c.walk_count = 0;
                            c.stage = 4;
                        }
                    }
                }
            }
            else if (c.stage == 4)
            {
                result = Result::Ok;
                if (c.walk >= c.workspace.capacity) return finish(c, Result::Invalid);
                const auto& label = c.workspace.labels[c.walk];
                if (label.previous == UINT32_MAX)
                {
                    const auto extra = size_t(start.partial) + size_t(end.partial);
                    if (c.walk_count > c.capacity || extra > c.capacity - c.walk_count) return finish(c, Result::BudgetExceeded);
                    c.count = c.walk_count + extra;
                    c.write_index = c.walk_count + size_t(start.partial);
                    c.walk = c.search.goal;
                    c.stage = 5;
                }
                else
                {
                    if (++c.walk_count > c.search.labels_used) return finish(c, Result::Invalid);
                    c.walk = label.previous;
                }
            }
            else if (c.stage == 5)
            {
                result = start.partial ? writeLeg(c, 0, start.leg) : Result::Ok;
                if (result == Result::Ok) c.stage = 6;
            }
            else if (c.stage == 6)
            {
                result = end.partial ? writeLeg(c, c.count - 1, end.leg) : Result::Ok;
                if (result == Result::Ok) c.stage = 7;
            }
            else
            {
                result = Result::Ok;
                if (c.walk >= c.workspace.capacity) return finish(c, Result::Invalid);
                const auto& label = c.workspace.labels[c.walk];
                if (label.previous == UINT32_MAX)
                {
                    c.cost_units = c.pending_cost;
                    c.found = true;
                    ++c.pair;
                    c.stage = 2;
                }
                else
                {
                    if (!c.write_index || label.key == UINT32_MAX || !label.key) return finish(c, Result::Invalid);
                    IntegerRouteLeg leg{};
                    if (!c.search.total_edges) return finish(c, Result::Invalid);
                    leg.edge = (label.key - 1) % c.search.total_edges;
                    leg.first_cm = 0;
                    leg.last_cm = UINT32_MAX;
                    result = writeLeg(c, c.write_index - 1, leg);
                    if (result == Result::Busy || result == Result::More) return result;
                    if (result != Result::Ok) return finish(c, result);
                    --c.write_index;
                    c.walk = label.previous;
                }
            }
        }
        if (result == Result::Busy) return result;
        if (result != Result::Ok && result != Result::More) return finish(c, result);
    }
    return Result::More;
}
void cancel(IntegerPreparedRouteCursor& c)
{
    cancel(c.search);
    cancel(c.direct);
    finish(c, Result::Cancelled);
}
Result integerPreparedRouteResult(const IntegerPreparedRouteCursor& c, const IntegerRouteLeg*& legs, size_t& count, uint64_t& cost)
{
    legs = nullptr;
    count = 0;
    cost = 0;
    if (c.terminal != Result::Ok) return c.terminal;
    if (c.sink_output) return Result::Unsupported;
    legs = c.legs;
    count = c.count;
    cost = c.cost_units;
    return Result::Ok;
}
Result integerPreparedRouteSummary(const IntegerPreparedRouteCursor& c, size_t& count, uint64_t& cost)
{
    count = 0;
    cost = 0;
    if (c.terminal != Result::Ok) return c.terminal;
    count = c.count;
    cost = c.cost_units;
    return Result::Ok;
}
} // namespace tmap
