#include "tmap/resource_budget.h"
#include <cstdio>
#include <cstdlib>
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
int main()
{
    ResourceOptions options;
    ResourceBudget budget;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage));
    options.read_cursors = 2;
    options.validation_cursors = 3;
    options.file_identity_cursors = 2;
    options.read_buffer_bytes = 1234;
    options.elevation_queries = 2;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    const size_t standalone = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) +
        2 * sizeof(ReadCursor) + 3 * sizeof(ValidationCursor) + 2 * sizeof(FileIdentityCursor) +
        1234 + sizeof(TerrainReader) + 2 * (sizeof(ElevationCursor) + sizeof(ElevationSample));
    CHECK(budget.total_bytes == standalone);
    options.elevation = true;
    CHECK(resourceBudget(options, budget) == Result::Ok && budget.total_bytes == standalone + sizeof(ElevationSample));
    for (size_t ResourceOptions::* field : {&ResourceOptions::read_cursors, &ResourceOptions::validation_cursors,
         &ResourceOptions::file_identity_cursors, &ResourceOptions::read_buffer_bytes, &ResourceOptions::elevation_queries})
    {
        const auto saved = options.*field;
        options.*field = SIZE_MAX;
        CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0 && budget.control_bytes == 0);
        options.*field = saved;
    }
    options = ResourceOptions{};
    options.calendar_predicates = 2;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + 2 * sizeof(CalendarPredicate));
    options.calendar_predicates = SIZE_MAX;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0);
    options.calendar_predicates = 0;
    options.calendar_rules = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(CalendarRuleCursor) + sizeof(CalendarRuleResult));
    options.calendar_rules = false;
    options.road_rules = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(RoadRuleCursor) + sizeof(RoadMasks));
    options.road_rules = false;
    options.section_file = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(PackageSectionFile));
    options.section_file = false;
    options.turn_rules = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(TurnRuleCursor) + sizeof(uint8_t));
    options.turn_rules = false;
    options.runtime_binding = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(RuntimeRoutingRules));
    options.runtime_binding = false;
    options.graph_reader = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader));
    options.graph_reader = false;
    options.virtual_sources = 3;
    CHECK(resourceBudget(options, budget) == Result::Invalid);
    options.virtual_extents = 17;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(VirtualMapCursor) + sizeof(VerifiedVirtualFile) + 3 * sizeof(VirtualSource) + 17 * sizeof(FileExtent));
    options.virtual_sources = options.virtual_extents = 0;
    options.integer_elevation = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) +
                                    sizeof(IntegerElevationCursor) + sizeof(IntegerElevationPrefix));
    options.integer_elevation = false;
    options.integer_interval = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) +
                                    sizeof(IntegerEdgeIntervalCursor) + sizeof(IntegerEdgeInterval));
    options.integer_interval = false;
    options.integer_endpoint = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) + sizeof(IntegerEndpointCursor) + sizeof(IntegerEndpointCandidates));
    options.integer_endpoint = false;
    options.integer_direct = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) + sizeof(IntegerDirectCursor) + sizeof(IntegerDirectRoute) + 2 * sizeof(IntegerEndpointCandidates));
    options.integer_direct = false;
    options.route_states = 128;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0);
    options.route_legs = 64;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    const size_t expected = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) +
                            sizeof(CoordinateRouteCursor) + 128 * (sizeof(RouteLabel) + 4) + 64 * sizeof(RouteLeg);
    CHECK(budget.total_bytes == expected);
    options.route_elevation = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == expected + sizeof(RouteElevationCursor) + sizeof(RouteElevation));
    options.integer_route = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.control_bytes == sizeof(OutdoorPackage) + sizeof(GraphReader) + sizeof(IntegerRouteCursor) + sizeof(IntegerRouteElevationCursor));
    options.route_elevation = false;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    const auto integer_base = budget.total_bytes;
    options.integer_label_scan = true;
    CHECK(resourceBudget(options, budget) == Result::Ok && budget.total_bytes == integer_base - 128 * sizeof(uint32_t));
    options.integer_landmarks = true;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0);
    options.integer_label_scan = false;
    options.integer_landmarks = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == integer_base + 128 * sizeof(uint32_t) + sizeof(IntegerLandmarkWorkspace));
    options.integer_landmarks = false;
    options.integer_prepared = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == integer_base - sizeof(IntegerRouteCursor) - 64 * sizeof(uint32_t) + sizeof(IntegerPreparedRouteCursor) + 64 * sizeof(IntegerRouteLeg) + 2 * sizeof(IntegerEndpointCandidates));
    options.integer_prepared = false;
    options.integer_prepared = true;
    options.route_geometry = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == integer_base - sizeof(IntegerRouteCursor) - 64 * sizeof(uint32_t) +
                                    sizeof(IntegerPreparedRouteCursor) + 64 * sizeof(IntegerRouteLeg) +
                                    2 * sizeof(IntegerEndpointCandidates) + sizeof(IntegerGeometryCursor));
    options.maneuvers = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == integer_base - sizeof(IntegerRouteCursor) - 64 * sizeof(uint32_t) +
                                    sizeof(IntegerPreparedRouteCursor) + 64 * sizeof(IntegerRouteLeg) +
                                    2 * sizeof(IntegerEndpointCandidates) + sizeof(IntegerGeometryCursor) + sizeof(IntegerManeuverCursor));
    options.maneuvers = false;
    options.route_elevation = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == integer_base - sizeof(IntegerRouteCursor) - 64 * sizeof(uint32_t) +
                                    sizeof(IntegerPreparedRouteCursor) + 64 * sizeof(IntegerRouteLeg) +
                                    2 * sizeof(IntegerEndpointCandidates) + sizeof(IntegerGeometryCursor) +
                                    sizeof(IntegerLegElevationCursor) + sizeof(IntegerRouteElevation));
    options.route_elevation = false;
    options.route_geometry = false;
    options.integer_prepared = false;
    options.integer_route = true;
    options.integer_prepared = true;
    options.integer_coordinate = true;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == integer_base - sizeof(IntegerRouteCursor) - 64 * sizeof(uint32_t) + sizeof(IntegerCoordinateRouteCursor) + 64 * sizeof(IntegerRouteLeg));
    options.integer_prepared = false;
    CHECK(resourceBudget(options, budget) == Result::Invalid);
    options.integer_coordinate = false;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) +
                                    sizeof(IntegerRouteCursor) + 128 * (sizeof(IntegerRouteLabel) + sizeof(uint32_t)) + 64 * sizeof(uint32_t));
    options.integer_route = false;
    options.administration = true;
    options.contour_segments = 512;
    options.profile = true;
    options.geometry_points = 10;
    options.profile_samples = 8;
    options.elevation = true;
    options.navigation = options.route_geometry = options.maneuvers = true;
    options.application_bytes = 1024;
    options.allocator_overhead_bytes = 128;
    CHECK(resourceBudget(options, budget) == Result::Ok);
    CHECK(budget.total_bytes == expected + sizeof(AdministrativeWorkspace) + sizeof(AdministrativeCursor) +
                                    sizeof(ContourRenderCursor) + 512 * sizeof(ContourSegment) + sizeof(TerrainReader) + sizeof(ProfileCursor) +
                                    sizeof(Navigator) + sizeof(NavigationCursor) + sizeof(LegGeometryCursor) + sizeof(ManeuverCursor) + 1152 +
                                    10 * sizeof(GeoPoint) + 8 * sizeof(ProfileSample) + sizeof(ElevationSample) + sizeof(NavigationState));
    options.contour_segments = SIZE_MAX;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.workspace_bytes == 0 && budget.output_bytes == 0);
    options.contour_segments = 0;
    options.application_bytes = SIZE_MAX;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0);
    options = ResourceOptions{};
    options.elevation = true;
    options.edge_rules = SIZE_MAX;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0 && budget.control_bytes == 0);
    options.edge_rules = 0;
    options.turn_context = SIZE_MAX;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0);
    options.turn_context = 0;
    options.runtime_binding = SIZE_MAX;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0);
    options.runtime_binding = 0;
    options.graph_reader = SIZE_MAX;
    CHECK(resourceBudget(options, budget) == Result::Invalid && budget.total_bytes == 0);
    std::printf("PASS exact ABI/profile budget, retained concurrent services, invalid combinations, multiplication/sum overflow\n");
    std::printf("route_elevation_cursor=%zu output=%zu\n", sizeof(RouteElevationCursor), sizeof(RouteElevation));
    std::printf("integer_route_elevation_cursor=%zu output=%zu\n", sizeof(IntegerRouteElevationCursor), sizeof(IntegerRouteElevation));
    std::printf("integer_prefix_cursor=%zu output=%zu\n", sizeof(IntegerElevationCursor), sizeof(IntegerElevationPrefix));
    std::printf("integer_interval_cursor=%zu output=%zu\n", sizeof(IntegerEdgeIntervalCursor), sizeof(IntegerEdgeInterval));
    std::printf("integer_endpoint_cursor=%zu output=%zu leg=%zu\n", sizeof(IntegerEndpointCursor), sizeof(IntegerEndpointCandidates), sizeof(IntegerRouteLeg));
    std::printf("integer_direct_cursor=%zu output=%zu\n", sizeof(IntegerDirectCursor), sizeof(IntegerDirectRoute));
    std::printf("integer_prepared_cursor=%zu endpoint_output=%zu\n", sizeof(IntegerPreparedRouteCursor), sizeof(IntegerEndpointCandidates));
    std::printf("integer_geometry_cursor=%zu\n", sizeof(IntegerGeometryCursor));
    std::printf("integer_leg_elevation_cursor=%zu\n", sizeof(IntegerLegElevationCursor));
    std::printf("integer_coordinate_cursor=%zu snap_cursor=%zu\n", sizeof(IntegerCoordinateRouteCursor), sizeof(SnapCursor));
    std::printf("integer_conversion_cursor=%zu integer_maneuver_cursor=%zu\n", sizeof(IntegerLegConversionCursor), sizeof(IntegerManeuverCursor));
    std::printf("history_graph_reader=%zu integer_route_cursor=%zu integer_label=%zu float_label=%zu\n", sizeof(GraphReader), sizeof(IntegerRouteCursor), sizeof(IntegerRouteLabel), sizeof(RouteLabel));
}
