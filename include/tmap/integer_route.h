#pragma once
#include "tmap/integer_cost.h"
#include "tmap/navigation.h"
namespace tmap
{
struct IntegerRouteLabel
{
    uint64_t cost = 0;
    uint32_t key = 0, node = 0, previous = UINT32_MAX;
    uint32_t queue_position = UINT32_MAX;
};
struct IntegerLandmarkWorkspace
{
    std::array<uint8_t, 64> scratch{};
    std::array<uint32_t, 16> goal{};
    uint8_t stage = 0, count = 0;
    uint8_t read_completed = 0;
};
struct IntegerRouteWorkspace
{
    IntegerRouteLabel* labels = nullptr;
    uint32_t* queue = nullptr; // May be null only for LabelScanDijkstra; that mode never retains a queue.
    size_t capacity = 0;
    uint32_t* lower_bounds = nullptr; // Optional capacity-sized storage, required by Landmarks.
    IntegerLandmarkWorkspace* landmarks = nullptr;
};
enum class IntegerRouteAlgorithm : uint8_t
{
    QueueRelaxation,
    Dijkstra,
    HeapDijkstra,
    Landmarks,
    LabelScanDijkstra
};
struct IntegerRouteOptions
{
    AscentRatio ascent{};
    TravelMode mode = TravelMode::Foot;
    uint32_t max_expansions = 50000;
    IntegerRouteAlgorithm algorithm = IntegerRouteAlgorithm::QueueRelaxation;
};
struct IntegerRouteCursor
{
    IntegerRouteWorkspace workspace{};
    IntegerRouteOptions options{};
    GraphEdge pending{};
    uint64_t generation = 0;
    size_t initialization = 0, labels_used = 0, queued = 0, head = 0;
    uint32_t start = 0, destination = 0, current = UINT32_MAX, goal = UINT32_MAX;
    uint32_t total_edges = 0;
    uint32_t initial_edge = UINT32_MAX, final_edge = UINT32_MAX;
    uint32_t first_edge = 0, next_edge = 0, edge_count = 0, expansions = 0;
    bool initialized = false, edge_loaded = false, boundary_seen = false, complete = true;
    uint8_t boundary_stage = 0;
    Result terminal = Result::Invalid;
    size_t selection = 0, best_position = SIZE_MAX;
    bool selected = false;
};
// Exact node-endpoint arrival-edge costs. FIFO drains the queue; optional
// Dijkstra uses a bounded linear scan; HeapDijkstra uses the same array as an
// indexed heap. LabelScanDijkstra scans all label slots without a queue array.
// Priority modes may finish at their first minimum target.
Result beginIntegerRoute(GraphReader& graph, uint32_t start, uint32_t destination,
                         IntegerRouteOptions options, IntegerRouteWorkspace workspace, IntegerRouteCursor& cursor);
Result beginIntegerRouteWithBoundaries(GraphReader& graph, uint32_t start, uint32_t destination,
                                       uint32_t initial_edge, uint32_t final_edge, IntegerRouteOptions options,
                                       IntegerRouteWorkspace workspace, IntegerRouteCursor& cursor);
Result integerRouteStep(GraphReader& graph, IntegerRouteCursor& cursor, size_t budget);
Result integerRouteEdges(const IntegerRouteCursor& cursor, uint32_t* output, size_t capacity, size_t& count);
void cancel(IntegerRouteCursor& cursor);
static_assert(sizeof(IntegerRouteLabel) <= 24, "Bounded integer arrival state");
} // namespace tmap
