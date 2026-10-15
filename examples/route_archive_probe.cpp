#include "tmap/integer_coordinate_route.h"
#include "tmap/native_file.h"
#include "tmap/native_route_file.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
using namespace tmap;
namespace
{
class MeasuredFile final : public RandomAccessFile
{
  public:
    RandomAccessFile& file;
    size_t every = 0, calls = 0;
    uint64_t bytes = 0;
    explicit MeasuredFile(RandomAccessFile& value) : file(value) {}
    uint64_t size() const override { return file.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t count) override
    {
        if (every && ++calls % every == 0) return Status::Busy;
        const auto r = file.readAt(at, output, count);
        if (r == Status::Ok) bytes += count;
        return r;
    }
};
class MeasuredWrite final : public WritableRouteFile
{
  public:
    NativeRouteFile file;
    size_t every = 0, calls = 0;
    uint64_t read_bytes = 0, written_bytes = 0;
    uint64_t size() const override { return file.size(); }
    bool busy() { return every && ++calls % every == 0; }
    Status readAt(uint64_t at, uint8_t* output, size_t count) override
    {
        if (busy()) return Status::Busy;
        const auto r = file.readAt(at, output, count);
        if (r == Status::Ok) read_bytes += count;
        return r;
    }
    Status writeAt(uint64_t at, const uint8_t* output, size_t count) override
    {
        if (busy()) return Status::Busy;
        const auto r = file.writeAt(at, output, count);
        if (r == Status::Ok) written_bytes += count;
        return r;
    }
};
bool retry(Result r) { return r == Result::Busy || r == Result::More; }
uint64_t hashPoint(uint64_t h, GeoPoint p)
{
    h = (h ^ uint32_t(p.latitude)) * 1099511628211ULL;
    return (h ^ uint32_t(p.longitude)) * 1099511628211ULL;
}
struct Summary
{
    uint64_t hash = 14695981039346656037ULL, count = 0, target = 0, closest = UINT64_MAX;
    GeoPoint middle{}, first{}, last{};
    const MapViewport* viewport = nullptr;
    uint64_t drawn = 0;
};
bool point(void* context, GeoPoint p)
{
    auto& s = *static_cast<Summary*>(context);
    if (!s.count) s.first = p;
    s.last = p;
    s.hash = hashPoint(s.hash, p);
    ++s.count;
    return true;
}
bool segment(void* context, const RouteArchiveSegment& value)
{
    auto& s = *static_cast<Summary*>(context);
    if (s.viewport)
    {
        ScreenPoint a, b;
        if (project(*s.viewport, value.first.point, a) && project(*s.viewport, value.last.point, b) &&
            clipSegment(s.viewport->width, s.viewport->height, a, b)) ++s.drawn;
    }
    if (!s.count) point(context, value.first.point);
    point(context, value.last.point);
    const auto d = value.last.distance_cm > s.target ? value.last.distance_cm - s.target : s.target - value.last.distance_cm;
    if (d < s.closest)
    {
        s.closest = d;
        s.middle = value.last.point;
    }
    return true;
}
} // namespace
#define REQUIRE(x)                                               \
    do                                                           \
    {                                                            \
        if (!(x))                                                \
        {                                                        \
            std::fprintf(stderr, "probe:%d %s\n", __LINE__, #x); \
            return 1;                                            \
        }                                                        \
    } while (false)
int main(int argc, char** argv)
{
    if (argc != 7 && argc != 10)
    {
        std::fprintf(stderr, "package.tmap output.tmroute start_node end_node state_capacity busy_every\n"
                             "package.tmap output.tmroute --coordinates start_lat_e7 start_lon_e7 end_lat_e7 end_lon_e7 state_capacity busy_every\n");
        return 2;
    }
    const bool coordinates = argc == 10;
    REQUIRE(!coordinates || std::string(argv[3]) == "--coordinates");
    const uint32_t start = coordinates ? UINT32_MAX : uint32_t(std::strtoul(argv[3], nullptr, 10));
    const uint32_t end = coordinates ? UINT32_MAX : uint32_t(std::strtoul(argv[4], nullptr, 10));
    const size_t capacity = std::strtoull(argv[coordinates ? 8 : 5], nullptr, 10), every = std::strtoull(argv[coordinates ? 9 : 6], nullptr, 10);
    REQUIRE(capacity >= 2 && capacity <= 1000000 && every != 1);
    NativeFile native;
    REQUIRE(native.open(argv[1]));
    MeasuredFile file(native);
    file.every = every;
    auto storage = std::make_unique<PackageWorkspace>();
    auto package = std::make_unique<OutdoorPackage>(*storage);
    Result r;
    do
    {
        r = package->open(file);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    const auto identity = package->info();
    auto graph = std::make_unique<GraphReader>(*package);
    GraphNode first, last;
    if (coordinates)
    {
        first.point = {int32_t(std::strtol(argv[4], nullptr, 10)), int32_t(std::strtol(argv[5], nullptr, 10))};
        last.point = {int32_t(std::strtol(argv[6], nullptr, 10)), int32_t(std::strtol(argv[7], nullptr, 10))};
    }
    else
    {
        do
        {
            r = graph->node(start, first);
        } while (retry(r));
        REQUIRE(r == Result::Ok);
        do
        {
            r = graph->node(end, last);
        } while (retry(r));
        REQUIRE(r == Result::Ok);
    }
    auto labels = std::make_unique<IntegerRouteLabel[]>(capacity);
    auto queue = std::make_unique<uint32_t[]>(capacity);
    auto bounds = std::make_unique<uint32_t[]>(capacity);
    auto landmarks = std::make_unique<IntegerLandmarkWorkspace>();
    auto planner = std::make_unique<IntegerCoordinateRouteCursor>();
    auto reference = std::make_unique<IntegerRouteLeg[]>(capacity);
    IntegerRouteOptions options{{0, 1}, TravelMode::Foot, 200000, IntegerRouteAlgorithm::Landmarks};
    IntegerRouteWorkspace work{labels.get(), queue.get(), capacity, bounds.get(), landmarks.get()};
    do
    {
        r = beginIntegerCoordinateRoute(*graph, first.point, last.point, options, 50, work, reference.get(), capacity, *planner);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    do
    {
        r = integerCoordinateRouteStep(*graph, *planner, 7);
    } while (retry(r));
    if (r != Result::Ok)
    {
        std::printf("{\"phase\":\"reference_search\",\"result\":\"%s\",\"state_capacity\":%zu,\"start_node\":%u,\"end_node\":%u,\"retained_labels\":%zu,\"expansions\":%u,\"max_expansions\":%u}\n",
                    resultName(r), capacity, start, end, planner->stage >= 4 ? planner->scratch.searching.search.labels_used : 0,
                    planner->stage >= 4 ? planner->scratch.searching.expansions : 0, options.max_expansions);
        return 3;
    }
    size_t reference_count = 0;
    uint64_t reference_cost = 0;
    REQUIRE(integerCoordinateRouteSummary(*planner, reference_count, reference_cost) == Result::Ok);
    auto geometry = std::make_unique<IntegerGeometryCursor>();
    Summary baseline;
    do
    {
        r = beginIntegerGeometry(*graph, reference.get(), reference_count, *geometry);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    do
    {
        r = integerGeometryStep(*graph, *geometry, 7, point, &baseline);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    geometry.reset();
    MeasuredWrite temporary, output;
    temporary.every = output.every = every;
    const std::string spool_path = std::string(argv[2]) + ".spool";
    REQUIRE(temporary.file.openNew(spool_path.c_str()));
    REQUIRE(output.file.openNew(argv[2]));
    RouteLegSpool spool(temporary, uint32_t(capacity));
    const auto search_before = file.bytes;
    graph.reset();
    package->close();
    do
    {
        r = package->open(file);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    graph = std::make_unique<GraphReader>(*package); // Cold library caches after parity control.
    do
    {
        r = beginIntegerCoordinateRouteToSink(*graph, first.point, last.point, options, 50, work, spool, uint32_t(capacity), *planner);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    size_t peak_labels = 0;
    do
    {
        r = integerCoordinateRouteStep(*graph, *planner, 1);
        if (planner->stage >= 4) peak_labels = std::max(peak_labels, planner->scratch.searching.search.labels_used);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    size_t count = 0;
    uint64_t cost = 0;
    REQUIRE(integerCoordinateRouteSummary(*planner, count, cost) == Result::Ok && count == reference_count && cost == reference_cost);
    for (uint32_t i = 0; i < count; ++i)
    {
        IntegerRouteLeg leg;
        do
        {
            r = spool.readLeg(i, leg);
        } while (retry(r));
        REQUIRE(r == Result::Ok && leg.edge == reference[i].edge && leg.first_cm == reference[i].first_cm && leg.last_cm == reference[i].last_cm);
    }
    const auto search_reads = file.bytes - search_before;
    const size_t search_payload = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) +
                                  sizeof(IntegerCoordinateRouteCursor) + sizeof(IntegerLandmarkWorkspace) +
                                  capacity * (sizeof(IntegerRouteLabel) + 8) + sizeof(RouteLegSpool);
    labels.reset();
    queue.reset();
    bounds.reset();
    landmarks.reset();
    planner.reset();
    reference.reset();
    auto memory = std::make_unique<RouteArchiveWorkspace>();
    auto writer = std::make_unique<RouteArchiveWriter>(*memory);
    auto exporter = std::make_unique<RouteArchiveExportCursor>();
    do
    {
        r = writer->begin(output, identity, uint32_t(count), cost, 64 * 1024 * 1024);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    do
    {
        r = beginRouteArchiveExport(*graph, spool, uint32_t(count), *writer, *exporter);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    const auto export_before = file.bytes;
    do
    {
        r = routeArchiveExportStep(*graph, spool, *writer, *exporter, 7);
    } while (retry(r));
    REQUIRE(r == Result::Ok && output.file.flush());
    const auto export_reads = file.bytes - export_before;
    const auto info = writer->info();
    const auto archive_bytes = output.size();
    const auto spool_bytes = temporary.size();
    const size_t export_payload = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) +
                                  sizeof(RouteArchiveWorkspace) + sizeof(RouteArchiveWriter) + sizeof(RouteArchiveExportCursor) + sizeof(RouteLegSpool);
    writer.reset();
    exporter.reset();
    graph.reset();
    package.reset();
    storage.reset();
    native.close();
    output.file.close();
    temporary.file.close();
    REQUIRE(std::remove(spool_path.c_str()) == 0);
    // Reopen from disk with no map or search workspace retained.
    NativeFile saved;
    REQUIRE(saved.open(argv[2]));
    MeasuredFile saved_file(saved);
    saved_file.every = every;
    RouteArchiveReader reader(*memory);
    do
    {
        r = reader.open(saved_file);
    } while (retry(r));
    REQUIRE(r == Result::Ok && reader.matches(identity));
    const auto open_bytes = saved_file.bytes;
    auto validation = std::make_unique<RouteArchiveValidationCursor>();
    REQUIRE(reader.beginValidation(*validation) == Result::Ok);
    do
    {
        r = reader.validationStep(*validation, 7);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    const auto validation_bytes = saved_file.bytes - open_bytes;
    validation.reset();
    do
    {
        r = reader.open(saved_file);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    const auto query_before = saved_file.bytes;
    auto query = std::make_unique<RouteArchiveQueryCursor>();
    REQUIRE(reader.beginQuery({}, *query) == Result::Ok);
    Summary complete;
    complete.target = info.length_cm / 2;
    do
    {
        r = reader.queryStep(*query, 7, segment, &complete);
    } while (retry(r));
    REQUIRE(r == Result::Ok && complete.hash == baseline.hash && complete.count == baseline.count);
    const auto full_read_bytes = saved_file.bytes - query_before;
    RouteArchiveQuery request;
    request.viewport = {complete.middle.longitude - 1000, complete.middle.latitude - 1000,
                        complete.middle.longitude + 1000, complete.middle.latitude + 1000};
    do
    {
        r = reader.open(saved_file);
    } while (retry(r)); // Cold page cache.
    REQUIRE(r == Result::Ok);
    REQUIRE(reader.beginQuery(request, *query) == Result::Ok);
    Summary visible;
    const auto before = saved_file.bytes;
    do
    {
        r = reader.queryStep(*query, 7, segment, &visible);
    } while (retry(r));
    REQUIRE(r == Result::Ok && visible.count > 0);
    const auto view_reads = saved_file.bytes - before;
    const auto view_nodes = query->nodes, view_segments = query->segments;
    request.viewport = {-1800000000, -900000000, 1800000000, 900000000};
    request.first_cm = info.length_cm / 2 > 1000 ? info.length_cm / 2 - 1000 : 0;
    request.last_cm = info.length_cm / 2 + 1000;
    do
    {
        r = reader.open(saved_file);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    REQUIRE(reader.beginQuery(request, *query) == Result::Ok);
    Summary nearby;
    const auto distance_before = saved_file.bytes;
    do
    {
        r = reader.queryStep(*query, 7, segment, &nearby);
    } while (retry(r));
    REQUIRE(r == Result::Ok);
    const auto distance_reads = saved_file.bytes - distance_before;
    struct Window
    {
        uint8_t zoom;
        unsigned position;
        uint64_t bytes, nodes, tested, returned, drawn;
    };
    Window windows[12]{};
    size_t window_count = 0;
    const GeoPoint centers[] = {complete.first, complete.middle, complete.last};
    for (unsigned position = 0; position < 3; ++position)
    {
        for (uint8_t zoom : {12, 14, 16, 18})
        {
            do
            {
                r = reader.open(saved_file);
            } while (retry(r));
            REQUIRE(r == Result::Ok);
            RouteArchiveQuery window;
            MapViewport viewport{centers[position], zoom, 320, 240};
            REQUIRE(routeArchiveViewport(viewport, window.viewport) == Result::Ok);
            REQUIRE(reader.beginQuery(window, *query) == Result::Ok);
            Summary sample;
            sample.viewport = &viewport;
            const auto window_before = saved_file.bytes;
            do
            {
                r = reader.queryStep(*query, 7, segment, &sample);
            } while (retry(r));
            REQUIRE(r == Result::Ok);
            windows[window_count++] = {zoom, position, saved_file.bytes - window_before, query->nodes, query->segments, sample.count ? sample.count - 1 : 0, sample.drawn};
            REQUIRE(reader.beginQuery({}, *query) == Result::Ok);
            Summary oracle;
            oracle.viewport = &viewport;
            do
            {
                r = reader.queryStep(*query, 7, segment, &oracle);
            } while (retry(r));
            REQUIRE(r == Result::Ok && oracle.drawn == sample.drawn);
        }
    }
    const size_t read_payload = sizeof(RouteArchiveWorkspace) + sizeof(RouteArchiveReader) + sizeof(RouteArchiveQueryCursor);
    std::printf("{\"start_node\":%u,\"end_node\":%u,\"start_latitude_e7\":%d,\"start_longitude_e7\":%d,\"end_latitude_e7\":%d,\"end_longitude_e7\":%d,\"state_capacity\":%zu,\"busy_every\":%zu,"
                "\"legs\":%zu,\"points\":%llu,\"chunks\":%u,\"length_cm\":%llu,\"cost_units\":%llu,"
                "\"archive_bytes\":%llu,\"spool_bytes\":%llu,\"spool_written_bytes\":%llu,"
                "\"search_map_read_bytes\":%llu,\"export_map_read_bytes\":%llu,\"archive_written_bytes\":%llu,"
                "\"search_library_payload_bytes\":%zu,\"export_library_payload_bytes\":%zu,\"read_library_payload_bytes\":%zu,"
                "\"open_read_bytes\":%llu,\"full_validation_read_bytes\":%llu,\"full_query_read_bytes\":%llu,\"viewport_query_read_bytes\":%llu,"
                "\"viewport_index_nodes\":%llu,\"viewport_segments_tested\":%llu,\"viewport_segments_returned\":%llu,"
                "\"distance_query_read_bytes\":%llu,\"array_leg_parity\":true,\"array_geometry_parity\":true,"
                "\"map_closed_before_reopen\":true,\"workspace_bytes\":%zu,\"reader_bytes\":%zu,\"query_cursor_bytes\":%zu,\"search_labels_peak\":%zu,\"windows\":[",
                start, end, first.point.latitude, first.point.longitude, last.point.latitude, last.point.longitude, capacity, every, count, static_cast<unsigned long long>(info.points), info.chunks,
                static_cast<unsigned long long>(info.length_cm), static_cast<unsigned long long>(cost),
                static_cast<unsigned long long>(archive_bytes), static_cast<unsigned long long>(spool_bytes),
                static_cast<unsigned long long>(temporary.written_bytes), static_cast<unsigned long long>(search_reads),
                static_cast<unsigned long long>(export_reads), static_cast<unsigned long long>(output.written_bytes),
                search_payload, export_payload, read_payload, static_cast<unsigned long long>(open_bytes), static_cast<unsigned long long>(validation_bytes),
                static_cast<unsigned long long>(full_read_bytes), static_cast<unsigned long long>(view_reads),
                static_cast<unsigned long long>(view_nodes), static_cast<unsigned long long>(view_segments),
                static_cast<unsigned long long>(visible.count ? visible.count - 1 : 0),
                static_cast<unsigned long long>(distance_reads), sizeof(RouteArchiveWorkspace), sizeof(RouteArchiveReader), sizeof(RouteArchiveQueryCursor), peak_labels);
    for (size_t i = 0; i < window_count; ++i)
        std::printf("%s{\"zoom\":%u,\"position\":%u,\"width\":320,\"height\":240,\"read_bytes\":%llu,\"nodes\":%llu,\"tested\":%llu,\"candidates\":%llu,\"drawn\":%llu}",
                    i ? "," : "", unsigned(windows[i].zoom), windows[i].position, static_cast<unsigned long long>(windows[i].bytes),
                    static_cast<unsigned long long>(windows[i].nodes), static_cast<unsigned long long>(windows[i].tested), static_cast<unsigned long long>(windows[i].returned), static_cast<unsigned long long>(windows[i].drawn));
    std::printf("]}\n");
    return 0;
}
