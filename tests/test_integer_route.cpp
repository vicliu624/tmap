#include "tmap/integer_route.h"
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
    size_t calls = 0;
    uint64_t bytes = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override
    {
        if (++calls % 2 == 0) return Status::Busy;
        const auto result = native.readAt(offset, output, bytes);
        if (result == Status::Ok) this->bytes += bytes;
        return result;
    }
};
bool retry(Result r) { return r == Result::Busy || r == Result::More; }
int main(int argc, char** argv)
{
    CHECK(argc == 3);
    const std::string directory = argv[1];
    auto storage = std::make_unique<PackageWorkspace>();
    auto cursor = std::make_unique<IntegerRouteCursor>();
    auto labels = std::make_unique<IntegerRouteLabel[]>(256);
    auto queue = std::make_unique<uint32_t[]>(256);
    auto lower_bounds = std::make_unique<uint32_t[]>(256);
    auto landmark_workspace = std::make_unique<IntegerLandmarkWorkspace>();
    uint32_t output[256];
    IntegerRouteWorkspace workspace{labels.get(), queue.get(), 256};
    File file;
    OutdoorPackage package(*storage);
    auto reference = std::fopen((directory + "/reference.txt").c_str(), "r");
    CHECK(reference);
    char name[128];
    unsigned start, destination, mode;
    double penalty, expected;
    size_t cases = 0;
    while (std::fscanf(reference, "%127s %u %u %u %lf %lf", name, &start, &destination, &mode, &penalty, &expected) == 6)
    {
        CHECK(penalty == 1.5);
        package.close();
        CHECK(file.native.open((directory + "/" + name).c_str()));
        Result result;
        size_t steps = 0;
        do
        {
            result = package.open(file);
            CHECK(++steps < 10000);
        } while (retry(result));
        CHECK(result == Result::Ok);
        GraphReader graph(package);
        const IntegerRouteOptions options{{3, 2}, static_cast<TravelMode>(mode), 10000};
        do
        {
            result = beginIntegerRoute(graph, start, destination, options, workspace, *cursor);
            CHECK(++steps < 10000);
        } while (retry(result));
        CHECK(result == Result::Ok);
        do
        {
            result = integerRouteStep(graph, *cursor, 1);
            CHECK(++steps < 100000);
        } while (retry(result));
        if (expected < 0) CHECK(result == Result::NoPath);
        else
        {
            CHECK(result == Result::Ok);
            CHECK(cursor->workspace.labels[cursor->goal].cost == uint64_t(std::llround(expected * 200)));
            size_t count = 0;
            CHECK(integerRouteEdges(*cursor, output, 256, count) == Result::Ok);
            uint64_t sum = 0;
            uint32_t node = start, previous = UINT32_MAX;
            for (size_t i = 0; i < count; ++i)
            {
                GraphEdge edge;
                do
                {
                    result = graph.edge(output[i], edge);
                    CHECK(++steps < 100000);
                } while (retry(result));
                CHECK(result == Result::Ok && edge.from == node && (edge.access & mode));
                bool allowed = false;
                do
                {
                    result = graph.turnAllowed(previous, output[i], options.mode, allowed);
                    CHECK(++steps < 100000);
                } while (retry(result));
                CHECK(result == Result::Ok && allowed);
                uint64_t cost = 0;
                CHECK(integerEdgeCost(edge.length_cm, edge.ascent_cm, edge.has_elevation, options.ascent, cost) == Result::Ok);
                CHECK(integerAccumulateCost(sum, cost, sum) == Result::Ok);
                node = edge.to;
                previous = output[i];
            }
            CHECK(node == destination && sum == cursor->workspace.labels[cursor->goal].cost);
            if (count) CHECK(integerRouteEdges(*cursor, nullptr, 0, count) == Result::BudgetExceeded);
        }
        cancel(*cursor);
        CHECK(integerRouteStep(graph, *cursor, 1) == Result::Cancelled);
        size_t count = 77;
        CHECK(integerRouteEdges(*cursor, output, 256, count) == Result::Cancelled && count == 0);
        for (const auto priority_algorithm : {IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::LabelScanDijkstra})
        {
            auto dijkstra_options = options;
            dijkstra_options.algorithm = priority_algorithm;
            auto selected_workspace = workspace;
            if (priority_algorithm == IntegerRouteAlgorithm::LabelScanDijkstra) selected_workspace.queue = nullptr;
            do
            {
                result = beginIntegerRoute(graph, start, destination, dijkstra_options, selected_workspace, *cursor);
            } while (retry(result));
            CHECK(result == Result::Ok);
            if (priority_algorithm == IntegerRouteAlgorithm::LabelScanDijkstra) CHECK(!cursor->workspace.queue);
            steps = 0;
            do
            {
                result = integerRouteStep(graph, *cursor, 1);
                if (priority_algorithm == IntegerRouteAlgorithm::HeapDijkstra)
                    for (size_t position = 0; position < cursor->queued; ++position)
                    {
                        const auto& entry = workspace.labels[workspace.queue[position]];
                        CHECK(entry.queue_position == position);
                        if (position)
                        {
                            const auto& parent = workspace.labels[workspace.queue[(position - 1) / 2]];
                            CHECK(parent.cost < entry.cost || (parent.cost == entry.cost && parent.key <= entry.key));
                        }
                    }
                CHECK(++steps < 100000);
            } while (retry(result));
            if (expected < 0) CHECK(result == Result::NoPath);
            else CHECK(result == Result::Ok && cursor->workspace.labels[cursor->goal].cost == uint64_t(std::llround(expected * 200)));
        }
        auto missing_options = options;
        missing_options.algorithm = IntegerRouteAlgorithm::Landmarks;
        auto missing_workspace = workspace;
        missing_workspace.lower_bounds = lower_bounds.get();
        missing_workspace.landmarks = landmark_workspace.get();
        do
        {
            result = beginIntegerRoute(graph, start, destination, missing_options, missing_workspace, *cursor);
        } while (retry(result));
        CHECK(result == Result::Ok);
        CHECK(integerRouteStep(graph, *cursor, 1) == Result::Missing);
        CHECK(integerRouteStep(graph, *cursor, 1) == Result::Missing);
        size_t missing_count = 77;
        CHECK(integerRouteEdges(*cursor, output, 256, missing_count) == Result::Missing && missing_count == 0);
        package.close();
        CHECK(file.native.open((directory + "/alt-" + name).c_str()));
        do
        {
            result = package.open(file);
        } while (retry(result));
        CHECK(result == Result::Ok);
        GraphReader alt_graph(package);
        auto alt_options = options;
        alt_options.algorithm = IntegerRouteAlgorithm::Landmarks;
        auto alt_workspace = workspace;
        alt_workspace.lower_bounds = lower_bounds.get();
        alt_workspace.landmarks = landmark_workspace.get();
        do
        {
            result = beginIntegerRoute(alt_graph, start, destination, alt_options, alt_workspace, *cursor);
        } while (retry(result));
        CHECK(result == Result::Ok);
        const auto partial = integerRouteStep(alt_graph, *cursor, 1);
        CHECK(retry(partial));
        cancel(*cursor);
        CHECK(integerRouteStep(alt_graph, *cursor, 1) == Result::Cancelled);
        do
        {
            result = beginIntegerRoute(alt_graph, start, destination, alt_options, alt_workspace, *cursor);
        } while (retry(result));
        CHECK(result == Result::Ok);
        CHECK(landmark_workspace->stage == 0 && landmark_workspace->count == 0);
        steps = 0;
        do
        {
            result = integerRouteStep(alt_graph, *cursor, 1);
            CHECK(++steps < 100000);
        } while (retry(result));
        if (expected < 0) CHECK(result == Result::NoPath);
        else CHECK(result == Result::Ok && cursor->workspace.labels[cursor->goal].cost == uint64_t(std::llround(expected * 200)));
        CHECK(beginIntegerRoute(alt_graph, start, destination, alt_options, workspace, *cursor) == Result::Invalid);
        ++cases;
    }
    std::fclose(reference);
    CHECK(cases == 24);
    GraphReader cancellation_graph(package);
    IntegerRouteOptions label_scan_options{{3, 2}, static_cast<TravelMode>(mode), 10000, IntegerRouteAlgorithm::LabelScanDijkstra};
    const IntegerRouteWorkspace no_queue{labels.get(), nullptr, 256};
    Result cancelled;
    do
    {
        cancelled = beginIntegerRoute(cancellation_graph, start, destination, label_scan_options, no_queue, *cursor);
    } while (retry(cancelled));
    CHECK(cancelled == Result::Ok);
    size_t cancellation_steps = 0;
    do
    {
        cancelled = integerRouteStep(cancellation_graph, *cursor, 1);
        CHECK(retry(cancelled) && ++cancellation_steps < 2000);
    } while (!cursor->initialized || !cursor->selection);
    CHECK(!cursor->selected && !cursor->workspace.queue);
    cancel(*cursor);
    CHECK(integerRouteStep(cancellation_graph, *cursor, 1) == Result::Cancelled);
    size_t unpublished = 99;
    CHECK(integerRouteEdges(*cursor, output, 256, unpublished) == Result::Cancelled && !unpublished);
    for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
    {
        auto rejected = label_scan_options;
        rejected.algorithm = algorithm;
        CHECK(beginIntegerRoute(cancellation_graph, start, destination, rejected, no_queue, *cursor) == Result::Invalid);
    }
    std::printf("LABEL_SCAN_CANCEL unpublished=1 other_algorithms_require_queue=4\n");
    package.close();
    CHECK(file.native.open((directory + "/large.tmap").c_str()));
    Result result;
    do
    {
        result = package.open(file);
    } while (retry(result));
    CHECK(result == Result::Ok);
    GraphReader large(package);
    auto large_labels = std::make_unique<IntegerRouteLabel[]>(2048);
    auto large_queue = std::make_unique<uint32_t[]>(2048);
    auto large_output = std::make_unique<uint32_t[]>(2048);
    const IntegerRouteWorkspace large_workspace{large_labels.get(), large_queue.get(), 2048};
    do
    {
        result = beginIntegerRoute(large, 0, 2047, {}, large_workspace, *cursor);
    } while (retry(result));
    CHECK(result == Result::Ok);
    size_t steps = 0;
    do
    {
        result = integerRouteStep(large, *cursor, 7);
        CHECK(++steps < 100000);
    } while (retry(result));
    CHECK(result == Result::Ok && cursor->labels_used == 2048);
    size_t count = 0;
    CHECK(integerRouteEdges(*cursor, large_output.get(), 2048, count) == Result::Ok && count == 2047);
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i)
    {
        CHECK(large_output[i] == i);
        GraphEdge edge;
        do
        {
            result = large.edge(uint32_t(i), edge);
            CHECK(++steps < 100000);
        } while (retry(result));
        CHECK(result == Result::Ok);
        total += edge.length_cm;
    }
    CHECK(total == cursor->workspace.labels[cursor->goal].cost);
    CHECK(beginIntegerRoute(large, 0, 2047, {}, {labels.get(), queue.get(), 2}, *cursor) == Result::Ok);
    do
    {
        result = integerRouteStep(large, *cursor, 7);
        CHECK(++steps < 100000);
    } while (retry(result));
    CHECK(result == Result::BudgetExceeded);
    CHECK(integerRouteEdges(*cursor, output, 256, count) == Result::BudgetExceeded && count == 0);
    CHECK(beginIntegerRoute(large, 0, 2047, {{0, 1}, TravelMode::Foot, 1}, large_workspace, *cursor) == Result::Ok);
    do
    {
        result = integerRouteStep(large, *cursor, 7);
        CHECK(++steps < 100000);
    } while (retry(result));
    CHECK(result == Result::BudgetExceeded);
    CHECK(beginIntegerRoute(large, 0, 2047, {{1, 1}, TravelMode::Foot, 5000}, large_workspace, *cursor) == Result::Ok);
    do
    {
        result = integerRouteStep(large, *cursor, 7);
        CHECK(++steps < 100000);
    } while (retry(result));
    CHECK(result == Result::Incomplete);
    auto floating_labels = std::make_unique<RouteLabel[]>(2048);
    auto floating = std::make_unique<RouteCursor>();
    auto report = std::fopen((directory + "/integer-resource-report.json").c_str(), "w");
    CHECK(report);
    std::fprintf(report, "{\n  \"scope\":\"Cold synthetic comb, node endpoints, zero ascent, 2048 caller slots, Busy every second read; no board latency/power measurement\",\n  \"page_capacity\":%u,\n  \"results\":[\n", unsigned(TMAP_CACHE_PAGE_BYTES));
    uint64_t integer_cost = 0;
    for (size_t variant = 0; variant < 4; ++variant)
    {
        package.close();
        CHECK(file.native.open((directory + "/landmarks.tmap").c_str()));
        do
        {
            result = package.open(file);
        } while (retry(result));
        CHECK(result == Result::Ok);
        GraphReader cold(package);
        const auto first_calls = file.calls;
        const auto first_bytes = file.bytes;
        size_t states = 0, array_bytes = 0, control_bytes = 0;
        uint32_t expansions = 0;
        const char* algorithm = "integer_fifo";
        if (!variant)
        {
            do
            {
                result = beginIntegerRoute(cold, 0, 63, {}, large_workspace, *cursor);
            } while (retry(result));
            CHECK(result == Result::Ok);
            steps = 0;
            do
            {
                result = integerRouteStep(cold, *cursor, 7);
                CHECK(++steps < 100000);
            } while (retry(result));
            CHECK(result == Result::Ok);
            integer_cost = cursor->workspace.labels[cursor->goal].cost;
            states = cursor->labels_used;
            expansions = cursor->expansions;
            array_bytes = 2048 * (sizeof(IntegerRouteLabel) + sizeof(uint32_t));
            control_bytes = sizeof(IntegerRouteCursor);
        }
        else
        {
            const RouteAlgorithm selected = variant == 1 ? RouteAlgorithm::Dijkstra : variant == 2 ? RouteAlgorithm::Landmarks
                                                                                                   : RouteAlgorithm::QueueRelaxation;
            algorithm = variant == 1 ? "dijkstra" : variant == 2 ? "alt4"
                                                                 : "floating_fifo";
            do
            {
                result = beginRoute(cold, 0, 63, {TravelMode::Foot, 0, 10000, selected},
                                    {floating_labels.get(), large_queue.get(), 2048}, *floating);
            } while (retry(result));
            CHECK(result == Result::Ok);
            steps = 0;
            do
            {
                result = routeStep(cold, *floating, 7);
                CHECK(++steps < 100000);
            } while (retry(result));
            CHECK(result == Result::Ok && uint64_t(std::llround(floating->workspace.labels[floating->goal_label].cost * 100)) == integer_cost);
            states = floating->labels_used;
            expansions = floating->expansions;
            array_bytes = 2048 * (sizeof(RouteLabel) + sizeof(uint32_t));
            control_bytes = sizeof(RouteCursor);
        }
        std::fprintf(report, "    {\"algorithm\":\"%s\",\"cost_cm\":%llu,\"states\":%zu,\"expansions\":%u,\"read_attempts\":%zu,\"successful_bytes\":%llu,\"reserved_arrays_bytes\":%zu,\"cursor_bytes\":%zu}%s\n",
                     algorithm, static_cast<unsigned long long>(integer_cost), states, expansions, file.calls - first_calls,
                     static_cast<unsigned long long>(file.bytes - first_bytes), array_bytes, control_bytes, variant == 3 ? "" : ",");
    }
    std::fprintf(report, "  ]\n}\n");
    std::fclose(report);
    GraphReader capacity_graph(package);
    do
    {
        result = beginIntegerRoute(capacity_graph, 0, 63, {}, workspace, *cursor);
    } while (retry(result));
    CHECK(result == Result::Ok);
    steps = 0;
    do
    {
        result = integerRouteStep(capacity_graph, *cursor, 7);
        CHECK(++steps < 100000);
    } while (retry(result));
    CHECK(result == Result::BudgetExceeded);
    CHECK(beginIntegerRoute(capacity_graph, 0, 63, {}, {large_labels.get(), large_queue.get(), 1494}, *cursor) == Result::Ok);
    steps = 0;
    do
    {
        result = integerRouteStep(capacity_graph, *cursor, 7);
        CHECK(++steps < 100000);
    } while (retry(result));
    CHECK(result == Result::Ok && cursor->labels_used == 1494 && cursor->workspace.labels[cursor->goal].cost == integer_cost);
    for (const auto only : {false, true})
    {
        package.close();
        CHECK(file.native.open((std::string(argv[2]) + (only ? "/history-only.tmap" : "/history-no.tmap")).c_str()));
        do
        {
            result = package.open(file);
        } while (retry(result));
        CHECK(result == Result::Ok);
        GraphReader history_graph(package);
        do
        {
            result = history_graph.initialize();
        } while (retry(result));
        CHECK(result == Result::Ok && history_graph.info().history_modes == 1);
        for (const auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
            for (const auto foot : {false, true})
                for (const auto entered : {false, true})
                {
                    IntegerRouteWorkspace hw{labels.get(), queue.get(), 256, lower_bounds.get(), landmark_workspace.get()};
                    const auto target = only ? 4U : 3U;
                    do
                    {
                        result = beginIntegerRoute(history_graph, entered ? 0 : 1, target, {{0, 1}, foot ? TravelMode::Foot : TravelMode::Bicycle, 10000, algorithm}, hw, *cursor);
                    } while (retry(result));
                    CHECK(result == Result::Ok);
                    size_t history_work = 0;
                    do
                    {
                        result = integerRouteStep(history_graph, *cursor, 1);
                        if (++history_work == 10000) std::fprintf(stderr, "history only=%d foot=%d entered=%d algorithm=%u current=%u key=%u edge=%u queued=%zu used=%zu result=%s reads=%zu\n", only, foot, entered, unsigned(algorithm), cursor->current, cursor->current == UINT32_MAX ? 0 : cursor->workspace.labels[cursor->current].key, cursor->next_edge, cursor->queued, cursor->labels_used, resultName(result), file.calls);
                        CHECK(history_work < 10000);
                    } while (retry(result));
                    CHECK(result == (foot && entered ? Result::NoPath : Result::Ok));
                    if (result == Result::Ok)
                    {
                        size_t count = 0;
                        CHECK(integerRouteEdges(*cursor, output, 256, count) == Result::Ok && count >= 2);
                        for (size_t i = 0; i < count; ++i) CHECK(output[i] < history_graph.info().edges);
                    }
                }
    }
    package.close();
    CHECK(file.native.open((std::string(argv[2]) + "/history-detour.tmap").c_str()));
    do
    {
        result = package.open(file);
    } while (retry(result));
    CHECK(result == Result::Ok);
    GraphReader detour_history(package);
    for (const auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
        for (const auto foot : {false, true})
        {
            do
            {
                result = beginIntegerRoute(detour_history, 0, 3, {{0, 1}, foot ? TravelMode::Foot : TravelMode::Bicycle, 10000, algorithm}, {labels.get(), queue.get(), 256, lower_bounds.get(), landmark_workspace.get()}, *cursor);
            } while (retry(result));
            CHECK(result == Result::Ok);
            steps = 0;
            do
            {
                result = integerRouteStep(detour_history, *cursor, 1);
                CHECK(++steps < 10000);
            } while (retry(result));
            size_t count = 0;
            CHECK(result == Result::Ok && integerRouteEdges(*cursor, output, 256, count) == Result::Ok && count == (foot ? 4U : 3U));
            if (foot) CHECK(output[0] == 1 && output[1] == 4 && output[2] == 2 && output[3] == 3);
        }
    package.close();
    CHECK(file.native.open((std::string(argv[2]) + "/history-invalid-flags.tmap").c_str()));
    do
    {
        result = package.open(file);
    } while (retry(result));
    CHECK(result == Result::Ok);
    GraphReader malformed_history(package);
    do
    {
        result = beginIntegerRoute(malformed_history, 0, 3, {}, workspace, *cursor);
    } while (retry(result));
    CHECK(result == Result::Ok);
    steps = 0;
    do
    {
        result = integerRouteStep(malformed_history, *cursor, 1);
        CHECK(++steps < 10000);
    } while (retry(result));
    CHECK(result == Result::Invalid);
    size_t invalid_count = 7;
    CHECK(integerRouteEdges(*cursor, output, 256, invalid_count) == Result::Invalid && invalid_count == 0);
    std::printf("PASS Bellman references and bounded via-history routing; label=%zu arrays256=%zu cursor=%zu\n", sizeof(IntegerRouteLabel), 256 * (sizeof(IntegerRouteLabel) + 4), sizeof(IntegerRouteCursor));
}
