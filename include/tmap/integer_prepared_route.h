#pragma once
#include "tmap/integer_endpoint.h"
#include "tmap/integer_route.h"
namespace tmap
{
struct IntegerPreparedRouteCursor
{
    const IntegerEndpointCandidates* starts = nullptr;
    const IntegerEndpointCandidates* ends = nullptr;
    IntegerDirectCursor direct{};
    IntegerRouteCursor search{};
    IntegerRouteWorkspace workspace{};
    IntegerRouteOptions options{};
    IntegerRouteLeg* legs = nullptr;
    size_t capacity = 0, count = 0, walk_count = 0, write_index = 0;
    uint64_t generation = 0, cost_units = 0, pending_cost = 0;
    uint32_t walk = UINT32_MAX;
    uint32_t expansions = 0;
    uint8_t pair = 0, stage = 0;
    bool found = false, coverage_limited = false;
    Result terminal = Result::Invalid;
};
Result beginIntegerPreparedRoute(GraphReader& graph, const IntegerEndpointCandidates& starts, const IntegerEndpointCandidates& ends,
                                 IntegerRouteOptions options, IntegerRouteWorkspace workspace, IntegerRouteLeg* legs, size_t capacity,
                                 IntegerPreparedRouteCursor& cursor);
Result integerPreparedRouteStep(GraphReader& graph, IntegerPreparedRouteCursor& cursor, size_t budget);
void cancel(IntegerPreparedRouteCursor& cursor);
Result integerPreparedRouteResult(const IntegerPreparedRouteCursor& cursor, const IntegerRouteLeg*& legs, size_t& count, uint64_t& cost_units);
} // namespace tmap
