#pragma once
#include "tmap/coordinate_route.h"
#include "tmap/integer_endpoint.h"
namespace tmap
{
// Optional drawing adapter: floating point is confined to geometry conversion.
struct IntegerLegConversionCursor
{
    const IntegerRouteLeg* legs = nullptr;
    size_t count = 0, index = 0;
    uint64_t generation = 0;
    GraphEdge edge{};
    RouteLeg converted{};
    GeoPoint previous{};
    double total = 0, walked = 0, first = 0, last = 0;
    uint32_t vertex = 0;
    uint8_t stage = 0;
    bool first_found = false;
    Result terminal = Result::Invalid;
};
// Single-leg conversion publishes converted/edge when step returns Ok.
Result beginIntegerLegConversion(GraphReader& graph, const IntegerRouteLeg& leg, IntegerLegConversionCursor& cursor);
Result integerLegConversionStep(GraphReader& graph, IntegerLegConversionCursor& cursor, size_t budget);
void cancel(IntegerLegConversionCursor& cursor);
struct IntegerGeometryCursor : IntegerLegConversionCursor
{
    LegGeometryCursor drawing{};
    GeoPoint emitted{};
    bool has_emitted = false;
};
Result beginIntegerGeometry(GraphReader& graph, const IntegerRouteLeg* legs, size_t count, IntegerGeometryCursor& cursor);
Result integerGeometryStep(GraphReader& graph, IntegerGeometryCursor& cursor, size_t budget, RoutePointVisitor visitor, void* context);
void cancel(IntegerGeometryCursor& cursor);
} // namespace tmap
