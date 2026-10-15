#include "tmap/resource_budget.h"
#include <limits>
#include <new>

namespace tmap
{
namespace
{
bool add(size_t& destination, size_t count, size_t bytes)
{
    if (count && bytes > (std::numeric_limits<size_t>::max() - destination) / count) return false;
    destination += count * bytes;
    return true;
}
} // namespace
Result resourceBudget(const ResourceOptions& options, ResourceBudget& output)
{
    ::new (static_cast<void*>(&output)) ResourceBudget{};
    if ((options.route_states == 0) != (options.route_legs == 0) ||
        (options.virtual_sources == 0) != (options.virtual_extents == 0) ||
        options.virtual_sources > UINT32_MAX || options.virtual_extents > UINT32_MAX ||
        options.route_states > UINT32_MAX || options.route_legs > UINT32_MAX ||
        ((options.route_geometry || options.maneuvers || options.route_elevation) && !options.route_states) ||
        (options.integer_landmarks && (!options.integer_route || !options.route_states)) ||
        (options.integer_label_scan && (!options.integer_route || !options.route_states || options.integer_landmarks)) ||
        (options.integer_prepared && (!options.integer_route || !options.route_states)) ||
        (options.integer_coordinate && !options.integer_prepared) ||
        ((options.navigation || options.profile) && options.geometry_points < 2) ||
        (options.profile_samples && !options.profile))
        return Result::Invalid;
    // Compute directly in caller storage rather than a large automatic aggregate.
    output.workspace_bytes = sizeof(PackageWorkspace);
    output.control_bytes = sizeof(OutdoorPackage);
    bool valid = true;
    valid = add(output.control_bytes, options.read_cursors, sizeof(ReadCursor)) && valid;
    valid = add(output.control_bytes, options.validation_cursors, sizeof(ValidationCursor)) && valid;
    valid = add(output.control_bytes, options.file_identity_cursors, sizeof(FileIdentityCursor)) && valid;
    valid = add(output.output_bytes, options.read_buffer_bytes, 1) && valid;
    valid = add(output.control_bytes, options.elevation_queries, sizeof(ElevationCursor)) && valid;
    valid = add(output.output_bytes, options.elevation_queries, sizeof(ElevationSample)) && valid;
    valid = add(output.workspace_bytes, options.calendar_predicates, sizeof(CalendarPredicate)) && valid;
    if (options.calendar_rules)
    {
        valid = add(output.control_bytes, 1, sizeof(CalendarRuleCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(CalendarRuleResult)) && valid;
    }
    if (options.road_rules)
    {
        valid = add(output.control_bytes, 1, sizeof(RoadRuleCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(RoadMasks)) && valid;
    }
    if (options.section_file) valid = add(output.control_bytes, 1, sizeof(PackageSectionFile)) && valid;
    if (options.edge_rules)
    {
        valid = add(output.control_bytes, options.edge_rules, sizeof(EdgeRuleReader)) && valid;
        valid = add(output.output_bytes, options.edge_rules, sizeof(uint8_t)) && valid;
    }
    if (options.turn_rules)
    {
        valid = add(output.control_bytes, 1, sizeof(TurnRuleCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(uint8_t)) && valid;
    }
    if (options.turn_program_read)
    {
        valid = add(output.control_bytes, 1, sizeof(TurnProgramReadCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(std::array<RoadRuleRange, 2>) + sizeof(uint8_t)) && valid;
    }
    if (options.turn_decision)
    {
        valid = add(output.control_bytes, 1, sizeof(TurnDecisionCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(bool)) && valid;
    }
    if (options.turn_context)
    {
        valid = add(output.control_bytes, options.turn_context, sizeof(TurnContextReader)) && valid;
        valid = add(output.output_bytes, options.turn_context, sizeof(bool)) && valid;
    }
    if (options.runtime_binding) valid = add(output.control_bytes, options.runtime_binding, sizeof(RuntimeRoutingRules)) && valid;
    if (options.road_program_read)
    {
        valid = add(output.control_bytes, 1, sizeof(RoadProgramReadCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(std::array<RoadRuleRange, 7>) + sizeof(uint8_t) + sizeof(bool)) && valid;
    }
    if (options.virtual_sources)
    {
        valid = add(output.workspace_bytes, options.virtual_sources, sizeof(VirtualSource)) && valid;
        valid = add(output.workspace_bytes, options.virtual_extents, sizeof(FileExtent)) && valid;
        valid = add(output.control_bytes, 1, sizeof(VirtualMapCursor) + sizeof(VerifiedVirtualFile)) && valid;
    }
    if (options.elevation || options.profile || options.elevation_queries)
        valid = add(output.control_bytes, 1, sizeof(TerrainReader)) && valid;
    if (options.elevation) valid = add(output.output_bytes, 1, sizeof(ElevationSample)) && valid;
    valid = add(output.output_bytes, options.geometry_points, sizeof(GeoPoint)) && valid;
    valid = add(output.output_bytes, options.profile_samples, sizeof(ProfileSample)) && valid;
    if (options.profile) valid = add(output.control_bytes, 1, sizeof(ProfileCursor)) && valid;
    if (options.route_states)
    {
        valid = add(output.workspace_bytes, options.route_states,
                    (options.integer_route ? sizeof(IntegerRouteLabel) : sizeof(RouteLabel)) + (options.integer_label_scan ? 0 : sizeof(uint32_t))) &&
                valid;
        valid = add(output.control_bytes, 1, sizeof(GraphReader) + (options.integer_route ? options.integer_prepared ? options.integer_coordinate ? sizeof(IntegerCoordinateRouteCursor) : sizeof(IntegerPreparedRouteCursor) : sizeof(IntegerRouteCursor) : sizeof(CoordinateRouteCursor))) && valid;
        valid = add(output.output_bytes, options.route_legs, options.integer_route ? options.integer_prepared ? sizeof(IntegerRouteLeg) : sizeof(uint32_t) : sizeof(RouteLeg)) && valid;
        if (options.integer_prepared && !options.integer_coordinate) valid = add(output.output_bytes, 2, sizeof(IntegerEndpointCandidates)) && valid;
    }
    if (options.integer_landmarks)
    {
        valid = add(output.workspace_bytes, options.route_states, sizeof(uint32_t)) && valid;
        valid = add(output.control_bytes, 1, sizeof(IntegerLandmarkWorkspace)) && valid;
    }
    if (options.integer_elevation)
    {
        valid = add(output.control_bytes, 1, sizeof(IntegerElevationCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(IntegerElevationPrefix)) && valid;
    }
    if (options.integer_interval)
    {
        valid = add(output.control_bytes, 1, sizeof(IntegerEdgeIntervalCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(IntegerEdgeInterval)) && valid;
    }
    if (options.integer_endpoint)
    {
        valid = add(output.control_bytes, 1, sizeof(IntegerEndpointCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(IntegerEndpointCandidates)) && valid;
    }
    if (options.integer_direct)
    {
        valid = add(output.control_bytes, 1, sizeof(IntegerDirectCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(IntegerDirectRoute) + 2 * sizeof(IntegerEndpointCandidates)) && valid;
    }
    if ((options.integer_elevation || options.integer_interval || options.integer_endpoint || options.integer_direct) && !options.route_states)
        valid = add(output.control_bytes, 1, sizeof(GraphReader)) && valid;
    const size_t implied_graph = options.route_states || options.integer_elevation || options.integer_interval || options.integer_endpoint || options.integer_direct ? 1 : 0;
    if (options.graph_reader > implied_graph)
        valid = add(output.control_bytes, options.graph_reader - implied_graph, sizeof(GraphReader)) && valid;
    if (options.contour_segments)
    {
        valid = add(output.control_bytes, 1, sizeof(ContourRenderCursor)) && valid;
        valid = add(output.output_bytes, options.contour_segments, sizeof(ContourSegment)) && valid;
    }
    if (options.administration)
    {
        valid = add(output.workspace_bytes, 1, sizeof(AdministrativeWorkspace)) && valid;
        valid = add(output.control_bytes, 1, sizeof(AdministrativeCursor)) && valid;
    }
    if (options.navigation)
    {
        valid = add(output.control_bytes, 1, sizeof(Navigator) + sizeof(NavigationCursor)) && valid;
        valid = add(output.output_bytes, 1, sizeof(NavigationState)) && valid;
    }
    if (options.route_geometry)
        valid = add(output.control_bytes, 1, options.integer_route ? options.integer_prepared ? sizeof(IntegerGeometryCursor) : sizeof(RouteGeometryCursor) : sizeof(LegGeometryCursor)) && valid;
    if (options.maneuvers) valid = add(output.control_bytes, 1, options.integer_prepared ? sizeof(IntegerManeuverCursor) : sizeof(ManeuverCursor)) && valid;
    if (options.route_elevation)
    {
        valid = add(output.control_bytes, 1, options.integer_route ? options.integer_prepared ? sizeof(IntegerLegElevationCursor) : sizeof(IntegerRouteElevationCursor) : sizeof(RouteElevationCursor)) && valid;
        valid = add(output.output_bytes, 1, options.integer_route ? sizeof(IntegerRouteElevation) : sizeof(RouteElevation)) && valid;
    }
    output.application_bytes = options.application_bytes;
    output.allocator_overhead_bytes = options.allocator_overhead_bytes;
    valid = add(output.total_bytes, 1, output.workspace_bytes) && valid;
    valid = add(output.total_bytes, 1, output.control_bytes) && valid;
    valid = add(output.total_bytes, 1, output.output_bytes) && valid;
    valid = add(output.total_bytes, 1, output.application_bytes) && valid;
    valid = add(output.total_bytes, 1, output.allocator_overhead_bytes) && valid;
    if (!valid)
    {
        ::new (static_cast<void*>(&output)) ResourceBudget{};
        return Result::Invalid;
    }
    return Result::Ok;
}
} // namespace tmap
