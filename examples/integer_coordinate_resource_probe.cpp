#include "tmap/integer_coordinate_route.h"
#include "tmap/integer_leg_elevation.h"
#include "tmap/integer_maneuvers.h"
#include "tmap/native_file.h"
#include "tmap/resource_budget.h"
#include "tmap/virtual_map.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
using namespace tmap;
class File : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0, every = 0;
    uint64_t bytes = 0;
    RandomAccessFile* view = nullptr;
    uint64_t size() const override { return view ? view->size() : native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t count) override
    {
        ++calls;
        if (every && calls % every == 0) return Status::Busy;
        const auto r = view ? view->readAt(at, output, count) : native.readAt(at, output, count);
        if (r == Status::Ok) bytes += count;
        return r;
    }
};
bool retry(Result r) { return r == Result::More || r == Result::Busy; }
struct Events
{
    Maneuver data[129]{};
    size_t count = 0;
};
bool instruction(void* context, const Maneuver& value)
{
    auto& events = *static_cast<Events*>(context);
    if (events.count == 129) return false;
    events.data[events.count++] = value;
    return true;
}
int main(int argc, char** argv)
{
    if (argc != 5 && argc != 6) return 2;
    const bool virtual_mode = argc == 6 && !std::strcmp(argv[5], "virtual");
    if (argc == 6 && !virtual_mode) return 2;
    char* end = nullptr;
    const auto busy = std::strtoul(argv[2], &end, 10);
    if (!*argv[2] || *end || *argv[2] == '-' || busy == 1) return 2;
    const auto penalty = std::strtoul(argv[3], &end, 10);
    if (!*argv[3] || *end || *argv[3] == '-' || penalty > 1000) return 2;
    IntegerRouteOptions options{{static_cast<uint32_t>(penalty), 1}, TravelMode::Foot, 50000, IntegerRouteAlgorithm::HeapDijkstra};
    if (!std::strcmp(argv[4], "fifo")) options.algorithm = IntegerRouteAlgorithm::QueueRelaxation;
    else if (!std::strcmp(argv[4], "linear")) options.algorithm = IntegerRouteAlgorithm::Dijkstra;
    else if (!std::strcmp(argv[4], "alt")) options.algorithm = IntegerRouteAlgorithm::Landmarks;
    else if (!std::strcmp(argv[4], "label-scan")) options.algorithm = IntegerRouteAlgorithm::LabelScanDijkstra;
    else if (std::strcmp(argv[4], "heap")) return 2;
    GeoPoint start{}, destination{};
    if (std::scanf("%d %d %d %d", &start.latitude, &start.longitude, &destination.latitude, &destination.longitude) != 4) return 2;
    constexpr size_t capacity = 128;
    auto workspace = std::make_unique<PackageWorkspace>();
    auto cursor = std::make_unique<IntegerCoordinateRouteCursor>();
    auto statistics = std::make_unique<IntegerLegElevationCursor>();
    auto maneuvers = std::make_unique<IntegerManeuverCursor>();
    auto events = std::make_unique<Events>();
    auto labels = std::make_unique<IntegerRouteLabel[]>(capacity);
    auto queue = options.algorithm == IntegerRouteAlgorithm::LabelScanDijkstra ? nullptr : std::make_unique<uint32_t[]>(capacity);
    auto legs = std::make_unique<IntegerRouteLeg[]>(capacity);
    std::unique_ptr<uint32_t[]> lower;
    std::unique_ptr<IntegerLandmarkWorkspace> landmarks;
    if (options.algorithm == IntegerRouteAlgorithm::Landmarks)
    {
        lower = std::make_unique<uint32_t[]>(capacity);
        landmarks = std::make_unique<IntegerLandmarkWorkspace>();
    }
    OutdoorPackage package(*workspace);
    File file;
    file.every = virtual_mode ? 0 : busy;
    std::unique_ptr<File[]> sources;
    std::unique_ptr<File> manifest;
    std::unique_ptr<VirtualSource[]> slots;
    std::unique_ptr<FileExtent[]> extents;
    std::unique_ptr<VirtualMapCursor> loader;
    std::unique_ptr<VerifiedVirtualFile> verified;
    size_t extent_capacity = 0, virtual_payload = 0, verification_reads = 0;
    Result r;
    size_t calls = 0;
    if (virtual_mode)
    {
        const std::string directory = argv[1];
        auto count_file = std::fopen((directory + "/extent-count.txt").c_str(), "r");
        if (!count_file) return 1;
        const auto parsed = std::fscanf(count_file, "%zu", &extent_capacity);
        std::fclose(count_file);
        if (parsed != 1 || !extent_capacity || extent_capacity > 65536) return 2;
        manifest = std::make_unique<File>();
        sources = std::make_unique<File[]>(3);
        slots = std::make_unique<VirtualSource[]>(3);
        extents = std::make_unique<FileExtent[]>(extent_capacity);
        loader = std::make_unique<VirtualMapCursor>();
        verified = std::make_unique<VerifiedVirtualFile>();
        if (!manifest->native.open((directory + "/map.bin").c_str())) return 1;
        manifest->every = busy;
        const char* names[]{"source-a.tmap", "source-b.tmap", "patch.bin"};
        uint64_t total_source_bytes = 0;
        for (size_t i = 0; i < 3; ++i)
        {
            if (!sources[i].native.open((directory + "/" + names[i]).c_str())) return 1;
            sources[i].every = busy;
            slots[i].file = &sources[i];
            total_source_bytes += sources[i].size();
        }
        FileIdentity trusted;
        trusted.bytes = manifest->size();
        char hex[65]{};
        auto digest_file = std::fopen((directory + "/map.sha256").c_str(), "r");
        if (!digest_file) return 1;
        const auto digest_parsed = std::fscanf(digest_file, "%64s", hex);
        std::fclose(digest_file);
        if (digest_parsed != 1 || std::strlen(hex) != 64) return 2;
        for (size_t i = 0; i < 32; ++i)
        {
            unsigned byte;
            if (std::sscanf(hex + 2 * i, "%2x", &byte) != 1) return 2;
            trusted.sha256[i] = uint8_t(byte);
        }
        r = beginVirtualMap(*manifest, trusted, slots.get(), 3, extents.get(), extent_capacity, *verified, *loader, total_source_bytes);
        while (retry(r))
        {
            r = virtualMapStep(*loader, 1);
            if (++calls > 1000000) return 1;
        }
        if (r != Result::Ok) return 1;
        file.view = verified.get();
        virtual_payload = sizeof(VirtualMapCursor) + sizeof(VerifiedVirtualFile) +
                          3 * sizeof(VirtualSource) + extent_capacity * sizeof(FileExtent);
        verification_reads = manifest->calls + sources[0].calls + sources[1].calls + sources[2].calls;
    }
    else if (!file.native.open(argv[1])) return 1;
    do
    {
        r = package.open(file);
        if (++calls > 1000000) return 1;
    } while (retry(r));
    if (r != Result::Ok) return 1;
    GraphReader graph(package);
    do
    {
        r = beginIntegerCoordinateRoute(graph, start, destination, options, 50, {labels.get(), queue.get(), capacity, lower.get(), landmarks.get()}, legs.get(), capacity, *cursor);
        if (++calls > 1000000) return 1;
    } while (retry(r));
    if (r == Result::Ok) do
        {
            r = integerCoordinateRouteStep(graph, *cursor, 1);
            if (++calls > 1000000) return 1;
        } while (retry(r));
    const IntegerRouteLeg* ready = nullptr;
    size_t count = 0;
    uint64_t cost = 0;
    const auto published = integerCoordinateRouteResult(*cursor, ready, count, cost);
    IntegerRouteElevation totals{};
    Result stats = Result::Missing;
    if (published == Result::Ok)
    {
        do
        {
            stats = beginIntegerLegElevation(graph, ready, count, *statistics);
            if (++calls > 1000000) return 1;
        } while (retry(stats));
        if (stats == Result::Ok) do
            {
                stats = integerLegElevationStep(graph, *statistics, 1, totals);
                if (++calls > 1000000) return 1;
            } while (retry(stats));
    }
    ResourceOptions resources;
    Result instructions = Result::Missing;
    if (published == Result::Ok)
    {
        do
        {
            instructions = beginIntegerManeuvers(graph, ready, count, *maneuvers);
            if (++calls > 1000000) return 1;
        } while (retry(instructions));
        if (instructions == Result::Ok) do
            {
                instructions = integerManeuverStep(graph, *maneuvers, 1, instruction, events.get());
                if (++calls > 1000000) return 1;
            } while (retry(instructions));
    }
    resources.route_states = resources.route_legs = capacity;
    resources.integer_route = resources.integer_prepared = resources.integer_coordinate = resources.route_elevation = true;
    resources.integer_landmarks = bool(landmarks);
    resources.integer_label_scan = options.algorithm == IntegerRouteAlgorithm::LabelScanDijkstra;
    resources.maneuvers = true;
    if (virtual_mode)
    {
        resources.virtual_sources = 3;
        resources.virtual_extents = extent_capacity;
    }
    ResourceBudget budget;
    if (resourceBudget(resources, budget) != Result::Ok) return 1;
    const auto physical_reads = virtual_mode ? manifest->calls + sources[0].calls + sources[1].calls + sources[2].calls : file.calls;
    const auto physical_bytes = virtual_mode ? manifest->bytes + sources[0].bytes + sources[1].bytes + sources[2].bytes : file.bytes;
    std::printf("{\"result\":\"%s\",\"statistics_result\":\"%s\",\"cost_units\":%llu,\"length_cm\":%llu,\"ascent_cm\":%llu,\"descent_cm\":%llu,\"continuation_calls\":%zu,\"read_attempts\":%zu,\"successful_bytes\":%llu,\"library_payload_bytes\":%zu,\"state_capacity\":128,\"leg_capacity\":128,\"legs\":[",
                resultName(r), resultName(stats), static_cast<unsigned long long>(cost), static_cast<unsigned long long>(totals.length_cm),
                static_cast<unsigned long long>(totals.ascent_cm), static_cast<unsigned long long>(totals.descent_cm), calls, physical_reads,
                static_cast<unsigned long long>(physical_bytes), budget.total_bytes);
    for (size_t i = 0; i < count; ++i) std::printf("%s[%u,%u,%u]", i ? "," : "", ready[i].edge, ready[i].first_cm, ready[i].last_cm);
    std::printf("],\"maneuver_result\":\"%s\",\"probe_event_storage_bytes\":%zu,\"maneuvers\":[", resultName(instructions), sizeof(Events));
    for (size_t i = 0; i < events->count; ++i)
    {
        const auto& event = events->data[i];
        std::printf("%s{\"kind\":%u,\"point\":[%d,%d],\"distance_m\":%.9f,\"name_id\":%llu}", i ? "," : "", unsigned(event.kind), event.point.latitude, event.point.longitude, event.distance_m, static_cast<unsigned long long>(event.name_id));
    }
    std::printf("],\"virtual_mode\":%s,\"virtual_payload_bytes\":%zu,\"extent_capacity\":%zu,\"verification_physical_reads\":%zu,\"virtual_read_attempts\":%zu}\n", virtual_mode ? "true" : "false", virtual_payload, extent_capacity, verification_reads, file.calls);
    return r == Result::Ok && stats == Result::Ok && instructions == Result::Ok ? 0 : 1;
}
