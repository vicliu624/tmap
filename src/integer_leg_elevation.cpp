#include "tmap/integer_leg_elevation.h"
#include <new>
namespace tmap
{
Result beginIntegerLegElevation(GraphReader& graph, const IntegerRouteLeg* legs, size_t count, IntegerLegElevationCursor& c)
{
    ::new (static_cast<void*>(&c)) IntegerLegElevationCursor{};
    if (!legs && count) return Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    c.legs = legs;
    c.count = count;
    c.generation = graph.package().generation();
    c.terminal = Result::More;
    return Result::Ok;
}
void cancel(IntegerLegElevationCursor& c) { c.terminal = Result::Cancelled; }
Result integerLegElevationStep(GraphReader& graph, IntegerLegElevationCursor& c, size_t budget, IntegerRouteElevation& output)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.total;
        return c.terminal;
    }
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.index == c.count)
        {
            output = c.total;
            return c.terminal = Result::Ok;
        }
        const auto& leg = c.legs[c.index];
        Result r = Result::Ok;
        if (c.stage == 0)
        {
            r = graph.edge(leg.edge, c.edge);
            if (r == Result::Ok)
            {
                const auto end = leg.last_cm == UINT32_MAX ? c.edge.length_cm : leg.last_cm;
                if (leg.first_cm > end || end > c.edge.length_cm) r = Result::Invalid;
                else if (leg.first_cm == end)
                {
                    c.value.length_cm = c.value.ascent_cm = c.value.descent_cm = 0;
                    c.stage = 3;
                }
                else if (!c.edge.has_elevation) r = Result::Incomplete;
                else if (!leg.first_cm && end == c.edge.length_cm)
                {
                    c.value.length_cm = c.edge.length_cm;
                    c.value.ascent_cm = c.edge.ascent_cm;
                    c.value.descent_cm = c.edge.descent_cm;
                    c.stage = 3;
                }
                else c.stage = 1;
            }
        }
        else if (c.stage == 1)
        {
            const auto end = leg.last_cm == UINT32_MAX ? c.edge.length_cm : leg.last_cm;
            r = beginIntegerEdgeInterval(graph, leg.edge, leg.first_cm, end, {0, 1}, c.interval);
            if (r == Result::Ok) c.stage = 2;
        }
        else if (c.stage == 2)
        {
            r = integerEdgeIntervalStep(graph, c.interval, 1, c.value);
            if (r == Result::Ok) c.stage = 3;
        }
        else
        {
            if (c.value.length_cm > UINT64_MAX - c.total.length_cm ||
                c.value.ascent_cm > UINT64_MAX - c.total.ascent_cm ||
                c.value.descent_cm > UINT64_MAX - c.total.descent_cm) return c.terminal = Result::BudgetExceeded;
            c.total.length_cm += c.value.length_cm;
            c.total.ascent_cm += c.value.ascent_cm;
            c.total.descent_cm += c.value.descent_cm;
            ++c.index;
            c.stage = 0;
        }
        if (r == Result::Busy) return r;
        if (r != Result::Ok && r != Result::More) return c.terminal = r;
    }
    return Result::More;
}
} // namespace tmap
