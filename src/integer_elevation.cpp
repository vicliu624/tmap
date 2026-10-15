#include "tmap/integer_elevation.h"
#include "binary.h"
namespace tmap
{
using namespace detail;
Result beginIntegerElevation(GraphReader& graph, uint32_t edge, uint32_t distance, IntegerElevationCursor& c)
{
    c.terminal = Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    if (edge >= graph.info().edges) return Result::Invalid;
    c.generation = graph.package().generation();
    c.edge = edge;
    c.distance_cm = distance;
    c.stage = 0;
    c.read_completed = 0;
    c.terminal = Result::More;
    return Result::Ok;
}
Result integerElevationStep(GraphReader& graph, IntegerElevationCursor& c, size_t budget, IntegerElevationPrefix& output)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.value;
        return c.terminal;
    }
    auto& package = graph.package();
    for (size_t work = 0; work < budget; ++work)
    {
        auto* b = c.bytes.data();
        Result result;
        if (!c.stage)
        {
            GraphEdge edge{};
            result = graph.edge(c.edge, edge);
            if (result == Result::Ok)
            {
                c.length = edge.length_cm;
                c.ascent = edge.ascent_cm;
                c.descent = edge.descent_cm;
                if (!c.length || c.distance_cm > c.length) return c.terminal = Result::Invalid;
                if (c.distance_cm && !edge.has_elevation) return c.terminal = Result::Incomplete;
                if (!c.distance_cm || c.distance_cm == c.length)
                {
                    c.value.ascent_cm = c.distance_cm ? c.ascent : 0;
                    c.value.descent_cm = c.distance_cm ? c.descent : 0;
                    output = c.value;
                    return c.terminal = Result::Ok;
                }
                c.stage = 1;
            }
        }
        else if (c.stage == 1)
        {
            result = fixedFragment(package, SectionType::EdgeTerrainIndex, uint64_t(c.edge) * 16, b, 16, c.read_completed);
            if (result == Result::Missing) return c.terminal = Result::Incomplete;
            if (result == Result::Ok)
            {
                c.read_completed = 0;
                c.offset = u64(b);
                c.count = u32(b + 8);
                const auto* data = package.section(SectionType::EdgeTerrainData);
                if (!c.count) return c.terminal = Result::Incomplete;
                if (c.count < 2 || !data || u32(b + 12) || !range(c.offset, uint64_t(c.count) * 12, data->bytes)) return c.terminal = Result::Invalid;
                c.stage = 2;
            }
        }
        else
        {
            const auto row = c.stage == 2 ? 0 : c.stage == 3 ? c.count - 1
                                            : c.stage == 4   ? c.lo + (c.hi - c.lo) / 2
                                            : c.stage == 5   ? c.lo - 1
                                                             : c.lo;
            result = fixedFragment(package, SectionType::EdgeTerrainData, c.offset + uint64_t(row) * 12, b, 12, c.read_completed);
            if (result == Result::Ok)
            {
                c.read_completed = 0;
                if (c.stage == 2)
                {
                    if (u32(b) || u32(b + 4) || u32(b + 8)) return c.terminal = Result::Invalid;
                    c.stage = 3;
                }
                else if (c.stage == 3)
                {
                    if (u32(b) != c.length || u32(b + 4) != c.ascent || u32(b + 8) != c.descent) return c.terminal = Result::Invalid;
                    c.lo = 1;
                    c.hi = c.count - 1;
                    c.stage = 4;
                }
                else if (c.stage == 4)
                {
                    if (u32(b) < c.distance_cm) c.lo = row + 1;
                    else c.hi = row;
                    if (c.lo == c.hi) c.stage = 5;
                }
                else if (c.stage == 5)
                {
                    c.lower_distance = u32(b);
                    c.lower_ascent = u32(b + 4);
                    c.lower_descent = u32(b + 8);
                    c.stage = 6;
                }
                else
                {
                    const auto upper = u32(b), up = u32(b + 4), down = u32(b + 8);
                    if (c.lower_distance >= upper || c.lower_distance > c.distance_cm || upper < c.distance_cm ||
                        c.lower_ascent > up || up > c.ascent || c.lower_descent > down || down > c.descent) return c.terminal = Result::Invalid;
                    const uint64_t width = upper - c.lower_distance, distance = c.distance_cm - c.lower_distance;
                    c.value.ascent_cm = c.lower_ascent + static_cast<uint32_t>((uint64_t(up - c.lower_ascent) * distance + width / 2) / width);
                    c.value.descent_cm = c.lower_descent + static_cast<uint32_t>((uint64_t(down - c.lower_descent) * distance + width / 2) / width);
                    output = c.value;
                    return c.terminal = Result::Ok;
                }
            }
        }
        if (result == Result::Busy || result == Result::More) return result;
        if (result != Result::Ok) return c.terminal = result;
    }
    return Result::More;
}
void cancel(IntegerElevationCursor& c) { c.terminal = Result::Cancelled; }
Result beginIntegerEdgeInterval(GraphReader& graph, uint32_t edge, uint32_t start, uint32_t end, AscentRatio ratio, IntegerEdgeIntervalCursor& c)
{
    c.terminal = Result::Invalid;
    if (start > end || !ratio.denominator) return Result::Invalid;
    const auto result = beginIntegerElevation(graph, edge, start, c.prefix);
    if (result != Result::Ok) return result;
    c.edge = edge;
    c.start_cm = start;
    c.end_cm = end;
    c.ratio = ratio;
    c.stage = 0;
    c.terminal = Result::More;
    return Result::Ok;
}
Result integerEdgeIntervalStep(GraphReader& graph, IntegerEdgeIntervalCursor& c, size_t budget, IntegerEdgeInterval& output)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.prefix.generation != graph.package().generation()) return c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.value;
        return c.terminal;
    }
    for (size_t work = 0; work < budget; ++work)
    {
        Result result;
        if (c.stage == 0)
        {
            result = integerElevationStep(graph, c.prefix, 1, c.first);
            if (c.start_cm == c.end_cm && (result == Result::Ok || result == Result::Incomplete))
            {
                c.value.length_cm = c.value.ascent_cm = c.value.descent_cm = 0;
                c.value.cost_units = 0;
                output = c.value;
                return c.terminal = Result::Ok;
            }
            if (result == Result::Ok) c.stage = 1;
        }
        else if (c.stage == 1)
        {
            result = beginIntegerElevation(graph, c.edge, c.end_cm, c.prefix);
            if (result == Result::Ok) c.stage = 2;
        }
        else
        {
            IntegerElevationPrefix last{};
            result = integerElevationStep(graph, c.prefix, 1, last);
            if (result == Result::Ok)
            {
                if (last.ascent_cm < c.first.ascent_cm || last.descent_cm < c.first.descent_cm) return c.terminal = Result::Invalid;
                c.value.length_cm = c.end_cm - c.start_cm;
                c.value.ascent_cm = last.ascent_cm - c.first.ascent_cm;
                c.value.descent_cm = last.descent_cm - c.first.descent_cm;
                result = integerEdgeCost(c.value.length_cm, c.value.ascent_cm, true, c.ratio, c.value.cost_units);
                if (result == Result::Ok)
                {
                    output = c.value;
                    return c.terminal = Result::Ok;
                }
            }
        }
        if (result == Result::Busy) return result;
        if (result != Result::Ok && result != Result::More) return c.terminal = result;
    }
    return Result::More;
}
void cancel(IntegerEdgeIntervalCursor& c)
{
    cancel(c.prefix);
    c.terminal = Result::Cancelled;
}
} // namespace tmap
