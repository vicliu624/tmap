#pragma once
#include "tmap/administration.h"
#include "tmap/calendar_condition.h"
#include "tmap/calendar_rules.h"
#include "tmap/contour_renderer.h"
#include "tmap/coordinate_route.h"
#include "tmap/edge_rules.h"
#include "tmap/integer_coordinate_route.h"
#include "tmap/integer_elevation.h"
#include "tmap/integer_endpoint.h"
#include "tmap/integer_geometry.h"
#include "tmap/integer_leg_elevation.h"
#include "tmap/integer_maneuvers.h"
#include "tmap/integer_prepared_route.h"
#include "tmap/integer_route.h"
#include "tmap/road_rules.h"
#include "tmap/route_elevation.h"
#include "tmap/routing_rules.h"
#include "tmap/section_file.h"
#include "tmap/turn_context.h"
#include "tmap/turn_rules.h"
#include "tmap/virtual_map.h"

namespace tmap
{
// Concurrently retained resources; zero capacities disable optional services.
// Counts do not estimate which routes succeed: exhausted capacities are explicit.
struct ResourceOptions
{
    size_t route_states = 0, route_legs = 0, contour_segments = 0;
    size_t virtual_sources = 0, virtual_extents = 0;
    size_t calendar_predicates = 0;
    // Additional independently retained cursors, not scratch already embedded
    // in readers/virtual-map verification. Read output capacities are summed.
    size_t read_cursors = 0, validation_cursors = 0, file_identity_cursors = 0;
    size_t read_buffer_bytes = 0;
    // Explicit beginElevation/elevationStep cursors plus one sample each.
    // One shared TerrainReader is included; elevation/profile convenience is extra.
    size_t elevation_queries = 0;
    bool calendar_rules = false;
    bool section_file = false;
    bool road_program_read = false;
    size_t edge_rules = 0; // Reader count; one in-flight edge per reader. Includes descriptor/condition scratch.
    bool turn_rules = false;
    bool turn_program_read = false;
    bool turn_decision = false;
    size_t turn_context = 0;
    size_t runtime_binding = 0;
    size_t graph_reader = 0; // Minimum total count; route/elevation services already imply one.
    bool road_rules = false; // Includes its embedded calendar cursor; separate calendar_rules is additional storage.
    // Integer node routes retain uint32 edge rows instead of clipped RouteLegs.
    bool integer_route = false;
    bool integer_label_scan = false; // No queue retained; only valid for integer routes without ALT.
    bool integer_landmarks = false;
    bool integer_prepared = false;
    bool integer_coordinate = false;
    // One retained polyline shared by navigation/profile, and optional samples.
    size_t geometry_points = 0, profile_samples = 0;
    bool elevation = false, profile = false, administration = false;
    bool navigation = false, route_geometry = false, maneuvers = false;
    bool route_elevation = false;
    bool integer_elevation = false;
    bool integer_interval = false;
    bool integer_endpoint = false;
    bool integer_direct = false;
    // Application-owned objects (file/provider/display/task storage) are explicit.
    size_t application_bytes = 0, allocator_overhead_bytes = 0;
};
struct ResourceBudget
{
    size_t workspace_bytes = 0, control_bytes = 0, output_bytes = 0;
    size_t application_bytes = 0, allocator_overhead_bytes = 0, total_bytes = 0;
};
// sizeof uses the consumer's selected cache profile and target ABI. No allocation.
// No shared scratch is presumed: services may retain their cursors concurrently.
// Zeroed output on invalid capacity/overflow, including caller-added resources.
Result resourceBudget(const ResourceOptions& options, ResourceBudget& output);
} // namespace tmap
