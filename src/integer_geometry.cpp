#include "tmap/integer_geometry.h"
#include <cmath>
#include <new>
namespace tmap
{
namespace
{
struct Sink
{
    IntegerGeometryCursor& cursor;
    RoutePointVisitor visitor;
    void* context;
};
bool emit(void* context, GeoPoint point)
{
    auto& sink = *static_cast<Sink*>(context);
    auto& c = sink.cursor;
    if (c.has_emitted && point.latitude == c.emitted.latitude && point.longitude == c.emitted.longitude) return true;
    if (!sink.visitor(sink.context, point)) return false;
    c.emitted = point;
    c.has_emitted = true;
    return true;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
Result
scanVertex(GraphReader& graph, IntegerLegConversionCursor& c)
{
    Result r = Result::Ok;
    const auto& leg = c.legs[c.index];
    GeoPoint point{};
    r = graph.point(c.edge, c.vertex, point);
    if (r == Result::Ok)
    {
        const double length = c.vertex ? distanceMeters(c.previous, point) : 0;
        if (!std::isfinite(length)) r = Result::Invalid;
        else if (c.stage == 1)
        {
            c.total += length;
            c.previous = point;
            if (++c.vertex == c.edge.points)
            {
                if (!(c.total > 0) || !std::isfinite(c.total)) r = Result::Invalid;
                else
                {
                    const auto end = leg.last_cm == UINT32_MAX ? c.edge.length_cm : leg.last_cm;
                    c.first = c.total * (double(leg.first_cm) / c.edge.length_cm);
                    c.last = c.total * (double(end) / c.edge.length_cm);
                    c.vertex = 0;
                    c.stage = 2;
                }
            }
        }
        else
        {
            const double next = c.walked + length;
            if (c.vertex && (next >= c.first || c.vertex + 1 == c.edge.points) && !c.first_found)
            {
                c.converted.first_segment = c.vertex - 1;
                c.converted.first_fraction = length > 0 ? float((c.first - c.walked) / length) : 0;
                c.first_found = true;
            }
            if (c.vertex && (next >= c.last || c.vertex + 1 == c.edge.points))
            {
                c.converted.last_segment = c.vertex - 1;
                c.converted.last_fraction = length > 0 ? float((c.last - c.walked) / length) : 0;
                // Rounding at the last vertex must not create an invalid fraction.
                if (c.converted.first_fraction < 0) c.converted.first_fraction = 0;
                if (c.converted.first_fraction > 1) c.converted.first_fraction = 1;
                if (c.converted.last_fraction < 0) c.converted.last_fraction = 0;
                if (c.converted.last_fraction > 1) c.converted.last_fraction = 1;
                c.stage = 3;
            }
            c.walked = next;
            c.previous = point;
            ++c.vertex;
        }
    }
    return r;
}
} // namespace
Result beginIntegerLegConversion(GraphReader& graph, const IntegerRouteLeg& leg, IntegerLegConversionCursor& c)
{
    ::new (static_cast<void*>(&c)) IntegerLegConversionCursor{};
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.legs = &leg;
    c.count = 1;
    c.generation = graph.package().generation();
    c.terminal = Result::More;
    return Result::Ok;
}
void cancel(IntegerLegConversionCursor& c) { c.terminal = Result::Cancelled; }
Result integerLegConversionStep(GraphReader& graph, IntegerLegConversionCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More) return c.terminal;
    if (c.stage == 3) return Result::Ok;
    if (!c.legs || c.index >= c.count || c.stage > 3) return c.terminal = Result::Invalid;
    for (size_t work = 0; work < budget; ++work)
    {
        Result r = Result::Ok;
        const auto& leg = c.legs[c.index];
        if (c.stage == 0)
        {
            r = graph.edge(leg.edge, c.edge);
            if (r == Result::Ok)
            {
                const auto end = leg.last_cm == UINT32_MAX ? c.edge.length_cm : leg.last_cm;
                if (c.edge.points < 2 || !c.edge.length_cm || leg.first_cm > end || end > c.edge.length_cm) r = Result::Invalid;
                else
                {
                    c.converted = RouteLeg{};
                    c.converted.edge = leg.edge;
                    c.total = c.walked = 0;
                    c.vertex = 0;
                    c.first_found = false;
                    c.stage = 1;
                }
            }
        }
        else if (c.stage == 1 || c.stage == 2) r = scanVertex(graph, c);

        if (r == Result::Busy) return r;
        if (r != Result::Ok && r != Result::More) return c.terminal = r;
        if (c.stage == 3) return Result::Ok;
    }
    return Result::More;
}
Result beginIntegerGeometry(GraphReader& graph, const IntegerRouteLeg* legs, size_t count, IntegerGeometryCursor& c)
{
    ::new (static_cast<void*>(&c)) IntegerGeometryCursor{};
    if (!legs && count) return Result::Invalid;
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.legs = legs;
    c.count = count;
    c.generation = graph.package().generation();
    c.terminal = count ? Result::More : Result::Ok;
    return Result::Ok;
}
void cancel(IntegerGeometryCursor& c) { c.terminal = Result::Cancelled; }
Result integerGeometryStep(GraphReader& graph, IntegerGeometryCursor& c, size_t budget, RoutePointVisitor visitor, void* context)
{
    if (!budget || !visitor) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More) return c.terminal;
    for (size_t work = 0; work < budget; ++work)
    {
        Result r = Result::Ok;
        if (c.stage < 3) r = integerLegConversionStep(graph, c, 1);
        else if (c.stage == 3)
        {
            r = beginLegGeometry(graph, &c.converted, 1, c.drawing);
            if (r == Result::Ok) c.stage = 4;
        }
        else
        {
            Sink sink{c, visitor, context};
            r = legGeometryStep(graph, c.drawing, 1, emit, &sink);
            if (r == Result::Ok)
            {
                c.stage = 0;
                if (++c.index == c.count) return c.terminal = Result::Ok;
            }
        }
        if (r == Result::Busy) return r;
        if (r != Result::Ok && r != Result::More) return c.terminal = r;
    }
    return Result::More;
}
} // namespace tmap
