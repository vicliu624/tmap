#include "tmap/coordinate_route.h"
#include "tmap/native_file.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
using namespace tmap;
static bool guard = false;
static size_t allocations = 0;
void* operator new(size_t n)
{
    if (guard) ++allocations;
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#define CHECK(x)                                                \
    do                                                          \
    {                                                           \
        if (!(x))                                               \
        {                                                       \
            std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); \
            std::exit(1);                                       \
        }                                                       \
    } while (false)
bool retry(Result r)
{
    return r == Result::More || r == Result::Busy;
}
class File : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0, successful = 0, every = 2;
    uint64_t bytes = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t n) override
    {
        ++calls;
        if (every && calls % every == 0) return Status::Busy;
        ++successful;
        bytes += n;
        return native.readAt(at, output, n);
    }
};
Result route(GraphReader& graph, RouteCursor& c, uint32_t start, uint32_t destination, RouteWorkspace workspace, RouteOptions options = {}, File* measured = nullptr, size_t* max_reads = nullptr)
{
    Result r;
    size_t steps = 0;
    do
    {
        r = beginRoute(graph, start, destination, options, workspace, c);
        CHECK(++steps < 200000);
    } while (retry(r));
    if (r != Result::Ok) return r;
    size_t stalled = 0;
    do
    {
        const auto old_current = c.current, old_edge = c.next_edge, old_expansions = c.expansions;
        const auto old_initialization = c.initialization;
        const auto before = measured ? measured->calls : 0;
        r = routeStep(graph, c, 7);
        if (measured && max_reads) *max_reads = std::max(*max_reads, measured->calls - before);
        if (c.current == old_current && c.next_edge == old_edge && c.expansions == old_expansions && c.initialization == old_initialization) ++stalled;
        else stalled = 0;
        CHECK(stalled < 4096);
        if (steps == 199999) std::fprintf(stderr, "stalled current=%u edge=%u init=%zu\n", c.current, c.next_edge, c.initialization);
        CHECK(++steps < 200000);
    } while (retry(r));
    return r;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    const std::string directory = argv[1];
    auto storage = std::make_unique<PackageWorkspace>();
    auto other_storage = std::make_unique<PackageWorkspace>();
    auto labels = std::make_unique<RouteLabel[]>(4096);
    auto heap = std::make_unique<uint32_t[]>(4096);
    auto output = std::make_unique<uint32_t[]>(4096);
    auto points = std::make_unique<GeoPoint[]>(10000);
    RouteWorkspace workspace{labels.get(), heap.get(), 4096};
    RouteCursor cursor{};
    File file;
    CHECK(file.native.open((directory + "/large.tmap").c_str()));
    OutdoorPackage package(*storage);
    Result r;
    size_t opens = 0;
    do
    {
        r = package.open(file);
        CHECK(++opens < 100);
    } while (retry(r));
    CHECK(r == Result::Ok);
    GraphReader graph(package);
    guard = true;
    size_t max_reads = 0;
    CHECK(route(graph, cursor, 0, 2047, workspace, {}, &file, &max_reads) == Result::Ok);
    size_t count = 0;
    CHECK(routeEdges(cursor, output.get(), 4096, count) == Result::Ok && count == 2047);
    CHECK(cursor.labels_used <= 2048 && cursor.expansions == 2047);
    CHECK(std::abs(cursor.workspace.labels[cursor.goal_label].cost - 2047 * 11.12) < 1e-6);
    for (size_t i = 0; i < count; ++i) CHECK(output[i] == i);
    const auto route_bytes = file.bytes;
    const auto route_reads = file.calls;
    CHECK(route(graph, cursor, 0, 2047, {labels.get(), heap.get(), 128}) == Result::BudgetExceeded);
    CHECK(beginRoute(graph, 0, 2047, {}, workspace, cursor) == Result::Ok);
    CHECK(routeStep(graph, cursor, 4) == Result::More);
    package.close();
    CHECK(routeStep(graph, cursor, 4) == Result::Cancelled);
    for (size_t i = 0; i < 10000; ++i) points[i] = {0, int32_t(i * 100)};
    Navigator navigator;
    NavigationCursor match{};
    NavigationState state{};
    CHECK(navigator.begin(points.get(), 10000) == Result::Ok);
    CHECK(navigator.beginUpdate({0, 777700}, 3, match) == Result::Ok);
    size_t steps = 0;
    do
    {
        const auto before = match.next_segment;
        r = navigator.updateStep(match, 13, state);
        CHECK(match.next_segment - before <= 13);
        CHECK(++steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && state.segment >= 7776 && state.segment <= 7777 && !state.off_route);
    CHECK(allocations == 0);
    guard = false;
    for (const auto* name : {"bad-adjacency.tmap", "bad-points.tmap", "truncated.tmap"})
    {
        CHECK(file.native.open((directory + "/" + name).c_str()));
        file.every = 0;
        r = package.open(file);
        if (r == Result::Ok)
        {
            GraphReader bad(package);
            r = route(bad, cursor, 0, 10, workspace);
        }
        CHECK(r == Result::Invalid);
    }
    auto reference = std::fopen((directory + "/reference.txt").c_str(), "r");
    CHECK(reference);
    char name[128];
    unsigned a, b, mode;
    double penalty, expected;
    size_t cases = 0;
    while (std::fscanf(reference, "%127s %u %u %u %lf %lf", name, &a, &b, &mode, &penalty, &expected) == 6)
    {
        CHECK(file.native.open((directory + "/" + name).c_str()));
        file.every = 2;
        do
        {
            r = package.open(file);
        } while (retry(r));
        CHECK(r == Result::Ok);
        GraphReader random(package);
        r = route(random, cursor, a, b, {labels.get(), heap.get(), 256}, {static_cast<TravelMode>(mode), penalty, 10000});
        if (expected < 0) CHECK(r == Result::NoPath);
        else CHECK(r == Result::Ok && std::abs(cursor.workspace.labels[cursor.goal_label].cost - expected) < 1e-6);
        r = route(random, cursor, a, b, {labels.get(), heap.get(), 256}, {static_cast<TravelMode>(mode), penalty, 10000, RouteAlgorithm::QueueRelaxation});
        if (expected < 0) CHECK(r == Result::NoPath);
        else CHECK(r == Result::Ok && std::abs(cursor.workspace.labels[cursor.goal_label].cost - expected) < 1e-6);
        CHECK(file.native.open((directory + "/alt-" + name).c_str()));
        do
        {
            r = package.open(file);
        } while (retry(r));
        CHECK(r == Result::Ok);
        GraphReader alt(package);
        r = route(alt, cursor, a, b, {labels.get(), heap.get(), 256}, {static_cast<TravelMode>(mode), penalty, 10000, RouteAlgorithm::Landmarks});
        if (expected < 0) CHECK(r == Result::NoPath);
        else CHECK(r == Result::Ok && std::abs(cursor.workspace.labels[cursor.goal_label].cost - expected) < 1e-6);
        ++cases;
    }
    std::fclose(reference);
    CHECK(cases == 24);
    CHECK(file.native.open((directory + "/landmarks.tmap").c_str()));
    file.every = 2;
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    GraphReader accelerated(package);
    guard = true;
    const auto baseline_reads_begin = file.calls;
    const auto baseline_bytes_begin = file.bytes;
    CHECK(route(accelerated, cursor, 0, 63, workspace, {TravelMode::Foot, 0, 10000, RouteAlgorithm::Dijkstra}) == Result::Ok);
    const auto baseline_cost = cursor.workspace.labels[cursor.goal_label].cost;
    const auto baseline_expansions = cursor.expansions, baseline_states = cursor.labels_used;
    const auto baseline_reads = file.calls - baseline_reads_begin;
    const auto baseline_bytes = file.bytes - baseline_bytes_begin;
    package.close();
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    GraphReader cold_alt(package);
    const auto alt_reads_begin = file.calls;
    const auto alt_bytes_begin = file.bytes;
    CHECK(route(cold_alt, cursor, 0, 63, workspace, {TravelMode::Foot, 0, 10000, RouteAlgorithm::Landmarks}) == Result::Ok);
    CHECK(std::abs(cursor.workspace.labels[cursor.goal_label].cost - baseline_cost) < 1e-6);
    CHECK(cursor.expansions < baseline_expansions && cursor.labels_used < baseline_states);
    CHECK(allocations == 0);
    guard = false;
    std::printf("ALT comb: Dijkstra expansions=%u states=%zu; ALT expansions=%u states=%zu; label=%zu cost=%.2f\n",
                baseline_expansions, baseline_states, cursor.expansions, cursor.labels_used, sizeof(RouteLabel), baseline_cost);
    std::printf("ALT cold I/O: Dijkstra attempts=%zu bytes=%llu; ALT attempts=%zu bytes=%llu; planner=%zu\n",
                baseline_reads, static_cast<unsigned long long>(baseline_bytes), file.calls - alt_reads_begin,
                static_cast<unsigned long long>(file.bytes - alt_bytes_begin), sizeof(CoordinateRouteCursor));
    CHECK(route(cold_alt, cursor, 0, 63, {labels.get(), heap.get(), 256}, {TravelMode::Foot, 0, 10000, RouteAlgorithm::Dijkstra}) == Result::BudgetExceeded);
    CHECK(route(cold_alt, cursor, 0, 63, {labels.get(), heap.get(), 256}, {TravelMode::Foot, 0, 10000, RouteAlgorithm::Landmarks}) == Result::Ok);
    CHECK(std::abs(cursor.workspace.labels[cursor.goal_label].cost - baseline_cost) < 1e-6);
    std::printf("ALT capacity: 256 slots succeeds, Dijkstra same capacity exhausts; 256-slot arrays=%zu bytes\n", size_t(256) * (sizeof(RouteLabel) + sizeof(uint32_t)));
    package.close();
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    GraphReader fifo_graph(package);
    const auto fifo_bytes_begin = file.bytes, fifo_reads_begin = uint64_t(file.calls);
    CHECK(route(fifo_graph, cursor, 0, 63, workspace, {TravelMode::Foot, 0, 10000, RouteAlgorithm::QueueRelaxation}) == Result::Ok);
    CHECK(std::abs(cursor.workspace.labels[cursor.goal_label].cost - baseline_cost) < 1e-6);
    std::printf("Queue relaxation: expansions=%u states=%zu attempts=%llu bytes=%llu; indexed heap comparisons=0\n",
                cursor.expansions, cursor.labels_used, static_cast<unsigned long long>(file.calls - fifo_reads_begin), static_cast<unsigned long long>(file.bytes - fifo_bytes_begin));
    File second;
    CHECK(file.native.open((directory + "/region-a.tmap").c_str()));
    CHECK(second.native.open((directory + "/region-b.tmap").c_str()));
    file.every = second.every = 0;
    OutdoorPackage other(*other_storage);
    CHECK(package.open(file) == Result::Ok && other.open(second) == Result::Ok);
    GraphReader region_a(package), region_b(other);
    CHECK(graphCompatibility(region_a, region_b) == Result::Ok);
    other.close();
    CHECK(second.native.open((directory + "/region-incompatible.tmap").c_str()));
    CHECK(other.open(second) == Result::Ok);
    GraphReader incompatible(other);
    CHECK(graphCompatibility(region_a, incompatible) == Result::Incompatible);
    package.close();
    CHECK(file.native.open((directory + "/regions-merged.tmap").c_str()));
    CHECK(package.open(file) == Result::Ok);
    GraphReader merged(package);
    CHECK(route(merged, cursor, 0, 2, workspace) == Result::Ok);
    CHECK(routeEdges(cursor, output.get(), 4096, count) == Result::Ok && count == 2);
    std::printf("PASS large graph, 24 independent reference cases, malformed data, 10000-point bounded matching, region stitching; route_reads=%zu bytes=%llu max_reads_per_7_operations=%zu core_allocations=%zu\n", route_reads, static_cast<unsigned long long>(route_bytes), max_reads, allocations);
}
