#include "tmap/integer_maneuvers.h"
#include <cmath>
#include <new>
namespace tmap
{
namespace
{
GeoPoint interpolate(GeoPoint a, GeoPoint b, float fraction)
{
    return {int32_t(std::llround(a.latitude + (double(b.latitude) - a.latitude) * fraction)),
            int32_t(std::llround(a.longitude + (double(b.longitude) - a.longitude) * fraction))};
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
ManeuverKind
classify(const IntegerManeuverCursor& c)
{
    if (!c.index) return ManeuverKind::Depart;
    const float scale = std::cos(float(c.start_a.latitude) * 1e-7f * 3.14159265358979323846f / 180);
    const float incoming = std::atan2(float(int64_t(c.previous_b.longitude) - c.previous_a.longitude) * scale,
                                      float(int64_t(c.previous_b.latitude) - c.previous_a.latitude));
    const float outgoing = std::atan2(float(int64_t(c.start_b.longitude) - c.start_a.longitude) * scale,
                                      float(int64_t(c.start_b.latitude) - c.start_a.latitude));
    const float angle = std::remainder((outgoing - incoming) * 180 / 3.14159265358979323846f, 360.0f);
    return std::abs(angle) > 150 ? ManeuverKind::UTurn : angle > 30 ? ManeuverKind::Right
                                                     : angle < -30  ? ManeuverKind::Left
                                                                    : ManeuverKind::Continue;
}
} // namespace
Result beginIntegerManeuvers(GraphReader& graph, const IntegerRouteLeg* legs, size_t count, IntegerManeuverCursor& c)
{
    ::new (static_cast<void*>(&c)) IntegerManeuverCursor{};
    if (!legs && count) return Result::Invalid;
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.legs = legs;
    c.count = count;
    c.generation = graph.package().generation();
    c.terminal = count ? Result::More : Result::Ok;
    return Result::Ok;
}
void cancel(IntegerManeuverCursor& c) { c.terminal = Result::Cancelled; }
Result integerManeuverStep(GraphReader& graph, IntegerManeuverCursor& c, size_t budget, ManeuverVisitor visitor, void* context)
{
    if (!budget || !visitor) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More) return c.terminal;
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.index == c.count)
        {
            c.value.kind = ManeuverKind::Arrive;
            c.value.point = c.arrival;
            c.value.distance_m = double(c.distance_cm) / 100;
            c.value.name_id = 0;
            return c.terminal = visitor(context, c.value) ? Result::Ok : Result::Cancelled;
        }
        Result r;
        if (c.stage == 0)
        {
            r = beginIntegerLegConversion(graph, c.legs[c.index], c.conversion);
            if (r == Result::Ok) c.stage = 1;
        }
        else if (c.stage == 1)
        {
            r = integerLegConversionStep(graph, c.conversion, 1);
            if (r == Result::Ok) c.stage = 2;
        }
        else if (c.stage <= 5)
        {
            const auto& leg = c.conversion.converted;
            const auto vertex = c.stage == 2 ? leg.first_segment : c.stage == 3 ? leg.first_segment + 1
                                                               : c.stage == 4   ? leg.last_segment
                                                                                : leg.last_segment + 1;
            auto& point = c.stage == 2 ? c.start_a : c.stage == 3 ? c.start_b
                                                 : c.stage == 4   ? c.finish_a
                                                                  : c.finish_b;
            r = graph.point(c.conversion.edge, vertex, point);
            if (r == Result::Ok) ++c.stage;
        }
        else
        {
            const auto& leg = c.legs[c.index];
            const auto end = leg.last_cm == UINT32_MAX ? c.conversion.edge.length_cm : leg.last_cm;
            const auto length = end - leg.first_cm;
            if (length > UINT64_MAX - c.distance_cm) return c.terminal = Result::BudgetExceeded;
            c.value.kind = classify(c);
            c.value.point = interpolate(c.start_a, c.start_b, c.conversion.converted.first_fraction);
            c.value.distance_m = double(c.distance_cm) / 100;
            c.value.name_id = c.conversion.edge.name_id;
            if (!visitor(context, c.value)) return c.terminal = Result::Cancelled;
            c.arrival = interpolate(c.finish_a, c.finish_b, c.conversion.converted.last_fraction);
            c.previous_a = c.finish_a;
            c.previous_b = c.finish_b;
            c.distance_cm += length;
            ++c.index;
            c.stage = 0;
            r = Result::Ok;
        }
        if (r == Result::Busy) return r;
        if (r != Result::Ok && r != Result::More) return c.terminal = r;
    }
    return Result::More;
}
} // namespace tmap
