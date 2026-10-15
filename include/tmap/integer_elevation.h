#pragma once
#include "tmap/integer_cost.h"
#include "tmap/navigation.h"
namespace tmap
{
struct IntegerElevationPrefix
{
    uint32_t ascent_cm = 0, descent_cm = 0;
};
struct IntegerElevationCursor
{
    uint64_t generation = 0, offset = 0;
    uint32_t edge = 0, distance_cm = 0, length = 0, ascent = 0, descent = 0;
    uint32_t count = 0, lo = 0, hi = 0, lower_distance = 0, lower_ascent = 0, lower_descent = 0;
    std::array<uint8_t, 48> bytes{};
    IntegerElevationPrefix value{};
    uint8_t stage = 0;
    uint8_t read_completed = 0;
    Result terminal = Result::Invalid;
};
Result beginIntegerElevation(GraphReader& graph, uint32_t edge, uint32_t distance_cm, IntegerElevationCursor& cursor);
Result integerElevationStep(GraphReader& graph, IntegerElevationCursor& cursor, size_t budget, IntegerElevationPrefix& output);
void cancel(IntegerElevationCursor& cursor);
struct IntegerEdgeInterval
{
    uint32_t length_cm = 0, ascent_cm = 0, descent_cm = 0;
    uint64_t cost_units = 0;
};
struct IntegerEdgeIntervalCursor
{
    IntegerElevationCursor prefix{};
    IntegerElevationPrefix first{};
    AscentRatio ratio{};
    IntegerEdgeInterval value{};
    uint32_t edge = 0, start_cm = 0, end_cm = 0;
    uint8_t stage = 0;
    Result terminal = Result::Invalid;
};
Result beginIntegerEdgeInterval(GraphReader& graph, uint32_t edge, uint32_t start_cm, uint32_t end_cm,
                                AscentRatio ratio, IntegerEdgeIntervalCursor& cursor);
Result integerEdgeIntervalStep(GraphReader& graph, IntegerEdgeIntervalCursor& cursor, size_t budget, IntegerEdgeInterval& output);
void cancel(IntegerEdgeIntervalCursor& cursor);
} // namespace tmap
