#include "tmap/cancellation.h"
#include "tmap/native_file.h"
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
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    auto workspace = std::make_unique<PackageWorkspace>();
    NativeFile file;
    CHECK(file.open((std::string(argv[1]) + "/chain.tmap").c_str()));
    OutdoorPackage package(*workspace);
    CHECK(package.open(file) == Result::Ok);
    ValidationCursor validation;
    CHECK(beginValidation(package, validation) == Result::Ok);
    CHECK(validationStep(package, validation, 1) == Result::More);
    cancel(validation);
    cancel(validation);
    CHECK(validationStep(package, validation, 1) == Result::Cancelled && package.isOpen());
    CHECK(beginValidation(package, validation) == Result::Ok);
    Result validated = Result::More;
    for (size_t i = 0; validated == Result::More && i < 10000; ++i)
        validated = validationStep(package, validation, 1);
    CHECK(validated == Result::Ok);
    cancel(validation);
    CHECK(validationStep(package, validation, 1) == Result::Cancelled);
    std::printf("VALIDATION_CANCEL cursor=%zu mid_scan=1 restart=1 completed_revoked=1 package_kept_open=1\n", sizeof(ValidationCursor));
    GraphReader graph(package);
    CHECK(graph.initialize() == Result::Ok);
    auto cursor = std::make_unique<RouteCursor>();
    RouteLabel labels[64];
    uint32_t heap[64], edges[16];
    RouteWorkspace search{labels, heap, 64};
    CHECK(beginRoute(graph, 0, 2, {}, search, *cursor) == Result::Ok);
    cancel(*cursor);
    cancel(*cursor);
    CHECK(routeStep(graph, *cursor, 1) == Result::Cancelled);
    size_t count = 99;
    CHECK(routeEdges(*cursor, edges, 16, count) == Result::Cancelled && count == 0);
    CHECK(beginRoute(graph, 0, 2, {}, search, *cursor) == Result::Ok);
    Result result = Result::More;
    size_t steps = 0;
    while (result == Result::More)
    {
        result = routeStep(graph, *cursor, 1);
        CHECK(++steps < 10000);
    }
    CHECK(result == Result::Ok);
    CHECK(routeEdges(*cursor, edges, 16, count) == Result::Ok && count);
    cancel(*cursor);
    CHECK(routeEdges(*cursor, edges, 16, count) == Result::Cancelled && count == 0);
    RouteGeometryCursor geometry;
    CHECK(beginRouteGeometry(graph, edges, 1, geometry) == Result::Ok);
    auto stop = [](void*, GeoPoint)
    { return false; };
    CHECK(routeGeometryStep(graph, geometry, 100, stop, nullptr) == Result::Cancelled);
    CHECK(routeGeometryStep(graph, geometry, 100, stop, nullptr) == Result::Cancelled);
    CHECK(beginRouteGeometry(graph, edges, 1, geometry) == Result::Ok);
    cancel(geometry);
    CHECK(routeGeometryStep(graph, geometry, 1, stop, nullptr) == Result::Cancelled);
    SnapCursor snapping;
    CHECK(beginSnap(graph, {0, 2500}, TravelMode::Foot, 20, snapping) == Result::Ok);
    cancel(snapping);
    CHECK(snapStep(graph, snapping, 1) == Result::Cancelled && snapping.best.edge == UINT32_MAX);
    GeoPoint points[2]{{0, 0}, {0, 10000}};
    Navigator navigator;
    CHECK(navigator.begin(points, 2) == Result::Ok);
    NavigationCursor matching;
    NavigationState state;
    CHECK(navigator.beginUpdate({0, 0}, 1, matching) == Result::Ok);
    cancel(matching);
    CHECK(navigator.updateStep(matching, 1, state) == Result::Cancelled);
    CHECK(navigator.beginUpdate({0, 0}, 1, matching) == Result::Ok);
    CHECK(navigator.updateStep(matching, 1, state) == Result::Ok);
    TerrainReader terrain(package);
    ProfileCursor profile;
    CHECK(beginProfile(terrain, package, points, 2, {}, profile) == Result::Ok);
    auto stop_profile = [](void*, const ProfileSample&)
    { return false; };
    CHECK(profileStep(terrain, package, profile, 100, stop_profile, nullptr) == Result::Cancelled);
    CHECK(profileStep(terrain, package, profile, 100, stop_profile, nullptr) == Result::Cancelled);
    CHECK(beginProfile(terrain, package, points, 2, {}, profile) == Result::Ok);
    cancel(profile);
    CHECK(profileStep(terrain, package, profile, 1, stop_profile, nullptr) == Result::Cancelled);
    auto contours = std::make_unique<ContourCursor>();
    CHECK(beginContours(package, {0, 0, 20000, 20000}, 16, *contours) == Result::Ok);
    auto stop_contour = [](void*, const ContourVertex&)
    { return false; };
    Result contour_result = Result::More;
    for (size_t i = 0; contour_result == Result::More && i < 1000; ++i)
        contour_result = contourStep(package, *contours, 1, stop_contour, nullptr);
    CHECK(contour_result == Result::Cancelled);
    CHECK(contourStep(package, *contours, 1, stop_contour, nullptr) == Result::Cancelled);
    auto coordinate = std::make_unique<CoordinateRouteCursor>();
    RouteLeg legs[16];
    CHECK(beginCoordinateRoute(graph, {0, 2500}, {0, 17500}, {}, 20, search, legs, 16, *coordinate) == Result::Ok);
    coordinate->count = 7;
    coordinate->found = true;
    cancel(*coordinate);
    CHECK(coordinateRouteStep(graph, *coordinate, 1) == Result::Cancelled && coordinate->count == 0 && !coordinate->found);
    auto render = std::make_unique<ContourRenderCursor>();
    render->source.generation = package.generation();
    render->count = 5;
    cancel(*render);
    CHECK(contourRenderStep(package, *render, 1) == Result::Cancelled && render->count == 0);
    ManeuverCursor maneuver;
    maneuver.generation = package.generation();
    cancel(maneuver);
    CHECK(maneuverStep(
              graph, maneuver, 1, [](void*, const Maneuver&)
              { return true; },
              nullptr) == Result::Cancelled);
    std::printf("PASS mid-route cancel, idempotence, restart to completion, successful-result revocation\n");
}
