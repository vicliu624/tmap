#include "tmap/route_elevation.h"

namespace tmap
{
namespace
{
Result finish(IntegerRouteElevationCursor& c, Result result)
{
    c.complete = true;
    c.terminal = result;
    return result;
}
} // namespace
Result beginIntegerRouteElevation(GraphReader& graph, const uint32_t* edges, size_t count, IntegerRouteElevationCursor& c)
{
    c.complete = true;
    c.terminal = Result::Invalid;
    if (!edges && count) return Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    c.edges = edges;
    c.count = count;
    c.index = 0;
    c.generation = graph.package().generation();
    c.total.length_cm = c.total.ascent_cm = c.total.descent_cm = 0;
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result integerRouteElevationStep(GraphReader& graph, IntegerRouteElevationCursor& c, size_t budget, IntegerRouteElevation& output)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return finish(c, Result::Cancelled);
    if (c.complete)
    {
        if (c.terminal == Result::Ok) output = c.total;
        return c.terminal;
    }
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.index == c.count)
        {
            output = c.total;
            return finish(c, Result::Ok);
        }
        const auto result = graph.edge(c.edges[c.index], c.scratch);
        if (result == Result::Busy || result == Result::More) return result;
        if (result != Result::Ok) return finish(c, result);
        if (!c.scratch.has_elevation) return finish(c, Result::Incomplete);
        if (c.scratch.length_cm > UINT64_MAX - c.total.length_cm || c.scratch.ascent_cm > UINT64_MAX - c.total.ascent_cm ||
            c.scratch.descent_cm > UINT64_MAX - c.total.descent_cm) return finish(c, Result::BudgetExceeded);
        c.total.length_cm += c.scratch.length_cm;
        c.total.ascent_cm += c.scratch.ascent_cm;
        c.total.descent_cm += c.scratch.descent_cm;
        ++c.index;
    }
    return Result::More;
}
void cancel(IntegerRouteElevationCursor& cursor) { finish(cursor, Result::Cancelled); }
} // namespace tmap
