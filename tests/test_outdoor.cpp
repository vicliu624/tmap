#include "tmap/coordinate_route.h"
#include "tmap/integer_route.h"
#include "tmap/native_file.h"
#include "tmap/navigation.h"
#include "tmap/routing_profile.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

static bool forbid_allocations = false;
static size_t unexpected_allocations = 0;
void* operator new(std::size_t n)
{
    if (forbid_allocations) ++unexpected_allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#define CHECK(expression)                                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(expression))                                                             \
        {                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            std::exit(1);                                                              \
        }                                                                              \
    } while (false)
using namespace tmap;
class BusyFile : public RandomAccessFile
{
  public:
    NativeFile file;
    size_t calls = 0, every = 0;
    explicit BusyFile(const char* path) : file(path) {}
    uint64_t size() const override { return file.size(); }
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override
    {
        ++calls;
        if (every && calls % every == 0) return Status::Busy;
        return file.readAt(offset, output, bytes);
    }
};
bool retry(Result r) { return r == Result::Busy || r == Result::More; }
struct Vertices
{
    size_t count = 0, starts = 0, ends = 0;
};
bool vertex(void* context, const ContourVertex& v)
{
    auto& n = *static_cast<Vertices*>(context);
    ++n.count;
    n.starts += v.first;
    n.ends += v.last;
    CHECK(v.point.latitude >= 0 && v.point.longitude >= 0);
    return true;
}
struct Samples
{
    size_t count = 0, gaps = 0;
    double first = 0, last = 0;
};
bool profile(void* context, const ProfileSample& s)
{
    auto& n = *static_cast<Samples*>(context);
    if (!n.count) n.first = s.elevation_m;
    ++n.count;
    n.gaps += !s.valid;
    n.last = s.elevation_m;
    return true;
}
bool routePoint(void* context, GeoPoint)
{
    ++*static_cast<size_t*>(context);
    return true;
}
bool maneuver(void* context, const Maneuver& m)
{
    auto& count = *static_cast<size_t*>(context);
    if (!count) CHECK(m.kind == ManeuverKind::Depart);
    ++count;
    return true;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    const std::string directory = argv[1];
    auto workspace = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*workspace);
    RoutingProfile policy{};
    CHECK(readRoutingProfile(package, policy) == Result::Invalid);
    BusyFile timed((directory + "/timed-profile.tmap").c_str());
    CHECK(package.open(timed) == Result::Ok);
    timed.every = 2;
    Result policy_result;
    do
    {
        policy_result = readRoutingProfile(package, policy);
    } while (retry(policy_result));
    CHECK(policy_result == Result::Ok);
    CHECK(policy.conditional_snapshot && policy.snapshot_utc_seconds == 3600 && policy.local_offset_seconds == 3600);
    CHECK(checkRoutingProfile(policy, 3600, 3600, 1) == Result::Ok);
    CHECK(checkRoutingProfile(policy, 3659, 3600, 1) == Result::Ok);
    CHECK(checkRoutingProfile(policy, 3660, 3600, 1) == Result::Incompatible);
    CHECK(checkRoutingProfile(policy, 3600, 0, 1) == Result::Incompatible);
    CHECK(checkRoutingProfile(policy, 3600, 3600, 2) == Result::Incompatible);
    CHECK(checkRoutingProfile(policy, 3600, 3600, 3) == Result::Invalid);
    CHECK(checkRoutingProfile(policy, UINT64_MAX, 3600, 1) == Result::Invalid);
    GraphReader timed_graph(package);
    CHECK(timed_graph.validateRouting(TravelMode::Foot) == Result::Incompatible);
    uint64_t clock_seconds = 3600;
    timed_graph.setRoutingClock([](void* context, uint64_t& seconds, int32_t& offset)
                                {
        seconds = *static_cast<uint64_t*>(context);
        offset = 3600;
        return true; },
                                &clock_seconds);
    do
    {
        policy_result = timed_graph.validateRouting(TravelMode::Foot);
    } while (retry(policy_result));
    CHECK(policy_result == Result::Ok);
    clock_seconds = 3660;
    CHECK(timed_graph.validateRouting(TravelMode::Foot) == Result::Incompatible);
    auto timed_labels = std::make_unique<RouteLabel[]>(16);
    auto timed_queue = std::make_unique<uint32_t[]>(16);
    RouteCursor timed_route{};
    do
    {
        policy_result = beginRoute(timed_graph, 0, 2, {}, {timed_labels.get(), timed_queue.get(), 16}, timed_route);
    } while (retry(policy_result));
    CHECK(policy_result == Result::Incompatible);
    clock_seconds = 3600;
    do
    {
        policy_result = beginRoute(timed_graph, 0, 2, {}, {timed_labels.get(), timed_queue.get(), 16}, timed_route);
    } while (retry(policy_result));
    CHECK(policy_result == Result::Ok);
    clock_seconds = 3660;
    CHECK(routeStep(timed_graph, timed_route, 1) == Result::Incompatible);
    CHECK(routeStep(timed_graph, timed_route, 1) == Result::Incompatible);
    clock_seconds = 3600;
    auto integer_labels = std::make_unique<IntegerRouteLabel[]>(16);
    IntegerRouteCursor integer_cursor{};
    do
    {
        policy_result = beginIntegerRoute(timed_graph, 0, 2, {}, {integer_labels.get(), timed_queue.get(), 16}, integer_cursor);
    } while (retry(policy_result));
    CHECK(policy_result == Result::Ok);
    clock_seconds = 3660;
    CHECK(integerRouteStep(timed_graph, integer_cursor, 1) == Result::Incompatible);
    clock_seconds = 3600;
    CHECK(integerRouteStep(timed_graph, integer_cursor, 1) == Result::Incompatible);
    CoordinateRouteCursor coordinate_cursor{};
    RouteLeg timed_legs[16];
    do
    {
        policy_result = beginCoordinateRoute(timed_graph, {0, 0}, {20000, 20000}, {}, 50,
                                             {timed_labels.get(), timed_queue.get(), 16}, timed_legs, 16, coordinate_cursor);
    } while (retry(policy_result));
    CHECK(policy_result == Result::Ok);
    clock_seconds = 3660;
    CHECK(coordinateRouteStep(timed_graph, coordinate_cursor, 1) == Result::Incompatible);
    clock_seconds = 3600;
    CHECK(coordinateRouteStep(timed_graph, coordinate_cursor, 1) == Result::Incompatible);
    package.close();
    for (int index = 0; index < 7; ++index)
    {
        NativeFile malformed((directory + "/bad-routing-profile-" + std::to_string(index) + ".tmap").c_str());
        CHECK(package.open(malformed) == Result::Ok);
        CHECK(readRoutingProfile(package, policy) == Result::Invalid);
        CHECK(policy.conditional_snapshot && policy.snapshot_utc_seconds == 3600 && policy.local_offset_seconds == 3600 &&
              policy.travel_modes == 1);
        package.close();
    }
    BusyFile file((directory + "/outdoor.tmap").c_str());
    CHECK(package.open(file) == Result::Ok);
    CHECK(readRoutingProfile(package, policy) == Result::Missing);
    CHECK(package.info().capabilities == 7);
    ValidationCursor validation{};
    CHECK(beginValidation(package, validation) == Result::Ok);
    Result verified;
    do
    {
        verified = validationStep(package, validation, 1);
    } while (retry(verified));
    CHECK(verified == Result::Ok);
    CHECK(sizeof(PackageWorkspace) <= sizeof(Workspace));
    TerrainReader terrain(package);
    ElevationSample z{};
    auto cursor = std::make_unique<ContourCursor>();
    forbid_allocations = true;
    CHECK(terrain.elevation({5000, 5000}, z) == Result::Ok);
    CHECK(std::abs(z.meters - 120) < 1e-8);
    CHECK(terrain.elevation({20000, 20000}, z) == Result::Ok && z.meters == 180);
    CHECK(terrain.elevation({-1, 0}, z) == Result::Missing);
    // Allocation of the caller's scratch is intentionally outside the guarded region.
    forbid_allocations = false;
    auto label_storage = std::make_unique<RouteLabel[]>(64);
    auto heap_storage = std::make_unique<uint32_t[]>(64);
    auto snap = std::make_unique<SnapCursor>();
    forbid_allocations = true;
    CHECK(beginContours(package, package.info().bounds, 7, *cursor) == Result::Ok && cursor->complete);
    CHECK(beginContours(package, package.info().bounds, 14, *cursor) == Result::Ok);
    file.every = 3;
    Vertices vertices{};
    Result r;
    size_t iterations = 0;
    do
    {
        r = contourStep(package, *cursor, 3, vertex, &vertices);
        CHECK(++iterations < 10000);
    } while (retry(r));
    CHECK(r == Result::Ok && vertices.count == 204 && vertices.starts == 3 && vertices.ends == 3);
    CHECK(beginContours(package, package.info().bounds, 18, *cursor) == Result::Ok);
    vertices = {};
    iterations = 0;
    do
    {
        r = contourStep(package, *cursor, 1, vertex, &vertices);
        CHECK(++iterations < 10000);
    } while (retry(r));
    CHECK(r == Result::Ok && vertices.count == 206);
    const GeoPoint line[] = {{0, 0}, {20000, 20000}};
    ProfileCursor pc{};
    Samples samples{};
    do
    {
        r = beginProfile(terrain, package, line, 2, {30, 0}, pc);
    } while (retry(r));
    CHECK(r == Result::Ok);
    iterations = 0;
    do
    {
        r = profileStep(terrain, package, pc, 2, profile, &samples);
        CHECK(++iterations < 10000);
    } while (retry(r));
    CHECK(r == Result::Ok && samples.first == 100 && samples.last == 180);
    CHECK(std::abs(pc.metrics.ascent_m - 80) < 1e-8 && pc.metrics.descent_m == 0);
    CHECK(pc.metrics.samples == samples.count);
    GraphReader graph(package);
    RouteCursor rc{};
    do
    {
        r = beginRoute(graph, 0, 2, {}, {label_storage.get(), heap_storage.get(), 64}, rc);
    } while (retry(r));
    CHECK(r == Result::Ok);
    iterations = 0;
    do
    {
        r = routeStep(graph, rc, 1);
        CHECK(++iterations < 10000);
    } while (retry(r));
    CHECK(r == Result::Ok);
    uint32_t edges[8]{};
    size_t count = 0;
    CHECK(routeEdges(rc, edges, 2, count) == Result::BudgetExceeded && count == 3);
    CHECK(routeEdges(rc, edges, 8, count) == Result::Ok && count == 3);
    CHECK(edges[0] == 0 && edges[1] == 2 && edges[2] == 4);
    RouteGeometryCursor gc{};
    do
    {
        r = beginRouteGeometry(graph, edges, count, gc);
    } while (retry(r));
    CHECK(r == Result::Ok);
    size_t points = 0;
    do
    {
        r = routeGeometryStep(graph, gc, 1, routePoint, &points);
    } while (retry(r));
    CHECK(r == Result::Ok && points == 4);
    ManeuverCursor mc{};
    do
    {
        r = beginManeuvers(graph, edges, count, mc);
    } while (retry(r));
    CHECK(r == Result::Ok);
    size_t maneuvers = 0;
    do
    {
        r = maneuverStep(graph, mc, 1, maneuver, &maneuvers);
    } while (retry(r));
    CHECK(r == Result::Ok && maneuvers == 4);
    file.every = 0;
    CHECK(beginRoute(graph, 0, 2, {TravelMode::Bicycle, 0, 100}, {label_storage.get(), heap_storage.get(), 64}, rc) == Result::Ok);
    do
    {
        r = routeStep(graph, rc, 2);
    } while (retry(r));
    CHECK(r == Result::Ok && routeEdges(rc, edges, 8, count) == Result::Ok && count == 2);
    CHECK(beginRoute(graph, 0, 4, {}, {label_storage.get(), heap_storage.get(), 64}, rc) == Result::Ok);
    do
    {
        r = routeStep(graph, rc, 8);
    } while (retry(r));
    CHECK(r == Result::NoPath);
    CHECK(beginRoute(graph, 0, 2, {}, {label_storage.get(), heap_storage.get(), 2}, rc) == Result::Ok);
    do
    {
        r = routeStep(graph, rc, 8);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded);
    CHECK(beginSnap(graph, {100, 5000}, TravelMode::Foot, 50, *snap) == Result::Ok);
    file.every = 3;
    do
    {
        r = snapStep(graph, *snap, 1);
    } while (retry(r));
    CHECK(r == Result::Ok && snap->best.edge == 0 && snap->best.distance_m < 2);
    const GeoPoint navigation_line[] = {{0, 0}, {0, 10000}, {0, 20000}};
    Navigator navigator;
    NavigationState ns{};
    CHECK(navigator.begin(navigation_line, 3) == Result::Ok);
    CHECK(navigator.update({0, 5000}, 3, 1, ns) == Result::BudgetExceeded);
    CHECK(navigator.update({0, 5000}, 3, 2, ns) == Result::Ok && ns.progress_m > 50 && !ns.off_route);
    for (size_t i = 0; i < 3; ++i) CHECK(navigator.update({10000, 5000}, 3, 2, ns) == Result::Ok);
    CHECK(ns.off_route);
    CHECK(navigator.update({0, 20000}, 3, 2, ns) == Result::Ok && ns.arrived && !ns.off_route);
    NavigationCursor nc{};
    CHECK(navigator.beginUpdate({0, 5000}, 3, nc) == Result::Ok);
    CHECK(navigator.updateStep(nc, 1, ns) == Result::More);
    CHECK(navigator.updateStep(nc, 1, ns) == Result::Ok && ns.progress_m > 50);
    NavigationCursor newer{};
    CHECK(navigator.beginUpdate({0, 10000}, 3, nc) == Result::Ok);
    CHECK(navigator.beginUpdate({0, 15000}, 3, newer) == Result::Ok);
    CHECK(navigator.updateStep(nc, 1, ns) == Result::Cancelled);
    ScreenPoint a{-10, 10}, b{110, 10};
    CHECK(clipSegment(100, 100, a, b) && std::abs(a.x) < 1e-9 && std::abs(b.x - 100) < 1e-9);
    a = {-10, -10};
    b = {-5, -5};
    CHECK(!clipSegment(100, 100, a, b));
    ScreenPoint center{};
    CHECK(project({{0, 0}, 18, 320, 240}, {0, 0}, center) && center.x == 160 && center.y == 120);
    CHECK(unexpected_allocations == 0);
    forbid_allocations = false;
    for (const auto* name : {"bad-directory.tmap", "unknown-capability.tmap"})
    {
        NativeFile bad((directory + "/" + name).c_str());
        const auto error = package.open(bad);
        CHECK(error == Result::Invalid || error == Result::Unsupported);
    }
    NativeFile bad_page((directory + "/bad-page.tmap").c_str());
    CHECK(package.open(bad_page) == Result::Ok);
    TerrainReader bad_terrain(package);
    CHECK(bad_terrain.initialize() == Result::Invalid);
    NativeFile missing((directory + "/missing.tmap").c_str());
    CHECK(package.open(missing) == Result::Ok);
    TerrainReader missing_terrain(package);
    CHECK(missing_terrain.elevation({10000, 10000}, z) == Result::Missing);
    CHECK(beginProfile(missing_terrain, package, line, 2, {30, 0}, pc) == Result::Ok);
    samples = {};
    do
    {
        r = profileStep(missing_terrain, package, pc, 4, profile, &samples);
    } while (retry(r));
    CHECK(r == Result::Incomplete && pc.metrics.gaps && samples.gaps);
    CHECK(profileStep(missing_terrain, package, pc, 4, profile, &samples) == Result::Incomplete);
    CHECK(beginContours(package, package.info().bounds, 18, *cursor) == Result::Ok);
    package.close();
    CHECK(contourStep(package, *cursor, 1, vertex, &vertices) == Result::Cancelled);
    auto legacy_storage = std::make_unique<Workspace>();
    Reader legacy(*legacy_storage);
    NativeFile legacy_file((directory + "/legacy.tmap").c_str());
    CHECK(legacy.open(legacy_file) == Status::Ok && legacy.poiCount() == 0);
    std::printf("PASS v1=%zu outdoor=%zu contour_cursor=%zu route_label=%zu core_allocations=%zu\n", sizeof(Workspace), sizeof(PackageWorkspace), sizeof(ContourCursor), sizeof(RouteLabel), unexpected_allocations);
}
