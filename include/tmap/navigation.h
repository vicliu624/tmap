#pragma once
#include "tmap/routing_profile.h"
#include "tmap/terrain.h"
#include <cstdint>

namespace tmap
{
class EdgeRuleReader;
class TurnContextReader;
struct RuntimeRoutingRules;
enum class TravelMode : uint8_t
{
    Foot = 1,
    Bicycle = 2
};
struct GraphInfo
{
    uint32_t nodes = 0, edges = 0;
    std::array<uint8_t, 16> topology{};
    bool topology_declared = false;
    uint8_t history_modes = 0;
    bool runtime_roads : 1;
    bool runtime_barriers : 1;
    bool runtime_turns : 1;
    GraphInfo() : runtime_roads(false), runtime_barriers(false), runtime_turns(false) {}
    GraphInfo(uint32_t n, uint32_t e, std::array<uint8_t, 16> identity, bool declared)
        : nodes(n), edges(e), topology(identity), topology_declared(declared), runtime_roads(false), runtime_barriers(false), runtime_turns(false) {}
};
struct GraphNode
{
    uint64_t id = 0;
    GeoPoint point{};
    uint32_t first_edge = 0, edge_count = 0;
    int32_t elevation_m = 0;
    bool has_elevation = false, boundary = false;
};
struct GraphEdge
{
    uint32_t from = 0, to = 0, length_cm = 0, ascent_cm = 0, descent_cm = 0;
    uint64_t shape_offset = 0, name_id = 0;
    uint32_t points = 0, opposite = UINT32_MAX;
    uint8_t access = 0, road_class = 0;
    bool reversed = false, has_elevation = false;
};
struct AscentCursor
{
    uint64_t generation = 0, offset = 0;
    double fraction = 0, value = 0;
    uint32_t edge = UINT32_MAX, count = 0, lo = 0, hi = 0;
    uint32_t length_cm = 0, ascent_cm = 0, descent_cm = 0;
    uint32_t lower_distance = 0, lower_ascent = 0, lower_descent = 0;
    std::array<uint8_t, 48> bytes{};
    uint8_t stage = 0;
    Result result = Result::More;
    bool descending = false;
    uint8_t read_completed = 0;
    uint32_t raw_edge = UINT32_MAX; // Shares the existing 48-byte scratch before prefix work.
};
struct TurnCursor
{
    uint64_t generation = 0, lo = 0, hi = 0;
    uint32_t incoming = UINT32_MAX, outgoing = UINT32_MAX;
    std::array<uint8_t, 16> bytes{};
    TravelMode mode = TravelMode::Foot;
    uint8_t stage = 0, scanned = 0;
    uint32_t gap_begin = 0, gap_end = UINT32_MAX;
    bool allowed = false;
    Result result = Result::More;
};
class GraphReader
{
  public:
    explicit GraphReader(OutdoorPackage& package, EdgeRuleReader* rules = nullptr)
        : package_(package), initialized_(false), rules_binding_(false), runtime_(rules) {}
    GraphReader(OutdoorPackage& package, const RuntimeRoutingRules& rules)
        : package_(package), initialized_(false), rules_binding_(true), runtime_(rules) {}
    GraphReader(OutdoorPackage&, RuntimeRoutingRules&&) = delete;
    GraphReader(const GraphReader&) = delete;
    GraphReader& operator=(const GraphReader&) = delete;
    Result initialize();
    using RoutingClock = bool (*)(void* context, uint64_t& utc_seconds, int32_t& local_offset_seconds);
    void setRoutingClock(RoutingClock clock, void* context)
    {
        routing_clock_ = clock;
        routing_clock_context_ = context;
    }
    Result validateRouting(TravelMode mode);
    const GraphInfo& info() const { return info_; }
    Result node(uint32_t row, GraphNode& output);
    Result edge(uint32_t row, GraphEdge& output);
    Result point(const GraphEdge& edge, uint32_t index, GeoPoint& output);
    Result roadName(uint64_t reference, char* output, size_t capacity);
    Result ascentPrefix(uint32_t edge_row, double fraction, double& ascent_m);
    Result descentPrefix(uint32_t edge_row, double fraction, double& descent_m);
    Result turnAllowed(uint32_t incoming, uint32_t outgoing, TravelMode mode, bool& allowed);
    Result routeTransition(uint32_t incoming_key, uint32_t outgoing, TravelMode mode, bool& allowed, uint32_t& next_key);
    uint32_t routeEdge(uint32_t key) const { return key == UINT32_MAX || !key || !info_.edges ? UINT32_MAX : key <= info_.edges ? key - 1
                                                                                                                                : (key - 1) % info_.edges; }
    OutdoorPackage& package() { return package_; }

  private:
    OutdoorPackage& package_;
    RoutingClock routing_clock_ = nullptr;
    void* routing_clock_context_ = nullptr;
    RoutingProfile routing_profile_{};
    uint64_t routing_profile_generation_ = UINT64_MAX;
    GraphInfo info_{};
    uint64_t generation_ = 0;
    bool initialized_ : 1;
    bool rules_binding_ : 1;
    union Runtime
    {
        EdgeRuleReader* edge;
        const RuntimeRoutingRules* binding;
        Runtime(EdgeRuleReader* p) : edge(p) {}
        Runtime(const RuntimeRoutingRules& rules) : binding(&rules) {}
    } runtime_;
    EdgeRuleReader* edgeRules() const;
    TurnContextReader* turnRules() const;
    bool validRuntime() const;
    AscentCursor ascent_{};
    Result elevationPrefix(uint32_t edge_row, double fraction, bool descending, double& output);
    Result loadRawEdge(uint32_t row);
    Result prefixRead(SectionType type, uint64_t offset, size_t bytes);
    TurnCursor turn_{};
    uint32_t restriction_gap_begin_ = 0, restriction_gap_end_ = 0;
};
Result graphCompatibility(GraphReader& a, GraphReader& b);
struct Snap
{
    GeoPoint point{};
    uint32_t edge = UINT32_MAX, segment = 0;
    double segment_fraction = 0, distance_m = 0, along_fraction = 0;
};
struct SnapCursor
{
    std::array<ContourCursor::Frame, 24> stack{};
    std::array<uint8_t, 48> read_bytes{};
    ReadCursor read{};
    Bounds bounds{};
    GeoPoint point{};
    GraphEdge candidate{};
    Snap best{};
    uint64_t generation = 0, refs = 0;
    uint32_t remaining = 0, depth = 0, candidate_id = UINT32_MAX, segment = 0;
    TravelMode mode = TravelMode::Foot;
    double max_distance_m = 0, candidate_length_m = 0, best_along_m = 0;
    bool complete = true;
    bool cancelled = false;
    bool edge_pending = false, point_pending = false;
};
Result beginSnap(GraphReader& graph, GeoPoint point, TravelMode mode, double max_distance_m, SnapCursor& cursor);
Result snapStep(GraphReader& graph, SnapCursor& cursor, size_t budget);
// Sparse caller-owned states, no allocation proportional to the complete graph.
struct RouteLabel
{
    double cost = 0;
    uint32_t key = 0;
    uint32_t node = 0, incoming = UINT32_MAX, previous = UINT32_MAX, heap_position = UINT32_MAX;
    uint32_t potential = 0; // Lower 31 bits: ALT centimeters; high bit: settled.
};
struct RouteWorkspace
{
    RouteLabel* labels = nullptr;
    uint32_t* heap = nullptr;
    size_t capacity = 0;
};
enum class RouteAlgorithm : uint8_t
{
    Auto,
    Dijkstra,
    Landmarks,
    QueueRelaxation
};
struct RouteOptions
{
    TravelMode mode = TravelMode::Foot;
    double ascent_penalty = 0;
    uint32_t max_expansions = 50000;
    RouteAlgorithm algorithm = RouteAlgorithm::Auto;
};
struct RouteCursor
{
    RouteWorkspace workspace{};
    RouteOptions options{};
    uint64_t generation = 0;
    size_t heap_size = 0, labels_used = 0, initialization = 0;
    uint32_t start = 0, initial_edge = UINT32_MAX, final_edge = UINT32_MAX;
    uint32_t destination = 0, current = UINT32_MAX, first_edge = 0, next_edge = 0, edge_count = 0;
    uint32_t goal_label = UINT32_MAX, expansions = 0;
    bool complete = true;
    Result terminal = Result::Invalid;
    bool initialized = false, boundary_seen = false;
    uint8_t boundary_stage = 0;
    GraphEdge pending{};
    bool edge_loaded = false;
    std::array<uint8_t, 64> landmark_scratch{};
    std::array<uint32_t, 16> goal_landmarks{};
    uint8_t landmark_stage = 0, landmark_count = 0;
    uint8_t landmark_completed = 0;
    uint32_t queue_head = 0;
};
Result beginRoute(GraphReader& graph, uint32_t start_node, uint32_t destination_node,
                  const RouteOptions& options, RouteWorkspace workspace, RouteCursor& cursor);
Result routeStep(GraphReader& graph, RouteCursor& cursor, size_t budget);
Result beginRouteWithBoundaries(GraphReader& graph, uint32_t start_node, uint32_t destination_node,
                                uint32_t initial_edge, uint32_t final_edge, const RouteOptions& options,
                                RouteWorkspace workspace, RouteCursor& cursor);
// Output is forward ordered. Capacity failure reports required count; no partial route.
Result routeEdges(const RouteCursor& cursor, uint32_t* output, size_t capacity, size_t& count);
enum class ManeuverKind : uint8_t
{
    Depart,
    Continue,
    Left,
    Right,
    UTurn,
    Arrive
};
struct Maneuver
{
    ManeuverKind kind = ManeuverKind::Continue;
    GeoPoint point{};
    double distance_m = 0;
    uint64_t name_id = 0;
};
using RoutePointVisitor = bool (*)(void*, GeoPoint);
using ManeuverVisitor = bool (*)(void*, const Maneuver&);
struct RouteLeg;
struct RouteGeometryCursor
{
    const uint32_t* edges = nullptr;
    size_t count = 0, edge_index = 0;
    uint32_t point_index = 0;
    uint64_t generation = 0;
    GraphEdge scratch{};
    bool complete = true, loaded = false;
    bool cancelled = false;
};
Result beginRouteGeometry(GraphReader& graph, const uint32_t* edges, size_t count, RouteGeometryCursor& cursor);
Result routeGeometryStep(GraphReader& graph, RouteGeometryCursor& cursor, size_t budget, RoutePointVisitor visitor, void* context);
struct ManeuverCursor
{
    const uint32_t* edges = nullptr;
    const RouteLeg* legs = nullptr;
    size_t count = 0, index = 0;
    GeoPoint previous_a{}, previous_b{};
    GraphEdge scratch{};
    GeoPoint start_a{}, start_b{}, finish_a{}, finish_b{}, arrival{};
    uint32_t first_segment = 0, last_segment = 0, segment = 0;
    float first_fraction = 0, last_fraction = 1;
    uint8_t stage = 0;
    double distance_m = 0;
    uint64_t generation = 0;
    bool complete = true;
    Result terminal = Result::More;
};
Result beginManeuvers(GraphReader& graph, const uint32_t* edges, size_t count, ManeuverCursor& cursor);
Result maneuverStep(GraphReader& graph, ManeuverCursor& cursor, size_t budget, ManeuverVisitor visitor, void* context);
struct NavigationOptions
{
    double deviation_m = 30, arrival_m = 15;
    uint8_t deviation_fixes = 3;
};
struct NavigationState
{
    double progress_m = 0, remaining_m = 0, cross_track_m = 0;
    GeoPoint matched{};
    uint32_t segment = 0;
    uint8_t outside_fixes = 0;
    bool off_route = false, arrived = false;
};
struct NavigationCursor
{
    GeoPoint fix{}, matched{};
    uint64_t generation = 0;
    size_t next_segment = 0;
    uint32_t matched_segment = 0;
    double accuracy_m = 0, accumulated_m = 0, best_m = 0, along_m = 0;
    bool complete = true;
    bool cancelled = false;
};
// Caller retains route points; no private copy. Each update has an explicit segment budget.
class Navigator
{
  public:
    Result begin(const GeoPoint* route, size_t count, const NavigationOptions& options = {});
    Result beginUpdate(GeoPoint fix, double accuracy_m, NavigationCursor& cursor);
    Result updateStep(NavigationCursor& cursor, size_t segment_budget, NavigationState& state);
    Result update(GeoPoint fix, double accuracy_m, size_t segment_budget, NavigationState& state);

  private:
    const GeoPoint* route_ = nullptr;
    size_t count_ = 0;
    double total_m_ = 0;
    NavigationOptions options_{};
    NavigationState state_{};
    uint64_t update_generation_ = 0;
};
static_assert(sizeof(RouteLabel) <= 32, "Sparse routing state must remain small");
static_assert(sizeof(SnapCursor) <= 1024, "Bounded snapping continuation");
static_assert(sizeof(ManeuverCursor) <= 256, "Bounded maneuver continuation");
} // namespace tmap
