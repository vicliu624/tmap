#pragma once
#include "tmap/integer_elevation.h"
namespace tmap
{
struct IntegerRouteLeg
{
    uint32_t edge = UINT32_MAX, first_cm = 0, last_cm = 0;
};
struct IntegerEndpoint
{
    IntegerRouteLeg leg{};
    uint32_t node = 0, boundary_edge = UINT32_MAX;
    uint32_t position_cm = 0;
    uint64_t cost_units = 0;
    bool partial = false;
};
struct IntegerEndpointCandidates
{
    std::array<IntegerEndpoint, 2> choices{};
    uint8_t count = 0;
    uint64_t generation = 0;
    AscentRatio ratio{};
    TravelMode mode = TravelMode::Foot;
    bool departure = true;
};
struct IntegerEndpointCursor
{
    Snap snap{};
    GraphEdge base{}, edge{};
    IntegerEdgeIntervalCursor interval{};
    IntegerEndpointCandidates value{};
    AscentRatio ratio{};
    uint64_t generation = 0;
    uint32_t position_cm = 0;
    TravelMode mode = TravelMode::Foot;
    bool departure = true;
    uint8_t direction = 0, stage = 0;
    Result terminal = Result::Invalid;
};
Result beginIntegerEndpoint(GraphReader& graph, const Snap& snap, bool departure, TravelMode mode,
                            AscentRatio ratio, IntegerEndpointCursor& cursor);
Result integerEndpointStep(GraphReader& graph, IntegerEndpointCursor& cursor, size_t budget, IntegerEndpointCandidates& output);
void cancel(IntegerEndpointCursor& cursor);
struct IntegerDirectRoute
{
    IntegerRouteLeg leg{};
    uint64_t cost_units = 0;
};
struct IntegerDirectCursor
{
    const IntegerEndpointCandidates* starts = nullptr;
    const IntegerEndpointCandidates* ends = nullptr;
    IntegerEdgeIntervalCursor interval{};
    GraphEdge edge{};
    IntegerDirectRoute best{};
    AscentRatio ratio{};
    uint64_t generation = 0;
    TravelMode mode = TravelMode::Foot;
    uint8_t pair = 0, stage = 0;
    bool found = false;
    Result terminal = Result::Invalid;
};
Result beginIntegerDirectRoute(GraphReader& graph, const IntegerEndpointCandidates& starts, const IntegerEndpointCandidates& ends,
                               TravelMode mode, AscentRatio ratio, IntegerDirectCursor& cursor);
Result integerDirectRouteStep(GraphReader& graph, IntegerDirectCursor& cursor, size_t budget, IntegerDirectRoute& output);
void cancel(IntegerDirectCursor& cursor);
} // namespace tmap
