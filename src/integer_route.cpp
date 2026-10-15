#include "tmap/integer_route.h"
#include "binary.h"
#include <algorithm>
#include <new>
namespace tmap
{
namespace
{
bool heapMode(const IntegerRouteCursor& c)
{
    return c.options.algorithm == IntegerRouteAlgorithm::HeapDijkstra || c.options.algorithm == IntegerRouteAlgorithm::Landmarks;
}
uint64_t priority(const IntegerRouteCursor& c, uint32_t index)
{
    const auto cost = c.workspace.labels[index].cost;
    if (c.options.algorithm != IntegerRouteAlgorithm::Landmarks) return cost;
    const auto bound = uint64_t(c.workspace.lower_bounds[index]) * c.options.ascent.denominator;
    return bound > UINT64_MAX - cost ? UINT64_MAX : cost + bound;
}
Result loadLandmarks(GraphReader& graph, IntegerRouteCursor& c)
{
    auto& package = graph.package();
    auto* bytes = c.workspace.landmarks->scratch.data();
    if (!c.workspace.landmarks->stage)
    {
        const auto* policy = package.section(SectionType::LandmarkPolicy);
        const auto* data = package.section(SectionType::LandmarkDistances);
        if (!policy && !data) return Result::Missing;
        if (!policy || !data || policy->bytes != 32 || policy->count != 1) return Result::Invalid;
        const auto result = package.read(SectionType::LandmarkPolicy, 0, bytes, 32);
        if (result != Result::Ok) return result;
        const auto count = detail::u32(bytes);
        if (!count || count > 8 || detail::u32(bytes + 4) != graph.info().nodes || detail::u32(bytes + 8) != 1 ||
            detail::u32(bytes + 12) || data->count != graph.info().nodes || data->bytes != uint64_t(graph.info().nodes) * count * 8)
            return Result::Invalid;
        c.workspace.landmarks->count = static_cast<uint8_t>(count);
        c.workspace.landmarks->stage = 1;
    }
    if (c.workspace.landmarks->stage == 1)
    {
        const auto result = detail::fixedFragment(package, SectionType::LandmarkDistances, uint64_t(c.destination) * c.workspace.landmarks->count * 8, bytes, c.workspace.landmarks->count * 8, c.workspace.landmarks->read_completed);
        if (result != Result::Ok) return result;
        c.workspace.landmarks->read_completed = 0;
        for (size_t i = 0; i < size_t(c.workspace.landmarks->count) * 2; ++i) c.workspace.landmarks->goal[i] = detail::u32(bytes + i * 4);
        c.workspace.landmarks->stage = 2;
    }
    return Result::Ok;
}
Result lowerBound(GraphReader& graph, IntegerRouteCursor& c, uint32_t node, uint32_t& output)
{
    auto* bytes = c.workspace.landmarks->scratch.data();
    const auto result = detail::fixedFragment(graph.package(), SectionType::LandmarkDistances, uint64_t(node) * c.workspace.landmarks->count * 8, bytes, c.workspace.landmarks->count * 8, c.workspace.landmarks->read_completed);
    if (result != Result::Ok) return result;
    c.workspace.landmarks->read_completed = 0;
    uint32_t value = 0;
    for (size_t i = 0; i < c.workspace.landmarks->count; ++i)
    {
        const auto from = detail::u32(bytes + i * 8), to = detail::u32(bytes + i * 8 + 4);
        const auto goal_from = c.workspace.landmarks->goal[i * 2], goal_to = c.workspace.landmarks->goal[i * 2 + 1];
        if (from != UINT32_MAX && goal_from != UINT32_MAX && goal_from > from) value = std::max(value, goal_from - from);
        if (to != UINT32_MAX && goal_to != UINT32_MAX && to > goal_to) value = std::max(value, to - goal_to);
    }
    output = std::min(value, UINT32_C(0x7fffffff));
    return Result::Ok;
}
Result finish(IntegerRouteCursor& c, Result result)
{
    c.complete = true;
    c.terminal = result;
    if (result != Result::Ok) c.goal = UINT32_MAX;
    return result;
}
uint32_t slot(IntegerRouteCursor& c, uint32_t key)
{
    size_t at = size_t(key * UINT32_C(2654435761)) % c.workspace.capacity;
    for (size_t i = 0; i < c.workspace.capacity; ++i, at = (at + 1) % c.workspace.capacity)
        if (!c.workspace.labels[at].key || c.workspace.labels[at].key == key) return uint32_t(at);
    return UINT32_MAX;
}
bool less(const IntegerRouteCursor& c, size_t a, size_t b)
{
    const auto& left = c.workspace.labels[c.workspace.queue[a]];
    const auto& right = c.workspace.labels[c.workspace.queue[b]];
    const auto left_priority = priority(c, c.workspace.queue[a]), right_priority = priority(c, c.workspace.queue[b]);
    if (left_priority != right_priority) return left_priority < right_priority;
    if (c.options.algorithm == IntegerRouteAlgorithm::Landmarks && left.cost != right.cost) return left.cost > right.cost;
    return left.cost < right.cost || (left.cost == right.cost && left.key < right.key);
}
void exchange(IntegerRouteCursor& c, size_t a, size_t b)
{
    const auto value = c.workspace.queue[a];
    c.workspace.queue[a] = c.workspace.queue[b];
    c.workspace.queue[b] = value;
    c.workspace.labels[c.workspace.queue[a]].queue_position = static_cast<uint32_t>(a);
    c.workspace.labels[c.workspace.queue[b]].queue_position = static_cast<uint32_t>(b);
}
void rise(IntegerRouteCursor& c, size_t position)
{
    while (position)
    {
        const auto parent = (position - 1) / 2;
        if (!less(c, position, parent)) break;
        exchange(c, position, parent);
        position = parent;
    }
}
void enqueue(IntegerRouteCursor& c, uint32_t index)
{
    if (c.options.algorithm == IntegerRouteAlgorithm::LabelScanDijkstra)
    {
        ++c.queued;
        c.workspace.labels[index].queue_position = 0;
        return;
    }
    if (heapMode(c))
    {
        const auto position = c.queued++;
        c.workspace.queue[position] = index;
        c.workspace.labels[index].queue_position = static_cast<uint32_t>(position);
        rise(c, position);
        return;
    }
    const auto tail = c.queued >= c.workspace.capacity - c.head ? c.queued - (c.workspace.capacity - c.head) : c.head + c.queued;
    c.workspace.queue[tail] = index;
    ++c.queued;
    c.workspace.labels[index].queue_position = 0;
}
void dequeue(IntegerRouteCursor& c)
{
    if (c.options.algorithm == IntegerRouteAlgorithm::LabelScanDijkstra)
    {
        c.workspace.labels[c.best_position].queue_position = UINT32_MAX;
        --c.queued;
        c.selection = 0;
        c.best_position = SIZE_MAX;
        c.selected = false;
        return;
    }
    c.workspace.labels[c.workspace.queue[c.head]].queue_position = UINT32_MAX;
    --c.queued;
    if (heapMode(c))
    {
        if (c.queued)
        {
            c.workspace.queue[0] = c.workspace.queue[c.queued];
            c.workspace.labels[c.workspace.queue[0]].queue_position = 0;
            size_t position = 0;
            while (position < c.queued / 2)
            {
                auto child = position * 2 + 1;
                if (child + 1 < c.queued && less(c, child + 1, child)) ++child;
                if (!less(c, child, position)) break;
                exchange(c, position, child);
                position = child;
            }
        }
    }
    else c.head = (c.head + 1) % c.workspace.capacity;
    c.selection = 0;
    c.selected = false;
}
} // namespace
void cancel(IntegerRouteCursor& c) { finish(c, Result::Cancelled); }
Result beginIntegerRoute(GraphReader& graph, uint32_t start, uint32_t destination, IntegerRouteOptions options,
                         IntegerRouteWorkspace workspace, IntegerRouteCursor& c)
{
    return beginIntegerRouteWithBoundaries(graph, start, destination, UINT32_MAX, UINT32_MAX, options, workspace, c);
}
Result beginIntegerRouteWithBoundaries(GraphReader& graph, uint32_t start, uint32_t destination, uint32_t initial_edge,
                                       uint32_t final_edge, IntegerRouteOptions options, IntegerRouteWorkspace workspace, IntegerRouteCursor& c)
{
    ::new (static_cast<void*>(&c)) IntegerRouteCursor{};
    if (!workspace.labels || (!workspace.queue && options.algorithm != IntegerRouteAlgorithm::LabelScanDijkstra) || workspace.capacity < 2 || workspace.capacity > UINT32_MAX ||
        !options.ascent.denominator || !options.max_expansions || (options.mode != TravelMode::Foot && options.mode != TravelMode::Bicycle) ||
        uint8_t(options.algorithm) > uint8_t(IntegerRouteAlgorithm::LabelScanDijkstra) ||
        (options.algorithm == IntegerRouteAlgorithm::Landmarks && (!workspace.lower_bounds || !workspace.landmarks)))
        return Result::Invalid;
    const auto result = graph.initialize();
    if (result != Result::Ok) return result;
    const auto policy = graph.validateRouting(options.mode);
    if (policy != Result::Ok) return policy;
    if (start >= graph.info().nodes || destination >= graph.info().nodes || graph.info().edges >= UINT32_MAX - 1)
        return Result::Invalid;
    for (size_t boundary = 0; boundary < 2; ++boundary)
    {
        const auto row = boundary ? final_edge : initial_edge;
        if (row == UINT32_MAX) continue;
        if (row >= graph.info().edges) return Result::Invalid;
    }
    c.workspace = workspace;
    if (options.algorithm == IntegerRouteAlgorithm::LabelScanDijkstra) c.workspace.queue = nullptr;
    if (options.algorithm == IntegerRouteAlgorithm::Landmarks)
    {
        workspace.landmarks->stage = 0;
        workspace.landmarks->count = 0;
        workspace.landmarks->read_completed = 0;
    }
    c.options = options;
    c.start = start;
    c.destination = destination;
    c.total_edges = graph.info().edges;
    c.initial_edge = initial_edge;
    c.final_edge = final_edge;
    c.boundary_stage = initial_edge == UINT32_MAX && final_edge == UINT32_MAX ? 2 : 0;
    c.generation = graph.package().generation();
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result integerRouteStep(GraphReader& graph, IntegerRouteCursor& c, size_t budget)
{
    if (!budget || !c.workspace.labels) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return finish(c, Result::Cancelled);
    if (c.complete) return c.terminal;
    const auto policy = graph.validateRouting(c.options.mode);
    if (policy == Result::Busy || policy == Result::More) return policy;
    if (policy != Result::Ok) return finish(c, policy);
    for (size_t work = 0; work < budget; ++work)
    {
        if (!c.initialized)
        {
            if (c.boundary_stage < 2)
            {
                const auto row = c.boundary_stage ? c.final_edge : c.initial_edge;
                if (row != UINT32_MAX)
                {
                    const auto result = graph.edge(row, c.pending);
                    if (result == Result::Busy || result == Result::More) return result;
                    if (result != Result::Ok) return finish(c, result);
                    if (!(c.pending.access & uint8_t(c.options.mode)) ||
                        (c.boundary_stage ? c.pending.from != c.destination : c.pending.to != c.start)) return finish(c, Result::Invalid);
                }
                ++c.boundary_stage;
                continue;
            }
            if (c.options.algorithm == IntegerRouteAlgorithm::Landmarks && c.workspace.landmarks->stage != 2)
            {
                const auto result = loadLandmarks(graph, c);
                if (result == Result::Busy || result == Result::More) return result;
                if (result != Result::Ok) return finish(c, result);
                continue;
            }
            if (c.initialization < c.workspace.capacity)
            {
                ::new (static_cast<void*>(c.workspace.labels + c.initialization++)) IntegerRouteLabel{};
                continue;
            }
            uint32_t root_key = UINT32_MAX;
            if (c.initial_edge != UINT32_MAX)
            {
                bool allowed = false;
                const auto result = graph.routeTransition(UINT32_MAX, c.initial_edge, c.options.mode, allowed, root_key);
                if (result == Result::Busy || result == Result::More) return result;
                if (result != Result::Ok) return finish(c, result);
                if (!allowed) return finish(c, Result::NoPath);
            }
            const auto root = slot(c, root_key);
            if (c.options.algorithm == IntegerRouteAlgorithm::Landmarks)
            {
                const auto result = lowerBound(graph, c, c.start, c.workspace.lower_bounds[root]);
                if (result == Result::Busy || result == Result::More) return result;
                if (result != Result::Ok) return finish(c, result);
            }
            auto& label = c.workspace.labels[root];
            label.key = root_key;
            label.node = c.start;
            enqueue(c, root);
            c.labels_used = 1;
            c.initialized = true;
            continue;
        }
        if (c.current == UINT32_MAX)
        {
            if (!c.queued) return finish(c, c.goal != UINT32_MAX ? Result::Ok : c.boundary_seen ? Result::OutsideCoverage
                                                                                                : Result::NoPath);
            if (c.options.algorithm == IntegerRouteAlgorithm::LabelScanDijkstra && !c.selected)
            {
                if (c.selection < c.workspace.capacity)
                {
                    const auto index = c.selection++;
                    const auto& candidate = c.workspace.labels[index];
                    if (candidate.key && candidate.queue_position != UINT32_MAX)
                    {
                        if (c.best_position == SIZE_MAX) c.best_position = index;
                        else
                        {
                            const auto& best = c.workspace.labels[c.best_position];
                            if (candidate.cost < best.cost || (candidate.cost == best.cost && candidate.key < best.key)) c.best_position = index;
                        }
                    }
                    continue;
                }
                if (c.best_position == SIZE_MAX) return finish(c, Result::Invalid);
                c.selected = true;
            }
            else if (c.options.algorithm == IntegerRouteAlgorithm::Dijkstra && !c.selected)
            {
                if (c.selection < c.queued)
                {
                    const auto position = c.selection >= c.workspace.capacity - c.head ? c.selection - (c.workspace.capacity - c.head) : c.head + c.selection;
                    const auto& candidate = c.workspace.labels[c.workspace.queue[position]];
                    if (!c.selection) c.best_position = position;
                    else
                    {
                        const auto& best = c.workspace.labels[c.workspace.queue[c.best_position]];
                        if (candidate.cost < best.cost || (candidate.cost == best.cost && candidate.key < best.key))
                            c.best_position = position;
                    }
                    ++c.selection;
                    continue;
                }
                const auto previous = c.workspace.queue[c.head];
                c.workspace.queue[c.head] = c.workspace.queue[c.best_position];
                c.workspace.queue[c.best_position] = previous;
                c.selected = true;
            }
            const auto index = c.options.algorithm == IntegerRouteAlgorithm::LabelScanDijkstra ? static_cast<uint32_t>(c.best_position) : c.workspace.queue[c.head];
            auto& label = c.workspace.labels[index];
            bool reached = label.node == c.destination;
            if (reached && c.final_edge != UINT32_MAX)
            {
                uint32_t final_key = 0;
                const auto result = graph.routeTransition(label.key, c.final_edge, c.options.mode, reached, final_key);
                if (result == Result::Busy || result == Result::More) return result;
                if (result != Result::Ok) return finish(c, result);
            }
            if (reached || (c.goal != UINT32_MAX && label.cost >= c.workspace.labels[c.goal].cost))
            {
                if (reached && (c.goal == UINT32_MAX || label.cost < c.workspace.labels[c.goal].cost)) c.goal = index;
                if (reached && c.options.algorithm != IntegerRouteAlgorithm::QueueRelaxation) return finish(c, Result::Ok);
                dequeue(c);
                continue;
            }
            GraphNode node;
            auto result = graph.node(label.node, node);
            if (result == Result::Busy || result == Result::More) return result;
            if (result != Result::Ok) return finish(c, result);
            if (c.expansions == c.options.max_expansions) return finish(c, Result::BudgetExceeded);
            ++c.expansions;
            c.boundary_seen = c.boundary_seen || node.boundary;
            dequeue(c);
            c.current = index;
            c.first_edge = node.first_edge;
            c.edge_count = node.edge_count;
            c.next_edge = 0;
            continue;
        }
        if (c.next_edge == c.edge_count)
        {
            c.current = UINT32_MAX;
            continue;
        }
        auto& parent = c.workspace.labels[c.current];
        const auto edge_id = c.first_edge + c.next_edge;
        auto& edge = c.pending;
        auto result = Result::Ok;
        if (!c.edge_loaded)
        {
            result = graph.edge(edge_id, edge);
            if (result == Result::Busy || result == Result::More) return result;
            if (result != Result::Ok) return finish(c, result);
            c.edge_loaded = true;
        }
        if (edge.from != parent.node) return finish(c, Result::Invalid);
        bool allowed = false;
        uint32_t next_key = 0;
        if (edge.access & uint8_t(c.options.mode))
        {
            result = graph.routeTransition(parent.key, edge_id, c.options.mode, allowed, next_key);
            if (result == Result::Busy || result == Result::More) return result;
            if (result != Result::Ok) return finish(c, result);
        }
        if (allowed)
        {
            uint64_t cost = 0;
            result = integerEdgeCost(edge.length_cm, edge.ascent_cm, edge.has_elevation, c.options.ascent, cost);
            if (result == Result::Ok) result = integerAccumulateCost(parent.cost, cost, cost);
            if (result != Result::Ok) return finish(c, result);
            const auto index = slot(c, next_key);
            if (index == UINT32_MAX) return finish(c, Result::BudgetExceeded);
            auto& label = c.workspace.labels[index];
            if (!label.key || cost < label.cost)
            {
                if (!label.key && c.options.algorithm == IntegerRouteAlgorithm::Landmarks)
                {
                    result = lowerBound(graph, c, edge.to, c.workspace.lower_bounds[index]);
                    if (result == Result::Busy || result == Result::More) return result;
                    if (result != Result::Ok) return finish(c, result);
                }
                if (!label.key) ++c.labels_used;
                label.cost = cost;
                label.key = next_key;
                label.node = edge.to;
                label.previous = c.current;
                if (label.queue_position == UINT32_MAX) enqueue(c, index);
                else if (heapMode(c)) rise(c, label.queue_position);
            }
        }
        ++c.next_edge;
        c.edge_loaded = false;
    }
    return Result::More;
}
Result integerRouteEdges(const IntegerRouteCursor& c, uint32_t* output, size_t capacity, size_t& count)
{
    count = 0;
    if (!c.complete || c.terminal != Result::Ok || c.goal == UINT32_MAX) return c.complete ? c.terminal : Result::More;
    auto at = c.goal;
    while (at < c.workspace.capacity && c.workspace.labels[at].previous != UINT32_MAX)
    {
        if (++count > c.labels_used)
        {
            count = 0;
            return Result::Invalid;
        }
        at = c.workspace.labels[at].previous;
    }
    if (at >= c.workspace.capacity)
    {
        count = 0;
        return Result::Invalid;
    }
    if (count > capacity || (!output && count)) return Result::BudgetExceeded;
    at = c.goal;
    for (size_t i = count; i; --i)
    {
        if (!c.total_edges) return Result::Invalid;
        output[i - 1] = (c.workspace.labels[at].key - 1) % c.total_edges;
        at = c.workspace.labels[at].previous;
    }
    return Result::Ok;
}
} // namespace tmap
