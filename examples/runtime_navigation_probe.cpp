#include "tmap/edge_rules.h"
#include "tmap/integer_route.h"
#include "tmap/native_file.h"
#include "tmap/resource_budget.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
using namespace tmap;
struct File : RandomAccessFile
{
    NativeFile native;
    size_t attempts = 0;
    RandomAccessFile* view = nullptr;
    uint64_t size() const override { return view ? view->size() : native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        if (++attempts % 2 && !view) return Status::Busy;
        return view ? view->readAt(at, output, bytes) : native.readAt(at, output, bytes);
    }
};
bool retry(Result r) { return r == Result::Busy || r == Result::More; }
int main(int argc, char** argv)
{
    if (argc != 8 && argc != 9) return 2;
    const bool virtual_mode = argc == 9 && !std::strcmp(argv[8], "virtual");
    if (argc == 9 && !virtual_mode && std::strcmp(argv[8], "edge")) return 2;
    File file;
    std::unique_ptr<File> manifest;
    std::unique_ptr<File[]> sources;
    std::unique_ptr<VirtualSource[]> slots;
    std::unique_ptr<FileExtent[]> extents;
    std::unique_ptr<VirtualMapCursor> loader;
    std::unique_ptr<VerifiedVirtualFile> verified;
    size_t extent_capacity = 0, verification_reads = 0;
    if (virtual_mode)
    {
        const std::string directory = argv[1];
        auto counts = std::fopen((directory + "/extent-count.txt").c_str(), "r");
        if (!counts) return 3;
        const auto parsed = std::fscanf(counts, "%zu", &extent_capacity);
        std::fclose(counts);
        if (parsed != 1 || !extent_capacity || extent_capacity > 65536) return 2;
        manifest = std::make_unique<File>();
        sources = std::make_unique<File[]>(3);
        slots = std::make_unique<VirtualSource[]>(3);
        extents = std::make_unique<FileExtent[]>(extent_capacity);
        loader = std::make_unique<VirtualMapCursor>();
        verified = std::make_unique<VerifiedVirtualFile>();
        if (!manifest->native.open((directory + "/map.bin").c_str())) return 3;
        uint64_t bytes = 0;
        const char* names[]{"source-a.tmap", "source-b.tmap", "patch.bin"};
        for (size_t i = 0; i < 3; ++i)
        {
            if (!sources[i].native.open((directory + "/" + names[i]).c_str())) return 3;
            slots[i].file = &sources[i];
            bytes += sources[i].size();
        }
        FileIdentity trusted;
        trusted.bytes = manifest->size();
        char hex[65]{};
        auto digest = std::fopen((directory + "/map.sha256").c_str(), "r");
        if (!digest) return 3;
        const auto read = std::fscanf(digest, "%64s", hex);
        std::fclose(digest);
        if (read != 1 || std::strlen(hex) != 64) return 2;
        for (size_t i = 0; i < 64; ++i)
        {
            const auto value = hex[i] >= '0' && hex[i] <= '9' ? hex[i] - '0' : hex[i] >= 'a' && hex[i] <= 'f' ? hex[i] - 'a' + 10
                                                                                                              : -1;
            if (value < 0) return 2;
            trusted.sha256[i / 2] = uint8_t((trusted.sha256[i / 2] << 4) | value);
        }
        auto status = beginVirtualMap(*manifest, trusted, slots.get(), 3, extents.get(), extent_capacity, *verified, *loader, bytes);
        size_t work = 0;
        while (retry(status))
        {
            status = virtualMapStep(*loader, 1);
            if (++work > 1000000) return 4;
        }
        if (status != Result::Ok) return 5;
        verification_reads = manifest->attempts + sources[0].attempts + sources[1].attempts + sources[2].attempts;
        file.view = verified.get();
    }
    else if (!file.native.open(argv[1])) return 3;
    auto storage = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*storage);
    Result result;
    size_t steps = 0;
    do
    {
        result = package.open(file);
        if (++steps > 10000) return 4;
    } while (retry(result));
    if (result != Result::Ok) return 5;
    const auto utc = std::strtoull(argv[2], nullptr, 10);
    const auto offset = static_cast<int32_t>(std::strtol(argv[3], nullptr, 10));
    auto roads = package.section(SectionType::RoadRulePrograms) ? std::make_unique<EdgeRuleReader>(package, utc, offset) : nullptr;
    auto turns = package.section(SectionType::TurnRulePrograms) ? std::make_unique<TurnContextReader>(package, utc, offset) : nullptr;
    auto binding = turns ? std::make_unique<RuntimeRoutingRules>(utc, offset, roads.get(), turns.get()) : nullptr;
    auto graph = binding ? std::make_unique<GraphReader>(package, *binding) : std::make_unique<GraphReader>(package, roads.get());
    if (argc == 9 && !virtual_mode)
    {
        GraphEdge edge;
        do
        {
            result = graph->edge(std::strtoul(argv[5], nullptr, 10), edge);
            if (++steps > 10000) return 6;
        } while (retry(result));
        ResourceOptions resources;
        resources.graph_reader = true;
        resources.edge_rules = bool(roads);
        resources.turn_context = bool(turns);
        resources.runtime_binding = bool(binding);
        resources.application_bytes = sizeof(edge);
        ResourceBudget budget;
        if (resourceBudget(resources, budget) != Result::Ok) return 9;
        std::printf("{\"result\":\"%s\",\"access\":%u,\"steps\":%zu,\"read_attempts\":%zu,\"library_and_output_payload\":%zu,\"graph_output\":%zu}\n",
                    resultName(result), result == Result::Ok ? unsigned(edge.access) : 0, steps, file.attempts, budget.total_bytes, sizeof(edge));
        return result == Result::Ok ? 0 : 10;
    }
    std::array<IntegerRouteLabel, 32> labels{};
    std::array<uint32_t, 32> edges{};
    const auto algorithm = static_cast<IntegerRouteAlgorithm>(std::strtoul(argv[7], nullptr, 10));
    const bool label_scan = algorithm == IntegerRouteAlgorithm::LabelScanDijkstra;
    auto queue = label_scan ? nullptr : std::make_unique<uint32_t[]>(32);
    auto bounds = label_scan ? nullptr : std::make_unique<uint32_t[]>(32);
    auto landmarks = label_scan ? nullptr : std::make_unique<IntegerLandmarkWorkspace>();
    IntegerRouteCursor cursor;
    IntegerRouteOptions options;
    options.mode = static_cast<TravelMode>(std::strtoul(argv[4], nullptr, 10));
    options.algorithm = algorithm;
    do
    {
        result = beginIntegerRoute(*graph, std::strtoul(argv[5], nullptr, 10), std::strtoul(argv[6], nullptr, 10), options,
                                   {labels.data(), queue.get(), labels.size(), bounds.get(), landmarks.get()}, cursor);
        if (++steps > 10000) return 6;
    } while (retry(result));
    if (result == Result::Ok)
        do
        {
            result = integerRouteStep(*graph, cursor, 1);
            if (++steps > 10000) return 7;
        } while (retry(result));
    size_t count = 0;
    if (result == Result::Ok && integerRouteEdges(cursor, edges.data(), edges.size(), count) != Result::Ok) return 8;
    ResourceOptions resources;
    resources.integer_route = true;
    resources.integer_landmarks = bool(landmarks);
    resources.integer_label_scan = label_scan;
    resources.edge_rules = bool(roads);
    resources.turn_context = bool(turns);
    resources.runtime_binding = bool(binding);
    resources.route_states = labels.size();
    resources.route_legs = edges.size();
    if (virtual_mode)
    {
        resources.virtual_sources = 3;
        resources.virtual_extents = extent_capacity;
    }
    ResourceBudget budget;
    if (resourceBudget(resources, budget) != Result::Ok) return 9;
    std::printf("{\"result\":\"%s\",\"steps\":%zu,\"read_attempts\":%zu,\"page_reads\":%llu,\"library_payload\":%zu,\"rule_reader\":%zu,\"turn_reader\":%zu,\"binding\":%zu,\"edges\":[",
                resultName(result), steps, file.attempts, static_cast<unsigned long long>(package.pageReads()), budget.total_bytes,
                roads ? sizeof(EdgeRuleReader) : 0, turns ? sizeof(TurnContextReader) : 0, binding ? sizeof(RuntimeRoutingRules) : 0);
    for (size_t i = 0; i < count; ++i) std::printf("%s%u", i ? "," : "", edges[i]);
    const size_t physical_reads = virtual_mode ? manifest->attempts + sources[0].attempts + sources[1].attempts + sources[2].attempts : file.attempts;
    std::printf("],\"virtual_mode\":%s,\"extent_capacity\":%zu,\"verification_read_attempts\":%zu,\"physical_read_attempts\":%zu,\"cost_units\":%llu,\"states\":%zu,\"expansions\":%u}\n",
                virtual_mode ? "true" : "false", extent_capacity, verification_reads, physical_reads,
                static_cast<unsigned long long>(result == Result::Ok ? labels[cursor.goal].cost : 0), cursor.labels_used, cursor.expansions);
    return result == Result::Ok || result == Result::NoPath || result == Result::BudgetExceeded ? 0 : 10;
}
