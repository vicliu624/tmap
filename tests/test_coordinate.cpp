#include "tmap/coordinate_route.h"
#include "tmap/integer_coordinate_route.h"
#include "tmap/integer_elevation.h"
#include "tmap/integer_endpoint.h"
#include "tmap/integer_geometry.h"
#include "tmap/integer_leg_elevation.h"
#include "tmap/integer_maneuvers.h"
#include "tmap/integer_prepared_route.h"
#include "tmap/integer_route.h"
#include "tmap/native_file.h"
#include "tmap/route_elevation.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string>
using namespace tmap;
static bool guard = false;
static size_t allocations = 0;
void* operator new(size_t n)
{
    if (guard) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
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
class File : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0, every = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        if (every && ++calls % every == 0) return Status::Busy;
        return native.readAt(at, output, bytes);
    }
};
bool retry(Result r) { return r == Result::More || r == Result::Busy; }
struct Points
{
    GeoPoint data[32]{};
    size_t count = 0;
};
bool visit(void* context, GeoPoint p)
{
    auto& a = *static_cast<Points*>(context);
    CHECK(a.count < 32);
    a.data[a.count++] = p;
    return true;
}
bool rejectInstruction(void* context, const Maneuver&)
{
    ++*static_cast<size_t*>(context);
    return false;
}
struct Instructions
{
    ManeuverKind kinds[16]{};
    GeoPoint first{}, last{};
    size_t count = 0;
    double meters = 0;
};
bool instruction(void* context, const Maneuver& m)
{
    auto& out = *static_cast<Instructions*>(context);
    if (!out.count)
    {
        CHECK(m.kind == ManeuverKind::Depart);
        out.first = m.point;
    }
    CHECK(out.count < 16);
    out.kinds[out.count++] = m.kind;
    out.last = m.point;
    out.meters = m.distance_m;
    return true;
}
Result run(GraphReader& graph, CoordinateRouteCursor& c, GeoPoint a, GeoPoint b, RouteWorkspace w, RouteLeg* legs, size_t cap, RouteOptions options = {})
{
    Result r;
    size_t steps = 0;
    do
    {
        r = beginCoordinateRoute(graph, a, b, options, 20, w, legs, cap, c);
        CHECK(++steps < 10000);
    } while (retry(r));
    if (r != Result::Ok) return r;
    do
    {
        r = coordinateRouteStep(graph, c, 1);
        if (steps == 9999) std::fprintf(stderr, "stalled stage=%u prepare=%u trial=%u snap_depth=%u init=%zu result=%s\n", c.stage, c.preparing, c.trial, c.snapping.depth, c.searching.initialization, resultName(r));
        CHECK(++steps < 10000);
    } while (retry(r));
    return r;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    std::string directory = argv[1];
    auto workspace = std::make_unique<PackageWorkspace>();
    auto cursor = std::make_unique<CoordinateRouteCursor>();
    auto labels = std::make_unique<RouteLabel[]>(64);
    auto heap = std::make_unique<uint32_t[]>(64);
    auto integer_labels = std::make_unique<IntegerRouteLabel[]>(64);
    auto integer_cursor = std::make_unique<IntegerRouteCursor>();
    auto integer_bounds = std::make_unique<uint32_t[]>(64);
    auto integer_landmarks = std::make_unique<IntegerLandmarkWorkspace>();
    auto endpoint_cursor = std::make_unique<IntegerEndpointCursor>();
    auto direct_cursor = std::make_unique<IntegerDirectCursor>();
    auto prepared_cursor = std::make_unique<IntegerPreparedRouteCursor>();
    auto integer_geometry = std::make_unique<IntegerGeometryCursor>();
    auto integer_leg_elevation = std::make_unique<IntegerLegElevationCursor>();
    auto integer_coordinate = std::make_unique<IntegerCoordinateRouteCursor>();
    auto integer_maneuvers = std::make_unique<IntegerManeuverCursor>();
    RouteWorkspace w{labels.get(), heap.get(), 64};
    RouteLeg legs[16]{};
    File file;
    CHECK(file.native.open((directory + "/chain.tmap").c_str()));
    OutdoorPackage package(*workspace);
    CHECK(package.open(file) == Result::Ok);
    GraphReader graph(package);
    file.every = 2;
    guard = true;
    CHECK(run(graph, *cursor, {0, 2500}, {0, 17500}, w, legs, 16) == Result::Ok);
    CHECK(cursor->count == 2 && legs[0].edge == 0 && legs[1].edge == 1);
    CHECK(std::abs(cursor->cost - distanceMeters({0, 2500}, {0, 17500})) < 0.03);
    LegGeometryCursor geometry{};
    Result r;
    do
    {
        r = beginLegGeometry(graph, legs, cursor->count, geometry);
    } while (retry(r));
    CHECK(r == Result::Ok);
    Points points{};
    size_t steps = 0;
    do
    {
        r = legGeometryStep(graph, geometry, 1, visit, &points);
        CHECK(++steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && points.count == 3 && points.data[0].longitude == 2500 && points.data[2].longitude == 17500);
    ManeuverCursor maneuvers{};
    do
    {
        r = beginLegManeuvers(graph, legs, cursor->count, maneuvers);
    } while (retry(r));
    CHECK(r == Result::Ok);
    Instructions instructions{};
    steps = 0;
    do
    {
        r = maneuverStep(graph, maneuvers, 1, instruction, &instructions);
        CHECK(++steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && instructions.count == 3 && instructions.first.longitude == 2500 && instructions.last.longitude == 17500);
    CHECK(std::abs(instructions.meters - distanceMeters({0, 2500}, {0, 17500})) < 0.03);
    CHECK(run(graph, *cursor, {0, 7500}, {0, 2500}, w, legs, 16) == Result::Ok && cursor->count == 1 && legs[0].edge == 2);
    CHECK(cursor->expansions == 0);
    CHECK(std::abs(cursor->cost - distanceMeters({0, 7500}, {0, 2500})) < 0.03);
    CHECK(run(graph, *cursor, {0, 2500}, {0, 7500}, w, legs, 16, {TravelMode::Foot, 2, 1000}) == Result::Ok && cursor->count == 1);
    CHECK(cursor->cost > distanceMeters({0, 2500}, {0, 7500}));
    CHECK(run(graph, *cursor, {0, 2500}, {0, 17500}, w, legs, 1) == Result::BudgetExceeded);
    CHECK(run(graph, *cursor, {0, 2500}, {0, 17500}, w, legs, 16, {TravelMode::Foot, 2, 1000}) == Result::Ok);
    CHECK(std::abs(cursor->cost - (distanceMeters({0, 2500}, {0, 17500}) + 30)) < 0.05);
    RouteElevationCursor elevation_cursor{};
    RouteElevation totals{-1, -1, -1};
    do
    {
        r = beginRouteElevation(graph, legs, cursor->count, elevation_cursor);
    } while (retry(r));
    CHECK(r == Result::Ok);
    size_t elevation_steps = 0;
    do
    {
        r = routeElevationStep(graph, elevation_cursor, 1, totals);
        CHECK(++elevation_steps < 1000);
        if (retry(r)) CHECK(totals.length_m == -1 && totals.ascent_m == -1 && totals.descent_m == -1);
    } while (retry(r));
    CHECK(r == Result::Ok && std::abs(totals.ascent_m - 15) < 0.02 && totals.descent_m >= 0);
    CHECK(std::abs(totals.length_m - distanceMeters({0, 2500}, {0, 17500})) < 0.03);
    CHECK(routeElevationStep(graph, elevation_cursor, 1, totals) == Result::Ok);
    cancel(elevation_cursor);
    CHECK(routeElevationStep(graph, elevation_cursor, 1, totals) == Result::Cancelled);
    CHECK(beginRouteElevation(graph, nullptr, 0, elevation_cursor) == Result::Ok);
    CHECK(routeElevationStep(graph, elevation_cursor, 1, totals) == Result::Ok && totals.length_m == 0 && totals.ascent_m == 0 && totals.descent_m == 0);
    const uint32_t complete_edges[] = {0, 1};
    IntegerRouteElevationCursor integer_elevation{};
    IntegerRouteElevation integer_totals{UINT64_MAX, UINT64_MAX, UINT64_MAX};
    do
    {
        r = beginIntegerRouteElevation(graph, complete_edges, 2, integer_elevation);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = integerRouteElevationStep(graph, integer_elevation, 1, integer_totals);
        if (retry(r)) CHECK(integer_totals.length_cm == UINT64_MAX && integer_totals.ascent_cm == UINT64_MAX);
    } while (retry(r));
    CHECK(r == Result::Ok && integer_totals.ascent_cm == 2000 && integer_totals.descent_cm == 0);
    CHECK(beginIntegerRouteElevation(graph, complete_edges, 2, integer_elevation) == Result::Ok);
    integer_elevation.total.length_cm = UINT64_MAX;
    do
    {
        r = integerRouteElevationStep(graph, integer_elevation, 1, integer_totals);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded && integer_totals.ascent_cm == 2000);
    cancel(integer_elevation);
    CHECK(integerRouteElevationStep(graph, integer_elevation, 1, integer_totals) == Result::Cancelled);
    double prefix = 0;
    do
    {
        r = graph.ascentPrefix(0, 0.5, prefix);
    } while (retry(r));
    CHECK(r == Result::Ok && prefix > 3 && prefix < 6);
    const auto expected_ascent = prefix;
    double descent = -1;
    do
    {
        r = graph.descentPrefix(0, 0.5, descent);
    } while (retry(r));
    CHECK(r == Result::Ok && descent >= 0);
    do
    {
        r = graph.ascentPrefix(0, 0.5, prefix);
    } while (retry(r));
    CHECK(r == Result::Ok && prefix == expected_ascent);
    GraphEdge elevation_edge{};
    do
    {
        r = graph.edge(0, elevation_edge);
    } while (retry(r));
    CHECK(r == Result::Ok && elevation_edge.has_elevation);
    Snap endpoint_snap{};
    endpoint_snap.edge = 0;
    endpoint_snap.along_fraction = 0.25;
    IntegerEndpointCandidates endpoints{};
    endpoints.count = 99;
    CHECK(beginIntegerEndpoint(graph, endpoint_snap, true, TravelMode::Foot, {3, 2}, *endpoint_cursor) == Result::Ok);
    size_t endpoint_steps = 0;
    do
    {
        r = integerEndpointStep(graph, *endpoint_cursor, 1, endpoints);
        CHECK(++endpoint_steps < 1000);
        if (retry(r)) CHECK(endpoints.count == 99);
    } while (retry(r));
    CHECK(r == Result::Ok && endpoints.count == 2 && endpoints.choices[0].node == 1 && endpoints.choices[1].node == 0);
    CHECK(endpoints.choices[0].partial && endpoints.choices[0].boundary_edge == 0);
    CHECK(endpoints.choices[0].leg.first_cm + endpoints.choices[1].leg.first_cm == elevation_edge.length_cm);
    const auto first_distance = endpoints.choices[0].leg.first_cm;
    const auto start_endpoints = endpoints;
    CHECK(beginIntegerEndpoint(graph, endpoint_snap, false, TravelMode::Foot, {0, 2}, *endpoint_cursor) == Result::Ok);
    do
    {
        r = integerEndpointStep(graph, *endpoint_cursor, 1, endpoints);
        CHECK(++endpoint_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && endpoints.choices[0].cost_units == uint64_t(first_distance) * 2);
    endpoint_snap.along_fraction = 0.75;
    CHECK(beginIntegerEndpoint(graph, endpoint_snap, false, TravelMode::Foot, {3, 2}, *endpoint_cursor) == Result::Ok);
    do
    {
        r = integerEndpointStep(graph, *endpoint_cursor, 1, endpoints);
        CHECK(++endpoint_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok);
    IntegerDirectRoute direct_route{};
    direct_route.cost_units = UINT64_MAX;
    CHECK(beginIntegerDirectRoute(graph, start_endpoints, endpoints, TravelMode::Foot, {3, 2}, *direct_cursor) == Result::Ok);
    do
    {
        r = integerDirectRouteStep(graph, *direct_cursor, 1, direct_route);
        CHECK(++endpoint_steps < 1000);
        if (retry(r)) CHECK(direct_route.cost_units == UINT64_MAX);
    } while (retry(r));
    CHECK(r == Result::Ok && direct_route.leg.edge == 0 && direct_route.leg.first_cm == first_distance && direct_route.leg.last_cm > first_distance);
    const auto expected_direct_cost = direct_route.cost_units;
    const auto same_road_ends = endpoints;
    IntegerRouteLeg prepared_legs[16]{};
    IntegerRouteOptions prepared_options{{3, 2}, TravelMode::Foot, 1000, IntegerRouteAlgorithm::HeapDijkstra};
    CHECK(beginIntegerPreparedRoute(graph, start_endpoints, endpoints, prepared_options, {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *prepared_cursor) == Result::Ok);
    const IntegerRouteLeg* ready_legs = nullptr;
    size_t ready_count = 99;
    uint64_t ready_cost = 99;
    do
    {
        r = integerPreparedRouteStep(graph, *prepared_cursor, 1);
        CHECK(++endpoint_steps < 2000);
        if (retry(r)) CHECK(integerPreparedRouteResult(*prepared_cursor, ready_legs, ready_count, ready_cost) == Result::More && !ready_legs && ready_count == 0 && ready_cost == 0);
    } while (retry(r));
    CHECK(r == Result::Ok && integerPreparedRouteResult(*prepared_cursor, ready_legs, ready_count, ready_cost) == Result::Ok);
    CHECK(ready_count == 1 && ready_cost == expected_direct_cost && ready_legs[0].edge == 0);
    IntegerRouteElevation clipped_totals{UINT64_MAX, UINT64_MAX, UINT64_MAX};
    CHECK(beginIntegerLegElevation(graph, ready_legs, ready_count, *integer_leg_elevation) == Result::Ok);
    do
    {
        r = integerLegElevationStep(graph, *integer_leg_elevation, 1, clipped_totals);
        CHECK(++endpoint_steps < 2500);
        if (retry(r)) CHECK(clipped_totals.length_cm == UINT64_MAX && clipped_totals.ascent_cm == UINT64_MAX);
    } while (retry(r));
    CHECK(r == Result::Ok && clipped_totals.length_cm == ready_legs[0].last_cm - ready_legs[0].first_cm);
    CHECK(2 * clipped_totals.length_cm + 3 * clipped_totals.ascent_cm == ready_cost);
    cancel(*integer_leg_elevation);
    clipped_totals = {UINT64_MAX, UINT64_MAX, UINT64_MAX};
    CHECK(integerLegElevationStep(graph, *integer_leg_elevation, 1, clipped_totals) == Result::Cancelled && clipped_totals.length_cm == UINT64_MAX);
    CHECK(beginIntegerLegElevation(graph, nullptr, 0, *integer_leg_elevation) == Result::Ok);
    CHECK(integerLegElevationStep(graph, *integer_leg_elevation, 1, clipped_totals) == Result::Ok && clipped_totals.length_cm == 0 && clipped_totals.ascent_cm == 0 && clipped_totals.descent_cm == 0);
    Points integer_points{};
    CHECK(beginIntegerGeometry(graph, ready_legs, ready_count, *integer_geometry) == Result::Ok);
    do
    {
        r = integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points);
        CHECK(++endpoint_steps < 2500);
    } while (retry(r));
    CHECK(r == Result::Ok && integer_points.count >= 2);
    Points reference_points{};
    LegGeometryCursor reference_geometry{};
    const RouteLeg reference_leg{0, 0, UINT32_MAX, 0.25f, 0.75f};
    CHECK(beginLegGeometry(graph, &reference_leg, 1, reference_geometry) == Result::Ok);
    do
    {
        r = legGeometryStep(graph, reference_geometry, 1, visit, &reference_points);
    } while (retry(r));
    CHECK(r == Result::Ok && reference_points.count == integer_points.count);
    for (size_t i = 0; i < integer_points.count; ++i)
        CHECK(distanceMeters(integer_points.data[i], reference_points.data[i]) < 0.02);
    cancel(*integer_geometry);
    CHECK(integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points) == Result::Cancelled);
    IntegerRouteLeg reverse_geometry_leg{2, ready_legs[0].first_cm, ready_legs[0].last_cm};
    integer_points.count = 0;
    CHECK(beginIntegerGeometry(graph, &reverse_geometry_leg, 1, *integer_geometry) == Result::Ok);
    do
    {
        r = integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points);
        CHECK(++endpoint_steps < 3000);
    } while (retry(r));
    CHECK(r == Result::Ok && integer_points.count == reference_points.count);
    for (size_t i = 0; i < integer_points.count; ++i)
        CHECK(distanceMeters(integer_points.data[i], reference_points.data[reference_points.count - i - 1]) < 0.02);
    reverse_geometry_leg.first_cm = UINT32_MAX;
    CHECK(beginIntegerGeometry(graph, &reverse_geometry_leg, 1, *integer_geometry) == Result::Ok);
    do
    {
        r = integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points);
    } while (retry(r));
    CHECK(r == Result::Invalid && integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points) == Result::Invalid);
    CHECK(beginIntegerGeometry(graph, nullptr, 0, *integer_geometry) == Result::Ok);
    CHECK(integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points) == Result::Ok);
    auto wrong_coefficient = prepared_options;
    wrong_coefficient.ascent.numerator = 4;
    CHECK(beginIntegerPreparedRoute(graph, start_endpoints, endpoints, wrong_coefficient, {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *prepared_cursor) == Result::Incompatible);
    cancel(*prepared_cursor);
    CHECK(integerPreparedRouteStep(graph, *prepared_cursor, 1) == Result::Cancelled);
    endpoint_snap.edge = 1;
    endpoint_snap.along_fraction = 0.75;
    CHECK(beginIntegerEndpoint(graph, endpoint_snap, false, TravelMode::Foot, {3, 2}, *endpoint_cursor) == Result::Ok);
    do
    {
        r = integerEndpointStep(graph, *endpoint_cursor, 1, endpoints);
        CHECK(++endpoint_steps < 3000);
    } while (retry(r));
    CHECK(r == Result::Ok);
    CHECK(beginIntegerPreparedRoute(graph, start_endpoints, endpoints, prepared_options, {integer_labels.get(), heap.get(), 64}, prepared_legs, 2, *prepared_cursor) == Result::Ok);
    do
    {
        r = integerPreparedRouteStep(graph, *prepared_cursor, 1);
        CHECK(++endpoint_steps < 5000);
    } while (retry(r));
    CHECK(r == Result::Ok && integerPreparedRouteResult(*prepared_cursor, ready_legs, ready_count, ready_cost) == Result::Ok);
    CHECK(ready_count == 2 && ready_legs[0].edge == 0 && ready_legs[1].edge == 1);
    CHECK(ready_cost == start_endpoints.choices[0].cost_units + endpoints.choices[0].cost_units);
    for (const auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra})
    {
        prepared_options.algorithm = algorithm;
        for (const auto reverse : {false, true})
        {
            size_t coordinate_work = 0;
            do
            {
                r = beginIntegerCoordinateRoute(graph, reverse ? GeoPoint{0, 17500} : GeoPoint{0, 2500}, reverse ? GeoPoint{0, 2500} : GeoPoint{0, 17500}, prepared_options, 20, {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *integer_coordinate);
                CHECK(++coordinate_work < 10000);
            } while (retry(r));
            CHECK(r == Result::Ok);
            do
            {
                r = integerCoordinateRouteStep(graph, *integer_coordinate, 1);
                CHECK(++coordinate_work < 10000);
                if (retry(r)) CHECK(integerCoordinateRouteResult(*integer_coordinate, ready_legs, ready_count, ready_cost) == Result::More && !ready_legs && !ready_count && !ready_cost);
            } while (retry(r));
            CHECK(r == Result::Ok && integerCoordinateRouteResult(*integer_coordinate, ready_legs, ready_count, ready_cost) == Result::Ok && ready_count == 2);
            CHECK(beginIntegerLegElevation(graph, ready_legs, ready_count, *integer_leg_elevation) == Result::Ok);
            do
            {
                r = integerLegElevationStep(graph, *integer_leg_elevation, 1, clipped_totals);
                CHECK(++coordinate_work < 10000);
            } while (retry(r));
            CHECK(r == Result::Ok && ready_cost == 2 * clipped_totals.length_cm + 3 * clipped_totals.ascent_cm);
            integer_points.count = 0;
            CHECK(beginIntegerGeometry(graph, ready_legs, ready_count, *integer_geometry) == Result::Ok);
            do
            {
                r = integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points);
                CHECK(++coordinate_work < 10000);
            } while (retry(r));
            CHECK(r == Result::Ok && integer_points.count == 3);
            CHECK(std::abs(integer_points.data[0].longitude - (reverse ? 17500 : 2500)) <= 1);
            CHECK(std::abs(integer_points.data[2].longitude - (reverse ? 2500 : 17500)) <= 1);
            Instructions integer_instructions{};
            CHECK(beginIntegerManeuvers(graph, ready_legs, ready_count, *integer_maneuvers) == Result::Ok);
            do
            {
                r = integerManeuverStep(graph, *integer_maneuvers, 1, instruction, &integer_instructions);
                CHECK(++coordinate_work < 10000);
            } while (retry(r));
            CHECK(r == Result::Ok && integer_instructions.count == ready_count + 1);
            CHECK(integer_instructions.kinds[1] == ManeuverKind::Continue && integer_instructions.kinds[2] == ManeuverKind::Arrive);
            CHECK(distanceMeters(integer_instructions.first, integer_points.data[0]) < 0.01 && distanceMeters(integer_instructions.last, integer_points.data[2]) < 0.01);
            CHECK(std::abs(integer_instructions.meters - double(clipped_totals.length_cm) / 100) < 1e-9);
            const auto delivered = integer_instructions.count;
            CHECK(integerManeuverStep(graph, *integer_maneuvers, 1, instruction, &integer_instructions) == Result::Ok && integer_instructions.count == delivered);
        }
    }
    cancel(*integer_coordinate);
    cancel(*integer_maneuvers);
    CHECK(integerManeuverStep(graph, *integer_maneuvers, 1, instruction, &instructions) == Result::Cancelled);
    CHECK(beginIntegerManeuvers(graph, nullptr, 0, *integer_maneuvers) == Result::Ok);
    CHECK(integerManeuverStep(graph, *integer_maneuvers, 1, instruction, &instructions) == Result::Ok);
    size_t rejected_events = 0;
    const IntegerRouteLeg reject_leg{0, 0, UINT32_MAX};
    CHECK(beginIntegerManeuvers(graph, &reject_leg, 1, *integer_maneuvers) == Result::Ok);
    size_t rejection_work = 0;
    do
    {
        r = integerManeuverStep(graph, *integer_maneuvers, 1, rejectInstruction, &rejected_events);
        CHECK(++rejection_work < 1000);
    } while (retry(r));
    CHECK(r == Result::Cancelled && rejected_events == 1);
    CHECK(integerManeuverStep(graph, *integer_maneuvers, 1, rejectInstruction, &rejected_events) == Result::Cancelled && rejected_events == 1);
    CHECK(integerCoordinateRouteStep(graph, *integer_coordinate, 1) == Result::Cancelled);
    CHECK(integerCoordinateRouteResult(*integer_coordinate, ready_legs, ready_count, ready_cost) == Result::Cancelled && !ready_legs && !ready_count);
    CHECK(beginIntegerCoordinateRoute(graph, {900000001, 0}, {0, 0}, prepared_options, 20, {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *integer_coordinate) == Result::Invalid);
    CHECK(beginIntegerCoordinateRoute(graph, {0, 0}, {0, 0}, prepared_options, std::numeric_limits<double>::quiet_NaN(), {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *integer_coordinate) == Result::Invalid);
    auto bad_coordinate_options = prepared_options;
    bad_coordinate_options.ascent.denominator = 0;
    CHECK(beginIntegerCoordinateRoute(graph, {0, 0}, {0, 0}, bad_coordinate_options, 20, {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *integer_coordinate) == Result::Invalid);
    size_t restarted_steps = 0;
    do
    {
        r = beginIntegerCoordinateRoute(graph, {0, 2500}, {0, 17500}, prepared_options, 20, {integer_labels.get(), heap.get(), 64}, prepared_legs, 1, *integer_coordinate);
        CHECK(++restarted_steps < 10000);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = integerCoordinateRouteStep(graph, *integer_coordinate, 1);
        CHECK(++restarted_steps < 10000);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded && integerCoordinateRouteResult(*integer_coordinate, ready_legs, ready_count, ready_cost) == Result::BudgetExceeded && !ready_legs && !ready_count);
    // Restore the original forward prepared route used by the following tests.
    CHECK(beginIntegerPreparedRoute(graph, start_endpoints, endpoints, prepared_options, {integer_labels.get(), heap.get(), 64}, prepared_legs, 2, *prepared_cursor) == Result::Ok);
    do
    {
        r = integerPreparedRouteStep(graph, *prepared_cursor, 1);
    } while (retry(r));
    CHECK(r == Result::Ok && integerPreparedRouteResult(*prepared_cursor, ready_legs, ready_count, ready_cost) == Result::Ok);
    clipped_totals = {UINT64_MAX, UINT64_MAX, UINT64_MAX};
    CHECK(beginIntegerLegElevation(graph, ready_legs, ready_count, *integer_leg_elevation) == Result::Ok);
    do
    {
        r = integerLegElevationStep(graph, *integer_leg_elevation, 1, clipped_totals);
        CHECK(++endpoint_steps < 5500);
    } while (retry(r));
    CHECK(r == Result::Ok && 2 * clipped_totals.length_cm + 3 * clipped_totals.ascent_cm == ready_cost);
    IntegerRouteLeg whole_leg{0, 0, UINT32_MAX};
    CHECK(beginIntegerLegElevation(graph, &whole_leg, 1, *integer_leg_elevation) == Result::Ok);
    do
    {
        r = integerLegElevationStep(graph, *integer_leg_elevation, 1, clipped_totals);
        CHECK(++endpoint_steps < 5500);
    } while (retry(r));
    CHECK(r == Result::Ok && clipped_totals.length_cm == elevation_edge.length_cm && clipped_totals.ascent_cm == elevation_edge.ascent_cm && clipped_totals.descent_cm == elevation_edge.descent_cm);
    whole_leg.first_cm = elevation_edge.length_cm + 1;
    clipped_totals = {UINT64_MAX, UINT64_MAX, UINT64_MAX};
    CHECK(beginIntegerLegElevation(graph, &whole_leg, 1, *integer_leg_elevation) == Result::Ok);
    do
    {
        r = integerLegElevationStep(graph, *integer_leg_elevation, 1, clipped_totals);
    } while (retry(r));
    CHECK(r == Result::Invalid && clipped_totals.length_cm == UINT64_MAX);
    integer_points.count = 0;
    CHECK(beginIntegerGeometry(graph, ready_legs, ready_count, *integer_geometry) == Result::Ok);
    do
    {
        r = integerGeometryStep(graph, *integer_geometry, 1, visit, &integer_points);
        CHECK(++endpoint_steps < 5500);
    } while (retry(r));
    CHECK(r == Result::Ok && integer_points.count == 3 && integer_points.data[1].longitude == 10000);
    CHECK(beginIntegerPreparedRoute(graph, start_endpoints, endpoints, prepared_options, {integer_labels.get(), heap.get(), 64}, prepared_legs, 1, *prepared_cursor) == Result::Ok);
    do
    {
        r = integerPreparedRouteStep(graph, *prepared_cursor, 1);
        CHECK(++endpoint_steps < 6000);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded && integerPreparedRouteResult(*prepared_cursor, ready_legs, ready_count, ready_cost) == Result::BudgetExceeded && !ready_legs && ready_count == 0);
    auto bounded_prepared = prepared_options;
    bounded_prepared.max_expansions = 1;
    CHECK(beginIntegerPreparedRoute(graph, start_endpoints, endpoints, bounded_prepared, {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *prepared_cursor) == Result::Ok);
    do
    {
        r = integerPreparedRouteStep(graph, *prepared_cursor, 1);
        CHECK(++endpoint_steps < 7000);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded && prepared_cursor->expansions <= 1);
    CHECK(beginIntegerDirectRoute(graph, same_road_ends, start_endpoints, TravelMode::Foot, {0, 1}, *direct_cursor) == Result::Ok);
    do
    {
        r = integerDirectRouteStep(graph, *direct_cursor, 1, direct_route);
        CHECK(++endpoint_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && direct_route.leg.edge == elevation_edge.opposite);
    CHECK(direct_route.cost_units == direct_route.leg.last_cm - direct_route.leg.first_cm);
    cancel(*direct_cursor);
    CHECK(integerDirectRouteStep(graph, *direct_cursor, 1, direct_route) == Result::Cancelled);
    endpoint_snap.along_fraction = 0;
    CHECK(beginIntegerEndpoint(graph, endpoint_snap, true, TravelMode::Foot, {3, 2}, *endpoint_cursor) == Result::Ok);
    do
    {
        r = integerEndpointStep(graph, *endpoint_cursor, 1, endpoints);
        CHECK(++endpoint_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && endpoints.count == 1 && !endpoints.choices[0].partial && endpoints.choices[0].cost_units == 0);
    cancel(*endpoint_cursor);
    CHECK(integerEndpointStep(graph, *endpoint_cursor, 1, endpoints) == Result::Cancelled);
    IntegerElevationCursor integer_prefix_cursor{};
    IntegerElevationPrefix integer_prefix{UINT32_MAX, UINT32_MAX};
    size_t prefix_steps = 0;
    do
    {
        r = beginIntegerElevation(graph, 0, elevation_edge.length_cm / 2, integer_prefix_cursor);
        CHECK(++prefix_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = integerElevationStep(graph, integer_prefix_cursor, 1, integer_prefix);
        CHECK(++prefix_steps < 1000);
        if (retry(r)) CHECK(integer_prefix.ascent_cm == UINT32_MAX && integer_prefix.descent_cm == UINT32_MAX);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = graph.ascentPrefix(0, double(elevation_edge.length_cm / 2) / elevation_edge.length_cm, prefix);
    } while (retry(r));
    CHECK(r == Result::Ok && std::abs(integer_prefix.ascent_cm - prefix * 100) <= 0.5);
    cancel(integer_prefix_cursor);
    CHECK(integerElevationStep(graph, integer_prefix_cursor, 1, integer_prefix) == Result::Cancelled);
    IntegerEdgeIntervalCursor interval_cursor{};
    IntegerEdgeInterval interval{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT64_MAX};
    CHECK(beginIntegerEdgeInterval(graph, 0, elevation_edge.length_cm / 4, elevation_edge.length_cm * 3 / 4, {3, 2}, interval_cursor) == Result::Ok);
    prefix_steps = 0;
    do
    {
        r = integerEdgeIntervalStep(graph, interval_cursor, 1, interval);
        CHECK(++prefix_steps < 1000);
        if (retry(r)) CHECK(interval.length_cm == UINT32_MAX && interval.cost_units == UINT64_MAX);
    } while (retry(r));
    CHECK(r == Result::Ok && interval.cost_units == uint64_t(interval.length_cm) * 2 + uint64_t(interval.ascent_cm) * 3);
    CHECK(interval.length_cm == elevation_edge.length_cm * 3 / 4 - elevation_edge.length_cm / 4);
    CHECK(beginIntegerEdgeInterval(graph, 0, 9, 8, {3, 2}, interval_cursor) == Result::Invalid);
    CHECK(beginIntegerEdgeInterval(graph, 0, 8, 9, {3, 0}, interval_cursor) == Result::Invalid);
    CHECK(beginIntegerEdgeInterval(graph, 0, 8, 8, {3, 2}, interval_cursor) == Result::Ok);
    do
    {
        r = integerEdgeIntervalStep(graph, interval_cursor, 1, interval);
        CHECK(++prefix_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && interval.length_cm == 0 && interval.cost_units == 0);
    cancel(interval_cursor);
    CHECK(integerEdgeIntervalStep(graph, interval_cursor, 1, interval) == Result::Cancelled);
    do
    {
        r = graph.descentPrefix(0, 1, descent);
    } while (retry(r));
    CHECK(r == Result::Ok && descent == elevation_edge.descent_cm / 100.0);
    do
    {
        r = graph.descentPrefix(0, 0, descent);
    } while (retry(r));
    CHECK(r == Result::Ok && descent == 0);
    descent = -1;
    CHECK(graph.descentPrefix(0, -0.1, descent) == Result::Invalid && descent == -1);
    CHECK(allocations == 0);
    const GeoPoint already_here[] = {{0, 2500}};
    Navigator navigator;
    NavigationState state{};
    CHECK(navigator.begin(already_here, 1) == Result::Ok);
    CHECK(navigator.update({0, 2500}, 2, 1, state) == Result::Ok && state.arrived && state.remaining_m == 0);
    guard = false;
    package.close();
    CHECK(file.native.open((directory + "/one-way.tmap").c_str()));
    file.every = 0;
    CHECK(package.open(file) == Result::Ok);
    GraphReader one_way(package);
    endpoint_snap.edge = 0;
    endpoint_snap.along_fraction = 0.5;
    CHECK(beginIntegerEndpoint(one_way, endpoint_snap, true, TravelMode::Foot, {0, 1}, *endpoint_cursor) == Result::Ok);
    do
    {
        r = integerEndpointStep(one_way, *endpoint_cursor, 1, endpoints);
        CHECK(++endpoint_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok && endpoints.count == 1 && endpoints.choices[0].node == 1);
    const auto one_way_starts = endpoints;
    endpoint_snap.along_fraction = 0.25;
    CHECK(beginIntegerEndpoint(one_way, endpoint_snap, false, TravelMode::Foot, {0, 1}, *endpoint_cursor) == Result::Ok);
    do
    {
        r = integerEndpointStep(one_way, *endpoint_cursor, 1, endpoints);
        CHECK(++endpoint_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::Ok);
    direct_route.cost_units = UINT64_MAX;
    CHECK(beginIntegerDirectRoute(one_way, one_way_starts, endpoints, TravelMode::Foot, {0, 1}, *direct_cursor) == Result::Ok);
    do
    {
        r = integerDirectRouteStep(one_way, *direct_cursor, 1, direct_route);
        CHECK(++endpoint_steps < 1000);
    } while (retry(r));
    CHECK(r == Result::NoPath && direct_route.cost_units == UINT64_MAX);
    CHECK(run(one_way, *cursor, {0, 7500}, {0, 2500}, w, legs, 16) == Result::NoPath);
    package.close();
    CHECK(file.native.open((directory + "/endpoint-restricted-alt.tmap").c_str()));
    CHECK(package.open(file) == Result::Ok);
    GraphReader restricted(package);
    file.every = 2;
    for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
    {
        auto run_boundary = [&](uint32_t start, uint32_t destination, uint32_t initial, uint32_t final, TravelMode mode)
        {
            size_t boundary_steps = 0;
            Result result;
            do
            {
                result = beginIntegerRouteWithBoundaries(restricted, start, destination, initial, final, {{0, 1}, mode, 1000, algorithm},
                                                         {integer_labels.get(), heap.get(), 64, integer_bounds.get(), integer_landmarks.get()}, *integer_cursor);
                CHECK(++boundary_steps < 1000);
            } while (retry(result));
            if (result != Result::Ok) return result;
            do
            {
                result = integerRouteStep(restricted, *integer_cursor, 1);
                CHECK(++boundary_steps < 1000);
            } while (retry(result));
            return result;
        };
        CHECK(run_boundary(1, 2, UINT32_MAX, UINT32_MAX, TravelMode::Foot) == Result::Ok);
        CHECK(run_boundary(1, 2, 0, UINT32_MAX, TravelMode::Foot) == Result::NoPath);
        CHECK(run_boundary(0, 1, UINT32_MAX, 1, TravelMode::Foot) == Result::NoPath);
        CHECK(run_boundary(1, 1, 0, 1, TravelMode::Foot) == Result::NoPath);
        CHECK(run_boundary(1, 1, 0, 1, TravelMode::Bicycle) == Result::Ok);
        size_t middle_edges = 99;
        CHECK(integerRouteEdges(*integer_cursor, nullptr, 0, middle_edges) == Result::Ok && middle_edges == 0);
        CHECK(run_boundary(0, 1, 1, UINT32_MAX, TravelMode::Foot) == Result::Invalid);
    }
    file.every = 0;
    CHECK(run(restricted, *cursor, {0, 2500}, {0, 17500}, w, legs, 16) == Result::NoPath);
    CHECK(run(restricted, *cursor, {0, 2500}, {0, 17500}, w, legs, 16, {TravelMode::Bicycle, 0, 1000}) == Result::Ok);
    package.close();
    CHECK(file.native.open((directory + "/unknown-edge-height.tmap").c_str()));
    CHECK(package.open(file) == Result::Ok);
    GraphReader unknown(package);
    CHECK(run(unknown, *cursor, {0, 2500}, {0, 17500}, w, legs, 16, {TravelMode::Foot, 1, 1000}) == Result::Incomplete);
    CHECK(run(unknown, *cursor, {0, 2500}, {0, 17500}, w, legs, 16) == Result::Ok);
    totals = {-1, -1, -1};
    CHECK(beginRouteElevation(unknown, legs, cursor->count, elevation_cursor) == Result::Ok);
    do
    {
        r = routeElevationStep(unknown, elevation_cursor, 1, totals);
    } while (retry(r));
    CHECK(r == Result::Incomplete && totals.length_m == -1 && totals.ascent_m == -1 && totals.descent_m == -1);
    CHECK(routeElevationStep(unknown, elevation_cursor, 1, totals) == Result::Incomplete);
    IntegerRouteLeg unknown_leg{0, 0, UINT32_MAX};
    clipped_totals = {UINT64_MAX, UINT64_MAX, UINT64_MAX};
    CHECK(beginIntegerLegElevation(unknown, &unknown_leg, 1, *integer_leg_elevation) == Result::Ok);
    do
    {
        r = integerLegElevationStep(unknown, *integer_leg_elevation, 1, clipped_totals);
    } while (retry(r));
    CHECK(r == Result::Incomplete && clipped_totals.length_cm == UINT64_MAX);
    CHECK(integerLegElevationStep(unknown, *integer_leg_elevation, 1, clipped_totals) == Result::Incomplete);
    unknown_leg.last_cm = 0;
    CHECK(beginIntegerLegElevation(unknown, &unknown_leg, 1, *integer_leg_elevation) == Result::Ok);
    do
    {
        r = integerLegElevationStep(unknown, *integer_leg_elevation, 1, clipped_totals);
    } while (retry(r));
    CHECK(r == Result::Ok && clipped_totals.length_cm == 0 && clipped_totals.ascent_cm == 0);
    RouteLeg invalid_leg = legs[0];
    invalid_leg.first_fraction = std::numeric_limits<float>::quiet_NaN();
    CHECK(beginRouteElevation(unknown, &invalid_leg, 1, elevation_cursor) == Result::Ok);
    CHECK(routeElevationStep(unknown, elevation_cursor, 1, totals) == Result::Invalid);
    CHECK(totals.length_m == -1 && totals.ascent_m == -1 && totals.descent_m == -1);
    CHECK(beginRouteElevation(unknown, legs, cursor->count, elevation_cursor) == Result::Ok);
    package.close();
    CHECK(routeElevationStep(unknown, elevation_cursor, 1, totals) == Result::Cancelled);
    CHECK(totals.length_m == -1 && totals.ascent_m == -1 && totals.descent_m == -1);
    CHECK(file.native.open((directory + "/boundary.tmap").c_str()));
    CHECK(package.open(file) == Result::Ok);
    GraphReader boundary(package);
    RouteCursor regional{};
    CHECK(beginRoute(boundary, 0, 3, {}, w, regional) == Result::Ok);
    do
    {
        r = routeStep(boundary, regional, 3);
    } while (retry(r));
    CHECK(r == Result::OutsideCoverage && regional.boundary_seen);
    package.close();
    CHECK(file.native.open((directory + "/bent-chain.tmap").c_str()));
    CHECK(package.open(file) == Result::Ok);
    GraphReader bent(package);
    const IntegerRouteLeg left_turn[]{{0, 0, UINT32_MAX}, {1, 0, UINT32_MAX}};
    const IntegerRouteLeg right_turn[]{{3, 0, UINT32_MAX}, {2, 0, UINT32_MAX}};
    const IntegerRouteLeg u_turn[]{{0, 0, UINT32_MAX}, {2, 0, UINT32_MAX}};
    for (const auto selection : {0, 1, 2})
    {
        Instructions turns{};
        CHECK(beginIntegerManeuvers(bent, selection == 0 ? left_turn : selection == 1 ? right_turn
                                                                                      : u_turn,
                                    2, *integer_maneuvers) == Result::Ok);
        size_t turn_work = 0;
        do
        {
            r = integerManeuverStep(bent, *integer_maneuvers, 1, instruction, &turns);
            CHECK(++turn_work < 1000);
        } while (retry(r));
        CHECK(r == Result::Ok && turns.count == 3 && turns.kinds[1] == (selection == 0 ? ManeuverKind::Left : selection == 1 ? ManeuverKind::Right
                                                                                                                             : ManeuverKind::UTurn));
    }
    Instructions closed_turns{};
    CHECK(beginIntegerManeuvers(bent, left_turn, 2, *integer_maneuvers) == Result::Ok);
    package.close();
    CHECK(integerManeuverStep(bent, *integer_maneuvers, 1, instruction, &closed_turns) == Result::Cancelled && !closed_turns.count);
    CHECK(file.native.open((directory + "/history-no.tmap").c_str()));
    file.every = 2;
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    GraphReader history_coordinates(package);
    for (const auto foot : {false, true})
        for (const auto entered : {false, true})
        {
            const IntegerRouteOptions history_options{{0, 1}, foot ? TravelMode::Foot : TravelMode::Bicycle, 10000, IntegerRouteAlgorithm::HeapDijkstra};
            size_t history_work = 0;
            do
            {
                r = beginIntegerCoordinateRoute(history_coordinates, {0, entered ? 2500 : 12500}, {0, 27500}, history_options, 20, {integer_labels.get(), heap.get(), 64}, prepared_legs, 16, *integer_coordinate);
                CHECK(++history_work < 10000);
            } while (retry(r));
            CHECK(r == Result::Ok);
            do
            {
                r = integerCoordinateRouteStep(history_coordinates, *integer_coordinate, 1);
                CHECK(++history_work < 10000);
            } while (retry(r));
            CHECK(r == (foot && entered ? Result::NoPath : Result::Ok));
            CHECK(integerCoordinateRouteResult(*integer_coordinate, ready_legs, ready_count, ready_cost) == r);
            if (r == Result::Ok)
                for (size_t i = 0; i < ready_count; ++i) CHECK(ready_legs[i].edge < history_coordinates.info().edges);
            else CHECK(!ready_legs && !ready_count && !ready_cost);
            for (const auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
                CHECK(run(history_coordinates, *cursor, {0, entered ? 2500 : 12500}, {0, 27500}, w, legs, 16,
                          {foot ? TravelMode::Foot : TravelMode::Bicycle, 0, 10000, algorithm}) == (foot && entered ? Result::NoPath : Result::Ok));
        }
    std::printf("PASS coordinate endpoints, clipping, one-way, turn boundaries, ascent profiles, Busy, zero allocations; cursor=%zu leg=%zu\n", sizeof(CoordinateRouteCursor), sizeof(RouteLeg));
}
