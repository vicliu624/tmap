#include "route_archive_wire.h"
#include <cmath>
#include <new>
namespace tmap
{
using namespace route_detail;
namespace
{
bool gatherPoint(void* context, GeoPoint point)
{
    auto& c = *static_cast<RouteArchiveExportCursor*>(context);
    if (c.have_previous && point.latitude == c.previous.latitude && point.longitude == c.previous.longitude) return true;
    c.point.break_before = c.have_previous && !c.geometry.has_emitted;
    const auto length = c.have_previous && !c.point.break_before ? distanceMeters(c.previous, point) * 100 : 0;
    if (!std::isfinite(length) || length < 0 || length >= double(UINT64_MAX - c.point.distance_cm))
    {
        c.terminal = Result::Invalid;
        return false;
    }
    c.point.distance_cm += uint64_t(std::llround(length));
    c.point.point = point;
    c.point.leg = c.index;
    c.previous = point;
    c.have_previous = c.pending = true;
    return true;
}
} // namespace
Result beginRouteArchiveExport(GraphReader& graph, RouteLegSpool& spool, uint32_t count,
                               RouteArchiveWriter& writer, RouteArchiveExportCursor& c)
{
    ::new (static_cast<void*>(&c)) RouteArchiveExportCursor{};
    if (!count || count > spool.capacity() || writer.result() != Result::More || writer.info().legs != count) return Result::Invalid;
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    if (writer.info().map_id != graph.package().info().id || writer.info().map_build != graph.package().info().build ||
        writer.info().map_series != graph.package().info().series || writer.info().map_revision != graph.package().info().revision) return Result::Incompatible;
    c.count = count;
    c.generation = graph.package().generation();
    c.terminal = Result::More;
    return Result::Ok;
}
Result routeArchiveExportStep(GraphReader& graph, RouteLegSpool& spool, RouteArchiveWriter& writer,
                              RouteArchiveExportCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal != Result::Ok) writer.cancel();
        return c.terminal;
    }
    while (budget--)
    {
        Result r = Result::Ok;
        if (c.pending)
        {
            r = writer.append(c.point);
            if (r == Result::Ok) c.pending = false;
        }
        else if (c.stage == 0 || c.stage == 2)
        {
            r = spool.readLeg(c.index, c.leg);
            if (r == Result::Ok) ++c.stage;
        }
        else if (c.stage == 1)
        {
            r = writer.writeLeg(c.index, c.leg);
            if (r == Result::Ok)
            {
                c.stage = 0;
                if (++c.index == c.count)
                {
                    c.index = 0;
                    c.stage = 2;
                }
            }
        }
        else if (c.stage == 3)
        {
            r = beginIntegerGeometry(graph, &c.leg, 1, c.geometry);
            if (r == Result::Ok) c.stage = 4;
        }
        else if (c.stage == 4)
        {
            r = integerGeometryStep(graph, c.geometry, 1, gatherPoint, &c);
            if (c.terminal != Result::More) r = c.terminal;
            if (r == Result::Ok)
            {
                c.stage = 2;
                if (++c.index == c.count) c.stage = 5;
            }
        }
        else
        {
            r = writer.finishStep(1);
            if (r == Result::Ok) return c.terminal = Result::Ok;
        }
        if (r == Result::Busy) return r;
        if (r != Result::Ok && r != Result::More)
        {
            writer.cancel();
            return c.terminal = r;
        }
    }
    return Result::More;
}
void cancelRouteArchiveExport(RouteArchiveWriter& writer, RouteArchiveExportCursor& c)
{
    if (c.terminal == Result::More)
    {
        c.terminal = Result::Cancelled;
        writer.cancel();
    }
}
} // namespace tmap
