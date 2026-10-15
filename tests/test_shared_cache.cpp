#include "tmap/native_file.h"
#include "tmap/resource_budget.h"
#include <cstdio>
#include <cmath>
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
struct File : RandomAccessFile
{
    NativeFile native;
    size_t calls = 0, index_reads = 0, data_reads = 0;
    uint64_t trigger = UINT64_MAX, blocked = UINT64_MAX;
    bool armed = false, alternating = false;
    std::array<uint32_t, 512> per_page{};
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        ++calls;
        if (at == blocked)
        {
            ++data_reads;
            if (armed)
            {
                armed = false;
                return Status::Busy;
            }
        }
        if (alternating)
        {
            const auto page = at / 1024;
            CHECK(page < per_page.size());
            // Fair per-page retries: unrelated cache pollution cannot consume
            // the successful retry opportunity of the requested physical page.
            if (++per_page[page] % 2) return Status::Busy;
        }
        const auto result = native.readAt(at, output, bytes);
        if (result == Status::Ok && at == trigger)
        {
            armed = true;
            ++index_reads;
        }
        return result;
    }
};
bool retry(Result r) { return r == Result::Busy || r == Result::More; }
void pollute(OutdoorPackage& package)
{
    uint8_t scratch;
    for (auto type : {SectionType::ContourIndex, SectionType::ContourData})
    {
        Result r;
        do
        {
            r = package.read(type, 0, &scratch, 1);
        } while (retry(r));
        CHECK(r == Result::Ok);
    }
}
struct Hash
{
    uint64_t value = 1469598103934665603ULL;
    size_t count = 0;
};
bool visit(void* p, const ContourVertex& v)
{
    auto& out = *static_cast<Hash*>(p);
    CHECK(v.point.latitude == 5000 && v.point.longitude >= 0 && v.point.longitude <= 20000);
    out.value = (out.value ^ uint32_t(v.point.longitude)) * 1099511628211ULL;
    out.value = (out.value ^ (unsigned(v.first) | unsigned(v.last) << 1)) * 1099511628211ULL;
    ++out.count;
    return true;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    auto memory = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*memory);
    File file;
    CHECK(file.native.open((std::string(argv[1]) + "/shared-cache.tmap").c_str()));
    Result r;
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    TerrainReader terrain(package);
    ElevationCursor elevation;
    ElevationSample height;
    CHECK(terrain.beginElevation({5000, 5000}, elevation) == Result::Ok);
    file.trigger = package.section(SectionType::DemIndex)->offset;
    file.blocked = package.section(SectionType::DemData)->offset;
    size_t attempts = 0;
    do
    {
        r = terrain.elevationStep(elevation, 1, height);
        if (retry(r)) pollute(package);
        CHECK(++attempts < 32);
    } while (retry(r));
    CHECK(r == Result::Ok && height.meters == 120 && file.index_reads == 4 && file.data_reads == 8);
    const auto dem_attempts = attempts;
    file.trigger = file.blocked = UINT64_MAX;
    file.armed = false;
    GraphReader graph(package);
    const auto row = package.payloadBytes() / 48;
    const auto* edges = package.section(SectionType::Edges);
    file.trigger = edges->offset + (uint64_t(row) * 48 / package.payloadBytes()) * package.pageBytes();
    file.blocked = file.trigger + package.pageBytes();
    GraphEdge edge;
    attempts = 0;
    do
    {
        r = graph.edge(row, edge);
        if (retry(r)) pollute(package);
        CHECK(++attempts < 16);
    } while (retry(r));
    CHECK(r == Result::Ok && edge.from == row && edge.to == row + 1);
    file.trigger = file.blocked = UINT64_MAX;
    file.armed = false;
    Hash reference, evicted;
    for (auto* hash : {&reference, &evicted})
    {
        file.alternating = hash == &evicted;
        ContourCursor contour;
        CHECK(beginContours(package, package.info().bounds, 16, contour) == Result::Ok);
        attempts = 0;
        do
        {
            r = contourStep(package, contour, 1, visit, hash);
            if (file.alternating && retry(r)) pollute(package);
            CHECK(++attempts < 200000);
        } while (retry(r));
        CHECK(r == Result::Ok && hash->count >= 20001);
    }
    CHECK(reference.count == evicted.count && reference.value == evicted.value);
    auto workspace = std::make_unique<AdministrativeWorkspace>();
    auto admin = std::make_unique<AdministrativeCursor>();
    CHECK(beginAdministrativeQuery(package, {5000, 5000}, *workspace, *admin) == Result::Ok);
    attempts = 0;
    do
    {
        r = administrativeStep(package, *admin, 1);
        if (retry(r)) pollute(package);
        CHECK(++attempts < 30000);
    } while (retry(r));
    CHECK(r == Result::Ok && admin->result.depth == 2);
    const auto admin_rounds = attempts;
    std::array<IntegerRouteLabel, 256> labels{};
    std::array<uint32_t, 256> queue{}, bounds{};
    IntegerLandmarkWorkspace landmark;
    IntegerRouteCursor route;
    IntegerRouteOptions options;
    options.algorithm = IntegerRouteAlgorithm::Landmarks;
    const auto destination = package.payloadBytes() / 64;
    do
    {
        r = beginIntegerRoute(graph, 0, destination, options, {labels.data(), queue.data(), labels.size(), bounds.data(), &landmark}, route);
    } while (retry(r));
    CHECK(r == Result::Ok);
    attempts = 0;
    do
    {
        r = integerRouteStep(graph, route, 1);
        if (retry(r)) pollute(package);
        CHECK(++attempts < 30000);
    } while (retry(r));
    CHECK(r == Result::Ok && landmark.count == 8);
    const auto exact_cost = labels[route.goal].cost;
    std::array<RouteLabel, 256> floating_labels{};
    RouteCursor floating;
    RouteOptions floating_options;
    floating_options.algorithm = RouteAlgorithm::Landmarks;
    do
    {
        r = beginRoute(graph, 0, destination, floating_options, {floating_labels.data(), queue.data(), queue.size()}, floating);
    } while (retry(r));
    CHECK(r == Result::Ok);
    attempts = 0;
    do
    {
        r = routeStep(graph, floating, 1);
        if (retry(r)) pollute(package);
        CHECK(++attempts < 30000);
    } while (retry(r));
    CHECK(r == Result::Ok && floating.landmark_count == 8 && uint64_t(std::llround(floating_labels[floating.goal_label].cost * 100)) == exact_cost);
    Snap reference_snap;
    for (bool evict : {false, true})
    {
        file.alternating = evict;
        auto snap = std::make_unique<SnapCursor>();
        CHECK(beginSnap(graph, {5000, 5000}, TravelMode::Foot, 500, *snap) == Result::Ok);
        attempts = 0;
        do
        {
            r = snapStep(graph, *snap, 1);
            if (evict && retry(r)) pollute(package);
            CHECK(++attempts < 30000);
        } while (retry(r));
        CHECK(r == Result::Ok);
        if (!evict) reference_snap = snap->best;
        else CHECK(snap->best.edge == reference_snap.edge && snap->best.point.latitude == reference_snap.point.latitude && snap->best.point.longitude == reference_snap.point.longitude);
    }
    ResourceOptions resources;
    resources.integer_route = true;
    resources.route_states = resources.route_legs = 8;
    resources.graph_reader = 2;
    resources.edge_rules = 2;
    resources.turn_context = 2;
    resources.runtime_binding = 2;
    ResourceBudget budget;
    CHECK(resourceBudget(resources, budget) == Result::Ok);
    const auto two = budget.total_bytes;
    resources.graph_reader = resources.edge_rules = resources.turn_context = resources.runtime_binding = 1;
    CHECK(resourceBudget(resources, budget) == Result::Ok);
    CHECK(two - budget.total_bytes == sizeof(GraphReader) + sizeof(EdgeRuleReader) + 1 + sizeof(TurnContextReader) + sizeof(bool) + sizeof(RuntimeRoutingRules));
    std::printf("SHARED_CACHE dem_rounds=%zu contour_vertices=%zu admin_rounds=%zu elevation_cursor=%zu terrain_reader=%zu contour_cursor=%zu admin_cursor=%zu graph_reader=%zu rule_reader=%zu alt_landmarks=8 snap_checked=1\n", dem_attempts, evicted.count, admin_rounds, sizeof(ElevationCursor), sizeof(TerrainReader), sizeof(ContourCursor), sizeof(AdministrativeCursor), sizeof(GraphReader), sizeof(EdgeRuleReader));
}
