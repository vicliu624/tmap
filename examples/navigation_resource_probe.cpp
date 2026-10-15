#include "tmap/integer_route.h"
#include "tmap/native_file.h"
#include "tmap/route_elevation.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
using namespace tmap;
class MeasuredFile : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0, busy_every = 0;
    uint64_t bytes = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (++calls && busy_every && calls % busy_every == 0) return Status::Busy;
        const auto status = native.readAt(offset, output, count);
        if (status == Status::Ok) bytes += count;
        return status;
    }
};
bool retry(Result result) { return result == Result::Busy || result == Result::More; }
int main(int argc, char** argv)
{
    if (argc != 6 && argc != 7)
    {
        std::fprintf(stderr, "package queries.txt capacity dijkstra|alt|fifo|integer busy_every [integer_ascent_penalty]\n");
        return 2;
    }
    const size_t capacity = std::strtoull(argv[3], nullptr, 10);
    const size_t busy_every = std::strtoull(argv[5], nullptr, 10);
    char* penalty_end = nullptr;
    const auto ascent_penalty = argc == 7 ? std::strtoul(argv[6], &penalty_end, 10) : 0;
    if (argc == 7 && (!*argv[6] || *penalty_end || *argv[6] == '-' || ascent_penalty > 1000)) return 2;
    if (capacity < 2 || capacity > 1000000 || busy_every == 1) return 2;
    const bool statistics_enabled = !std::strcmp(argv[4], "integer-stats");
    const bool integer_dijkstra = !std::strcmp(argv[4], "integer-dijkstra");
    const bool integer_heap = !std::strcmp(argv[4], "integer-heap");
    const bool integer_alt = !std::strcmp(argv[4], "integer-alt");
    const bool integer_label_scan = !std::strcmp(argv[4], "integer-label-scan");
    const uint64_t step_limit = integer_label_scan ? uint64_t(capacity) * 200001 + 2000000 : 2000000;
    const bool integer = statistics_enabled || integer_dijkstra || integer_heap || integer_alt || integer_label_scan || !std::strcmp(argv[4], "integer");
    const auto algorithm = !std::strcmp(argv[4], "dijkstra") ? RouteAlgorithm::Dijkstra : !std::strcmp(argv[4], "alt") ? RouteAlgorithm::Landmarks
                                                                                                                       : RouteAlgorithm::QueueRelaxation;
    if (!integer && std::strcmp(argv[4], "dijkstra") && std::strcmp(argv[4], "alt") && std::strcmp(argv[4], "fifo")) return 2;
    auto queries = std::fopen(argv[2], "r");
    if (!queries) return 1;
    std::printf("[\n");
    unsigned start, destination;
    size_t query = 0;
    while (std::fscanf(queries, "%u %u", &start, &destination) == 2)
    {
        MeasuredFile file;
        file.busy_every = busy_every;
        if (!file.native.open(argv[1])) return 1;
        auto workspace = std::make_unique<PackageWorkspace>();
        auto edges = std::make_unique<uint32_t[]>(capacity);
        auto queue = integer_label_scan ? nullptr : std::make_unique<uint32_t[]>(capacity);
        auto lower_bounds = integer_alt ? std::make_unique<uint32_t[]>(capacity) : nullptr;
        auto landmark_workspace = integer_alt ? std::make_unique<IntegerLandmarkWorkspace>() : nullptr;
        OutdoorPackage package(*workspace);
        Result result;
        uint64_t steps = 0;
        size_t retained = 0, count = 0, max_reads = 0;
        uint32_t expanded = 0;
        uint64_t cost = 0;
        std::unique_ptr<IntegerRouteElevationCursor> statistics;
        IntegerRouteElevation totals{};
        Result statistics_result = Result::Missing;
        size_t statistics_reads = 0;
        do
        {
            result = package.open(file);
            if (++steps > step_limit) return 1;
        } while (retry(result));
        if (result != Result::Ok) return 1;
        GraphReader graph(package);
        const size_t control_bytes = integer ? sizeof(IntegerRouteCursor) : sizeof(RouteCursor);
        const size_t label_bytes = integer ? sizeof(IntegerRouteLabel) : sizeof(RouteLabel);
        Result output_result = Result::Invalid;
        if (integer)
        {
            auto labels = std::make_unique<IntegerRouteLabel[]>(capacity);
            auto cursor = std::make_unique<IntegerRouteCursor>();
            do
            {
                result = beginIntegerRoute(graph, start, destination, {{static_cast<uint32_t>(ascent_penalty), 1}, TravelMode::Foot, 200000, integer_label_scan ? IntegerRouteAlgorithm::LabelScanDijkstra : integer_alt    ? IntegerRouteAlgorithm::Landmarks
                                                                                                                                                                                                         : integer_heap     ? IntegerRouteAlgorithm::HeapDijkstra
                                                                                                                                                                                                         : integer_dijkstra ? IntegerRouteAlgorithm::Dijkstra
                                                                                                                                                                                                                            : IntegerRouteAlgorithm::QueueRelaxation},
                                           {labels.get(), queue.get(), capacity, lower_bounds.get(), landmark_workspace.get()}, *cursor);
                if (++steps > step_limit) return 1;
            } while (retry(result));
            if (result == Result::Ok)
            {
                do
                {
                    const auto before = file.calls;
                    result = integerRouteStep(graph, *cursor, 7);
                    max_reads = std::max(max_reads, file.calls - before);
                    if (++steps > step_limit) return 1;
                } while (retry(result));
                retained = cursor->labels_used;
                expanded = cursor->expansions;
                if (result == Result::Ok) cost = labels[cursor->goal].cost;
                output_result = integerRouteEdges(*cursor, edges.get(), capacity, count);
                if (statistics_enabled && output_result == Result::Ok)
                {
                    statistics = std::make_unique<IntegerRouteElevationCursor>();
                    const auto before = file.calls;
                    do
                    {
                        statistics_result = beginIntegerRouteElevation(graph, edges.get(), count, *statistics);
                    } while (retry(statistics_result));
                    if (statistics_result == Result::Ok) do
                        {
                            statistics_result = integerRouteElevationStep(graph, *statistics, 7, totals);
                            if (++steps > step_limit) return 1;
                        } while (retry(statistics_result));
                    statistics_reads = file.calls - before;
                }
            }
        }
        else
        {
            auto labels = std::make_unique<RouteLabel[]>(capacity);
            auto cursor = std::make_unique<RouteCursor>();
            do
            {
                result = beginRoute(graph, start, destination, {TravelMode::Foot, static_cast<double>(ascent_penalty), 200000, algorithm},
                                    {labels.get(), queue.get(), capacity}, *cursor);
                if (++steps > step_limit) return 1;
            } while (retry(result));
            if (result == Result::Ok)
            {
                do
                {
                    const auto before = file.calls;
                    result = routeStep(graph, *cursor, 7);
                    max_reads = std::max(max_reads, file.calls - before);
                    if (++steps > step_limit) return 1;
                } while (retry(result));
                retained = cursor->labels_used;
                expanded = cursor->expansions;
                if (result == Result::Ok) cost = uint64_t(std::llround(labels[cursor->goal_label].cost * 100));
                output_result = routeEdges(*cursor, edges.get(), capacity, count);
            }
        }
        const size_t reserved = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) + control_bytes + capacity * (label_bytes + 4 + (integer_label_scan ? 0 : sizeof(uint32_t)) + (integer_alt ? sizeof(uint32_t) : 0)) +
                                (statistics ? sizeof(IntegerRouteElevationCursor) + sizeof(IntegerRouteElevation) : 0) +
                                (landmark_workspace ? sizeof(IntegerLandmarkWorkspace) : 0);
        std::printf("%s  {\"query\":%zu,\"start_row\":%u,\"destination_row\":%u,\"algorithm\":\"%s\",\"capacity\":%zu,\"result\":\"%s\",\"output_result\":\"%s\",\"cost_cm\":%llu,\"edges\":%zu,\"states\":%zu,\"expansions\":%u,\"read_attempts\":%zu,\"successful_bytes\":%llu,\"max_reads_per_7_work\":%zu,\"reserved_library_payload_bytes\":%zu",
                    query ? ",\n" : "", query, start, destination, argv[4], capacity, resultName(result), resultName(output_result),
                    static_cast<unsigned long long>(cost), count, retained, expanded, file.calls, static_cast<unsigned long long>(file.bytes), max_reads, reserved);
        if (statistics_enabled)
            std::printf(",\"statistics_result\":\"%s\",\"statistics_read_attempts\":%zu,\"length_cm\":%llu,\"ascent_cm\":%llu,\"descent_cm\":%llu",
                        resultName(statistics_result), statistics_reads, static_cast<unsigned long long>(totals.length_cm),
                        static_cast<unsigned long long>(totals.ascent_cm), static_cast<unsigned long long>(totals.descent_cm));
        std::printf(",\"continuation_calls\":%llu}", static_cast<unsigned long long>(steps));
        ++query;
    }
    std::fclose(queries);
    std::printf("\n]\n");
    return query ? 0 : 1;
}
