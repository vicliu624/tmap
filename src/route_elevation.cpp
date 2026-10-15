#include "tmap/route_elevation.h"
#include <algorithm>
#include <cmath>

namespace tmap
{
namespace
{
Result finish(RouteElevationCursor& c, Result result)
{
    c.complete = true;
    c.terminal = result;
    return result;
}
} // namespace
Result beginRouteElevation(GraphReader& graph, const RouteLeg* legs, size_t count, RouteElevationCursor& c)
{
    c.complete = true;
    c.terminal = Result::Invalid;
    if (!legs && count) return Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    c.legs = legs;
    c.count = count;
    c.index = 0;
    c.generation = graph.package().generation();
    c.total.length_m = c.total.ascent_m = c.total.descent_m = 0;
    c.stage = 0;
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result routeElevationStep(GraphReader& graph, RouteElevationCursor& c, size_t budget, RouteElevation& output)
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
        const auto& leg = c.legs[c.index];
        Result result;
        if (c.stage == 0)
        {
            result = graph.edge(leg.edge, c.edge);
            if (result != Result::Ok)
            {
                if (result == Result::Busy || result == Result::More) return result;
                return finish(c, result);
            }
            c.last_segment = leg.last_segment == UINT32_MAX ? c.edge.points - 2 : leg.last_segment;
            if (leg.first_segment > c.last_segment || c.last_segment >= c.edge.points - 1 ||
                !std::isfinite(leg.first_fraction) || !std::isfinite(leg.last_fraction) || leg.first_fraction < 0 ||
                leg.first_fraction > 1 || leg.last_fraction < 0 || leg.last_fraction > 1 ||
                (leg.first_segment == c.last_segment && leg.first_fraction > leg.last_fraction))
                return finish(c, Result::Invalid);
            if (!c.edge.has_elevation) return finish(c, Result::Incomplete);
            if (!leg.first_segment && !leg.first_fraction && c.last_segment == c.edge.points - 2 && leg.last_fraction == 1)
            {
                c.total.length_m += c.edge.length_cm / 100.0;
                c.total.ascent_m += c.edge.ascent_cm / 100.0;
                c.total.descent_m += c.edge.descent_cm / 100.0;
                ++c.index;
                continue;
            }
            c.shape_length = c.first_distance = c.last_distance = 0;
            c.segment = 0;
            c.stage = 1;
        }
        else if (c.stage == 1)
        {
            result = graph.point(c.edge, 0, c.previous);
            if (result == Result::Ok) c.stage = 2;
            else if (result == Result::Busy || result == Result::More) return result;
            else return finish(c, result);
        }
        else if (c.stage == 2)
        {
            GeoPoint next{};
            result = graph.point(c.edge, c.segment + 1, next);
            if (result != Result::Ok)
            {
                if (result == Result::Busy || result == Result::More) return result;
                return finish(c, result);
            }
            const auto length = distanceMeters(c.previous, next);
            if (c.segment == leg.first_segment) c.first_distance = c.shape_length + length * leg.first_fraction;
            if (c.segment == c.last_segment) c.last_distance = c.shape_length + length * leg.last_fraction;
            c.shape_length += length;
            c.previous = next;
            if (++c.segment == c.edge.points - 1)
            {
                if (c.shape_length <= 0 || c.last_distance < c.first_distance) return finish(c, Result::Invalid);
                c.first_fraction = std::clamp(c.first_distance / c.shape_length, 0.0, 1.0);
                c.last_fraction = std::clamp(c.last_distance / c.shape_length, c.first_fraction, 1.0);
                c.stage = 3;
            }
        }
        else
        {
            double value = 0;
            if (c.stage == 3) result = graph.ascentPrefix(leg.edge, c.first_fraction, value);
            else if (c.stage == 4) result = graph.ascentPrefix(leg.edge, c.last_fraction, value);
            else if (c.stage == 5) result = graph.descentPrefix(leg.edge, c.first_fraction, value);
            else result = graph.descentPrefix(leg.edge, c.last_fraction, value);
            if (result != Result::Ok)
            {
                if (result == Result::Busy || result == Result::More) return result;
                return finish(c, result);
            }
            if (c.stage == 3) c.first_ascent = value;
            else if (c.stage == 4) c.last_ascent = value;
            else if (c.stage == 5) c.first_descent = value;
            else
            {
                if (c.last_ascent < c.first_ascent || value < c.first_descent) return finish(c, Result::Invalid);
                c.total.length_m += (c.last_fraction - c.first_fraction) * c.edge.length_cm / 100.0;
                c.total.ascent_m += c.last_ascent - c.first_ascent;
                c.total.descent_m += value - c.first_descent;
                ++c.index;
                c.stage = 0;
                continue;
            }
            ++c.stage;
        }
    }
    return Result::More;
}
void cancel(RouteElevationCursor& cursor) { finish(cursor, Result::Cancelled); }
} // namespace tmap
