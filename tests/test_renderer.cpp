#include "tmap/contour_renderer.h"
#include "tmap/native_file.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
using namespace tmap;
#define CHECK(x)                                                \
    do                                                          \
    {                                                           \
        if (!(x))                                               \
        {                                                       \
            std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); \
            std::exit(1);                                       \
        }                                                       \
    } while (false)
class File : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0, bytes = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t n) override
    {
        if (++calls % 4 == 0) return Status::Busy;
        bytes += n;
        return native.readAt(at, output, n);
    }
};
bool retry(Result r) { return r == Result::More || r == Result::Busy; }
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    const std::string directory = argv[1];
    auto memory = std::make_unique<PackageWorkspace>();
    auto cursor = std::make_unique<ContourRenderCursor>();
    auto segments = std::make_unique<ContourSegment[]>(256);
    OutdoorPackage package(*memory);
    File file;
    CHECK(file.native.open((directory + "/lod.tmap").c_str()));
    Result r;
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    // Compare local float projection with a double Mercator reference. Include
    // high latitudes and E7 coordinates far from zero to catch cancellation.
    constexpr double pi = 3.14159265358979323846;
    for (int latitude : {-840000000, -450000000, 0, 450000000, 840000000})
        for (int longitude : {-1700000000, 0, 1700000000})
            for (uint8_t zoom : {12, 14, 16, 18})
            {
                MapViewport view{{latitude, longitude}, zoom, 320, 240};
                CHECK(beginContourRender(package, view, segments.get(), 256, {}, *cursor) == Result::Ok);
                const double scale = std::ldexp(256.0, zoom);
                const double m = std::asinh(std::tan(latitude * 1e-7 * pi / 180));
                const double top = std::atan(std::sinh(m + pi * 240 / scale)) * 180 / pi * 1e7;
                const double bottom = std::atan(std::sinh(m - pi * 240 / scale)) * 180 / pi * 1e7;
                CHECK(cursor->source.bounds.north >= top && cursor->source.bounds.south <= bottom);
                for (int delta : {-1000, -10, 0, 10, 1000})
                {
                    GeoPoint point{latitude + delta, longitude + delta};
                    ScreenPoint screen{};
                    CHECK(project(view, point, screen));
                    const double x = 160 + double(delta) * 1e-7 / 360 * scale;
                    const double y = 120 - (std::asinh(std::tan(point.latitude * 1e-7 * pi / 180)) - m) / (2 * pi) * scale;
                    CHECK(std::abs(screen.x - x) < 0.01 && std::abs(screen.y - y) < 0.01);
                }
            }
    CHECK(beginContourRender(package, {{10000, 10000}, 16, 320, 240}, segments.get(), 256, {}, *cursor) == Result::Ok);
    size_t steps = 0;
    do
    {
        r = contourRenderStep(package, *cursor, 2);
        CHECK(++steps < 10000);
    } while (retry(r));
    CHECK(r == Result::Ok && cursor->count == 3 && cursor->operations < 30);
    for (size_t i = 0; i < cursor->count; ++i)
    {
        const auto& s = segments[i];
        CHECK(s.x1 >= 0 && s.x1 <= 320 && s.y1 >= 0 && s.y1 <= 240);
        CHECK(s.x2 >= 0 && s.x2 <= 320 && s.y2 >= 0 && s.y2 <= 240);
    }
    CHECK(beginContourRender(package, {{0, 0}, 7, 320, 240}, nullptr, 0, {}, *cursor) == Result::Ok);
    CHECK(contourRenderStep(package, *cursor, 1) == Result::Ok && cursor->count == 0);
    package.close();
    CHECK(file.native.open((directory + "/dense-contours.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    CHECK(beginContourRender(package, {{0, 0}, 16, 320, 240}, segments.get(), 64, {100000, 5}, *cursor) == Result::Ok);
    steps = 0;
    do
    {
        r = contourRenderStep(package, *cursor, 3);
        CHECK(++steps < 100000);
    } while (retry(r));
    CHECK(r == Result::Ok && cursor->count <= 64 && cursor->density_retries > 0 && cursor->interval_m > 10);
    const auto count = cursor->count, operations = cursor->operations;
    CHECK(beginContourRender(package, {{0, 0}, 16, 320, 240}, segments.get(), 1, {1000, 0}, *cursor) == Result::Ok);
    do
    {
        r = contourRenderStep(package, *cursor, 4);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded && cursor->count == 0);
    CHECK(beginContourRender(package, {{0, 0}, 16, 320, 240}, segments.get(), 256, {2, 5}, *cursor) == Result::Ok);
    do
    {
        r = contourRenderStep(package, *cursor, 4);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded && cursor->operations == 2 && cursor->count == 0);
    CHECK(beginContourRender(package, {{0, 0}, 16, 320, 240}, segments.get(), 256, {}, *cursor) == Result::Ok);
    package.close();
    CHECK(contourRenderStep(package, *cursor, 1) == Result::Cancelled);
    std::printf("PASS LOD, fixed draw storage, density fallback, operation budget, cancellation; segments=%zu operations=%zu reads=%zu bytes=%zu cursor=%zu\n", count, operations, file.calls, file.bytes, sizeof(ContourRenderCursor));
}
