#include "route_archive_wire.h"
#include <cmath>
namespace tmap
{
using namespace route_detail;
Result routeArchiveViewport(const MapViewport& v, Bounds& out)
{
    if (!validPoint(v.center) || v.zoom > 29 || !std::isfinite(v.width) || !std::isfinite(v.height) || v.width <= 0 || v.height <= 0) return Result::Invalid;
    constexpr double pi = 3.14159265358979323846;
    const double scale = std::ldexp(256.0, v.zoom), half_lon = double(v.width) * 1800000000.0 / scale;
    const double y = std::asinh(std::tan(std::clamp<int32_t>(v.center.latitude, -850511287, 850511287) * (pi / 1800000000.0)));
    const double half_y = double(v.height) * pi / scale;
    const double north = y + half_y, south = y - half_y;
    out.north = north >= pi ? 900000000 : int32_t(std::ceil(std::atan(std::sinh(north)) * (1800000000.0 / pi)));
    out.south = south <= -pi ? -900000000 : int32_t(std::floor(std::atan(std::sinh(south)) * (1800000000.0 / pi)));
    if (half_lon >= 1800000000.0)
    {
        out.west = -1800000000;
        out.east = 1800000000;
    }
    else
    {
        int64_t west = int64_t(std::floor(v.center.longitude - half_lon)), east = int64_t(std::ceil(v.center.longitude + half_lon));
        if (west < -1800000000) west += 3600000000LL;
        if (east > 1800000000) east -= 3600000000LL;
        out.west = int32_t(west);
        out.east = int32_t(east);
    }
    return Result::Ok;
}
} // namespace tmap
