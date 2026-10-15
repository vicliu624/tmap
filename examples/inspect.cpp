#include "tmap/contour_renderer.h"
#include "tmap/coordinate_route.h"
#include "tmap/legacy_administration.h"
#include "tmap/native_file.h"
#include "tmap/navigation.h"
#include "tmap/route_elevation.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
bool parsePoint(const char* lon_text, const char* lat_text, tmap::GeoPoint& point)
{
    char* end = nullptr;
    const double lon = std::strtod(lon_text, &end);
    if (!*lon_text || *end || !std::isfinite(lon) || lon < -180 || lon > 180) return false;
    const double lat = std::strtod(lat_text, &end);
    if (!*lat_text || *end || !std::isfinite(lat) || lat < -90 || lat > 90) return false;
    point = {int32_t(std::llround(lat * 1e7)), int32_t(std::llround(lon * 1e7))};
    return true;
}
bool parseZoom(const char* text, uint8_t& zoom)
{
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (!*text || *end || value > 18) return false;
    zoom = uint8_t(value);
    return true;
}
class MeasuredFile final : public tmap::RandomAccessFile
{
  public:
    explicit MeasuredFile(const char* path) : file_(path) {}
    uint64_t size() const override { return file_.size(); }
    tmap::Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        ++calls;
        read_bytes += bytes;
        return file_.readAt(at, output, bytes);
    }
    size_t calls = 0;
    uint64_t read_bytes = 0;

  private:
    tmap::NativeFile file_;
};
struct Drawing
{
    tmap::MapViewport viewport{};
    tmap::ScreenPoint previous{};
    bool has_previous = false;
};
bool draw(void* context, const tmap::ContourVertex& vertex)
{
    auto& d = *static_cast<Drawing*>(context);
    tmap::ScreenPoint p{};
    if (!tmap::project(d.viewport, vertex.point, p)) return false;
    if (!vertex.first && d.has_previous)
    {
        auto a = d.previous, b = p;
        if (tmap::clipSegment(d.viewport.width, d.viewport.height, a, b))
            std::printf("<path d=\"M %.2f %.2f L %.2f %.2f\" stroke=\"#9b633b\" stroke-width=\"%u\" fill=\"none\"/>\n", a.x, a.y, b.x, b.y, vertex.elevation_m % 50 == 0 ? 2U : 1U);
    }
    if (vertex.first && p.x >= 0 && p.y >= 0 && p.x <= d.viewport.width && p.y <= d.viewport.height && vertex.elevation_m % 50 == 0)
        std::printf("<text x=\"%.2f\" y=\"%.2f\" font-size=\"10\" fill=\"#694022\">%ld m</text>\n", p.x, p.y, static_cast<long>(vertex.elevation_m));
    d.previous = p;
    d.has_previous = true;
    return true;
}
} // namespace
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "tmap_inspect file [elevation lon lat | contours-svg lon lat zoom | measure-contours lon lat zoom | route start-node end-node | route-coordinates lon1 lat1 lon2 lat2]\n");
        return 2;
    }
    MeasuredFile file(argv[1]);
    if (!file.size())
    {
        std::fprintf(stderr, "cannot open file\n");
        return 1;
    }
    auto storage = std::make_unique<tmap::PackageWorkspace>();
    tmap::OutdoorPackage package(*storage);
    auto r = package.open(file);
    if (r == tmap::Result::Unsupported)
    {
        auto legacy_storage = std::make_unique<tmap::Workspace>();
        tmap::Reader legacy(*legacy_storage);
        const auto status = legacy.open(file);
        if (status != tmap::Status::Ok)
        {
            std::fprintf(stderr, "legacy open failed: %u\n", unsigned(status));
            return 1;
        }
        std::printf("profile=v1 bytes=%llu pois=%llu workspace=%zu\n", static_cast<unsigned long long>(file.size()), static_cast<unsigned long long>(legacy.poiCount()), sizeof(tmap::Workspace));
        if (argc == 5 && !std::strcmp(argv[2], "region"))
        {
            tmap::GeoPoint point{};
            if (!parsePoint(argv[3], argv[4], point)) return 2;
            auto workspace = std::make_unique<tmap::LegacyAdministrativeWorkspace>();
            auto cursor = std::make_unique<tmap::LegacyAdministrativeCursor>();
            auto candidates = std::make_unique<tmap::LegacyAdministrativeCandidate[]>(4);
            r = tmap::LegacyAdministrativeQuery::begin(legacy, point, *workspace, candidates.get(), 4, *cursor);
            if (r == tmap::Result::Ok) do
                {
                    r = tmap::LegacyAdministrativeQuery::step(legacy, *cursor, 16);
                } while (r == tmap::Result::More || r == tmap::Result::Busy);
            std::printf("region=%s depth=%u approximate=1 limited=%u result=%s\n", cursor->result.path ? cursor->result.path : "", unsigned(cursor->result.depth), unsigned(cursor->result.limited), tmap::resultName(r));
            return r == tmap::Result::Ok ? 0 : 1;
        }
        return 0;
    }
    if (r != tmap::Result::Ok)
    {
        std::fprintf(stderr, "open: %s\n", tmap::resultName(r));
        return 1;
    }
    if (argc == 2)
    {
        std::printf("profile=outdoor-v2 bytes=%llu capabilities=%u workspace=%zu\n", static_cast<unsigned long long>(file.size()), package.info().capabilities, sizeof(tmap::PackageWorkspace));
        return 0;
    }
    if (!std::strcmp(argv[2], "region") && argc == 5)
    {
        tmap::GeoPoint point{};
        if (!parsePoint(argv[3], argv[4], point)) return 2;
        auto workspace = std::make_unique<tmap::AdministrativeWorkspace>();
        auto cursor = std::make_unique<tmap::AdministrativeCursor>();
        r = tmap::beginAdministrativeQuery(package, point, *workspace, *cursor);
        if (r == tmap::Result::Ok) do
            {
                r = tmap::administrativeStep(package, *cursor, 16);
            } while (r == tmap::Result::More || r == tmap::Result::Busy);
        std::printf("region=%s depth=%u approximate=0 boundary=%u result=%s reads=%zu bytes=%llu operations=%u\n", cursor->result.path ? cursor->result.path : "", unsigned(cursor->result.depth), unsigned(cursor->result.boundary), tmap::resultName(r), file.calls, static_cast<unsigned long long>(file.read_bytes), cursor->operations);
    }
    else if (!std::strcmp(argv[2], "elevation") && argc == 5)
    {
        tmap::TerrainReader terrain(package);
        tmap::ElevationSample z{};
        tmap::GeoPoint point{};
        if (!parsePoint(argv[3], argv[4], point)) return 2;
        r = terrain.elevation(point, z);
        if (r == tmap::Result::Ok) std::printf("elevation=%.2f m resolution=%u m datum=%u\n", z.meters, z.resolution_m, z.vertical_datum);
    }
    else if (!std::strcmp(argv[2], "contours-svg") && argc == 6)
    {
        auto cursor = std::make_unique<tmap::ContourCursor>();
        tmap::GeoPoint point{};
        uint8_t zoom = 0;
        if (!parsePoint(argv[3], argv[4], point) || !parseZoom(argv[5], zoom)) return 2;
        r = tmap::beginContours(package, package.info().bounds, zoom, *cursor);
        Drawing d{{point, zoom, 640, 480}, {}, false};
        std::printf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"480\" viewBox=\"0 0 640 480\"><rect width=\"640\" height=\"480\" fill=\"#f6f1e6\"/>\n");
        if (r == tmap::Result::Ok) do
            {
                r = tmap::contourStep(package, *cursor, 128, draw, &d);
            } while (r == tmap::Result::More);
        std::printf("</svg>\n");
    }
    else if (!std::strcmp(argv[2], "route") && argc == 5)
    {
        tmap::GraphReader graph(package);
        auto labels = std::make_unique<tmap::RouteLabel[]>(2048);
        auto heap = std::make_unique<uint32_t[]>(2048);
        tmap::RouteCursor cursor{};
        r = tmap::beginRoute(graph, uint32_t(std::atoi(argv[3])), uint32_t(std::atoi(argv[4])), {}, {labels.get(), heap.get(), 2048}, cursor);
        if (r == tmap::Result::Ok) do
            {
                r = tmap::routeStep(graph, cursor, 64);
            } while (r == tmap::Result::More);
        if (r == tmap::Result::Ok)
        {
            auto edges = std::make_unique<uint32_t[]>(2048);
            size_t count = 0;
            r = tmap::routeEdges(cursor, edges.get(), 2048, count);
            std::printf("edges=%zu expansions=%u states=%zu\n", count, cursor.expansions, cursor.labels_used);
            for (size_t i = 0; i < count; ++i) std::printf("%u\n", edges[i]);
        }
    }
    else if (!std::strcmp(argv[2], "measure-contours") && argc == 6)
    {
        auto cursor = std::make_unique<tmap::ContourRenderCursor>();
        auto segments = std::make_unique<tmap::ContourSegment[]>(512);
        tmap::GeoPoint point{};
        uint8_t zoom = 0;
        if (!parsePoint(argv[3], argv[4], point) || !parseZoom(argv[5], zoom)) return 2;
        tmap::MapViewport view{point, zoom, 320, 240};
        r = tmap::beginContourRender(package, view, segments.get(), 512, {}, *cursor);
        if (r == tmap::Result::Ok) do
            {
                r = tmap::contourRenderStep(package, *cursor, 16);
            } while (r == tmap::Result::More);
        std::printf("{\"result\":\"%s\",\"segments\":%zu,\"interval_m\":%u,\"density_retries\":%u,\"operations\":%u,\"reads\":%zu,\"read_bytes\":%llu,\"workspace_bytes\":%zu,\"cursor_bytes\":%zu,\"segment_buffer_bytes\":%zu}\n",
                    tmap::resultName(r), cursor->count, cursor->interval_m, cursor->density_retries, cursor->operations, file.calls,
                    static_cast<unsigned long long>(file.read_bytes), sizeof(tmap::PackageWorkspace), sizeof(tmap::ContourRenderCursor), 512 * sizeof(tmap::ContourSegment));
    }
    else if ((!std::strcmp(argv[2], "route-coordinates") || !std::strcmp(argv[2], "measure-route-coordinates")) && (argc == 7 || argc == 8))
    {
        tmap::GeoPoint a{}, b{};
        if (!parsePoint(argv[3], argv[4], a) || !parsePoint(argv[5], argv[6], b)) return 2;
        tmap::GraphReader graph(package);
        const bool measured = !std::strcmp(argv[2], "measure-route-coordinates");
        const size_t capacity = measured ? 128 : 2048;
        char* end = nullptr;
        const double penalty = argc == 8 ? std::strtod(argv[7], &end) : 0;
        if (argc == 8 && (!*argv[7] || *end || !std::isfinite(penalty) || penalty < 0 || penalty > 1000)) return 2;
        auto cursor = std::make_unique<tmap::CoordinateRouteCursor>();
        auto labels = std::make_unique<tmap::RouteLabel[]>(capacity);
        auto heap = std::make_unique<uint32_t[]>(capacity);
        auto legs = std::make_unique<tmap::RouteLeg[]>(capacity);
        r = tmap::beginCoordinateRoute(graph, a, b, {tmap::TravelMode::Foot, penalty}, 50, {labels.get(), heap.get(), capacity}, legs.get(), capacity, *cursor);
        if (r == tmap::Result::Ok) do
            {
                r = tmap::coordinateRouteStep(graph, *cursor, 64);
            } while (r == tmap::Result::More);
        if (r == tmap::Result::Ok && measured)
        {
            auto elevation = std::make_unique<tmap::RouteElevationCursor>();
            tmap::RouteElevation totals{};
            auto statistics_result = tmap::beginRouteElevation(graph, legs.get(), cursor->count, *elevation);
            if (statistics_result == tmap::Result::Ok) do
                {
                    statistics_result = tmap::routeElevationStep(graph, *elevation, 7, totals);
                } while (statistics_result == tmap::Result::More || statistics_result == tmap::Result::Busy);
            const size_t payload = sizeof(tmap::PackageWorkspace) + sizeof(tmap::OutdoorPackage) + sizeof(tmap::GraphReader) +
                                   sizeof(tmap::CoordinateRouteCursor) + sizeof(tmap::RouteElevationCursor) + sizeof(totals) +
                                   capacity * (sizeof(tmap::RouteLabel) + sizeof(uint32_t) + sizeof(tmap::RouteLeg));
            std::printf("{\"result\":\"%s\",\"statistics_result\":\"%s\",\"length_m\":%.8f,\"ascent_m\":%.8f,\"descent_m\":%.8f,\"cost_m\":%.8f,\"ascent_penalty\":%.3f,\"reads\":%zu,\"read_bytes\":%llu,\"host_payload_bytes\":%zu,\"legs\":[",
                        tmap::resultName(r), tmap::resultName(statistics_result), totals.length_m, totals.ascent_m, totals.descent_m,
                        cursor->cost, penalty, file.calls, static_cast<unsigned long long>(file.read_bytes), payload);
            for (size_t i = 0; i < cursor->count; ++i)
                std::printf("%s{\"edge\":%u,\"first_segment\":%u,\"last_segment\":%u,\"first_fraction\":%.9g,\"last_fraction\":%.9g}", i ? "," : "",
                            legs[i].edge, legs[i].first_segment, legs[i].last_segment, legs[i].first_fraction, legs[i].last_fraction);
            std::printf("]}\n");
            r = statistics_result;
        }
        else if (r == tmap::Result::Ok)
            std::printf("legs=%zu cost_m=%.2f expansions=%zu coverage_limited=%u\n", cursor->count, cursor->cost, cursor->expansions, unsigned(cursor->coverage_limited));
    }
    else
    {
        std::fprintf(stderr, "invalid command\n");
        return 2;
    }
    if (r != tmap::Result::Ok)
    {
        std::fprintf(stderr, "result: %s\n", tmap::resultName(r));
        return 1;
    }
    return 0;
}
