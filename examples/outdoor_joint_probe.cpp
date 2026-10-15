#include "tmap/native_file.h"
#include "tmap/resource_budget.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace tmap;
struct File : RandomAccessFile
{
    NativeFile native;
    size_t reads = 0;
    uint64_t transferred = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        if (++reads % 2 == 0) return Status::Busy;
        const auto result = native.readAt(at, output, bytes);
        if (result == Status::Ok) transferred += bytes;
        return result;
    }
};
bool retry(Result r) { return r == Result::More || r == Result::Busy; }
int main(int argc, char** argv)
{
    if (argc != 9 && argc != 10) return 2;
    const auto start = std::strtoul(argv[2], nullptr, 10), destination = std::strtoul(argv[3], nullptr, 10);
    const auto utc = std::strtoull(argv[4], nullptr, 10);
    const auto offset = argc == 10 ? static_cast<int32_t>(std::strtol(argv[9], nullptr, 10)) : 0;
    GeoPoint location{static_cast<int32_t>(std::strtol(argv[5], nullptr, 10)), static_cast<int32_t>(std::strtol(argv[6], nullptr, 10))};
    const auto zoom = static_cast<uint8_t>(std::strtoul(argv[7], nullptr, 10));
    const auto edge = std::strtoul(argv[8], nullptr, 10);
    File file;
    if (!file.native.open(argv[1])) return 3;
    auto memory = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*memory);
    Result result;
    size_t calls = 0;
    do
    {
        result = package.open(file, 1);
        if (++calls > 10000) return 4;
    } while (retry(result));
    if (result != Result::Ok) return 5;
    auto roads = package.section(SectionType::RoadRulePrograms) ? std::make_unique<EdgeRuleReader>(package, utc, offset) : nullptr;
    auto turns = package.section(SectionType::TurnRulePrograms) ? std::make_unique<TurnContextReader>(package, utc, offset) : nullptr;
    RuntimeRoutingRules binding(utc, offset, roads.get(), turns.get());
    GraphReader graph(package, binding);
    auto prefix_roads = roads ? std::make_unique<EdgeRuleReader>(package, utc, offset) : nullptr;
    auto prefix_turns = turns ? std::make_unique<TurnContextReader>(package, utc, offset) : nullptr;
    RuntimeRoutingRules prefix_binding(utc, offset, prefix_roads.get(), prefix_turns.get());
    GraphReader prefix_graph(package, prefix_binding);
    TerrainReader terrain(package);
    auto admin_space = std::make_unique<AdministrativeWorkspace>();
    auto admin = std::make_unique<AdministrativeCursor>();
    auto render = std::make_unique<ContourRenderCursor>();
    auto labels = std::make_unique<IntegerRouteLabel[]>(128);
    auto queue = std::make_unique<uint32_t[]>(128);
    auto bounds = std::make_unique<uint32_t[]>(128);
    auto output = std::make_unique<uint32_t[]>(128);
    auto segments = std::make_unique<ContourSegment[]>(128);
    auto landmark = std::make_unique<IntegerLandmarkWorkspace>();
    auto route = std::make_unique<IntegerRouteCursor>();
    auto prefix = std::make_unique<IntegerElevationCursor>();
    auto statistics = std::make_unique<IntegerRouteElevationCursor>();
    IntegerRouteElevation totals;
    IntegerElevationPrefix height;
    ElevationSample sample;
    IntegerRouteOptions options;
    options.algorithm = IntegerRouteAlgorithm::Landmarks;
    do
    {
        result = beginIntegerRoute(graph, start, destination, options, {labels.get(), queue.get(), 128, bounds.get(), landmark.get()}, *route);
        if (++calls > 10000) return 6;
    } while (retry(result));
    if (result != Result::Ok) return 7;
    uint32_t half = 0;
    {
        GraphEdge item;
        do
        {
            result = prefix_graph.edge(edge, item);
            if (++calls > 10000) return 8;
        } while (retry(result));
        if (result != Result::Ok) return 9;
        half = item.length_cm / 2;
    }
    if (beginIntegerElevation(prefix_graph, edge, half, *prefix) != Result::Ok ||
        beginAdministrativeQuery(package, location, *admin_space, *admin) != Result::Ok ||
        beginContourRender(package, {location, zoom, 320, 240}, segments.get(), 128, {}, *render) != Result::Ok) return 10;
    Result a = Result::More, c = Result::More, e = Result::More, n = Result::More, z = Result::More;
    size_t rounds = 0;
    while (retry(a) || retry(c) || retry(e) || retry(n) || retry(z))
    {
        if (retry(a)) a = administrativeStep(package, *admin, 1);
        if (retry(c)) c = contourRenderStep(package, *render, 1);
        if (retry(e)) e = integerElevationStep(prefix_graph, *prefix, 1, height);
        if (retry(n)) n = integerRouteStep(graph, *route, 1);
        if (retry(z)) z = terrain.elevation(location, sample);
        if (++rounds > 1000000)
        {
            std::fprintf(stderr, "joint budget: admin=%s contours=%s prefix=%s route=%s elevation=%s stages=%u/%u/%u reads=%zu\n", resultName(a), resultName(c), resultName(e), resultName(n), resultName(z), unsigned(admin->stage), unsigned(prefix->stage), unsigned(route->next_edge), file.reads);
            return 11;
        }
    }
    size_t count = 0;
    if (n == Result::Ok && integerRouteEdges(*route, output.get(), 128, count) != Result::Ok) return 12;
    Result t = n;
    if (n == Result::Ok)
    {
        t = beginIntegerRouteElevation(graph, output.get(), count, *statistics);
        if (t == Result::Ok)
            do
            {
                t = integerRouteElevationStep(graph, *statistics, 1, totals);
                if (++rounds > 1000000) return 11;
            } while (retry(t));
    }
    ResourceOptions resources;
    resources.route_states = resources.route_legs = 128;
    resources.integer_route = resources.integer_landmarks = resources.integer_elevation = true;
    resources.route_elevation = true;
    resources.graph_reader = 2;
    resources.edge_rules = roads ? 2 : 0;
    resources.turn_context = turns ? 2 : 0;
    resources.runtime_binding = 2;
    resources.elevation = resources.administration = true;
    resources.contour_segments = 128;
    ResourceBudget budget;
    if (resourceBudget(resources, budget) != Result::Ok) return 13;
    std::printf("{\"route\":\"%s\",\"admin\":\"%s\",\"contours\":\"%s\",\"prefix\":\"%s\",\"elevation\":\"%s\",\"path\":\"%s\",\"depth\":%u,\"boundary\":%s,\"segments\":%zu,\"interval_m\":%u,\"density_retries\":%u,\"elevation_m\":%.9f,\"prefix_ascent_cm\":%u,\"prefix_descent_cm\":%u,\"prefix_distance_cm\":%u,\"route_cost_units\":%llu,\"rounds\":%zu,\"read_attempts\":%zu,\"transferred_bytes\":%llu,\"library_payload\":%zu,\"workspace\":%zu,\"edges\":[",
                resultName(n), resultName(a), resultName(c), resultName(e), resultName(z), admin->result.path ? admin->result.path : "", unsigned(admin->result.depth), admin->result.boundary ? "true" : "false", render->count, render->interval_m, unsigned(render->density_retries), z == Result::Ok ? sample.meters : 0., height.ascent_cm, height.descent_cm, half, static_cast<unsigned long long>(n == Result::Ok ? labels[route->goal].cost : 0), rounds, file.reads, static_cast<unsigned long long>(file.transferred), budget.total_bytes, sizeof(PackageWorkspace));
    for (size_t i = 0; i < count; ++i) std::printf("%s%u", i ? "," : "", output[i]);
    std::printf("],\"statistics\":\"%s\",\"length_cm\":%llu,\"ascent_cm\":%llu,\"descent_cm\":%llu}\n", resultName(t), static_cast<unsigned long long>(totals.length_cm), static_cast<unsigned long long>(totals.ascent_cm), static_cast<unsigned long long>(totals.descent_cm));
    return (a == Result::Ok || a == Result::Missing) && z == Result::Ok && e == Result::Ok && (c == Result::Ok || c == Result::BudgetExceeded) &&
                   (n != Result::Ok || t == Result::Ok) &&
                   (n == Result::Ok || n == Result::NoPath || n == Result::BudgetExceeded)
               ? 0
               : 14;
}
