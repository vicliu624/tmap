#include "tmap/contour_renderer.h"
#include "binary.h"
#include <algorithm>
#include <cmath>
namespace tmap
{
using namespace detail;
namespace
{
Result finish(ContourRenderCursor& c, Result r)
{
    c.complete = true;
    c.terminal = r;
    if (r != Result::Ok) c.count = 0;
    return r;
}
bool draw(void* context, const ContourVertex& v)
{
    auto& c = *static_cast<ContourRenderCursor*>(context);
    ScreenPoint p{};
    if (!project(c.viewport, v.point, p))
    {
        c.invalid = true;
        return false;
    }
    if (!v.first && c.has_previous)
    {
        auto a = c.previous, b = p;
        if (clipSegment(c.viewport.width, c.viewport.height, a, b))
        {
            if (c.count == c.capacity)
            {
                c.overflow = true;
                return false;
            }
            c.segments[c.count++] = {float(a.x), float(a.y), float(b.x), float(b.y), v.elevation_m, v.elevation_m % int32_t(c.major_interval_m) == 0};
        }
    }
    c.previous = p;
    c.has_previous = !v.last;
    return true;
}
uint32_t coarser(uint32_t interval)
{
    constexpr uint32_t choices[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
    for (auto choice : choices)
        if (choice > interval) return choice;
    return 0;
}
} // namespace
Result beginContourRender(OutdoorPackage& package, const MapViewport& v, ContourSegment* segments, size_t capacity,
                          const ContourRenderOptions& options, ContourRenderCursor& c)
{
    reset(c);
    ScreenPoint center{};
    if (!project(v, v.center, center) || v.zoom > 18 || !options.max_operations || (!segments && capacity)) return Result::Invalid;
    c.viewport = v;
    c.segments = segments;
    c.capacity = capacity;
    c.options = options;
    const auto policy = contourPolicy(v.zoom);
    c.interval_m = policy.interval_m;
    c.major_interval_m = policy.major_interval_m;
    if (!policy.interval_m)
    {
        c.terminal = Result::Ok;
        return Result::Ok;
    }
    if (!capacity) return Result::BudgetExceeded;
    constexpr float pi = 3.14159265358979323846f;
    const float scale = std::ldexp(256.0f, v.zoom);
    if (v.width > scale || v.height > scale || std::abs(v.center.latitude) > 850511287) return Result::Unsupported;
    const auto dx = int64_t(std::ceil(v.width * (1800000000.0f / scale)));
    const int64_t west = int64_t(v.center.longitude) - dx, east = int64_t(v.center.longitude) + dx;
    if (west < -1800000000 || east > 1800000000) return Result::Unsupported;
    const float m = std::asinh(std::tan(float(v.center.latitude) * 1e-7f * pi / 180));
    const float delta = pi * v.height / scale;
    const float top = std::atan(std::sinh(m + delta)) * 180 / pi, bottom = std::atan(std::sinh(m - delta)) * 180 / pi;
    if (top > 85.05112878f || bottom < -85.05112878f) return Result::Unsupported;
    // Inflate latitude query by <6m for conservative float inverse bounds.
    // Projection itself uses a stable local difference and clipping is exact
    // within the selected single-precision screen representation.
    Bounds box{int32_t(west), int32_t(std::floor(bottom * 1e7f)) - 512, int32_t(east), int32_t(std::ceil(top * 1e7f)) + 512};
    const auto r = beginContours(package, box, v.zoom, c.source);
    if (r != Result::Ok) return r;
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result contourRenderStep(OutdoorPackage& package, ContourRenderCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (c.complete) return c.terminal;
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.operations >= c.options.max_operations) return finish(c, Result::BudgetExceeded);
        const auto r = contourStep(package, c.source, 1, draw, &c);
        if (r == Result::Busy) return r;
        ++c.operations;
        if (c.invalid) return finish(c, Result::Invalid);
        if (c.overflow)
        {
            const auto interval = coarser(c.interval_m);
            if (!interval || c.density_retries == c.options.max_density_retries) return finish(c, Result::BudgetExceeded);
            ++c.density_retries;
            c.interval_m = interval;
            c.major_interval_m = std::max(interval, c.major_interval_m);
            // beginContours consumes bounds before resetting its output; retain
            // the small bounds value so source reset cannot erase the request.
            const auto box = c.source.bounds;
            const auto restart = beginContours(package, box, c.viewport.zoom, c.source);
            if (restart != Result::Ok) return finish(c, restart);
            c.source.policy.interval_m = c.interval_m;
            c.count = 0;
            c.overflow = false;
            c.has_previous = false;
            continue;
        }
        if (r == Result::Ok) return finish(c, Result::Ok);
        if (r != Result::More) return finish(c, r);
    }
    return Result::More;
}
} // namespace tmap
